#pragma once

#include <JuceHeader.h>
#include "TimelineEngine.h"
#include "VideoDecoder.h"   // FilmstripStore (M6)

//==============================================================================
/**
    TimelineComponent — ruler, lanes, clips and playhead (the track
    headers are TrackHeaderComponent's job since M3.3).

    This component is the content of a Viewport owned by MainComponent, so
    its size is the full scrollable session size (MainComponent asks for
    getDesiredWidth/Height()). All edits go through TimelineEngine's locked
    edit API; when this component reads the session directly (painting, hit
    testing) it holds the engine's model lock. The audio thread never takes
    that lock (it renders from an immutable snapshot), so UI lock-holding
    can never disturb playback.

    Interactions:
      - click the ruler (or drag in it) to seek
      - click a clip to select it
      - drag a clip body to move it (vertically moves between tracks too)
      - drag a clip's left/right edge (6 px zone) to trim it
      - Delete / Backspace removes the selected clip
      - the view page-scrolls to follow the playhead when it leaves the
        visible area: always while playing, and while stopped only when
        the position jumped (seek / return-to-start), so manual
        scrolling while stopped is never overridden; never mid-drag

    M3 additions: MIDI lanes draw their clips (deep teal, with a tick
    per note) and treat them like audio clips for selection and
    horizontal dragging (move only — no cross-track, no trim).
    Double-clicking empty MIDI lane space creates a clip; double-
    clicking a MIDI clip asks MainComponent to open the piano roll.

    M6.9: MIDI clips trim by their edges exactly like the other clip
    kinds (6 px zones, resize cursor). A right trim only changes the
    length — extending reveals empty beats in the piano roll — and a
    left trim slides the clip's beat window (MidiClipData::offsetBeats)
    so surviving notes keep their absolute timeline positions. Notes
    outside the window are hidden, never deleted.

    M3.2 additions: a slim stereo peak meter was painted into each
    track header (it moved with the headers in M3.3, see below).

    M3.3: this component now draws ONLY the ruler and the lanes — its
    content X is pure timeline X (x = 0 is the session start). The
    track headers live in TrackHeaderComponent, a sibling that
    MainComponent pins to the left of the Viewport, so horizontal
    scrolling can no longer carry them away (かいさん's report). The
    header geometry constants below remain the shared source of truth
    both components lay themselves out by. Vertical positions are
    unchanged: rulerHeightPx of ruler, then one trackHeightPx row per
    track — the header column applies the viewport's view Y as an
    offset to land on the same rows.

    M6: the video track's lane draws its clips as a FILMSTRIP — one
    thumbnail per whole source second, decoded in the background by
    FilmstripStore (a paint never blocks on FFmpeg; not-yet-extracted
    seconds show the plain clip colour). Video clips behave like
    audio clips for editing: select, drag to move, drag an edge to
    trim (the engine clamps to the source bounds), Split/Delete via
    the inspector row.

    M6.3: video clips may OVERLAP (cross-dissolves — the lane paints
    them in start order so the later clip covers the earlier one),
    and dragging a clip's TOP corner horizontally sets its fade:
    top-left = fade-in, top-right = fade-out, drawn as a shaded
    diagonal over the faded region. The text track's lane draws its
    telop clips (first line of the text inside the clip) and edits
    them like MIDI clips — drag to move, edges trim, double-click
    opens the text editor (MainComponent's job, via
    onTextClipDoubleClicked), Split/Delete via the inspector row.
    Filmstrip thumbnails are only requested for the region actually
    being painted, and thumbnail arrivals repaint via the 30 Hz
    timer instead of one repaint per thumbnail (the M6.2 freeze
    audit's thundering-herd fix — see the README).
*/
class TimelineComponent final : public juce::Component,
                                private juce::Timer
{
public:
    explicit TimelineComponent (TimelineEngine& engineToUse);
    ~TimelineComponent() override;

    //==========================================================================
    static constexpr int rulerHeightPx = 28;
    // Width of the pinned header column (TrackHeaderComponent) — kept
    // here because both components lay out by it (M3.3).
    static constexpr int headerWidthPx = 190;

    // Width reserved for the peak meter at the header's right edge
    // (TrackHeaderComponent keeps the header controls out of this strip).
    static constexpr int meterStripWidthPx = 18;

    // Vertical zoom (M2): per-track height in pixels, 40–160. The one
    // value drives headers, lanes and hit-testing. Session-only.
    static constexpr int minTrackHeightPx = 40;
    static constexpr int maxTrackHeightPx = 160;
    int getTrackHeight() const noexcept { return trackHeight; }
    void setTrackHeight (int newHeightPx);

    void paint (juce::Graphics& g) override;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed (const juce::KeyPress& key) override;

    //==========================================================================
    void setPixelsPerSecond (double newPixelsPerSecond);
    double getPixelsPerSecond() const noexcept { return pixelsPerSecond; }

    juce::int64 getSelectedClipId() const noexcept { return selectedClipId; }
    juce::int64 getSelectedMidiClipId() const noexcept { return selectedMidiClipId; }
    juce::int64 getSelectedVideoClipId() const noexcept { return selectedVideoClipId; }
    juce::int64 getSelectedTextClipId() const noexcept { return selectedTextClipId; }
    void selectMidiClip (juce::int64 clipId);   // -1 clears the MIDI selection
    void selectVideoClip (juce::int64 clipId);   // -1 clears the video selection (M6)
    void selectTextClip (juce::int64 clipId);   // -1 clears the text selection (M6.3)
    void clearSelection();

    /** Scroll the parent Viewport so the playhead is visible, if it is
        currently outside the visible area (landing near the left edge,
        same ~8% margin as the timer follow). The deterministic
        counterpart to the timer-based followPlayheadIfNeeded: transport
        actions that move the position on purpose (return-to-start,
        ruler seek) call this directly instead of relying on the timer
        noticing a jump. No playing-state or position-delta tests.
        No-op while a drag is in progress. */
    void ensurePlayheadVisible();

    int getDesiredWidth() const;
    int getDesiredHeight() const;

    /** Called whenever the selection changes (MainComponent refreshes its
        inspector strip). */
    std::function<void()> onSelectionChanged;

    /** Called when the track height changes via Ctrl+mouse-wheel, so
        MainComponent can sync its V Zoom slider. */
    std::function<void()> onTrackHeightChanged;

    /** Called on double-click of a MIDI clip: MainComponent opens the
        piano roll for it. */
    std::function<void (juce::int64 trackId, juce::int64 clipId)> onMidiClipDoubleClicked;

    /** Called on double-click of a text clip (M6.3): MainComponent
        opens the telop text editor for it. */
    std::function<void (juce::int64 trackId, juce::int64 clipId)> onTextClipDoubleClicked;

private:
    //==========================================================================
    void timerCallback() override;

    /** Page-style follow: scroll the parent Viewport so an off-screen
        playhead re-enters near the left edge of the visible area. Acts
        while playing, or while stopped when the position just jumped
        (see the .cpp for the exact rules); never mid-drag.
        playheadSeconds is the engine position this tick — it is compared
        against lastPlayheadSeconds to tell a jump from a parked playhead. */
    void followPlayheadIfNeeded (int playheadX, double playheadSeconds);

    // Geometry helpers (x in component pixels, y in component pixels).
    int sampleToX (juce::int64 samples) const;
    juce::int64 xToSample (int x) const;
    int trackIndexToY (int trackIndex) const noexcept { return rulerHeightPx + trackIndex * trackHeight; }
    int yToTrackIndex (int y) const;

    struct ClipHit
    {
        juce::int64 clipId = -1;
        int trackIndex = -1;
        // 0 = body, 1 = left edge, 2 = right edge;
        // video clips only (M6.3): 3 = top-left fade-in corner,
        // 4 = top-right fade-out corner
        int zone = 0;
    };

    ClipHit hitTestClip (int x, int y) const;   // model lock must be held
    ClipHit hitTestMidiClip (int x, int y) const;   // model lock must be held
    ClipHit hitTestVideoClip (int x, int y) const;   // model lock must be held (M6)
    ClipHit hitTestTextClip (int x, int y) const;   // model lock must be held (M6.3)
    void selectClip (juce::int64 clipId);

    static juce::Colour getTrackColour (int trackIndex);

    TimelineEngine& engine;

    int lastModelVersion = -1;

    double pixelsPerSecond = 40.0;
    int trackHeight = 64;
    juce::int64 selectedClipId = -1;
    juce::int64 selectedMidiClipId = -1;   // M3: MIDI clips select separately
    juce::int64 selectedVideoClipId = -1;  // M6: video clips select separately
    juce::int64 selectedTextClipId = -1;   // M6.3: text clips select separately

    int lastPlayheadX = -1;
    double lastPlayheadSeconds = -1.0;   // engine position at the previous timer tick

    enum class DragMode { none, seek, moveClip, trimLeft, trimRight,
                          moveMidiClip, trimMidiLeft, trimMidiRight,   // M6.9
                          moveVideoClip, trimVideoLeft, trimVideoRight,
                          fadeVideoIn, fadeVideoOut,               // M6.3
                          moveTextClip, trimTextLeft, trimTextRight };   // M6.3
    DragMode dragMode = DragMode::none;
    juce::int64 dragClipId = -1;
    juce::int64 dragMidiClipId = -1;
    juce::int64 dragVideoClipId = -1;
    juce::int64 dragTextClipId = -1;       // M6.3
    juce::int64 dragTrackId = -1;
    ClipData dragOriginal;
    MidiClipData dragMidiOriginal;
    VideoClipData dragVideoOriginal;
    TextClipData dragTextOriginal;         // M6.3
    int dragStartX = 0;
    int dragStartY = 0;

    // M6: background filmstrip thumbnails for the video lane.
    FilmstripStore filmstrip;

    // M6.3: thumbnail arrivals set this instead of repainting
    // directly; the 30 Hz timer repaints at most once per tick,
    // however many thumbnails landed in between.
    bool filmstripDirty = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelineComponent)
};
