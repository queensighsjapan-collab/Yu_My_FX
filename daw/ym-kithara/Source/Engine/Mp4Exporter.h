#pragma once

#include <JuceHeader.h>
#include <atomic>

class TimelineEngine;

//==============================================================================
/** Options for one mp4 export (M6 Phase 2). The frame rate is not an
    option here: an export always runs at the session timecode rate
    (SessionData::timecodeFps), exactly like the display. */
struct Mp4ExportOptions
{
    int width  = 0;   // output picture size; 0/0 = "Original" — the
    int height = 0;   // earliest video clip's dimensions, even-rounded
    int crf    = 23;  // libx264 quality: 18 High / 23 Standard / 28 Light
};

//==============================================================================
/**
    Mp4Exporter — renders the timeline to an H.264/AAC mp4 (M6 Phase 2).

    The video track's clips are sampled at the session timecode rate
    (frames duplicated/dropped as needed when a source's own fps
    differs), fitted into the output frame with their aspect preserved
    and black bars filling the rest; timeline spans with no clip are
    black. M6.3: composition goes through the shared layer mapping
    (VideoComposite.h) — overlapping clips cross-dissolve, clip fades
    fade from/to black, and the text track's telops are drawn over
    every frame (including over black gap frames). The audio is the session mixdown rendered through the
    engine's offline path over exactly the WAV export span (session
    length + 2 s tail), encoded as 48 kHz stereo AAC at 192 kbps
    (resampled from the engine rate when needed). The container is
    written with faststart so the file streams and plays anywhere.

    Runs entirely on the CALLING thread (the UI drives it from a
    progress thread). progress01 (optional) is updated as frames are
    encoded. shouldCancel (optional) is polled throughout: when set,
    everything is closed, the partial file is DELETED, and the call
    returns false with errorOut = "Export cancelled.".

    Threading: the exporter owns a dedicated VideoDecoder (the
    preview's decoder is never touched) and holds the engine's model
    lock for the whole span, like the WAV export. The realtime audio
    path is kept out by the engine's offlineRendering flag.
*/
class Mp4Exporter final
{
public:
    Mp4Exporter() = default;
    bool exportMp4 (TimelineEngine& engine, const juce::File& target,
                    const Mp4ExportOptions& options, juce::String& errorOut,
                    std::atomic<float>* progress01,
                    std::atomic<bool>* shouldCancel);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Mp4Exporter)
};
