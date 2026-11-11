#include "Mp4Exporter.h"
#include "TimelineEngine.h"
#include "VideoDecoder.h"
#include "VideoComposite.h"

#include <map>

extern "C"
{
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/avutil.h>
    #include <libavutil/audio_fifo.h>
    #include <libavutil/channel_layout.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/opt.h>
    #include <libswscale/swscale.h>
    #include <libswresample/swresample.h>
}

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace
{
    constexpr int audioOutRate      = 48000;
    constexpr int audioChunkSamples = 2048;   // engine-rate samples per render bite

    juce::String ffmpegError (int code)
    {
        char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror (code, buffer, sizeof (buffer));
        return juce::String (buffer);
    }

    int evenUp (int v) noexcept { return juce::jmax (2, (v + 1) & ~1); }
    int evenDown (int v) noexcept { return juce::jmax (2, v & ~1); }

    bool isCancelled (const std::atomic<bool>* flag)
    {
        return flag != nullptr && flag->load();
    }

    void fillBlack (AVFrame* frame)
    {
        // Limited-range black: Y = 16, U/V = 128. Decoded back to RGB
        // this is exactly 0.
        for (int y = 0; y < frame->height; ++y)
            std::memset (frame->data[0] + (size_t) y * frame->linesize[0], 16,
                         (size_t) frame->width);

        for (int y = 0; y < frame->height / 2; ++y)
        {
            std::memset (frame->data[1] + (size_t) y * frame->linesize[1], 128,
                         (size_t) frame->width / 2);
            std::memset (frame->data[2] + (size_t) y * frame->linesize[2], 128,
                         (size_t) frame->width / 2);
        }
    }

    //======================================================================
    /** Everything FFmpeg-side for one export, freed wholesale. */
    struct FfmpegState
    {
        FfmpegState() = default;

        AVFormatContext* format = nullptr;
        AVCodecContext* videoCtx = nullptr;
        AVCodecContext* audioCtx = nullptr;
        AVStream* videoStream = nullptr;
        AVStream* audioStream = nullptr;
        AVFrame* videoFrame = nullptr;      // the composed output frame
        AVFrame* scaledFrame = nullptr;     // one fitted clip picture
        AVFrame* audioFrame = nullptr;
        AVPacket* packet = nullptr;
        SwsContext* scaler = nullptr;
        SwsContext* canvasScaler = nullptr;   // M6.3: RGB canvas -> YUV
        SwrContext* resampler = nullptr;
        AVAudioFifo* fifo = nullptr;
        int scaledWidth = 0, scaledHeight = 0;
        bool headerWritten = false;
        bool ioOpened = false;

        ~FfmpegState() { close(); }

        void close()
        {
            // Idempotent: safe to call twice (explicitly, then from the
            // destructor). The av_*_free functions that take a pointer
            // null it themselves; the rest are nulled here by hand.
            if (scaler != nullptr)     { sws_freeContext (scaler); scaler = nullptr; }
            if (canvasScaler != nullptr) { sws_freeContext (canvasScaler); canvasScaler = nullptr; }
            if (resampler != nullptr)  swr_free (&resampler);
            if (fifo != nullptr)       { av_audio_fifo_free (fifo); fifo = nullptr; }
            if (packet != nullptr)     av_packet_free (&packet);
            if (videoFrame != nullptr) av_frame_free (&videoFrame);
            if (scaledFrame != nullptr) av_frame_free (&scaledFrame);
            if (audioFrame != nullptr) av_frame_free (&audioFrame);
            if (videoCtx != nullptr)   avcodec_free_context (&videoCtx);
            if (audioCtx != nullptr)   avcodec_free_context (&audioCtx);

            if (format != nullptr)
            {
                if (ioOpened && format->pb != nullptr)
                    avio_closep (&format->pb);

                avformat_free_context (format);
                format = nullptr;
                ioOpened = false;
            }
        }

        JUCE_DECLARE_NON_COPYABLE (FfmpegState)
    };

    bool writeEncodedPackets (FfmpegState& s, AVCodecContext* ctx, AVStream* stream,
                              juce::String& errorOut)
    {
        for (;;)
        {
            const int result = avcodec_receive_packet (ctx, s.packet);

            if (result == AVERROR (EAGAIN) || result == AVERROR_EOF)
                return true;

            if (result < 0)
            {
                errorOut = "Encoding failed: " + ffmpegError (result);
                return false;
            }

            s.packet->stream_index = stream->index;
            av_packet_rescale_ts (s.packet, ctx->time_base, stream->time_base);

            if (av_interleaved_write_frame (s.format, s.packet) < 0)
            {
                av_packet_unref (s.packet);
                errorOut = "Writing the mp4 file failed.";
                return false;
            }

            av_packet_unref (s.packet);
        }
    }
}

//==============================================================================
bool Mp4Exporter::exportMp4 (TimelineEngine& engine, const juce::File& target,
                             const Mp4ExportOptions& options,
                             juce::String& errorOut,
                             std::atomic<float>* progress01,
                             std::atomic<bool>* shouldCancel)
{
    //======================================================================
    // 1. Snapshot the work under the model lock (held for the whole
    //    export, exactly like the WAV mixdown).
    juce::ScopedLock modelLock (engine.getModelLock());

    std::vector<VideoClipData> clips;
    std::vector<TextClipData> textClips;   // M6.3: telops
    TelopStyleData telopStyle;             // M6.6: session telop style
    int fpsNum = 30, fpsDen = 1;
    {
        bool userSet = false;
        engine.getTimecodeFps (fpsNum, fpsDen, userSet);
        telopStyle = engine.getSession().telopStyle;

        for (auto& track : engine.getSession().tracks)
        {
            if (track.kind == TrackKind::video)
                clips = track.videoClips;
            else if (track.kind == TrackKind::text)
                textClips = track.textClips;
        }
    }

    if (clips.empty())
    {
        errorOut = "Export MP4 needs a video track.\n"
                   "Use \"Add Video...\" to import a video clip first.";
        return false;
    }

    std::sort (clips.begin(), clips.end(),
               [] (const VideoClipData& a, const VideoClipData& b)
               { return a.startSeconds < b.startSeconds; });

    // Every source is checked up front, so a missing file aborts with
    // its name instead of failing halfway through a long render.
    for (auto& clip : clips)
    {
        if (! juce::File (clip.filePath).existsAsFile())
        {
            errorOut = "A video source file is missing:\n" + clip.filePath;
            return false;
        }
    }

    juce::String beginError;
    const juce::int64 totalAudioSamples = engine.beginOfflineMixdown (beginError);

    if (totalAudioSamples < 0)
    {
        errorOut = beginError;
        return false;
    }

    // From here on, every exit path must end the offline render.
    struct OfflineGuard
    {
        TimelineEngine& engine;
        ~OfflineGuard() { engine.endOfflineMixdown(); }
    } offlineGuard { engine };

    const double engineRate = engine.getSampleRate();
    const double durationSeconds = (double) totalAudioSamples / engineRate;
    const double fps = (double) fpsNum / (double) juce::jmax (1, fpsDen);
    const juce::int64 totalFrames = juce::jmax<juce::int64> (
        1, (juce::int64) std::llround (durationSeconds * fps));
    const juce::int64 totalOutAudio = (juce::int64) std::llround (
        durationSeconds * (double) audioOutRate);

    // Output size: the preset, or "Original" = the earliest clip's
    // dimensions, rounded up to even for yuv420p.
    int outWidth = options.width, outHeight = options.height;

    if (outWidth <= 0 || outHeight <= 0)
    {
        outWidth  = evenUp (clips.front().width  > 0 ? clips.front().width  : 1280);
        outHeight = evenUp (clips.front().height > 0 ? clips.front().height : 720);
    }

    //======================================================================
    // 2. FFmpeg setup.
    FfmpegState s;

    auto fail = [&] (const juce::String& message)
    {
        errorOut = message;
        return false;
    };

    if (avformat_alloc_output_context2 (&s.format, nullptr, "mp4",
                                        target.getFullPathName().toRawUTF8()) < 0
        || s.format == nullptr)
        return fail ("Could not create the mp4 output context.");

    //--- video stream (libx264)
    const AVCodec* videoCodec = avcodec_find_encoder_by_name ("libx264");

    if (videoCodec == nullptr)
        return fail ("libx264 is not available in this FFmpeg build, "
                     "so H.264 video cannot be encoded.");

    s.videoStream = avformat_new_stream (s.format, nullptr);
    s.videoCtx = avcodec_alloc_context3 (videoCodec);

    if (s.videoStream == nullptr || s.videoCtx == nullptr)
        return fail ("Could not allocate the video stream.");

    s.videoCtx->width = outWidth;
    s.videoCtx->height = outHeight;
    s.videoCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    s.videoCtx->time_base = { fpsDen, fpsNum };         // 1 / fps
    s.videoCtx->framerate = { fpsNum, fpsDen };
    s.videoCtx->gop_size = 250;

    av_opt_set (s.videoCtx->priv_data, "preset", "veryfast", 0);
    av_opt_set (s.videoCtx->priv_data, "crf",
                juce::String (juce::jlimit (0, 51, options.crf)).toRawUTF8(), 0);

    if ((s.format->oformat->flags & AVFMT_GLOBALHEADER) != 0)
        s.videoCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2 (s.videoCtx, videoCodec, nullptr) < 0)
        return fail ("Could not open the H.264 encoder.");

    if (avcodec_parameters_from_context (s.videoStream->codecpar, s.videoCtx) < 0)
        return fail ("Could not set the video stream parameters.");

    s.videoStream->time_base = s.videoCtx->time_base;

    //--- audio stream (native AAC, 48 kHz stereo, 192 kbps)
    const AVCodec* audioCodec = avcodec_find_encoder (AV_CODEC_ID_AAC);

    if (audioCodec == nullptr)
        return fail ("The AAC encoder is not available in this FFmpeg build.");

    s.audioStream = avformat_new_stream (s.format, nullptr);
    s.audioCtx = avcodec_alloc_context3 (audioCodec);

    if (s.audioStream == nullptr || s.audioCtx == nullptr)
        return fail ("Could not allocate the audio stream.");

    s.audioCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;

    // The whole audio path (FIFO, resampler) speaks planar float, so
    // the encoder must accept it. The native AAC encoder always does;
    // anything else is refused with a clear message rather than
    // silently producing a broken stream.
    {
        bool supportsFltp = false;

       #if LIBAVCODEC_VERSION_MAJOR >= 61
        const AVSampleFormat* formats = nullptr;
        int numFormats = 0;

        if (avcodec_get_supported_config (nullptr, audioCodec,
                                          AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
                                          (const void**) &formats,
                                          &numFormats) == 0
            && formats != nullptr)
        {
            for (int i = 0; i < numFormats; ++i)
                if (formats[i] == AV_SAMPLE_FMT_FLTP)
                    supportsFltp = true;
        }
       #else
        if (audioCodec->sample_fmts != nullptr)
            for (const AVSampleFormat* f = audioCodec->sample_fmts;
                 *f != AV_SAMPLE_FMT_NONE; ++f)
                if (*f == AV_SAMPLE_FMT_FLTP)
                    supportsFltp = true;
       #endif

        if (! supportsFltp)
            return fail ("The AAC encoder in this FFmpeg build does not "
                         "accept planar float audio.");
    }

    av_channel_layout_default (&s.audioCtx->ch_layout, 2);
    s.audioCtx->sample_rate = audioOutRate;
    s.audioCtx->bit_rate = 192000;
    s.audioCtx->time_base = { 1, audioOutRate };

    if ((s.format->oformat->flags & AVFMT_GLOBALHEADER) != 0)
        s.audioCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2 (s.audioCtx, audioCodec, nullptr) < 0)
        return fail ("Could not open the AAC encoder.");

    if (avcodec_parameters_from_context (s.audioStream->codecpar, s.audioCtx) < 0)
        return fail ("Could not set the audio stream parameters.");

    s.audioStream->time_base = s.audioCtx->time_base;

    const int audioFrameSize = s.audioCtx->frame_size > 0 ? s.audioCtx->frame_size : 1024;

    //--- frames + packet
    s.videoFrame = av_frame_alloc();
    s.audioFrame = av_frame_alloc();
    s.packet = av_packet_alloc();

    if (s.videoFrame == nullptr || s.audioFrame == nullptr || s.packet == nullptr)
        return fail ("Out of memory allocating encoder frames.");

    s.videoFrame->format = AV_PIX_FMT_YUV420P;
    s.videoFrame->width = outWidth;
    s.videoFrame->height = outHeight;

    if (av_frame_get_buffer (s.videoFrame, 32) < 0)
        return fail ("Out of memory allocating the video frame.");

    s.audioFrame->format = s.audioCtx->sample_fmt;
    av_channel_layout_copy (&s.audioFrame->ch_layout, &s.audioCtx->ch_layout);
    s.audioFrame->sample_rate = audioOutRate;
    s.audioFrame->nb_samples = audioFrameSize;

    if (av_frame_get_buffer (s.audioFrame, 0) < 0)
        return fail ("Out of memory allocating the audio frame.");

    //--- input resampler (engine rate -> 48 kHz), skipped when equal
    const int engineRateInt = (int) std::llround (engineRate);
    const bool needResample = engineRateInt != audioOutRate;

    if (needResample)
    {
        AVChannelLayout inLayout, outLayout;
        av_channel_layout_default (&inLayout, 2);
        av_channel_layout_default (&outLayout, 2);

        const int result = swr_alloc_set_opts2 (&s.resampler,
                                                &outLayout, AV_SAMPLE_FMT_FLTP, audioOutRate,
                                                &inLayout, AV_SAMPLE_FMT_FLTP, engineRateInt,
                                                0, nullptr);
        av_channel_layout_uninit (&inLayout);
        av_channel_layout_uninit (&outLayout);

        if (result < 0 || s.resampler == nullptr || swr_init (s.resampler) < 0)
            return fail ("Could not create the audio resampler.");
    }

    s.fifo = av_audio_fifo_alloc (AV_SAMPLE_FMT_FLTP, 2, audioFrameSize * 4);

    if (s.fifo == nullptr)
        return fail ("Out of memory allocating the audio FIFO.");

    //--- open the file and write the header (faststart for streaming)
    if ((s.format->oformat->flags & AVFMT_NOFILE) == 0)
    {
        if (avio_open (&s.format->pb, target.getFullPathName().toRawUTF8(),
                       AVIO_FLAG_WRITE) < 0)
            return fail ("Could not create the file:\n" + target.getFullPathName());

        s.ioOpened = true;
    }

    {
        AVDictionary* muxOptions = nullptr;
        av_dict_set (&muxOptions, "movflags", "+faststart", 0);
        const int result = avformat_write_header (s.format, &muxOptions);
        av_dict_free (&muxOptions);

        if (result < 0)
            return fail ("Could not write the mp4 header: " + ffmpegError (result));

        s.headerWritten = true;
    }

    //======================================================================
    // 3. Audio feeder: engine chunks -> (resample) -> FIFO, sample
    //    exact. Production is capped at totalOutAudio and padded with
    //    silence at the end, so the AAC stream is exactly the export
    //    span long.
    juce::AudioBuffer<float> renderBuffer (2, audioChunkSamples);
    std::vector<float> resampled[2];
    juce::int64 renderPos = 0;          // engine samples rendered so far
    juce::int64 producedAudio = 0;      // 48 kHz samples written to FIFO
    bool resamplerDrained = false;
    juce::int64 encodedAudio = 0;       // 48 kHz samples sent to the AAC encoder

    auto writeToFifo = [&] (const float* const* channels, int numSamples) -> bool
    {
        if (numSamples <= 0)
            return true;

        const juce::int64 room = totalOutAudio - producedAudio;
        if (room <= 0)
            return true;

        const int toWrite = (int) juce::jmin<juce::int64> (numSamples, room);
        void* data[2] = { const_cast<float*> (channels[0]),
                          const_cast<float*> (channels[1]) };

        if (av_audio_fifo_write (s.fifo, data, toWrite) < toWrite)
        {
            errorOut = "Out of memory in the audio FIFO.";
            return false;
        }

        producedAudio += toWrite;
        return true;
    };

    std::function<bool()> produceMore = [&]() -> bool
    {
        if (isCancelled (shouldCancel))
            return false;

        if (renderPos < totalAudioSamples)
        {
            const int chunk = (int) juce::jmin<juce::int64> (
                audioChunkSamples, totalAudioSamples - renderPos);

            engine.renderOfflineMixdownBlock (renderBuffer, renderPos, chunk);
            renderPos += chunk;

            if (! needResample)
            {
                const float* channels[2] = { renderBuffer.getReadPointer (0),
                                             renderBuffer.getReadPointer (1) };
                return writeToFifo (channels, chunk);
            }

            const int maxOut = (int) swr_get_out_samples (s.resampler, chunk) + 1024;

            if ((int) resampled[0].size() < maxOut)
            {
                resampled[0].resize ((size_t) maxOut);
                resampled[1].resize ((size_t) maxOut);
            }

            uint8_t* inData[2] = { (uint8_t*) renderBuffer.getWritePointer (0),
                                   (uint8_t*) renderBuffer.getWritePointer (1) };
            uint8_t* outData[2] = { (uint8_t*) resampled[0].data(),
                                    (uint8_t*) resampled[1].data() };

            const int converted = swr_convert (s.resampler, outData, maxOut,
                                               (const uint8_t**) inData, chunk);

            if (converted < 0)
            {
                errorOut = "Audio resampling failed.";
                return false;
            }

            const float* channels[2] = { resampled[0].data(), resampled[1].data() };
            return writeToFifo (channels, converted);
        }

        if (needResample && ! resamplerDrained)
        {
            // Flush whatever the resampler is still holding.
            const int maxOut = 16384;

            if ((int) resampled[0].size() < maxOut)
            {
                resampled[0].resize ((size_t) maxOut);
                resampled[1].resize ((size_t) maxOut);
            }

            uint8_t* outData[2] = { (uint8_t*) resampled[0].data(),
                                    (uint8_t*) resampled[1].data() };

            const int converted = swr_convert (s.resampler, outData, maxOut,
                                               nullptr, 0);
            resamplerDrained = true;

            if (converted < 0)
            {
                errorOut = "Audio resampling failed.";
                return false;
            }

            const float* channels[2] = { resampled[0].data(), resampled[1].data() };
            return writeToFifo (channels, converted);
        }

        // Everything rendered: pad with silence so the stream reaches
        // exactly totalOutAudio samples.
        if (producedAudio < totalOutAudio)
        {
            static float silence[audioChunkSamples] = {};
            const float* channels[2] = { silence, silence };
            const int pad = (int) juce::jmin<juce::int64> (
                audioChunkSamples, totalOutAudio - producedAudio);
            return writeToFifo (channels, pad);
        }

        return true;
    };

    std::function<bool (juce::int64)> encodeAudioUntil = [&] (juce::int64 target48) -> bool
    {
        target48 = juce::jmin (target48, totalOutAudio);

        while (encodedAudio + audioFrameSize <= target48)
        {
            while (av_audio_fifo_size (s.fifo) < audioFrameSize)
            {
                const juce::int64 before = producedAudio;
                if (! produceMore())
                    return false;
                if (producedAudio == before && av_audio_fifo_size (s.fifo) < audioFrameSize)
                    break;   // cannot happen (padding guarantees supply)
            }

            if (av_audio_fifo_size (s.fifo) < audioFrameSize)
                return false;

            if (av_frame_make_writable (s.audioFrame) < 0)
                return false;

            s.audioFrame->nb_samples = audioFrameSize;

            if (av_audio_fifo_read (s.fifo, (void**) s.audioFrame->data,
                                   audioFrameSize) < audioFrameSize)
                return false;

            s.audioFrame->pts = encodedAudio;

            if (avcodec_send_frame (s.audioCtx, s.audioFrame) < 0)
            {
                errorOut = "Sending audio to the AAC encoder failed.";
                return false;
            }

            encodedAudio += audioFrameSize;

            if (! writeEncodedPackets (s, s.audioCtx, s.audioStream, errorOut))
                return false;
        }

        return true;
    };

    //======================================================================
    // 4. Video composition (M6.3): the shared layer mapping
    //    (VideoCompositing::computeLayers) decides what covers t —
    //    overlaps dissolve, fades scale alpha, and the text track's
    //    active telop is drawn over the result (Telop). Two paths:
    //      - fast path: zero/one fully-opaque layer and no telop —
    //        the original Phase 2 route below (decode -> sws-fit
    //        straight into the YUV frame), kept so plain exports stay
    //        numerically identical to M6.2;
    //      - canvas path: anything layered, faded or telop'd composes
    //        an output-size RGB canvas first (each layer fitted into
    //        it at its alpha, telop on top), then one sws pass
    //        converts the canvas to YUV.
    //    Decoders are pooled per clip id: export time runs
    //    monotonically, so every clip's decoder only ever steps
    //    forward through its own window (reseek only at clip
    //    boundaries — exactly VideoDecoder's own seek behaviour).
    std::map<juce::int64, std::unique_ptr<VideoDecoder>> decoderPool;

    auto layerImage = [&] (const VideoCompositing::Layer& layer,
                           juce::Image& imageOut) -> bool
    {
        auto& decoder = decoderPool[layer.clip->id];

        if (decoder == nullptr)
        {
            decoder = std::make_unique<VideoDecoder>();

            if (! decoder->open (juce::File (layer.clip->filePath)))
            {
                errorOut = "Could not open the video source:\n" + layer.clip->filePath;
                return false;
            }
        }

        imageOut = decoder->getFrameAt (layer.sourceSeconds);
        return true;
    };

    auto composeFrame = [&] (double t, juce::int64 frameIndex) -> bool
    {
        std::vector<VideoCompositing::Layer> layers;
        VideoCompositing::computeLayers (clips, t, layers);
        const auto* telop = Telop::findActiveClip (textClips, t);

        if (av_frame_make_writable (s.videoFrame) < 0)
            return false;

        s.videoFrame->pts = frameIndex;

        const bool simple = telop == nullptr
                            && (layers.empty()
                                || (layers.size() == 1 && layers[0].alpha >= 0.999f));

        if (! simple)
        {
            std::vector<VideoCompositing::LayerImage> layerImages;

            for (auto& layer : layers)
            {
                juce::Image image;

                if (! layerImage (layer, image))
                    return false;

                layerImages.push_back ({ image, layer.alpha });
            }

            auto canvas = VideoCompositing::composeFrame (outWidth, outHeight,
                                                          layerImages);

            // A gap with an active telop lands here too: the canvas
            // is black and the text still shows (lyric-only spans).
            if (telop != nullptr)
                Telop::drawOnto (canvas, telop->text, telopStyle);

            s.canvasScaler = sws_getCachedContext (
                s.canvasScaler,
                outWidth, outHeight, AV_PIX_FMT_BGR24,
                outWidth, outHeight, AV_PIX_FMT_YUV420P,
                SWS_BILINEAR, nullptr, nullptr, nullptr);

            if (s.canvasScaler == nullptr)
            {
                errorOut = "Could not create the video scaler.";
                return false;
            }

            // The decoder's juce::Image::RGB layout is packed BGR —
            // AV_PIX_FMT_BGR24 (see the fast path below).
            juce::Image::BitmapData bitmap (canvas, juce::Image::BitmapData::readOnly);
            const uint8_t* srcData[1] = { bitmap.getLinePointer (0) };
            const int srcStride[1] = { bitmap.lineStride };

            sws_scale (s.canvasScaler, srcData, srcStride, 0, outHeight,
                       s.videoFrame->data, s.videoFrame->linesize);
            return true;
        }

        //--- Fast path: the Phase 2 single-picture route, unchanged.
        fillBlack (s.videoFrame);

        if (layers.empty())
            return true;   // gap: a black frame

        juce::Image image;

        if (! layerImage (layers[0], image))
            return false;

        if (! image.isValid())
            return true;   // undecodable instant: keep the black frame

        const int srcWidth = image.getWidth();
        const int srcHeight = image.getHeight();
        const double scale = juce::jmin ((double) outWidth / srcWidth,
                                         (double) outHeight / srcHeight);
        const int scaledWidth = juce::jlimit (2, outWidth,
                                evenDown ((int) std::floor (srcWidth * scale)));
        const int scaledHeight = juce::jlimit (2, outHeight,
                                evenDown ((int) std::floor (srcHeight * scale)));
        // NB: the offsets must NOT go through evenDown() — that
        // helper clamps dimensions to a minimum of 2, which would
        // turn a legitimate 0 offset into 2 and shift the picture.
        // Both sizes are even, so the difference is even already.
        const int offsetX = ((outWidth - scaledWidth) / 2) & ~1;
        const int offsetY = ((outHeight - scaledHeight) / 2) & ~1;

        if (s.scaledFrame == nullptr)
            s.scaledFrame = av_frame_alloc();

        if (s.scaledWidth != scaledWidth || s.scaledHeight != scaledHeight)
        {
            av_frame_unref (s.scaledFrame);
            s.scaledFrame->format = AV_PIX_FMT_YUV420P;
            s.scaledFrame->width = scaledWidth;
            s.scaledFrame->height = scaledHeight;

            if (av_frame_get_buffer (s.scaledFrame, 32) < 0)
            {
                errorOut = "Out of memory allocating the scaler frame.";
                return false;
            }

            s.scaledWidth = scaledWidth;
            s.scaledHeight = scaledHeight;
        }

        if (av_frame_make_writable (s.scaledFrame) < 0)
            return false;

        s.scaler = sws_getCachedContext (s.scaler,
                                         srcWidth, srcHeight, AV_PIX_FMT_BGR24,
                                         scaledWidth, scaledHeight, AV_PIX_FMT_YUV420P,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);

        if (s.scaler == nullptr)
        {
            errorOut = "Could not create the video scaler.";
            return false;
        }

        {
            // Phase 1's decoder delivers juce::Image::RGB, whose memory
            // layout is packed B,G,R — AV_PIX_FMT_BGR24.
            juce::Image::BitmapData bitmap (image, juce::Image::BitmapData::readOnly);
            const uint8_t* srcData[1] = { bitmap.getLinePointer (0) };
            const int srcStride[1] = { bitmap.lineStride };

            sws_scale (s.scaler, srcData, srcStride, 0, srcHeight,
                       s.scaledFrame->data, s.scaledFrame->linesize);
        }


        for (int plane = 0; plane < 3; ++plane)
        {
            const int planeWidth  = plane == 0 ? scaledWidth  : scaledWidth / 2;
            const int planeHeight = plane == 0 ? scaledHeight : scaledHeight / 2;
            const int planeX = plane == 0 ? offsetX : offsetX / 2;
            const int planeY = plane == 0 ? offsetY : offsetY / 2;

            for (int y = 0; y < planeHeight; ++y)
            {
                std::memcpy (s.videoFrame->data[plane]
                                 + (size_t) (planeY + y) * s.videoFrame->linesize[plane]
                                 + planeX,
                             s.scaledFrame->data[plane]
                                 + (size_t) y * s.scaledFrame->linesize[plane],
                             (size_t) planeWidth);
            }
        }

        return true;
    };

    //======================================================================
    // 5. The interleave loop, then flush + trailer.
    bool ok = true;
    bool cancelled = false;

    for (juce::int64 frame = 0; frame < totalFrames && ok; ++frame)
    {
        if (isCancelled (shouldCancel))
        {
            cancelled = true;
            ok = false;
            break;
        }

        const double t = (double) frame * (double) fpsDen / (double) fpsNum;

        if (! composeFrame (t, frame))
        {
            ok = false;
            break;
        }



        if (avcodec_send_frame (s.videoCtx, s.videoFrame) < 0)
        {
            errorOut = "Sending video to the H.264 encoder failed.";
            ok = false;
            break;
        }

        if (! writeEncodedPackets (s, s.videoCtx, s.videoStream, errorOut))
        {
            ok = false;
            break;
        }

        // Keep the audio roughly one frame ahead of the video.
        const juce::int64 audioTarget = (juce::int64) std::llround (
            (double) (frame + 1) * (double) fpsDen / (double) fpsNum * audioOutRate);

        if (! encodeAudioUntil (audioTarget))
        {
            cancelled = isCancelled (shouldCancel);
            ok = false;
            break;
        }

        if (progress01 != nullptr)
            progress01->store (0.95f * (float) frame / (float) totalFrames);
    }

    if (ok)
    {
        // Remaining audio (the tail past the last frame boundary) and
        // the encoder flushes.
        if (! encodeAudioUntil (totalOutAudio))
        {
            cancelled = isCancelled (shouldCancel);
            ok = false;
        }

        // Final partial AAC frame, if any samples are left.
        if (ok && av_audio_fifo_size (s.fifo) > 0)
        {
            const int remaining = av_audio_fifo_size (s.fifo);

            if (av_frame_make_writable (s.audioFrame) >= 0)
            {
                s.audioFrame->nb_samples = remaining;
                av_audio_fifo_read (s.fifo, (void**) s.audioFrame->data, remaining);
                s.audioFrame->pts = encodedAudio;
                encodedAudio += remaining;

                if (avcodec_send_frame (s.audioCtx, s.audioFrame) < 0)
                {
                    errorOut = "Sending audio to the AAC encoder failed.";
                    ok = false;
                }
                else if (! writeEncodedPackets (s, s.audioCtx, s.audioStream, errorOut))
                {
                    ok = false;
                }
            }
        }

        if (ok)
        {
            avcodec_send_frame (s.videoCtx, nullptr);
            ok = writeEncodedPackets (s, s.videoCtx, s.videoStream, errorOut);
        }

        if (ok)
        {
            avcodec_send_frame (s.audioCtx, nullptr);
            ok = writeEncodedPackets (s, s.audioCtx, s.audioStream, errorOut);
        }

        if (ok && av_write_trailer (s.format) < 0)
        {
            errorOut = "Writing the mp4 trailer failed.";
            ok = false;
        }

        if (ok && progress01 != nullptr)
            progress01->store (1.0f);
    }

    s.close();
    decoderPool.clear();   // M6.3: closes every pooled clip decoder

    if (! ok)
    {
        if (cancelled || isCancelled (shouldCancel))
        {
            target.deleteFile();
            errorOut = "Export cancelled.";
        }

        return false;
    }

    return true;
}
