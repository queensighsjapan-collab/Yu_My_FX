#include "TimelineComponent.h"

#include <algorithm>

namespace
{
    const juce::Colour backgroundColour { 0xff1c1e24 };
    const juce::Colour laneColourA      { 0xff20232b };
    const juce::Colour laneColourB      { 0xff1e2129 };
    const juce::Colour rulerColour      { 0xff262932 };
    const juce::Colour gridColour       { 0xff31353f };
    const juce::Colour barGridColour    { 0xff3d424f };
    const juce::Colour textColour       { 0xffe8e9ec };
    const juce::Colour dimTextColour    { 0xff9aa0ad };
    const juce::Colour accentColour     { 0xffd8a24a };
    const juce::Colour playheadColour   { 0xffe5533d };
    const juce::Colour midiClipColour   { 0xff147d7a };   // deep teal (M3)
    const juce::Colour videoClipColour  { 0xff4a4470 };   // slate violet (M6)
    const juce::Colour textClipColour   { 0xff7a5c22 };   // dark amber (M6.3 telops)

    constexpr int edgeZonePx = 6;

    juce::String formatRulerTime (double seconds)
    {
        if (seconds < 0.0)
            seconds = 0.0;

        const int total = (int) std::floor (seconds);
        return juce::String::formatted ("%d:%02d", total / 60, total % 60);
    }
}

//==============================================================================
TimelineComponent::TimelineComponent (TimelineEngine& engineToUse)
    : engine (engineToUse)
{
    setWantsKeyboardFocus (true);

    // M6: a landed filmstrip thumbnail means "repaint eventually".
    // M6.3: it no longer repaints directly — a long clip can land
    // hundreds of thumbnails in a burst right after import, and one
    // full repaint per thumbnail flooded the message queue (part of
    // the M6.2 freeze audit). The callback just raises a flag (via a
    // SafePointer, because the store's worker thread posts these
    // asynchronously and may outlive the component by a hair) and
    // the 30 Hz timer repaints at most once per tick.
    filmstrip.setOnThumbnailReady (
        [safeThis = juce::Component::SafePointer<TimelineComponent> (this)]
        {
            if (safeThis != nullptr)
                safeThis->filmstripDirty = true;
        });

    startTimerHz (30);
}

TimelineComponent::~TimelineComponent()
{
    stopTimer();
}

//==============================================================================
juce::Colour TimelineComponent::getTrackColour (int trackIndex)
{
    static const juce::uint32 palette[] = {
        0xff4f7fa8, 0xffa85f4f, 0xff5fa86b, 0xff8a6fc9,
        0xffc9a227, 0xff4fa8a0, 0xffb45fa0, 0xff7d8aa5
    };

    return juce::Colour (palette[juce::jmax (0, trackIndex) % 8]);
}

int TimelineComponent::sampleToX (juce::int64 samples) const
{
    // M3.3: content X is pure timeline X — the header column is a
    // separate component now, so there is no header offset anymore.
    return (int) std::floor (engine.samplesToSeconds (samples) * pixelsPerSecond);
}

juce::int64 TimelineComponent::xToSample (int x) const
{
    const double seconds = (double) x / pixelsPerSecond;
    return engine.secondsToSamples (juce::jmax (0.0, seconds));
}

int TimelineComponent::yToTrackIndex (int y) const
{
    if (y < rulerHeightPx)
        return -1;

    return (y - rulerHeightPx) / trackHeight;
}

int TimelineComponent::getDesiredWidth() const
{
    double totalSeconds = engine.samplesToSeconds (
        const_cast<TimelineEngine&> (engine).getTotalLengthSamples());

    // M6: the scrollable area must also cover the clips. M6.4: video
    // clips now count toward the session length itself, so this extra
    // pass is redundant for them — but text clips still do not count,
    // and the user still needs to be able to scroll to them (M6.3).
    {
        juce::ScopedLock lock (const_cast<TimelineEngine&> (engine).getModelLock());

        for (auto& track : engine.getSession().tracks)
        {
            for (auto& clip : track.videoClips)
                totalSeconds = juce::jmax (totalSeconds, clip.getEndSeconds());

            for (auto& clip : track.textClips)
                totalSeconds = juce::jmax (totalSeconds, clip.getEndSeconds());
        }
    }

    return (int) std::ceil (totalSeconds * pixelsPerSecond) + 320;
}

int TimelineComponent::getDesiredHeight() const
{
    juce::ScopedLock lock (const_cast<TimelineEngine&> (engine).getModelLock());
    const int tracks = (int) engine.getSession().tracks.size();
    return juce::jmax (rulerHeightPx + tracks * trackHeight, 240);
}

void TimelineComponent::setPixelsPerSecond (double newPixelsPerSecond)
{
    pixelsPerSecond = juce::jlimit (4.0, 300.0, newPixelsPerSecond);
    lastPlayheadX = -1;
    repaint();
}

void TimelineComponent::setTrackHeight (int newHeightPx)
{
    const int clamped = juce::jlimit (minTrackHeightPx, maxTrackHeightPx, newHeightPx);

    if (clamped == trackHeight)
        return;

    trackHeight = clamped;
    repaint();
}

void TimelineComponent::mouseWheelMove (const juce::MouseEvent& e,
                                        const juce::MouseWheelDetails& wheel)
{
    // Ctrl+wheel over the timeline = vertical zoom (the V Zoom slider
    // in the bottom bar does the same thing).
    if (e.mods.isCtrlDown())
    {
        const int step = wheel.deltaY > 0.0f ? 8 : (wheel.deltaY < 0.0f ? -8 : 0);

        if (step != 0)
        {
            setTrackHeight (trackHeight + step);

            if (onTrackHeightChanged != nullptr)
                onTrackHeightChanged();
        }

        return;
    }

    juce::Component::mouseWheelMove (e, wheel);
}

void TimelineComponent::clearSelection()
{
    selectClip (-1);
    selectMidiClip (-1);
    selectVideoClip (-1);
    selectTextClip (-1);
}

void TimelineComponent::selectClip (juce::int64 clipId)
{
    if (selectedClipId == clipId && (clipId > 0
                                     || (selectedMidiClipId < 0 && selectedVideoClipId < 0
                                         && selectedTextClipId < 0)))
        return;

    selectedClipId = clipId;

    if (clipId > 0)
    {
        selectedMidiClipId = -1;   // one selection across all clip kinds
        selectedVideoClipId = -1;
        selectedTextClipId = -1;
    }

    repaint();

    if (onSelectionChanged != nullptr)
        onSelectionChanged();
}

void TimelineComponent::selectMidiClip (juce::int64 clipId)
{
    if (selectedMidiClipId == clipId && (clipId > 0
                                         || (selectedClipId < 0 && selectedVideoClipId < 0
                                             && selectedTextClipId < 0)))
        return;

    selectedMidiClipId = clipId;

    if (clipId > 0)
    {
        selectedClipId = -1;   // one selection across all clip kinds
        selectedVideoClipId = -1;
        selectedTextClipId = -1;
    }

    repaint();

    if (onSelectionChanged != nullptr)
        onSelectionChanged();
}

void TimelineComponent::selectVideoClip (juce::int64 clipId)
{
    if (selectedVideoClipId == clipId && (clipId > 0
                                          || (selectedClipId < 0 && selectedMidiClipId < 0
                                              && selectedTextClipId < 0)))
        return;

    selectedVideoClipId = clipId;

    if (clipId > 0)
    {
        selectedClipId = -1;   // one selection across all clip kinds
        selectedMidiClipId = -1;
        selectedTextClipId = -1;
    }

    repaint();

    if (onSelectionChanged != nullptr)
        onSelectionChanged();
}

void TimelineComponent::selectTextClip (juce::int64 clipId)
{
    if (selectedTextClipId == clipId && (clipId > 0
                                         || (selectedClipId < 0 && selectedMidiClipId < 0
                                             && selectedVideoClipId < 0)))
        return;

    selectedTextClipId = clipId;

    if (clipId > 0)
    {
        selectedClipId = -1;   // one selection across all clip kinds
        selectedMidiClipId = -1;
        selectedVideoClipId = -1;
    }

    repaint();

    if (onSelectionChanged != nullptr)
        onSelectionChanged();
}

//==============================================================================
void TimelineComponent::timerCallback()
{
    // Drop a selection whose clip no longer exists. (Header-control
    // sync lives in TrackHeaderComponent's own timer since M3.3.)
    if (engine.getModelVersion() != lastModelVersion)
    {
        if (selectedClipId > 0)
        {
            ClipData clip;
            juce::int64 ownerTrack = -1;

            if (! engine.getClipData (selectedClipId, clip, ownerTrack))
                selectClip (-1);
        }

        if (selectedMidiClipId > 0)
        {
            MidiClipData clip;
            juce::int64 ownerTrack = -1;

            if (! engine.getMidiClipData (selectedMidiClipId, clip, ownerTrack))
                selectMidiClip (-1);
        }

        if (selectedVideoClipId > 0)
        {
            VideoClipData clip;
            juce::int64 ownerTrack = -1;

            if (! engine.getVideoClipData (selectedVideoClipId, clip, ownerTrack))
                selectVideoClip (-1);
        }

        if (selectedTextClipId > 0)
        {
            TextClipData clip;
            juce::int64 ownerTrack = -1;

            if (! engine.getTextClipData (selectedTextClipId, clip, ownerTrack))
                selectTextClip (-1);
        }

        repaint();
    }

    // M6.3: coalesced filmstrip repaint (see the constructor).
    if (filmstripDirty)
    {
        filmstripDirty = false;
        repaint();
    }

    lastModelVersion = engine.getModelVersion();

    // Playhead: repaint only the strips the line leaves/enters.
    const int playheadX = sampleToX (engine.getPositionSamples());
    const double playheadSeconds = engine.getPositionSeconds();

    if (playheadX != lastPlayheadX)
    {
        if (lastPlayheadX >= 0)
            repaint (lastPlayheadX - 2, 0, 5, getHeight());

        repaint (playheadX - 2, 0, 5, getHeight());
        lastPlayheadX = playheadX;
    }

    followPlayheadIfNeeded (playheadX, playheadSeconds);
    lastPlayheadSeconds = playheadSeconds;

    if (dragMode != DragMode::none)
        repaint();
}

void TimelineComponent::followPlayheadIfNeeded (int playheadX, double playheadSeconds)
{
    // Page-style follow (standard DAW behaviour): when the playhead is
    // outside the visible area, jump the view so it re-enters near the
    // left edge. While playing this always applies — the on-screen test
    // below is two-sided, so a playhead that jumps left of the view
    // (return-to-start mid-playback) is followed just like one that runs
    // off the right edge. While stopped it applies only when the
    // position just jumped (transport seek, "|<" return-to-start): a
    // playhead parked off-screen while the user inspects another section
    // must never drag the view back, so an unmoved playhead never
    // scrolls. Never mid-drag (the user is busy editing or seeking;
    // during a ruler scrub the playhead follows the mouse anyway).
    if (dragMode != DragMode::none)
        return;

    auto* viewport = findParentComponentOfClass<juce::Viewport>();

    if (viewport == nullptr)
        return;

    const auto viewArea = viewport->getViewArea();

    if (viewArea.getWidth() <= 0)
        return;

    if (playheadX >= viewArea.getX() && playheadX < viewArea.getRight())
        return;   // still on screen

    if (! engine.isPlaying())
    {
        // Stopped: only a jump counts. Anything at or below the epsilon
        // is a parked playhead (or the very first tick, when there is no
        // previous position to compare against) — leave the view alone.
        if (lastPlayheadSeconds < 0.0
             || std::abs (playheadSeconds - lastPlayheadSeconds) <= 0.05)
            return;
    }

    const int margin = juce::jmax (16, (int) std::round (viewArea.getWidth() * 0.08));
    viewport->setViewPosition (juce::jmax (0, playheadX - margin),
                               viewport->getViewPositionY());
}

void TimelineComponent::ensurePlayheadVisible()
{
    // Explicit, deterministic follow for transport actions (return-to-
    // start from MainComponent, ruler seek below): the caller knows the
    // position just changed on purpose, so there are no playing-state
    // or position-delta tests — if the playhead is off-screen, scroll
    // so it lands near the left edge, exactly like the timer follow.
    // Still never mid-drag; the ruler path calls this from mouseUp,
    // after dragMode has been reset, so a finished scrub is followed.
    if (dragMode != DragMode::none)
        return;

    auto* viewport = findParentComponentOfClass<juce::Viewport>();

    if (viewport == nullptr)
        return;

    const auto viewArea = viewport->getViewArea();

    if (viewArea.getWidth() <= 0)
        return;

    const int playheadX = sampleToX (engine.getPositionSamples());

    if (playheadX >= viewArea.getX() && playheadX < viewArea.getRight())
        return;   // already on screen

    const int margin = juce::jmax (16, (int) std::round (viewArea.getWidth() * 0.08));
    viewport->setViewPosition (juce::jmax (0, playheadX - margin),
                               viewport->getViewPositionY());
}

//==============================================================================
void TimelineComponent::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour);

    juce::ScopedLock lock (engine.getModelLock());
    auto& session = engine.getSession();
    const int width = getWidth();
    const int numTracks = (int) session.tracks.size();

    //--- lane backgrounds (the headers are TrackHeaderComponent's) ---
    for (int i = 0; i < numTracks; ++i)
    {
        const int y = trackIndexToY (i);

        g.setColour (i % 2 == 0 ? laneColourA : laneColourB);
        g.fillRect (0, y, width, trackHeight);

        g.setColour (gridColour);
        g.drawHorizontalLine (y + trackHeight - 1, 0.0f, (float) width);
    }

    const auto clipBounds = juce::Rectangle<int> (0, 0, width, getHeight());
    g.saveState();
    g.reduceClipRegion (clipBounds);

    //--- ruler ---------------------------------------------------------------
    g.setColour (rulerColour);
    g.fillRect (0, 0, width, rulerHeightPx);

    // Choose a tick step so labels stay readable at any zoom.
    static const double stepChoices[] = { 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0 };
    double tickStep = stepChoices[juce::numElementsInArray (stepChoices) - 1];

    for (double step : stepChoices)
    {
        if (step * pixelsPerSecond >= 64.0)
        {
            tickStep = step;
            break;
        }
    }

    const double visibleStartSec = 0.0;
    const double visibleEndSec = (double) width / pixelsPerSecond;

    g.setFont (juce::FontOptions (10.0f));

    for (double t = visibleStartSec; t <= visibleEndSec + 1.0e-9; t += tickStep)
    {
        const int x = (int) std::floor (t * pixelsPerSecond);
        g.setColour (gridColour);
        g.drawVerticalLine (x, (float) rulerHeightPx - 7.0f, (float) rulerHeightPx);
        g.setColour (dimTextColour);
        g.drawText (formatRulerTime (t), x + 4, 2, 60, 12, juce::Justification::centredLeft);
    }

    // Bar grid (4 beats per bar at the session tempo).
    const double secondsPerBeat = 60.0 / juce::jmax (1.0, session.tempoBpm);
    const double secondsPerBar = secondsPerBeat * 4.0;

    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));

    for (int bar = 0;; ++bar)
    {
        const double t = bar * secondsPerBar;

        if (t > visibleEndSec)
            break;

        const int x = (int) std::floor (t * pixelsPerSecond);
        g.setColour (barGridColour);
        g.drawVerticalLine (x, (float) rulerHeightPx, (float) getHeight());
        g.setColour (accentColour.withAlpha (0.75f));
        g.drawText (juce::String (bar + 1), x + 4, rulerHeightPx - 13, 40, 11,
                    juce::Justification::centredLeft);
    }

    // Beat lines (lighter than bar lines).
    g.setColour (gridColour.withAlpha (0.45f));

    for (int beat = 0;; ++beat)
    {
        const double t = beat * secondsPerBeat;

        if (t > visibleEndSec)
            break;

        if (beat % 4 == 0)
            continue;   // bar line already drawn

        const int x = (int) std::floor (t * pixelsPerSecond);
        g.drawVerticalLine (x, (float) rulerHeightPx, (float) getHeight());
    }

    if (numTracks == 0)
    {
        g.setColour (dimTextColour);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (juce::String::fromUTF8 ("Import audio files to create tracks \xe2\x80\x94 drag & drop works too"),
                    24, rulerHeightPx + 24, 560, 24,
                    juce::Justification::centredLeft);
    }

    //--- clips -------------------------------------------------------------------
    const double samplesPerPixel = engine.getSampleRate() / juce::jmax (1.0, pixelsPerSecond);

    for (int trackIndex = 0; trackIndex < numTracks; ++trackIndex)
    {
        const auto& track = session.tracks[(size_t) trackIndex];
        const int laneY = trackIndexToY (trackIndex);
        const auto baseColour = getTrackColour (trackIndex);

        for (auto& clip : track.clips)
        {
            const int x0 = sampleToX (clip.start);
            const int x1 = juce::jmax (x0 + 2, sampleToX (clip.getEnd()));

            if (x1 < 0 || x0 > width)
                continue;

            auto rect = juce::Rectangle<int> (x0, laneY + 4, x1 - x0, trackHeight - 8);
            const bool selected = (clip.id == selectedClipId);

            g.setColour (baseColour.withAlpha (track.mute ? 0.35f : 0.85f));
            g.fillRoundedRectangle (rect.toFloat(), 4.0f);
            g.setColour (selected ? accentColour : baseColour.brighter (0.4f));
            g.drawRoundedRectangle (rect.toFloat(), 4.0f, selected ? 2.0f : 1.0f);

            // Waveform from the source peaks.
            if (auto* source = engine.findSource (clip.sourceId))
            {
                const int waveTop = rect.getY() + 15;
                const int waveBottom = rect.getBottom() - 3;
                const float waveMid = (waveTop + waveBottom) * 0.5f;
                const float waveHalf = juce::jmax (1.0f, (waveBottom - waveTop) * 0.5f);
                const int numPeaks = (int) source->peakMin.size();

                g.setColour (juce::Colours::white.withAlpha (0.75f));

                for (int px = rect.getX(); px < rect.getRight(); ++px)
                {
                    const auto sampleBegin = clip.offset
                        + (juce::int64) ((px - x0) * samplesPerPixel);
                    const auto sampleEnd = sampleBegin + (juce::int64) std::ceil (samplesPerPixel);

                    const int peakBegin = (int) (sampleBegin / SourceData::peakWindowSamples);
                    const int peakEnd = juce::jmin (numPeaks - 1,
                                                    (int) (sampleEnd / SourceData::peakWindowSamples));

                    if (peakBegin < 0 || peakBegin >= numPeaks || peakEnd < peakBegin)
                        continue;

                    float minValue = 0.0f, maxValue = 0.0f;

                    for (int p = peakBegin; p <= peakEnd; ++p)
                    {
                        minValue = juce::jmin (minValue, source->peakMin[(size_t) p]);
                        maxValue = juce::jmax (maxValue, source->peakMax[(size_t) p]);
                    }

                    const float top = waveMid - maxValue * waveHalf;
                    const float bottom = juce::jmax (top + 1.0f, waveMid - minValue * waveHalf);
                    g.drawVerticalLine (px, top, bottom);
                }
            }

            // Clip name.
            g.setColour (textColour);
            g.setFont (juce::FontOptions (11.0f));
            juce::String name;

            if (auto* source = engine.findSource (clip.sourceId))
                name = source->name;

            g.drawText (name, rect.getX() + 5, rect.getY() + 1, rect.getWidth() - 8, 13,
                        juce::Justification::centredLeft);
        }
    }

    //--- MIDI clips (M3) ---------------------------------------------------------
    for (int trackIndex = 0; trackIndex < numTracks; ++trackIndex)
    {
        const auto& track = session.tracks[(size_t) trackIndex];

        if (track.kind != TrackKind::midi)
            continue;

        const int laneY = trackIndexToY (trackIndex);
        const double secondsPerBeatPaint = 60.0 / juce::jmax (1.0, session.tempoBpm);

        for (auto& clip : track.midiClips)
        {
            const int x0 = (int) std::floor (clip.startSeconds * pixelsPerSecond);
            const int x1 = juce::jmax (x0 + 2,
                                       (int) std::floor (clip.getEndSeconds() * pixelsPerSecond));

            if (x1 < 0 || x0 > width)
                continue;

            auto rect = juce::Rectangle<int> (x0, laneY + 4, x1 - x0, trackHeight - 8);
            const bool selected = (clip.id == selectedMidiClipId);

            g.setColour (midiClipColour.withAlpha (track.mute ? 0.35f : 0.9f));
            g.fillRoundedRectangle (rect.toFloat(), 4.0f);
            g.setColour (selected ? accentColour : midiClipColour.brighter (0.5f));
            g.drawRoundedRectangle (rect.toFloat(), 4.0f, selected ? 2.0f : 1.0f);

            // A small tick per note, at the note's vertical position.
            g.setColour (juce::Colours::white.withAlpha (0.8f));

            for (auto& note : clip.notes)
            {
                // M6.9: ticks sit at their WINDOW position (content
                // beat minus the clip's offset); ticks outside the
                // clip rect are skipped by the check below, exactly
                // like notes windowed out on the right already were.
                const int noteX = x0 + (int) std::floor ((note.startBeat - clip.offsetBeats)
                                                         * secondsPerBeatPaint
                                                         * pixelsPerSecond);

                if (noteX < rect.getX() || noteX >= rect.getRight())
                    continue;

                const int noteY = rect.getBottom() - 4
                                    - (juce::jlimit (0, 127, note.noteNumber) % 24)
                                          * juce::jmax (1, (rect.getHeight() - 8) / 24);
                g.fillRect (noteX, noteY, juce::jmax (2, (int) std::floor (
                                        note.lengthBeats * secondsPerBeatPaint * pixelsPerSecond)), 2);
            }

            g.setColour (textColour);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText ("MIDI", rect.getX() + 5, rect.getY() + 1, rect.getWidth() - 8, 13,
                        juce::Justification::centredLeft);
        }
    }

    //--- Video clips (M6): filmstrip over a slate body ---------------------
    // M6.3: clips paint in START order (ties by id) — where clips
    // overlap (cross-dissolves), the later-starting clip is the one
    // on top, so it paints over the earlier one, matching the frame
    // mapping in VideoCompositing.
    const auto visibleArea = g.getClipBounds();

    for (int trackIndex = 0; trackIndex < numTracks; ++trackIndex)
    {
        const auto& track = session.tracks[(size_t) trackIndex];

        if (track.kind != TrackKind::video)
            continue;

        const int laneY = trackIndexToY (trackIndex);

        std::vector<const VideoClipData*> paintOrder;

        for (auto& clip : track.videoClips)
            paintOrder.push_back (&clip);

        std::sort (paintOrder.begin(), paintOrder.end(),
                   [] (const VideoClipData* a, const VideoClipData* b)
                   {
                       if (a->startSeconds != b->startSeconds)
                           return a->startSeconds < b->startSeconds;

                       return a->id < b->id;
                   });

        for (auto* clipPtr : paintOrder)
        {
            const auto& clip = *clipPtr;
            const int x0 = (int) std::floor (clip.startSeconds * pixelsPerSecond);
            const int x1 = juce::jmax (x0 + 2,
                                       (int) std::floor (clip.getEndSeconds() * pixelsPerSecond));

            if (x1 < 0 || x0 > width)
                continue;

            auto rect = juce::Rectangle<int> (x0, laneY + 4, x1 - x0, trackHeight - 8);
            const bool selected = (clip.id == selectedVideoClipId);

            g.setColour (videoClipColour);
            g.fillRoundedRectangle (rect.toFloat(), 4.0f);

            // One thumbnail per whole source second. The segment for
            // source second s covers timeline [start + (s - offset),
            // +1 s); getThumbnail answers from the store's cache or
            // queues a background extraction and returns nothing, in
            // which case the plain clip colour shows through for now.
            // M6.3: segments outside the region actually being
            // painted are skipped entirely — a paint no longer queues
            // extractions for off-screen seconds (freeze audit).
            const auto stripArea = rect.withTrimmedTop (14);

            if (stripArea.getHeight() >= 12)
            {
                const juce::File clipFile (clip.filePath);
                const int firstSecond = (int) std::floor (clip.offsetSeconds);

                for (int s = firstSecond;; ++s)
                {
                    const double segmentStart = clip.startSeconds
                                                + ((double) s - clip.offsetSeconds);

                    if (segmentStart >= clip.getEndSeconds())
                        break;

                    const int segmentX0 = juce::jmax ((int) std::floor (segmentStart * pixelsPerSecond),
                                                      rect.getX());
                    const int segmentX1 = juce::jmin ((int) std::floor ((segmentStart + 1.0) * pixelsPerSecond),
                                                      rect.getRight());

                    if (segmentX1 - segmentX0 < 2)
                        continue;

                    if (segmentX1 <= visibleArea.getX()
                         || segmentX0 >= visibleArea.getRight())
                        continue;   // not in the painted region

                    auto thumbnail = filmstrip.getThumbnail (clipFile, (double) s,
                                                             stripArea.getHeight());

                    if (thumbnail.isValid())
                        g.drawImageWithin (thumbnail, segmentX0, stripArea.getY(),
                                           segmentX1 - segmentX0, stripArea.getHeight(),
                                           juce::RectanglePlacement::centred, false);
                }
            }

            // M6.3: fade shading — a translucent black triangle over
            // each faded end, with the diagonal running from the top
            // corner (fully faded at the edge) down to where the fade
            // completes at the bottom. Matches the corner the user
            // drags to set the fade (see hitTestVideoClip).
            auto drawFadeShade = [&] (bool fadeIn, double fadeSeconds)
            {
                const int fadeWidth = (int) std::floor (fadeSeconds * pixelsPerSecond);

                if (fadeWidth < 2)
                    return;

                const auto rf = rect.toFloat();
                juce::Path shade;
                float diagonalTopX = 0.0f, diagonalBottomX = 0.0f;

                if (fadeIn)
                {
                    shade.addTriangle (rf.getX(), rf.getY(),
                                       rf.getX() + (float) fadeWidth, rf.getY(),
                                       rf.getX(), rf.getBottom());
                    diagonalTopX = rf.getX() + (float) fadeWidth;
                    diagonalBottomX = rf.getX();
                }
                else
                {
                    shade.addTriangle (rf.getRight(), rf.getY(),
                                       rf.getRight() - (float) fadeWidth, rf.getY(),
                                       rf.getRight(), rf.getBottom());
                    diagonalTopX = rf.getRight() - (float) fadeWidth;
                    diagonalBottomX = rf.getRight();
                }

                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.fillPath (shade);
                g.setColour (juce::Colours::white.withAlpha (0.5f));
                g.drawLine (diagonalTopX, rf.getY(), diagonalBottomX, rf.getBottom(), 1.0f);
            };

            drawFadeShade (true, clip.fadeInSeconds);
            drawFadeShade (false, clip.fadeOutSeconds);

            g.setColour (selected ? accentColour : videoClipColour.brighter (0.5f));
            g.drawRoundedRectangle (rect.toFloat(), 4.0f, selected ? 2.0f : 1.0f);

            g.setColour (textColour);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (clip.getDisplayName(), rect.getX() + 5, rect.getY() + 1,
                        rect.getWidth() - 8, 13, juce::Justification::centredLeft);
        }
    }

    //--- Text clips (M6.3): telop chips showing their first line --------
    for (int trackIndex = 0; trackIndex < numTracks; ++trackIndex)
    {
        const auto& track = session.tracks[(size_t) trackIndex];

        if (track.kind != TrackKind::text)
            continue;

        const int laneY = trackIndexToY (trackIndex);

        std::vector<const TextClipData*> paintOrder;

        for (auto& clip : track.textClips)
            paintOrder.push_back (&clip);

        std::sort (paintOrder.begin(), paintOrder.end(),
                   [] (const TextClipData* a, const TextClipData* b)
                   {
                       if (a->startSeconds != b->startSeconds)
                           return a->startSeconds < b->startSeconds;

                       return a->id < b->id;
                   });

        for (auto* clipPtr : paintOrder)
        {
            const auto& clip = *clipPtr;
            const int x0 = (int) std::floor (clip.startSeconds * pixelsPerSecond);
            const int x1 = juce::jmax (x0 + 2,
                                       (int) std::floor (clip.getEndSeconds() * pixelsPerSecond));

            if (x1 < 0 || x0 > width)
                continue;

            auto rect = juce::Rectangle<int> (x0, laneY + 4, x1 - x0, trackHeight - 8);
            const bool selected = (clip.id == selectedTextClipId);

            g.setColour (textClipColour.withAlpha (0.92f));
            g.fillRoundedRectangle (rect.toFloat(), 4.0f);
            g.setColour (selected ? accentColour : textClipColour.brighter (0.6f));
            g.drawRoundedRectangle (rect.toFloat(), 4.0f, selected ? 2.0f : 1.0f);

            // The telop's first line stands in for the whole text
            // (double-click edits it); drawText clips at the edge.
            g.setColour (textColour);
            g.setFont (juce::FontOptions (11.0f));
            const auto firstLine = clip.getFirstLine();
            g.drawText (firstLine.isNotEmpty() ? firstLine : juce::String ("(empty)"),
                        rect.getX() + 5, rect.getY() + 1, rect.getWidth() - 8, 13,
                        juce::Justification::centredLeft);
        }
    }

    g.restoreState();

    //--- playhead ------------------------------------------------------------------
    const int playheadX = sampleToX (engine.getPositionSamples());

    if (playheadX >= 0)
    {
        g.setColour (playheadColour);
        g.drawVerticalLine (playheadX, 0.0f, (float) getHeight());
        g.fillPath ([&]
        {
            juce::Path path;
            path.addTriangle ((float) playheadX - 5.0f, 0.0f,
                              (float) playheadX + 5.0f, 0.0f,
                              (float) playheadX, 9.0f);
            return path;
        }());
    }
}

//==============================================================================
// Mouse editing
//==============================================================================
TimelineComponent::ClipHit TimelineComponent::hitTestClip (int x, int y) const
{
    ClipHit result;
    const int trackIndex = yToTrackIndex (y);

    if (trackIndex < 0 || x < 0)
        return result;

    auto& session = engine.getSession();

    if (trackIndex >= (int) session.tracks.size())
        return result;

    const auto& track = session.tracks[(size_t) trackIndex];

    // Later clips win (they are drawn last within the track loop order we
    // care about); scan in reverse so overlapping clips pick the top one.
    for (int i = (int) track.clips.size() - 1; i >= 0; --i)
    {
        const auto& clip = track.clips[(size_t) i];
        const int x0 = sampleToX (clip.start);
        const int x1 = sampleToX (clip.getEnd());

        if (x >= x0 && x <= x1)
        {
            result.clipId = clip.id;
            result.trackIndex = trackIndex;

            if (x - x0 <= edgeZonePx && clip.length > engine.secondsToSamples (0.05))
                result.zone = 1;
            else if (x1 - x <= edgeZonePx && clip.length > engine.secondsToSamples (0.05))
                result.zone = 2;
            else
                result.zone = 0;

            return result;
        }
    }

    return result;
}

TimelineComponent::ClipHit TimelineComponent::hitTestMidiClip (int x, int y) const
{
    ClipHit result;
    const int trackIndex = yToTrackIndex (y);

    if (trackIndex < 0 || x < 0)
        return result;

    auto& session = engine.getSession();

    if (trackIndex >= (int) session.tracks.size())
        return result;

    const auto& track = session.tracks[(size_t) trackIndex];

    if (track.kind != TrackKind::midi)
        return result;

    // M6.9: the 6 px edge zones trim, exactly like the other clip
    // kinds (the body between them still moves the clip).
    for (int i = (int) track.midiClips.size() - 1; i >= 0; --i)
    {
        const auto& clip = track.midiClips[(size_t) i];
        const int x0 = (int) std::floor (clip.startSeconds * pixelsPerSecond);
        const int x1 = (int) std::floor (clip.getEndSeconds() * pixelsPerSecond);

        if (x >= x0 && x <= x1)
        {
            result.clipId = clip.id;
            result.trackIndex = trackIndex;

            if (x - x0 <= edgeZonePx && clip.lengthSeconds > 0.05)
                result.zone = 1;
            else if (x1 - x <= edgeZonePx && clip.lengthSeconds > 0.05)
                result.zone = 2;
            else
                result.zone = 0;

            return result;
        }
    }

    return result;
}

TimelineComponent::ClipHit TimelineComponent::hitTestVideoClip (int x, int y) const
{
    ClipHit result;
    const int trackIndex = yToTrackIndex (y);

    if (trackIndex < 0 || x < 0)
        return result;

    auto& session = engine.getSession();

    if (trackIndex >= (int) session.tracks.size())
        return result;

    const auto& track = session.tracks[(size_t) trackIndex];

    if (track.kind != TrackKind::video)
        return result;

    // M6.3: overlapping clips pick the one that PAINTS on top (the
    // latest start, ties by id) — scan in paint order, reversed.
    std::vector<const VideoClipData*> pickOrder;

    for (auto& clip : track.videoClips)
        pickOrder.push_back (&clip);

    std::sort (pickOrder.begin(), pickOrder.end(),
               [] (const VideoClipData* a, const VideoClipData* b)
               {
                   if (a->startSeconds != b->startSeconds)
                       return a->startSeconds < b->startSeconds;

                   return a->id < b->id;
               });

    // The clip body occupies the lane row inset by 4 px top/bottom
    // (see paint); its top band is where the fade corners live.
    const int laneY = trackIndexToY (trackIndex);
    const int rectTop = laneY + 4;
    constexpr int fadeBandPx = 10;

    for (int i = (int) pickOrder.size() - 1; i >= 0; --i)
    {
        const auto& clip = *pickOrder[(size_t) i];
        const int x0 = (int) std::floor (clip.startSeconds * pixelsPerSecond);
        const int x1 = (int) std::floor (clip.getEndSeconds() * pixelsPerSecond);

        if (x >= x0 && x <= x1)
        {
            result.clipId = clip.id;
            result.trackIndex = trackIndex;

            // M6.3: the TOP corners set the fades (zones 3/4) and win
            // over the trim edges inside the top band — the corner
            // is where the fade's diagonal starts, per the spec's
            // "drag the top corner" gesture. The handle is at least
            // 12 px wide (a zero fade still has a corner to grab)
            // and grows with the current fade.
            if (y >= rectTop && y < rectTop + fadeBandPx
                 && clip.durationSeconds > 0.05)
            {
                const int fadeInHandle = juce::jmax (
                    12, (int) std::floor (clip.fadeInSeconds * pixelsPerSecond));
                const int fadeOutHandle = juce::jmax (
                    12, (int) std::floor (clip.fadeOutSeconds * pixelsPerSecond));

                if (x - x0 <= fadeInHandle && x - x0 <= x1 - x)
                {
                    result.zone = 3;
                    return result;
                }

                if (x1 - x <= fadeOutHandle)
                {
                    result.zone = 4;
                    return result;
                }
            }

            if (x - x0 <= edgeZonePx && clip.durationSeconds > 0.05)
                result.zone = 1;
            else if (x1 - x <= edgeZonePx && clip.durationSeconds > 0.05)
                result.zone = 2;
            else
                result.zone = 0;

            return result;
        }
    }

    return result;
}

TimelineComponent::ClipHit TimelineComponent::hitTestTextClip (int x, int y) const
{
    ClipHit result;
    const int trackIndex = yToTrackIndex (y);

    if (trackIndex < 0 || x < 0)
        return result;

    auto& session = engine.getSession();

    if (trackIndex >= (int) session.tracks.size())
        return result;

    const auto& track = session.tracks[(size_t) trackIndex];

    if (track.kind != TrackKind::text)
        return result;

    // Like video clips: latest start paints/picks on top; body moves,
    // the 6 px edges trim.
    std::vector<const TextClipData*> pickOrder;

    for (auto& clip : track.textClips)
        pickOrder.push_back (&clip);

    std::sort (pickOrder.begin(), pickOrder.end(),
               [] (const TextClipData* a, const TextClipData* b)
               {
                   if (a->startSeconds != b->startSeconds)
                       return a->startSeconds < b->startSeconds;

                   return a->id < b->id;
               });

    for (int i = (int) pickOrder.size() - 1; i >= 0; --i)
    {
        const auto& clip = *pickOrder[(size_t) i];
        const int x0 = (int) std::floor (clip.startSeconds * pixelsPerSecond);
        const int x1 = (int) std::floor (clip.getEndSeconds() * pixelsPerSecond);

        if (x >= x0 && x <= x1)
        {
            result.clipId = clip.id;
            result.trackIndex = trackIndex;

            if (x - x0 <= edgeZonePx && clip.durationSeconds > 0.05)
                result.zone = 1;
            else if (x1 - x <= edgeZonePx && clip.durationSeconds > 0.05)
                result.zone = 2;
            else
                result.zone = 0;

            return result;
        }
    }

    return result;
}

void TimelineComponent::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    if (e.position.y < rulerHeightPx)
    {
        dragMode = DragMode::seek;
        engine.setPositionSamples (xToSample ((int) e.position.x));
        repaint();
        return;
    }

    juce::ScopedLock lock (engine.getModelLock());

    // M3: MIDI lanes hit-test their own clip type first.
    const auto midiHit = hitTestMidiClip ((int) e.position.x, (int) e.position.y);

    if (midiHit.clipId > 0)
    {
        selectMidiClip (midiHit.clipId);

        MidiClipData clip;
        juce::int64 ownerTrack = -1;

        if (engine.getMidiClipData (midiHit.clipId, clip, ownerTrack))
        {
            dragMidiClipId = midiHit.clipId;
            dragTrackId = ownerTrack;
            dragMidiOriginal = clip;
            dragStartX = (int) e.position.x;
            dragStartY = (int) e.position.y;
            dragMode = midiHit.zone == 1 ? DragMode::trimMidiLeft
                       : midiHit.zone == 2 ? DragMode::trimMidiRight
                                           : DragMode::moveMidiClip;
        }

        return;
    }

    // M6: the video lane hit-tests its own clip type.
    const auto videoHit = hitTestVideoClip ((int) e.position.x, (int) e.position.y);

    if (videoHit.clipId > 0)
    {
        selectVideoClip (videoHit.clipId);

        VideoClipData clip;
        juce::int64 ownerTrack = -1;

        if (engine.getVideoClipData (videoHit.clipId, clip, ownerTrack))
        {
            dragVideoClipId = videoHit.clipId;
            dragTrackId = ownerTrack;
            dragVideoOriginal = clip;
            dragStartX = (int) e.position.x;
            dragStartY = (int) e.position.y;
            dragMode = videoHit.zone == 1 ? DragMode::trimVideoLeft
                       : videoHit.zone == 2 ? DragMode::trimVideoRight
                       : videoHit.zone == 3 ? DragMode::fadeVideoIn     // M6.3
                       : videoHit.zone == 4 ? DragMode::fadeVideoOut    // M6.3
                                            : DragMode::moveVideoClip;
        }

        return;
    }

    // M6.3: the text lane hit-tests its own clip type.
    const auto textHit = hitTestTextClip ((int) e.position.x, (int) e.position.y);

    if (textHit.clipId > 0)
    {
        selectTextClip (textHit.clipId);

        TextClipData clip;
        juce::int64 ownerTrack = -1;

        if (engine.getTextClipData (textHit.clipId, clip, ownerTrack))
        {
            dragTextClipId = textHit.clipId;
            dragTrackId = ownerTrack;
            dragTextOriginal = clip;
            dragStartX = (int) e.position.x;
            dragStartY = (int) e.position.y;
            dragMode = textHit.zone == 1 ? DragMode::trimTextLeft
                       : textHit.zone == 2 ? DragMode::trimTextRight
                                           : DragMode::moveTextClip;
        }

        return;
    }

    const auto hit = hitTestClip ((int) e.position.x, (int) e.position.y);

    if (hit.clipId > 0)
    {
        selectClip (hit.clipId);

        ClipData clip;
        juce::int64 ownerTrack = -1;

        if (engine.getClipData (hit.clipId, clip, ownerTrack))
        {
            dragClipId = hit.clipId;
            dragTrackId = ownerTrack;
            dragOriginal = clip;
            dragStartX = (int) e.position.x;
            dragStartY = (int) e.position.y;
            dragMode = hit.zone == 1 ? DragMode::trimLeft
                       : hit.zone == 2 ? DragMode::trimRight
                                       : DragMode::moveClip;
        }
    }
    else
    {
        clearSelection();
        dragMode = DragMode::none;
    }
}

void TimelineComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    // M3: double-click is a MIDI-lane gesture; M6.3 adds the text
    // lane (clip -> editor). Audio/video lanes keep having no
    // double-click behaviour. (JUCE delivers mouseDown twice
    // before this, so a click on empty lane space has already cleared
    // the selection, and a clip double-click has selected the clip.)
    if (e.position.y < rulerHeightPx)
        return;

    juce::ScopedLock lock (engine.getModelLock());
    auto& session = engine.getSession();
    const int trackIndex = yToTrackIndex ((int) e.position.y);

    if (trackIndex < 0 || trackIndex >= (int) session.tracks.size())
        return;

    const auto& track = session.tracks[(size_t) trackIndex];

    // M6.3: double-clicking a TEXT clip opens its editor (a clip
    // double-click has already selected it in mouseDown). Empty text
    // lane space does nothing — telops arrive via SRT import.
    if (track.kind == TrackKind::text)
    {
        const auto textHit = hitTestTextClip ((int) e.position.x, (int) e.position.y);

        if (textHit.clipId > 0 && onTextClipDoubleClicked != nullptr)
            onTextClipDoubleClicked (track.id, textHit.clipId);

        return;
    }

    if (track.kind != TrackKind::midi)
        return;

    const auto hit = hitTestMidiClip ((int) e.position.x, (int) e.position.y);

    if (hit.clipId > 0)
    {
        if (onMidiClipDoubleClicked != nullptr)
            onMidiClipDoubleClicked (track.id, hit.clipId);

        return;
    }

    // Empty lane space: create a clip, snapped to the bar under the
    // click, one bar long (engine default).
    const double secondsPerBar = 4.0 * 60.0 / juce::jmax (1.0, session.tempoBpm);
    const double clickSeconds = (double) e.position.x / pixelsPerSecond;
    const double snapped = std::floor (juce::jmax (0.0, clickSeconds) / secondsPerBar) * secondsPerBar;
    const auto newClipId = engine.addMidiClip (track.id, snapped);

    if (newClipId > 0)
        selectMidiClip (newClipId);
}

void TimelineComponent::mouseDrag (const juce::MouseEvent& e)
{
    const int x = (int) e.position.x;
    const int y = (int) e.position.y;

    switch (dragMode)
    {
        case DragMode::seek:
            engine.setPositionSamples (xToSample (x));
            repaint();
            break;

        case DragMode::moveClip:
        {
            juce::ScopedLock lock (engine.getModelLock());
            auto& session = engine.getSession();
            const int targetIndex = juce::jlimit (0, (int) session.tracks.size() - 1,
                                                  yToTrackIndex (y));

            if (session.tracks.empty())
                break;

            ClipData values = dragOriginal;
            values.start = juce::jmax<juce::int64> (
                0, dragOriginal.start + (xToSample (x) - xToSample (dragStartX)));
            engine.updateClip (dragClipId, session.tracks[(size_t) targetIndex].id, values);
            repaint();
            break;
        }

        case DragMode::trimLeft:
        {
            const auto delta = xToSample (x) - xToSample (dragStartX);
            const auto minLength = juce::jmax<juce::int64> (1, engine.secondsToSamples (0.01));

            ClipData values = dragOriginal;
            auto newStart = dragOriginal.start + delta;
            newStart = juce::jlimit (dragOriginal.start - dragOriginal.offset,
                                     dragOriginal.start + dragOriginal.length - minLength,
                                     newStart);
            values.offset += (newStart - dragOriginal.start);
            values.length -= (newStart - dragOriginal.start);
            values.start = newStart;
            engine.updateClip (dragClipId, dragTrackId, values);
            repaint();
            break;
        }

        case DragMode::trimRight:
        {
            const auto delta = xToSample (x) - xToSample (dragStartX);
            const auto minLength = juce::jmax<juce::int64> (1, engine.secondsToSamples (0.01));

            ClipData values = dragOriginal;
            values.length = juce::jmax (minLength, dragOriginal.length + delta);
            engine.updateClip (dragClipId, dragTrackId, values);
            repaint();
            break;
        }

        case DragMode::moveMidiClip:
        {
            // Horizontal move only, within the clip's own track.
            const double deltaSeconds = (xToSample (x) - xToSample (dragStartX))
                                          / engine.getSampleRate();
            engine.moveMidiClip (dragMidiClipId, dragMidiOriginal.startSeconds + deltaSeconds);
            repaint();
            break;
        }

        case DragMode::trimMidiLeft:
        case DragMode::trimMidiRight:
        {
            // M6.9: same gestures as the other clip kinds, in seconds.
            // A right trim only changes the length (extending reveals
            // empty beats; shortening windows the content — the notes
            // themselves are preserved). A left trim slides the beat
            // window: start and offsetBeats move together, so every
            // surviving note keeps its absolute timeline position.
            // The engine applies the final clamps (timeline start,
            // offset >= 0, length >= 0.25 beats).
            const double deltaSeconds = (xToSample (x) - xToSample (dragStartX))
                                          / engine.getSampleRate();
            const double secondsPerBeat = 60.0 / juce::jmax (1.0, engine.getTempo());
            const double minLength = 0.25 * secondsPerBeat;

            MidiClipData values = dragMidiOriginal;

            if (dragMode == DragMode::trimMidiLeft)
            {
                auto newStart = dragMidiOriginal.startSeconds + deltaSeconds;
                newStart = juce::jlimit (
                    juce::jmax (0.0, dragMidiOriginal.startSeconds
                                         - dragMidiOriginal.offsetBeats * secondsPerBeat),
                    dragMidiOriginal.getEndSeconds() - minLength,
                    newStart);
                values.offsetBeats += (newStart - dragMidiOriginal.startSeconds)
                                      / secondsPerBeat;
                values.lengthSeconds -= (newStart - dragMidiOriginal.startSeconds);
                values.startSeconds = newStart;
            }
            else
            {
                values.lengthSeconds = juce::jmax (
                    minLength, dragMidiOriginal.lengthSeconds + deltaSeconds);
            }

            engine.updateMidiClip (dragMidiClipId, values);
            repaint();
            break;
        }

        case DragMode::moveVideoClip:
        case DragMode::trimVideoLeft:
        case DragMode::trimVideoRight:
        case DragMode::fadeVideoIn:     // M6.3
        case DragMode::fadeVideoOut:    // M6.3
        {
            // M6: same gestures as audio clips, in seconds; the
            // engine clamps the result to the source bounds (and,
            // M6.3, keeps the fades inside the duration).
            const double deltaSeconds = (xToSample (x) - xToSample (dragStartX))
                                          / engine.getSampleRate();
            VideoClipData values = dragVideoOriginal;

            if (dragMode == DragMode::moveVideoClip)
            {
                values.startSeconds = dragVideoOriginal.startSeconds + deltaSeconds;
            }
            else if (dragMode == DragMode::trimVideoLeft)
            {
                const double minLength = 0.01;
                auto newStart = dragVideoOriginal.startSeconds + deltaSeconds;
                newStart = juce::jlimit (dragVideoOriginal.startSeconds
                                             - dragVideoOriginal.offsetSeconds,
                                         dragVideoOriginal.getEndSeconds() - minLength,
                                         newStart);
                values.offsetSeconds += (newStart - dragVideoOriginal.startSeconds);
                values.durationSeconds -= (newStart - dragVideoOriginal.startSeconds);
                values.startSeconds = newStart;
            }
            else if (dragMode == DragMode::trimVideoRight)
            {
                values.durationSeconds = juce::jmax (0.01,
                                                     dragVideoOriginal.durationSeconds + deltaSeconds);
            }
            else if (dragMode == DragMode::fadeVideoIn)
            {
                // Dragging the top-left corner right grows the fade
                // (the shaded triangle follows the pointer).
                values.fadeInSeconds = juce::jlimit (
                    0.0, dragVideoOriginal.durationSeconds,
                    dragVideoOriginal.fadeInSeconds + deltaSeconds);
            }
            else   // fadeVideoOut: dragging the top-right corner LEFT
            {      // grows the fade, so the delta subtracts.
                values.fadeOutSeconds = juce::jlimit (
                    0.0, dragVideoOriginal.durationSeconds,
                    dragVideoOriginal.fadeOutSeconds - deltaSeconds);
            }

            engine.updateVideoClip (dragVideoClipId, values);
            repaint();
            break;
        }

        case DragMode::moveTextClip:
        case DragMode::trimTextLeft:
        case DragMode::trimTextRight:
        {
            // M6.3: text clips move/trim in seconds on their own
            // track; there is no source window behind them, so a
            // left trim simply re-times the text's appearance.
            const double deltaSeconds = (xToSample (x) - xToSample (dragStartX))
                                          / engine.getSampleRate();
            TextClipData values = dragTextOriginal;

            if (dragMode == DragMode::moveTextClip)
            {
                values.startSeconds = dragTextOriginal.startSeconds + deltaSeconds;
            }
            else if (dragMode == DragMode::trimTextLeft)
            {
                const double minLength = 0.05;
                auto newStart = dragTextOriginal.startSeconds + deltaSeconds;
                newStart = juce::jlimit (0.0,
                                         dragTextOriginal.getEndSeconds() - minLength,
                                         newStart);
                values.durationSeconds -= (newStart - dragTextOriginal.startSeconds);
                values.startSeconds = newStart;
            }
            else
            {
                values.durationSeconds = juce::jmax (0.05,
                                                     dragTextOriginal.durationSeconds + deltaSeconds);
            }

            engine.updateTextClip (dragTextClipId, values);
            repaint();
            break;
        }

        case DragMode::none:
            break;
    }
}

void TimelineComponent::mouseUp (const juce::MouseEvent&)
{
    const bool wasSeek = (dragMode == DragMode::seek);
    dragMode = DragMode::none;
    dragClipId = -1;
    dragMidiClipId = -1;
    dragVideoClipId = -1;
    dragTextClipId = -1;

    // A ruler click/scrub just set the position explicitly: make sure
    // the playhead is on screen. Called after the drag reset above so
    // ensurePlayheadVisible does not suppress it as mid-drag.
    if (wasSeek)
        ensurePlayheadVisible();
}

void TimelineComponent::mouseMove (const juce::MouseEvent& e)
{
    if (e.position.y < rulerHeightPx)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        return;
    }

    {
        juce::ScopedLock lock (engine.getModelLock());

        // M6.9: MIDI clip edges trim now, so they get the resize
        // cursor too (checked first, like mouseDown).
        const auto midiHit = hitTestMidiClip ((int) e.position.x, (int) e.position.y);

        if (midiHit.clipId > 0 && midiHit.zone != 0)
        {
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            return;
        }

        const auto hit = hitTestClip ((int) e.position.x, (int) e.position.y);

        if (hit.clipId > 0 && hit.zone != 0)
        {
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            return;
        }

        const auto videoHit = hitTestVideoClip ((int) e.position.x, (int) e.position.y);

        if (videoHit.clipId > 0 && videoHit.zone != 0)
        {
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            return;
        }

        const auto textHit = hitTestTextClip ((int) e.position.x, (int) e.position.y);

        if (textHit.clipId > 0 && textHit.zone != 0)
        {
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            return;
        }
    }

    setMouseCursor (juce::MouseCursor::NormalCursor);
}

bool TimelineComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (selectedClipId > 0)
        {
            const auto toRemove = selectedClipId;
            selectClip (-1);
            engine.removeClip (toRemove);
            repaint();
            return true;
        }

        if (selectedMidiClipId > 0)
        {
            const auto toRemove = selectedMidiClipId;
            selectMidiClip (-1);
            engine.removeMidiClip (toRemove);
            repaint();
            return true;
        }

        if (selectedVideoClipId > 0)
        {
            const auto toRemove = selectedVideoClipId;
            selectVideoClip (-1);
            engine.removeVideoClip (toRemove);
            repaint();
            return true;
        }

        if (selectedTextClipId > 0)
        {
            const auto toRemove = selectedTextClipId;
            selectTextClip (-1);
            engine.removeTextClip (toRemove);
            repaint();
            return true;
        }
    }

    return false;
}
