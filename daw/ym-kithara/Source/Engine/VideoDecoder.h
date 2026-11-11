#pragma once

#include <JuceHeader.h>
#include <deque>
#include <map>
#include <set>

//==============================================================================
/**
    Video file probing result (M6): everything the session model caches
    about a video file at import time.
*/
struct VideoFileInfo
{
    bool   ok = false;
    int    fpsNum = 0, fpsDen = 1;    // frame rate as a rational
    int    width = 0, height = 0;
    double durationSeconds = 0.0;

    // M6.5: the file is a still image (jpg/png/bmp — an image codec
    // behind an image demuxer, or a stream whose whole duration is a
    // single frame). A still yields its one frame at any time.
    bool   stillImage = false;
};

//==============================================================================
/**
    VideoDecoder — one video file, decoded with FFmpeg (M6).

    Wraps libavformat / libavcodec / libswscale behind a PIMPL so the
    FFmpeg headers stay out of the rest of the codebase. NOT thread
    safe: each decoding thread owns its own instance.

    getFrameAt() seeks to the nearest keyframe at or before the target
    and decodes forward to the first frame whose PTS reaches it. Small
    forward steps from the last returned frame decode sequentially
    without seeking (that is what makes 30 Hz preview playback cheap),
    and asking for the frame at (or within half a frame of) the last
    returned time returns the cached image untouched — repeated calls
    at the same position are bit-identical.

    Past the end of the stream, the LAST frame is returned (a video
    freezes on its final image rather than going black mid-clip). With
    nothing decoded yet, an invalid image comes back.

    M6.3: one call decodes at most a bounded number of frames/packets
    (far beyond any legitimate seek + decode-forward). A pathological
    stream that never reaches the target returns the best frame found
    instead of looping forever.

    M6.5: still images (jpg/png/bmp). An image stream is detected at
    open() (image codec behind an image demuxer, or a one-frame
    stream) and takes a dedicated path: the single frame is decoded
    once, cached, and returned for every requested time — image
    demuxers are pipe-style and must never be seeked or read past
    their one packet.
*/
class VideoDecoder final
{
public:
    VideoDecoder();
    ~VideoDecoder();

    /** Opens a file (replacing any currently open one). */
    bool open (const juce::File& file);
    void close();
    bool isOpen() const noexcept;
    juce::File getFile() const;

    int    getWidth()  const noexcept;
    int    getHeight() const noexcept;
    double getDurationSeconds() const noexcept;
    int    getFpsNum() const noexcept;
    int    getFpsDen() const noexcept;

    /** M6.5: the open file is a still image — getFrameAt() returns
        its single decoded frame for ANY requested time >= 0 (the
        frame is decoded once and cached; an image stream is never
        seeked and never EOF-fails). */
    bool   isStillImage() const noexcept;

    juce::Image getFrameAt (double sourceSeconds);

    /** Opens the file just long enough to read its stream info. */
    static VideoFileInfo probe (const juce::File& file);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VideoDecoder)
};

//==============================================================================
/**
    VideoFrameService — the preview's background decoder (M6).

    A dedicated thread owns a VideoDecoder. requestFrame() is
    latest-wins: a newer request supersedes a pending older one, and
    every request carries a generation number so the consumer can drop
    results that arrive after it has already moved on. Decoded frames
    are delivered through onFrame on the MESSAGE thread. The audio
    thread never touches this class, and the message thread only posts
    requests — it never blocks on a decode.
*/
class VideoFrameService final : private juce::Thread
{
public:
    VideoFrameService();
    ~VideoFrameService() override;

    /** Sets the frame callback (called on the MESSAGE thread with
        the request's generation). M6.3: the callback is stored under
        the state lock and the worker copies it under the same lock
        before delivering — before this, the std::function was
        written by the owner thread and read by the worker with no
        synchronisation (a data race, and the worker thread starts in
        the constructor, before the callback is even assigned). Set it
        once, before issuing requests; it is never cleared — service
        destruction stops and joins the worker first, and queued
        deliveries guard themselves against a dead owner (see
        VideoFrameFeed's alive flag). */
    void setOnFrame (std::function<void (juce::uint32 generation,
                                         const juce::Image& image)> callback);

    /** Queues a frame request and returns its generation number. */
    juce::uint32 requestFrame (const juce::File& file, double sourceSeconds);

    /** Drops any pending request (the playhead has no video now). */
    void cancelPending();

private:
    void run() override;

    juce::CriticalSection stateLock;
    juce::WaitableEvent wakeEvent;
    juce::String pendingPath;
    double pendingSeconds = 0.0;
    juce::uint32 pendingGeneration = 0;
    bool hasPending = false;
    juce::uint32 nextGeneration = 0;
    std::function<void (juce::uint32, const juce::Image&)> onFrame;   // stateLock

    VideoDecoder decoder;   // owned by the worker thread

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VideoFrameService)
};

//==============================================================================
/**
    FilmstripStore — background thumbnail extraction for the video
    lane (M6).

    getThumbnail() is called from the timeline's paint: a cache hit
    returns the image immediately; a miss queues an extraction (one
    thumbnail per whole source second, at a height bucketed from the
    current track height) and returns an invalid image — the lane
    paints its plain clip colour until onThumbnailReady fires and the
    lane repaints. Extraction decodes on this store's own thread, so
    painting never blocks on FFmpeg.

    Simplification (documented in the README): the cache is capped and
    simply cleared wholesale when it outgrows the cap, and thumbnails
    are extracted at the height they were first requested with —
    changing the vertical zoom re-extracts at the new bucket height.
*/
class FilmstripStore final : private juce::Thread
{
public:
    FilmstripStore();
    ~FilmstripStore() override;

    /** Sets the callback invoked (on the message thread) whenever a
        thumbnail lands. Stored under the state lock and copied by
        the worker under the same lock — the same M6.3 synchronisation
        fix as VideoFrameService::setOnFrame. */
    void setOnThumbnailReady (std::function<void()> callback);

    juce::Image getThumbnail (const juce::File& file, double sourceSeconds,
                              int heightPx);

    void clear();

private:
    void run() override;

    struct Key
    {
        juce::String path;
        int sourceSecond = 0;
        int heightBucket = 0;

        bool operator< (const Key& other) const
        {
            if (path != other.path) return path < other.path;
            if (sourceSecond != other.sourceSecond) return sourceSecond < other.sourceSecond;
            return heightBucket < other.heightBucket;
        }

        bool operator== (const Key& other) const
        {
            return path == other.path && sourceSecond == other.sourceSecond
                    && heightBucket == other.heightBucket;
        }
    };

    juce::CriticalSection stateLock;
    juce::WaitableEvent wakeEvent;
    std::map<Key, juce::Image> cache;
    std::set<Key> queued;
    std::deque<Key> queue;
    std::function<void()> onThumbnailReady;   // stateLock (M6.3)
    static constexpr size_t maxCachedThumbnails = 900;
    // M6.3: the pending queue is capped — paint used to be able to
    // queue one extraction per second of a long clip in a single
    // pass (the import-time thundering herd). Requests beyond the cap
    // are dropped for that paint; scrolling/zooming re-queues them.
    static constexpr size_t maxQueuedThumbnails = 240;

    VideoDecoder decoder;   // owned by the worker thread

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilmstripStore)
};
