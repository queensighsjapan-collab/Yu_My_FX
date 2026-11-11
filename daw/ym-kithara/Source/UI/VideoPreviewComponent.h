#pragma once

#include <JuceHeader.h>
#include "TimelineEngine.h"
#include "VideoDecoder.h"   // VideoFrameService
#include "VideoComposite.h"   // layer mapping / blending / telop (M6.3)
#include "Timecode.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>

//==============================================================================
/**
    VideoFrameFeed — the ONE frame stream behind the video preview
    (M6; shared feed since M6.2; layered since M6.3).

    A 30 Hz timer reads the engine's playhead position, maps it
    through the SHARED layer mapping (VideoCompositing::computeLayers
    — the same code the mp4 exporter uses) and asks its decoder
    services for the layers' frames. Each service decodes on its own
    thread and hands finished images back on the message thread; a
    per-slot generation counter drops results a newer request has
    already superseded, so scrubbing never shows stale frames out of
    order.

    M6.3 — layers and telops: cross-dissolves mean the playhead can
    sit under TWO clips at once, so the feed owns two frame services
    (bottom + top layer) and composes the published picture from the
    decoded frames (VideoCompositing::composeFrame). Deeper stacks
    are truncated to the top two layers — exports always use all of
    them; see VideoComposite.h. The active telop (text track) is
    drawn onto the composed copy before publishing, including over a
    black gap frame; the decoder's cached frames are never mutated.

    Surfaces (VideoMonitorComponent instances) register a listener
    while they exist; the feed's timer runs only while at least one
    listener is registered. Since M6.3 the only surface is the
    pop-out preview window (the M6.2 docked PiP monitor was removed),
    so decoding starts when the window opens — as it did in Phase 1 —
    and importing a video starts no decode work at all (see the
    README's M6.3 freeze notes).
*/
class VideoFrameFeed final : private juce::Timer
{
public:
    explicit VideoFrameFeed (TimelineEngine& engineToUse);
    ~VideoFrameFeed() override;

    /** Registers a change listener (called on the message thread
        whenever a new frame lands or the has-video state flips) and
        returns its id. The first listener starts the feed. */
    int addListener (std::function<void()> callback);
    void removeListener (int listenerId);

    const juce::Image& getImage() const noexcept { return currentImage; }
    bool hasVideoTrack() const noexcept { return videoTrackExists; }
    bool playheadHasVideo() const noexcept { return hasVideoAtPlayhead; }
    TimelineEngine& getEngine() noexcept { return engine; }

private:
    void timerCallback() override;
    void notifyListeners();

    /** Composes and publishes when every needed layer frame has
        landed and the desired picture (layers/alphas/text) changed
        since the last publish. Message thread only. */
    void publishIfReady();

    struct LayerSlot
    {
        VideoFrameService service;
        juce::Image image;              // last frame delivered for key
        juce::int64 key = -1;           // (file ^ frame index) wanted
        juce::uint32 generation = 0;    // generation issued for key
        bool ready = false;             // image belongs to key
    };

    TimelineEngine& engine;
    LayerSlot slots[2];                 // [0] = bottom, [1] = top

    // The desired picture, refreshed by every timer tick and consumed
    // by publishIfReady() (which frame deliveries also call).
    struct DesiredLayer
    {
        juce::File file;
        juce::int64 key = -1;           // (file ^ frame index) dedupe key
        double frameSeconds = 0.0;      // request time inside the source
        float alpha = 1.0f;
    };

    std::vector<DesiredLayer> desiredLayers;   // bottom -> top, <= 2
    bool desiredTextActive = false;
    juce::String desiredText;
    TelopStyleData desiredTelopStyle;   // M6.6: the session telop style
    int fallbackWidth = 640, fallbackHeight = 360;   // gap+telop canvas
    juce::String desiredSignature;

    juce::Image currentImage;
    juce::String publishedSignature;
    bool videoTrackExists = false;
    bool hasVideoAtPlayhead = false;

    // Late-delivery guard: queued frame callbacks check this before
    // touching the feed (the feed may be gone by the time a posted
    // delivery runs). Shared so the flag outlives the feed itself.
    std::shared_ptr<std::atomic<bool>> alive;

    std::map<int, std::function<void()>> listeners;
    int nextListenerId = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VideoFrameFeed)
};

//==============================================================================
/**
    VideoMonitorComponent — a letterboxed picture surface painting
    whatever VideoFrameFeed currently holds (M6).

    The picture is letterboxed on black. Where the playhead is over a
    gap the surface is plain black (or shows the telop alone, when a
    text clip is active there); with no video track at all it says
    so, quietly. The current timecode (at the session rate) is drawn
    in the bottom-left corner. The compact variant (smaller text, a
    thin border, click reporting) remains from the M6.2 docked
    monitor; M6.3 removed that monitor, so only the full-size variant
    is used — by the pop-out preview window.
*/
class VideoMonitorComponent : public juce::Component,
                              public juce::SettableTooltipClient
{
public:
    VideoMonitorComponent (VideoFrameFeed& feedToUse, bool compactMonitor);
    ~VideoMonitorComponent() override;

    /** Compact surfaces only: fired on mouse down. */
    std::function<void()> onClick;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;

protected:
    VideoFrameFeed& feed;
    const bool compact;

private:
    int listenerId = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VideoMonitorComponent)
};

//==============================================================================
/** The pop-out window's content: the full-size monitor (M6). */
class VideoPreviewComponent final : public VideoMonitorComponent
{
public:
    explicit VideoPreviewComponent (VideoFrameFeed& feedToUse)
        : VideoMonitorComponent (feedToUse, false)
    {
        setSize (640, 360);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VideoPreviewComponent)
};
