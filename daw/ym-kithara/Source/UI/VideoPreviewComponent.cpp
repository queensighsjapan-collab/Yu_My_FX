#include "VideoPreviewComponent.h"

namespace
{
    const juce::Colour textColour    { 0xffe8e9ec };
    const juce::Colour dimTextColour { 0xff9aa0ad };
    const juce::Colour borderColour  { 0xff3a3f4a };

    juce::int64 frameKeyFor (const juce::File& file, juce::int64 frameIndex)
    {
        return (juce::int64) file.getFullPathName().hashCode64()
               ^ (frameIndex + 0x9e3779b9);
    }
}

//==============================================================================
VideoFrameFeed::VideoFrameFeed (TimelineEngine& engineToUse)
    : engine (engineToUse), alive (std::make_shared<std::atomic<bool>> (true))
{
    // Each slot's service delivers into that slot only. The callback
    // runs on the message thread (the service posts it); the alive
    // flag guards the queued-delivery-after-destruction case, and the
    // per-slot generation drops superseded frames. (M6.3: delivery
    // goes through VideoFrameService::setOnFrame — the callback is
    // synchronised inside the service; see VideoDecoder.h.)
    for (int i = 0; i < 2; ++i)
    {
        slots[i].service.setOnFrame (
            [this, aliveFlag = alive, i] (juce::uint32 generation,
                                          const juce::Image& image)
            {
                if (! aliveFlag->load())
                    return;

                auto& slot = slots[i];

                if (generation != slot.generation)
                    return;

                slot.image = image;
                slot.ready = true;
                publishIfReady();
            });
    }
}

VideoFrameFeed::~VideoFrameFeed()
{
    alive->store (false);
    stopTimer();
}

int VideoFrameFeed::addListener (std::function<void()> callback)
{
    const int id = nextListenerId++;
    listeners[id] = std::move (callback);

    if (listeners.size() == 1)
        startTimerHz (30);

    return id;
}

void VideoFrameFeed::removeListener (int listenerId)
{
    listeners.erase (listenerId);

    if (listeners.empty())
        stopTimer();
}

void VideoFrameFeed::notifyListeners()
{
    // Copy first: a callback may unregister (surface being destroyed).
    const auto snapshot = listeners;

    for (auto& pair : snapshot)
        if (pair.second != nullptr)
            pair.second();
}

//==============================================================================
void VideoFrameFeed::timerCallback()
{
    const double playheadSeconds = engine.getPositionSeconds();

    // Snapshot the model pieces the mapping needs (model lock held,
    // like every other component that reads the session directly).
    bool hasTrack = false;
    std::vector<VideoClipData> videoClips;
    std::vector<TextClipData> textClips;
    TelopStyleData telopStyle;

    {
        juce::ScopedLock lock (engine.getModelLock());
        auto& session = engine.getSession();
        telopStyle = session.telopStyle;

        for (auto& track : session.tracks)
        {
            if (track.kind == TrackKind::video)
            {
                hasTrack = true;
                videoClips = track.videoClips;
            }
            else if (track.kind == TrackKind::text)
            {
                textClips = track.textClips;
            }
        }
    }

    videoTrackExists = hasTrack;

    // The shared layer mapping (identical to the exporter's).
    std::vector<VideoCompositing::Layer> layers;
    VideoCompositing::computeLayers (videoClips, playheadSeconds, layers);

    // Preview truncation: keep the TOP two layers (see the header).
    if (layers.size() > 2)
        layers.erase (layers.begin(), layers.end() - 2);

    const auto* activeText = Telop::findActiveClip (textClips, playheadSeconds);

    //--- refresh the desired picture ----------------------------------
    desiredLayers.clear();
    juce::String signature;

    for (auto& layer : layers)
    {
        DesiredLayer desired;
        desired.file = juce::File (layer.clip->filePath);

        if (layer.clip->stillImage)
        {
            // M6.5: a still's picture never changes — one constant
            // request key (frame 0) for the whole clip, so the feed
            // issues exactly one request per still layer instead of
            // re-asking on every frame index the playhead crosses.
            // (The decoder returns the still's frame for any time
            // anyway; this keeps the FEED from churning too.)
            desired.key = frameKeyFor (desired.file, 0);
            desired.frameSeconds = 0.0;
        }
        else
        {
            const double fps = (layer.clip->fpsNum > 0 && layer.clip->fpsDen > 0)
                                   ? (double) layer.clip->fpsNum / (double) layer.clip->fpsDen
                                   : 30.0;
            const juce::int64 frameIndex = (juce::int64) std::floor (
                layer.sourceSeconds * fps);
            desired.key = frameKeyFor (desired.file, frameIndex);
            desired.frameSeconds = fps > 0.0 ? (double) frameIndex / fps
                                             : layer.sourceSeconds;
        }

        desired.alpha = layer.alpha;
        desiredLayers.push_back (desired);

        signature += juce::String (desired.key)
                     + ":" + juce::String ((int) (layer.alpha * 128.0f)) + ";";
    }

    desiredTextActive = activeText != nullptr;
    desiredText = desiredTextActive ? activeText->text : juce::String();
    desiredTelopStyle = telopStyle;

    if (desiredTextActive)
        signature += "T" + juce::String (activeText->id)
                     + ":" + desiredTelopStyle.fontName
                     + ":" + juce::String ((int) desiredTelopStyle.scalePercent)
                     + ":" + juce::String ((int) desiredTelopStyle.vertical)
                     + ":" + juce::String ((int) desiredTelopStyle.align)
                     + ":" + juce::String::toHexString (
                                 (juce::int64) desiredTelopStyle.colourArgb);

    // Gap+telop canvas: the size of the track's first clip, else 16:9.
    fallbackWidth = 640;
    fallbackHeight = 360;

    if (! videoClips.empty() && videoClips.front().width > 0
         && videoClips.front().height > 0)
    {
        fallbackWidth = videoClips.front().width;
        fallbackHeight = videoClips.front().height;
    }

    signature += "S" + juce::String (fallbackWidth) + "x"
                 + juce::String (fallbackHeight);
    desiredSignature = signature;

    hasVideoAtPlayhead = ! desiredLayers.empty() || desiredTextActive;

    //--- (re)issue the layer requests whose frame changed ------------
    for (int i = 0; i < 2; ++i)
    {
        auto& slot = slots[i];

        if (i >= (int) desiredLayers.size())
        {
            if (slot.key != -1)
            {
                slot.key = -1;
                slot.ready = false;
                slot.service.cancelPending();
            }

            continue;
        }

        const auto& desired = desiredLayers[(size_t) i];

        if (desired.key != slot.key)
        {
            slot.key = desired.key;
            slot.ready = false;
            slot.generation = slot.service.requestFrame (desired.file,
                                                         desired.frameSeconds);
        }
    }

    publishIfReady();
}

void VideoFrameFeed::publishIfReady()
{
    // Every needed layer frame must have landed for the CURRENT key;
    // until then the previous picture stays up (scrubbing over a
    // dissolve therefore never flashes half-loaded frames).
    for (int i = 0; i < (int) desiredLayers.size() && i < 2; ++i)
        if (! slots[i].ready)
            return;

    if (desiredSignature == publishedSignature)
        return;

    juce::Image published;

    if (desiredLayers.empty())
    {
        // Gap: black — unless a telop is active, in which case the
        // text shows on black (lyric-only spans).
        if (desiredTextActive)
        {
            published = VideoCompositing::composeFrame (fallbackWidth,
                                                        fallbackHeight, {});
            Telop::drawOnto (published, desiredText, desiredTelopStyle);
        }
    }
    else if (desiredLayers.size() == 1 && desiredLayers[0].alpha >= 0.999f
              && ! desiredTextActive)
    {
        // The common case keeps the Phase 1 fast path: publish the
        // decoded frame itself (nothing mutates published images —
        // compositing only ever happens on a composeFrame canvas).
        published = slots[0].image;
    }
    else
    {
        // Layered / faded / telop'd: compose over black at the top
        // layer's size, each layer fitted inside (aspect preserved),
        // telop last.
        const auto& topImage = slots[desiredLayers.size() - 1].image;
        const auto& bottomImage = slots[0].image;
        const int width = topImage.isValid() ? topImage.getWidth()
                                             : bottomImage.getWidth();
        const int height = topImage.isValid() ? topImage.getHeight()
                                              : bottomImage.getHeight();

        if (width > 0 && height > 0)
        {
            std::vector<VideoCompositing::LayerImage> layerImages;

            for (size_t i = 0; i < desiredLayers.size(); ++i)
                layerImages.push_back ({ slots[i].image, desiredLayers[i].alpha });

            published = VideoCompositing::composeFrame (width, height, layerImages);

            if (desiredTextActive)
                Telop::drawOnto (published, desiredText, desiredTelopStyle);
        }
    }

    currentImage = published;
    publishedSignature = desiredSignature;
    hasVideoAtPlayhead = published.isValid();
    notifyListeners();
}

//==============================================================================
VideoMonitorComponent::VideoMonitorComponent (VideoFrameFeed& feedToUse,
                                              bool compactMonitor)
    : feed (feedToUse), compact (compactMonitor)
{
    listenerId = feed.addListener ([safeThis = juce::Component::SafePointer<VideoMonitorComponent> (this)]
    {
        if (safeThis != nullptr)
            safeThis->repaint();
    });
}

VideoMonitorComponent::~VideoMonitorComponent()
{
    feed.removeListener (listenerId);
}

void VideoMonitorComponent::mouseDown (const juce::MouseEvent&)
{
    if (compact && onClick != nullptr)
        onClick();
}

void VideoMonitorComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    const auto& image = feed.getImage();

    if (image.isValid())
    {
        // Letterbox: fit the whole frame inside the surface, centred.
        g.drawImageWithin (image, 0, 0, getWidth(), getHeight(),
                           juce::RectanglePlacement::centred, false);
    }
    else if (! feed.hasVideoTrack())
    {
        g.setColour (dimTextColour);
        g.setFont (juce::FontOptions (compact ? 11.0f : 14.0f));
        g.drawText (compact ? "No video track"
                            : "No video track yet - use Add Video... to import an mp4.",
                    getLocalBounds(), juce::Justification::centred);
    }
    else if (! feed.playheadHasVideo())
    {
        g.setColour (dimTextColour);
        g.setFont (juce::FontOptions (compact ? 11.0f : 14.0f));
        g.drawText ("(no video at the playhead)",
                    getLocalBounds(), juce::Justification::centred);
    }

    // Current timecode, bottom-left, at the session rate.
    auto& engine = feed.getEngine();
    int num = 30, den = 1;
    bool userSet = false;
    engine.getTimecodeFps (num, den, userSet);

    g.setColour (textColour.withAlpha (0.85f));
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                             compact ? 10.0f : 13.0f, juce::Font::plain)));
    g.drawText (Timecode::format (engine.getPositionSeconds(), { num, den }),
                8, getHeight() - (compact ? 18 : 22), 160, 16,
                juce::Justification::centredLeft);

    if (compact)
    {
        g.setColour (borderColour);
        g.drawRect (getLocalBounds(), 1);
    }
}
