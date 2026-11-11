#include "VideoDecoder.h"

#include <mutex>

extern "C"
{
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/avutil.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/rational.h>
    #include <libswscale/swscale.h>
}

//==============================================================================
namespace
{
    void silenceFfmpegLog()
    {
        static std::once_flag once;
        std::call_once (once, [] { av_log_set_level (AV_LOG_ERROR); });
    }
}

//==============================================================================
struct VideoDecoder::Impl
{
    juce::File file;
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    int streamIndex = -1;
    AVRational timeBase { 0, 1 };
    int fpsNum = 0, fpsDen = 1;
    int width = 0, height = 0;
    double durationSeconds = 0.0;

    SwsContext* scaler = nullptr;
    AVFrame* decoded = nullptr;
    AVFrame* converted = nullptr;
    uint8_t* convertedBuffer = nullptr;
    int convertedWidth = 0, convertedHeight = 0;
    AVPacket* packet = nullptr;

    // Frame-position bookkeeping for getFrameAt: the PTS (seconds) of
    // the image cached in lastImage. -1 = nothing decoded yet.
    double lastFrameSeconds = -1.0;
    juce::Image lastImage;

    // M6.5 still-image state: set at open() when the file is a still
    // (see VideoDecoder.h). stillFrame caches the one decoded frame;
    // getFrameAt returns it for any requested time.
    bool isStillImage = false;
    juce::Image stillFrame;

    /** Decodes (once) and caches the still's single frame. Reads
        packets until the first frame comes out; running into EOF
        after the image's one packet is expected, never an error.
        Bounded exactly like getFrameAt's main loop. */
    juce::Image decodeStillFrame()
    {
        if (stillFrame.isValid())
            return stillFrame;

        bool flushSent = false;
        constexpr int maxPackets = 4096;

        for (int guard = 0; guard < maxPackets; ++guard)
        {
            const int receiveResult = avcodec_receive_frame (codec, decoded);

            if (receiveResult == 0)
            {
                auto image = convertFrame (decoded);
                av_frame_unref (decoded);

                if (image.isValid())
                {
                    stillFrame = image;
                    lastImage = image;
                    lastFrameSeconds = 0.0;
                }

                return stillFrame;
            }

            if (receiveResult == AVERROR_EOF)
                break;

            if (receiveResult != AVERROR (EAGAIN) || flushSent)
                break;

            if (av_read_frame (format, packet) < 0)
            {
                avcodec_send_packet (codec, nullptr);
                flushSent = true;
                continue;
            }

            if (packet->stream_index != streamIndex)
            {
                av_packet_unref (packet);
                continue;
            }

            const int sendResult = avcodec_send_packet (codec, packet);
            av_packet_unref (packet);

            if (sendResult < 0 && sendResult != AVERROR (EAGAIN))
                break;
        }

        return stillFrame;
    }

    double frameDuration() const noexcept
    {
        return fpsNum > 0 ? (double) fpsDen / (double) fpsNum : 1.0 / 30.0;
    }

    double frameSeconds (const AVFrame* frameToUse) const noexcept
    {
        int64_t timestamp = frameToUse->best_effort_timestamp;

        if (timestamp == AV_NOPTS_VALUE)
            timestamp = frameToUse->pts;

        if (timestamp == AV_NOPTS_VALUE)
            return -1.0;

        return (double) timestamp * av_q2d (timeBase);
    }

    void releaseCodec()
    {
        if (scaler != nullptr)
            sws_freeContext (scaler);

        scaler = nullptr;

        if (convertedBuffer != nullptr)
            av_freep (&convertedBuffer);

        if (decoded != nullptr)
            av_frame_free (&decoded);

        if (converted != nullptr)
            av_frame_free (&converted);

        if (packet != nullptr)
            av_packet_free (&packet);

        if (codec != nullptr)
            avcodec_free_context (&codec);

        if (format != nullptr)
            avformat_close_input (&format);

        streamIndex = -1;
        lastFrameSeconds = -1.0;
        lastImage = {};
        convertedWidth = convertedHeight = 0;
        isStillImage = false;
        stillFrame = {};
    }

    bool seekTo (double seconds)
    {
        const int64_t target = (int64_t) std::llround (seconds / av_q2d (timeBase));

        int result = avformat_seek_file (format, streamIndex,
                                         std::numeric_limits<int64_t>::min(),
                                         target, target, AVSEEK_FLAG_BACKWARD);

        if (result < 0)
            result = av_seek_frame (format, streamIndex, target, AVSEEK_FLAG_BACKWARD);

        avcodec_flush_buffers (codec);
        lastFrameSeconds = -1.0;
        return result >= 0;
    }

    juce::Image convertFrame (const AVFrame* source)
    {
        const int w = source->width;
        const int h = source->height;

        if (w <= 0 || h <= 0)
            return {};

        scaler = sws_getCachedContext (scaler,
                                       w, h, (AVPixelFormat) source->format,
                                       w, h, AV_PIX_FMT_BGR24,
                                       SWS_BILINEAR, nullptr, nullptr, nullptr);

        if (scaler == nullptr)
            return {};

        if (converted == nullptr)
            converted = av_frame_alloc();

        if (convertedWidth != w || convertedHeight != h)
        {
            if (convertedBuffer != nullptr)
                av_freep (&convertedBuffer);

            const int size = av_image_get_buffer_size (AV_PIX_FMT_BGR24, w, h, 32);

            if (size <= 0)
                return {};

            convertedBuffer = (uint8_t*) av_mallocz ((size_t) size);

            if (convertedBuffer == nullptr)
                return {};

            av_image_fill_arrays (converted->data, converted->linesize,
                                  convertedBuffer, AV_PIX_FMT_BGR24, w, h, 32);
            convertedWidth = w;
            convertedHeight = h;
        }

        sws_scale (scaler, source->data, source->linesize, 0, h,
                   converted->data, converted->linesize);

        // AV_PIX_FMT_BGR24 rows are packed B,G,R triplets, which is
        // exactly the memory layout of juce::Image::RGB (PixelRGB) on
        // little-endian platforms, so a per-row copy suffices.
        juce::Image image (juce::Image::RGB, w, h, false);
        juce::Image::BitmapData bitmap (image, juce::Image::BitmapData::writeOnly);
        const int bytesPerRow = juce::jmin (bitmap.lineStride,
                                            (int) converted->linesize[0]);

        for (int y = 0; y < h; ++y)
            std::memcpy (bitmap.getLinePointer (y),
                         converted->data[0] + (size_t) y * (size_t) converted->linesize[0],
                         (size_t) juce::jmin (bytesPerRow, w * 3));

        return image;
    }
};

//==============================================================================
VideoDecoder::VideoDecoder() : impl (std::make_unique<Impl>()) { silenceFfmpegLog(); }
VideoDecoder::~VideoDecoder() { close(); }

bool VideoDecoder::open (const juce::File& file)
{
    close();

    if (! file.existsAsFile())
        return false;

    AVFormatContext* format = nullptr;

    if (avformat_open_input (&format, file.getFullPathName().toRawUTF8(),
                             nullptr, nullptr) < 0)
        return false;

    impl->format = format;

    if (avformat_find_stream_info (format, nullptr) < 0)
    {
        close();
        return false;
    }

    const int streamIndex = av_find_best_stream (format, AVMEDIA_TYPE_VIDEO,
                                                 -1, -1, nullptr, 0);

    if (streamIndex < 0)
    {
        close();
        return false;
    }

    auto* stream = format->streams[streamIndex];
    const auto* decoder = avcodec_find_decoder (stream->codecpar->codec_id);

    if (decoder == nullptr)
    {
        close();
        return false;
    }

    impl->codec = avcodec_alloc_context3 (decoder);

    if (impl->codec == nullptr
         || avcodec_parameters_to_context (impl->codec, stream->codecpar) < 0
         || avcodec_open2 (impl->codec, decoder, nullptr) < 0)
    {
        close();
        return false;
    }

    impl->file = file;
    impl->streamIndex = streamIndex;
    impl->timeBase = stream->time_base;
    impl->width = impl->codec->width;
    impl->height = impl->codec->height;

    // Frame rate: prefer the stream's average; fall back to the raw
    // rate the demuxer guessed, then to 30 fps as a last resort.
    AVRational rate = stream->avg_frame_rate;

    if (rate.num <= 0 || rate.den <= 0)
        rate = stream->r_frame_rate;

    if (rate.num > 0 && rate.den > 0)
    {
        impl->fpsNum = rate.num;
        impl->fpsDen = rate.den;
    }
    else
    {
        impl->fpsNum = 30;
        impl->fpsDen = 1;
    }

    if (format->duration != AV_NOPTS_VALUE)
        impl->durationSeconds = (double) format->duration / (double) AV_TIME_BASE;
    else if (stream->duration != AV_NOPTS_VALUE)
        impl->durationSeconds = (double) stream->duration * av_q2d (stream->time_base);
    else
        impl->durationSeconds = 0.0;

    // M6.5 still-image detection. Two independent witnesses:
    //  - an image codec (MJPEG/PNG/BMP) behind an image demuxer
    //    (image2 / *_pipe): how jpg/png/bmp FILES always arrive.
    //    The demuxer half matters — MJPEG is also a real video
    //    codec (old cameras), but those come in movie containers,
    //    never through the image demuxers.
    //  - a stream whose entire duration is a single frame, whatever
    //    the codec/container (a one-frame video is a still).
    {
        const auto codecId = stream->codecpar->codec_id;
        const bool imageCodec = codecId == AV_CODEC_ID_MJPEG
                             || codecId == AV_CODEC_ID_PNG
                             || codecId == AV_CODEC_ID_BMP;
        const juce::String demuxerName (
            format->iformat != nullptr && format->iformat->name != nullptr
                ? format->iformat->name : "");
        const bool imageDemuxer = demuxerName.contains ("image2")
                               || demuxerName.endsWith ("_pipe");
        const bool singleFrame = impl->durationSeconds > 0.0
                              && impl->durationSeconds
                                     <= 1.5 * impl->frameDuration();

        impl->isStillImage = (imageCodec && imageDemuxer) || singleFrame;
    }

    impl->decoded = av_frame_alloc();
    impl->packet = av_packet_alloc();

    if (impl->decoded == nullptr || impl->packet == nullptr)
    {
        close();
        return false;
    }

    return true;
}

void VideoDecoder::close()
{
    impl->releaseCodec();
    impl->file = juce::File();
    impl->width = impl->height = 0;
    impl->fpsNum = 0;
    impl->fpsDen = 1;
    impl->durationSeconds = 0.0;
}

bool VideoDecoder::isOpen() const noexcept { return impl->format != nullptr; }
juce::File VideoDecoder::getFile() const { return impl->file; }
int VideoDecoder::getWidth() const noexcept { return impl->width; }
int VideoDecoder::getHeight() const noexcept { return impl->height; }
double VideoDecoder::getDurationSeconds() const noexcept { return impl->durationSeconds; }
int VideoDecoder::getFpsNum() const noexcept { return impl->fpsNum; }
int VideoDecoder::getFpsDen() const noexcept { return impl->fpsDen; }
bool VideoDecoder::isStillImage() const noexcept { return impl->isStillImage; }

juce::Image VideoDecoder::getFrameAt (double sourceSeconds)
{
    if (impl->format == nullptr || impl->codec == nullptr)
        return {};

    // M6.5: a still image answers every time with its one frame,
    // decoded once and cached — no seeking, no EOF handling, and
    // (importantly for the preview feed) the same image object comes
    // back every call, so layers over a still cost nothing per tick.
    if (impl->isStillImage)
        return impl->decodeStillFrame();

    double target = juce::jmax (0.0, sourceSeconds);

    if (impl->durationSeconds > 0.0)
        target = juce::jmin (target, impl->durationSeconds);

    const double frameDur = impl->frameDuration();

    // Cached frame close enough (same frame index, essentially): hand
    // it back untouched so repeated reads at one position are stable.
    if (impl->lastImage.isValid()
         && std::abs (target - impl->lastFrameSeconds) <= frameDur * 0.5)
        return impl->lastImage;

    // Sequential small steps decode forward; anything else seeks to
    // the nearest keyframe at or before the target first.
    if (impl->lastFrameSeconds < 0.0
         || target < impl->lastFrameSeconds - 1.0e-9
         || target - impl->lastFrameSeconds > 2.5)
        impl->seekTo (target);

    juce::Image bestImage;
    double bestSeconds = -1.0;
    bool flushSent = false;

    // M6.3 safety bounds: a legitimate call decodes from the nearest
    // keyframe forward — never more than a few hundred frames. A
    // pathological stream (timestamp-less frames whose step never
    // reaches the target, an endless run of other-stream packets,
    // ...) must not be able to wedge the calling thread forever:
    // past these caps the call returns the best frame it has, like
    // the corrupt-stretch fallback below. (Before M6.3 the loop had
    // no bound at all — one of the hazards in the M6.2 freeze audit.)
    constexpr int maxFramesPerCall  = 8192;    // ~4.5 min at 30 fps
    constexpr int maxPacketsPerCall = 65536;
    int framesDecoded = 0;
    int packetsRead = 0;

    for (;;)
    {
        const int receiveResult = avcodec_receive_frame (impl->codec, impl->decoded);

        if (receiveResult == 0)
        {
            double pts = impl->frameSeconds (impl->decoded);

            if (pts < 0.0)
                pts = bestSeconds + frameDur;   // timestamp-less frame: step forward

            auto image = impl->convertFrame (impl->decoded);
            av_frame_unref (impl->decoded);

            if (image.isValid())
            {
                bestImage = image;
                bestSeconds = pts;
            }

            if (pts >= target - 1.0e-9)
                break;   // reached the requested frame

            if (++framesDecoded > maxFramesPerCall)
                break;   // safety bound (see above)

            continue;
        }

        if (receiveResult == AVERROR_EOF)
            break;   // fully drained: keep the best frame we have

        if (receiveResult != AVERROR (EAGAIN))
            break;   // unexpected error

        if (flushSent)
            break;

        // Feed the decoder until it accepts a packet or the input runs
        // out (then send the null flush packet exactly once).
        for (;;)
        {
            const bool readFailed = av_read_frame (impl->format, impl->packet) < 0;

            if (readFailed || ++packetsRead > maxPacketsPerCall)
            {
                if (! readFailed)
                    av_packet_unref (impl->packet);   // the capped read

                avcodec_send_packet (impl->codec, nullptr);
                flushSent = true;
                break;
            }

            if (impl->packet->stream_index != impl->streamIndex)
            {
                av_packet_unref (impl->packet);
                continue;
            }

            const int sendResult = avcodec_send_packet (impl->codec, impl->packet);
            av_packet_unref (impl->packet);

            if (sendResult == AVERROR (EAGAIN))
                break;   // decoder full: go back to receiving

            break;       // accepted (or a real error): go receive
        }
    }

    if (bestImage.isValid())
    {
        impl->lastImage = bestImage;
        impl->lastFrameSeconds = bestSeconds;
        return bestImage;
    }

    // Nothing decoded (e.g. a corrupt stretch of file): fall back to
    // whatever frame was shown before rather than going black.
    return impl->lastImage;
}

VideoFileInfo VideoDecoder::probe (const juce::File& file)
{
    VideoFileInfo info;
    VideoDecoder decoder;

    if (! decoder.open (file))
        return info;

    info.ok = true;
    info.fpsNum = decoder.getFpsNum();
    info.fpsDen = decoder.getFpsDen();
    info.width = decoder.getWidth();
    info.height = decoder.getHeight();
    info.durationSeconds = decoder.getDurationSeconds();
    info.stillImage = decoder.isStillImage();
    return info;
}

//==============================================================================
// VideoFrameService
//==============================================================================
VideoFrameService::VideoFrameService() : juce::Thread ("VideoPreviewDecoder")
{
    startThread (juce::Thread::Priority::normal);
}

VideoFrameService::~VideoFrameService()
{
    signalThreadShouldExit();
    wakeEvent.signal();
    stopThread (3000);
}

void VideoFrameService::setOnFrame (std::function<void (juce::uint32,
                                                         const juce::Image&)> callback)
{
    juce::ScopedLock lock (stateLock);
    onFrame = std::move (callback);
}

juce::uint32 VideoFrameService::requestFrame (const juce::File& file, double sourceSeconds)
{
    juce::ScopedLock lock (stateLock);
    pendingPath = file.getFullPathName();
    pendingSeconds = sourceSeconds;
    pendingGeneration = ++nextGeneration;
    hasPending = true;
    wakeEvent.signal();
    return pendingGeneration;
}

void VideoFrameService::cancelPending()
{
    juce::ScopedLock lock (stateLock);
    hasPending = false;
}

void VideoFrameService::run()
{
    while (! threadShouldExit())
    {
        juce::String path;
        double seconds = 0.0;
        juce::uint32 generation = 0;
        bool work = false;

        {
            juce::ScopedLock lock (stateLock);

            if (hasPending)
            {
                path = pendingPath;
                seconds = pendingSeconds;
                generation = pendingGeneration;
                hasPending = false;
                work = true;
            }
        }

        if (! work)
        {
            wakeEvent.wait (100);
            wakeEvent.reset();
            continue;
        }

        if (decoder.getFile().getFullPathName() != path)
            decoder.open (juce::File (path));

        auto image = decoder.isOpen() ? decoder.getFrameAt (seconds) : juce::Image();

        // M6.3: copy the callback under the state lock (it is written
        // by the owner thread via setOnFrame) — never read it bare
        // from this thread.
        std::function<void (juce::uint32, const juce::Image&)> callback;

        {
            juce::ScopedLock lock (stateLock);
            callback = onFrame;
        }

        if (image.isValid() && callback != nullptr
             && juce::MessageManager::getInstanceWithoutCreating() != nullptr)
        {
            juce::MessageManager::callAsync (
                [callback, generation, image]
                {
                    callback (generation, image);
                });
        }
    }
}

//==============================================================================
// FilmstripStore
//==============================================================================
FilmstripStore::FilmstripStore() : juce::Thread ("FilmstripExtractor")
{
    startThread (juce::Thread::Priority::background);
}

FilmstripStore::~FilmstripStore()
{
    signalThreadShouldExit();
    wakeEvent.signal();
    stopThread (3000);
}

void FilmstripStore::setOnThumbnailReady (std::function<void()> callback)
{
    juce::ScopedLock lock (stateLock);
    onThumbnailReady = std::move (callback);
}

juce::Image FilmstripStore::getThumbnail (const juce::File& file, double sourceSeconds,
                                          int heightPx)
{
    Key key;
    key.path = file.getFullPathName();
    key.sourceSecond = (int) std::floor (juce::jmax (0.0, sourceSeconds));
    key.heightBucket = juce::jlimit (24, 160, (heightPx / 8) * 8);

    juce::ScopedLock lock (stateLock);

    if (auto found = cache.find (key); found != cache.end())
        return found->second;

    // M6.3: cap the pending queue (see the header) — a paint that
    // wants hundreds of fresh thumbnails (first paint after an
    // import) queues only the first batch; the rest are re-requested
    // by later paints as the worker drains the queue.
    if (queue.size() < maxQueuedThumbnails && queued.insert (key).second)
    {
        queue.push_back (key);
        wakeEvent.signal();
    }

    return {};
}

void FilmstripStore::clear()
{
    juce::ScopedLock lock (stateLock);
    cache.clear();
}

void FilmstripStore::run()
{
    while (! threadShouldExit())
    {
        Key key;
        bool work = false;

        {
            juce::ScopedLock lock (stateLock);

            if (! queue.empty())
            {
                key = queue.front();
                queue.pop_front();
                work = true;
            }
        }

        if (! work)
        {
            wakeEvent.wait (100);
            wakeEvent.reset();
            continue;
        }

        const juce::File file (key.path);

        if (decoder.getFile() != file)
            decoder.open (file);

        juce::Image thumb;

        if (decoder.isOpen())
        {
            // Aim at the middle of the requested second — a more
            // representative still than its very first frame.
            auto frame = decoder.getFrameAt ((double) key.sourceSecond + 0.5);

            if (frame.isValid() && frame.getHeight() > 0)
            {
                const int thumbWidth = juce::jmax (2, (int) std::lround (
                    (double) frame.getWidth() * (double) key.heightBucket
                        / (double) frame.getHeight()));
                thumb = frame.rescaled (thumbWidth, key.heightBucket,
                                        juce::Graphics::highResamplingQuality);
            }
        }

        bool landed = false;
        std::function<void()> readyCallback;

        {
            juce::ScopedLock lock (stateLock);
            queued.erase (key);
            readyCallback = onThumbnailReady;   // M6.3: copied under lock

            if (thumb.isValid())
            {
                if (cache.size() >= maxCachedThumbnails)
                    cache.clear();   // documented wholesale-cap behaviour

                cache[key] = thumb;
                landed = true;
            }
        }

        if (landed && readyCallback != nullptr
             && juce::MessageManager::getInstanceWithoutCreating() != nullptr)
        {
            juce::MessageManager::callAsync ([readyCallback] { readyCallback(); });
        }
    }
}
