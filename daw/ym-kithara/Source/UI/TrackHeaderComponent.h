#pragma once

#include <JuceHeader.h>
#include "TimelineEngine.h"
#include "TimelineComponent.h"   // shared timeline geometry constants

//==============================================================================
/**
    TrackHeaderComponent — the FIXED track-info column (M3.3).

    Until M3.2 the track headers (name, FX summary, Mute/Solo, gain
    slider, peak meter) were painted inside TimelineComponent, which
    lives in the scrolling Viewport — so scrolling right to see later
    parts of the song scrolled the headers away with the waveforms.
    かいさん reported exactly that, so the column is now a sibling
    component that MainComponent places OUTSIDE (left of) the lanes
    Viewport: it never scrolls horizontally.

    Vertical alignment is kept by MainComponent feeding the lanes
    viewport's view Y into setViewOffsetY() whenever it changes: rows
    (and their child controls) are laid out shifted up by that offset,
    so header row i always sits next to lane row i at any track height
    or scroll position. The component is exactly
    TimelineComponent::headerWidthPx wide; its top rulerHeightPx is a
    blank corner block so its rows begin where the lanes' rows begin
    (below the ruler, which stays with the lanes inside the Viewport).

    M3.3 also adds the per-track "M→FX" toggle (live MIDI input to the
    track's FX chain — see TimelineEngine's M3.3 notes), synced with
    the model like Mute/Solo.

    M4 (M6.8) adds the record-arm toggle (red dot, MIDI tracks only)
    between Solo and M→FX — arming a track monitors live MIDI through
    its instrument and records it while the transport runs (see
    TimelineEngine's M4 notes). The gain slider narrows on MIDI rows
    to make room; other rows keep the M3.3 layout exactly. (M6.9: the
    dot is painted by the button itself, not a ● glyph — the glyph
    label mojibaked on Japanese Windows.)

    Everything here reads the session under the engine's model lock,
    exactly as the headers did inside TimelineComponent.
*/
class TrackHeaderComponent final : public juce::Component,
                                   private juce::Timer
{
public:
    explicit TrackHeaderComponent (TimelineEngine& engineToUse);
    ~TrackHeaderComponent() override;

    // Vertical zoom mirror of TimelineComponent::setTrackHeight —
    // MainComponent keeps the two in sync.
    void setTrackHeight (int newHeightPx);
    int getTrackHeight() const noexcept { return trackHeight; }

    /** The lanes viewport's vertical scroll offset. Rows are shifted
        up by this amount so they stay glued to their lanes. */
    void setViewOffsetY (int offsetY);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

    /** Called (before the removal) when a header's remove button
        deletes a track, so MainComponent can drop a clip selection
        that belonged to it — the same courtesy the headers performed
        when they lived inside TimelineComponent. */
    std::function<void (juce::int64 trackId)> onTrackRemoved;

private:
    //==========================================================================
    void timerCallback() override;

    struct HeaderControls
    {
        juce::int64 trackId = 0;
        std::unique_ptr<juce::Label> nameLabel;
        std::unique_ptr<juce::Label> fxLabel;   // one-line FX chain summary
        std::unique_ptr<juce::TextButton> muteButton;
        std::unique_ptr<juce::TextButton> soloButton;
        std::unique_ptr<juce::TextButton> fxMidiButton;   // M3.3: "M→FX"
        // M4 (M6.8): MIDI tracks only — the record-arm toggle (a red
        // dot, drawn programmatically since M6.9 — see the .cpp).
        // Runtime-only engine state; see TimelineEngine's M4 notes.
        std::unique_ptr<juce::Button> armButton;
        bool isMidiTrack = false;
        std::unique_ptr<juce::TextButton> removeButton;
        std::unique_ptr<juce::Slider> gainSlider;
        // M6.3: text tracks only — the session telop font-size
        // selector (S/M/L), sitting in the otherwise-empty mid row.
        std::unique_ptr<juce::ComboBox> telopSizeBox;
        // M6.6: text tracks only — opens the telop style dialog
        // (font / size / position / alignment / colour).
        std::unique_ptr<juce::TextButton> telopStyleButton;
        bool isTextTrack = false;
    };

    void rebuildHeaders();
    void layoutHeaders();
    juce::int64 computeTrackSignature() const;

    //==========================================================================
    // Peak meters (moved from TimelineComponent, M3.2). Geometry is
    // derived from trackIndex; state is keyed by track id so zoom or
    // reorder can't mix up one track's level with another's.
    juce::Rectangle<int> getMeterStripRect (int trackIndex) const;
    juce::Rectangle<int> getMeterLedRect (int trackIndex) const;
    void drawTrackMeter (juce::Graphics& g, int trackIndex, juce::int64 trackId);
    void updateMeters();   // one dynamics tick; called from timerCallback

    // Row geometry in THIS component's coordinates (content Y minus
    // the lanes' vertical scroll offset).
    int trackIndexToY (int trackIndex) const noexcept
    {
        return TimelineComponent::rulerHeightPx + trackIndex * trackHeight - viewOffsetY;
    }

    TimelineEngine& engine;

    std::vector<HeaderControls> headers;
    juce::int64 lastTrackSignature = 0;
    int lastModelVersion = -1;
    bool rebuildingHeaders = false;

    int trackHeight = 64;   // kept in sync with TimelineComponent by MainComponent
    int viewOffsetY = 0;

    // Meter display state (message thread only).
    std::map<juce::int64, float> meterLevelLeft;
    std::map<juce::int64, float> meterLevelRight;
    std::map<juce::int64, bool>  meterClipLatched;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackHeaderComponent)
};
