#include "VideoAudioExtractor.h"

#include <cmath>

extern "C"
{
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libswresample/swresample.h>
    #include <libavutil/audio_fifo.h>
    #include <libavutil/channel_layout.h>
    #include <libavutil/mem.h>
    #include <libavutil/opt.h>
}

//==============================================================================
namespace
{
    struct DecodeState
    {
        AVFormatContext* format = nullptr;
        AVCodecContext* codec = nullptr;
        SwrContext* swr = nullptr;
        int audioStreamIndex = -1;
        double streamSeconds = 0.0;   // for progress (may stay 0)

        ~DecodeState()
        {
            if (swr != nullptr) swr_free (&swr);
            if (codec != nullptr) avcodec_free_context (&codec);
            if (format != nullptr) avformat_close_input (&format);
        }
    };

    bool cancelled (std::atomic<bool>* flag)
    {
        return flag != nullptr && flag->load();
    }
}

//==============================================================================
bool VideoAudioExtraction::decodeToWav (const juce::File& videoFile,
                                        const juce::File& wavTarget,
                                        double targetSampleRate,
                                        double& durationSecondsOut,
                                        bool& noAudioStream,
                                        juce::String& errorOut,
                                        std::atomic<float>* progress01,
                                        std::atomic<bool>* shouldCancel)
{
    durationSecondsOut = 0.0;
    noAudioStream = false;

    DecodeState s;

    if (avformat_open_input (&s.format, videoFile.getFullPathName().toRawUTF8(),
                             nullptr, nullptr) < 0
         || avformat_find_stream_info (s.format, nullptr) < 0)
    {
        errorOut = "could not open the file";
        return false;
    }

    const AVCodec* decoder = nullptr;
    s.audioStreamIndex = av_find_best_stream (s.format, AVMEDIA_TYPE_AUDIO,
                                              -1, -1, &decoder, 0);

    if (s.audioStreamIndex < 0 || decoder == nullptr)
    {
        noAudioStream = true;
        errorOut = "the file has no audio stream";
        return false;
    }

    auto* stream = s.format->streams[s.audioStreamIndex];

    if (stream->duration > 0)
        s.streamSeconds = (double) stream->duration * av_q2d (stream->time_base);
    else if (s.format->duration > 0)
        s.streamSeconds = (double) s.format->duration / (double) AV_TIME_BASE;

    s.codec = avcodec_alloc_context3 (decoder);

    if (s.codec == nullptr
         || avcodec_parameters_to_context (s.codec, stream->codecpar) < 0
         || avcodec_open2 (s.codec, decoder, nullptr) < 0)
    {
        errorOut = "could not open the audio decoder";
        return false;
    }

    // Resample everything to interleaved s16 stereo at the target rate.
    AVChannelLayout outLayout;
    av_channel_layout_default (&outLayout, 2);

    AVChannelLayout inLayout;

    if (s.codec->ch_layout.nb_channels > 0)
        av_channel_layout_copy (&inLayout, &s.codec->ch_layout);
    else
        av_channel_layout_default (&inLayout, s.codec->ch_layout.nb_channels > 0
                                               ? s.codec->ch_layout.nb_channels : 2);

    const int swrResult = swr_alloc_set_opts2 (&s.swr,
                                               &outLayout, AV_SAMPLE_FMT_S16,
                                               (int) targetSampleRate,
                                               &inLayout, s.codec->sample_fmt,
                                               s.codec->sample_rate, 0, nullptr);
    av_channel_layout_uninit (&outLayout);
    av_channel_layout_uninit (&inLayout);

    if (swrResult < 0 || s.swr == nullptr || swr_init (s.swr) < 0)
    {
        errorOut = "could not set up the resampler";
        return false;
    }

    // Decode + convert into a growing stereo float buffer (converted
    // back from the s16 stream; the WAV write below stays 16-bit, so
    // nothing is lost).
    juce::AudioBuffer<float> audio (2, 0);
    int capacity = 0;
    int written = 0;

    auto appendSamples = [&] (const int16_t* interleaved, int frames)
    {
        if (frames <= 0)
            return;

        if (written + frames > capacity)
        {
            const int newCapacity = juce::jmax (written + frames,
                                                juce::jmax (capacity * 2, 1 << 15));
            juce::AudioBuffer<float> bigger (2, newCapacity);
            bigger.copyFrom (0, 0, audio, 0, 0, written);
            bigger.copyFrom (1, 0, audio, 1, 0, written);
            audio = std::move (bigger);
            capacity = newCapacity;
        }

        auto* left = audio.getWritePointer (0);
        auto* right = audio.getWritePointer (1);

        for (int i = 0; i < frames; ++i)
        {
            left[written + i]  = (float) interleaved[2 * i]     / 32768.0f;
            right[written + i] = (float) interleaved[2 * i + 1] / 32768.0f;
        }

        written += frames;
    };

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    bool failed = false;

    auto convertAndAppend = [&] (AVFrame* decodedFrame) -> bool
    {
        const int outCapacity = (int) ((juce::int64) decodedFrame->nb_samples
                                           * (juce::int64) targetSampleRate
                                           / juce::jmax (1, s.codec->sample_rate)) + 1024;
        juce::HeapBlock<int16_t> converted ((size_t) juce::jmax (1, outCapacity) * 2);
        uint8_t* outData = reinterpret_cast<uint8_t*> (converted.getData());

        const int framesOut = swr_convert (s.swr, &outData, outCapacity,
                                           (const uint8_t**) decodedFrame->extended_data,
                                           decodedFrame->nb_samples);
        if (framesOut < 0)
            return false;

        appendSamples (converted.getData(), framesOut);
        return true;
    };

    while (! failed && av_read_frame (s.format, packet) >= 0)
    {
        if (cancelled (shouldCancel))
            break;

        if (packet->stream_index == s.audioStreamIndex
             && avcodec_send_packet (s.codec, packet) >= 0)
        {
            while (avcodec_receive_frame (s.codec, frame) >= 0)
            {
                if (! convertAndAppend (frame))
                {
                    errorOut = "resampling failed";
                    failed = true;
                    break;
                }

                av_frame_unref (frame);
            }
        }

        av_packet_unref (packet);

        if (progress01 != nullptr && s.streamSeconds > 0.0 && targetSampleRate > 0.0)
            progress01->store (juce::jlimit (0.0f, 1.0f,
                                             (float) ((written / targetSampleRate)
                                                      / s.streamSeconds)));
    }

    // Flush the decoder, then drain the resampler's delay line.
    if (! failed && ! cancelled (shouldCancel))
    {
        avcodec_send_packet (s.codec, nullptr);

        while (avcodec_receive_frame (s.codec, frame) >= 0)
        {
            if (! convertAndAppend (frame))
            {
                errorOut = "resampling failed";
                failed = true;
                break;
            }

            av_frame_unref (frame);
        }

        for (int guard = 0; guard < 64 && ! failed; ++guard)
        {
            const int delay = (int) swr_get_delay (s.swr, (int) targetSampleRate);

            if (delay <= 0)
                break;

            juce::HeapBlock<int16_t> tail ((size_t) delay * 2);
            uint8_t* outData = reinterpret_cast<uint8_t*> (tail.getData());
            const int framesOut = swr_convert (s.swr, &outData, delay, nullptr, 0);

            if (framesOut <= 0)
                break;

            appendSamples (tail.getData(), framesOut);
        }
    }

    av_frame_free (&frame);
    av_packet_free (&packet);

    if (cancelled (shouldCancel))
    {
        errorOut = "cancelled";
        return false;
    }

    if (failed)
        return false;

    if (written <= 0)
    {
        noAudioStream = true;
        errorOut = "the audio stream decoded to silence (no samples)";
        return false;
    }

    durationSecondsOut = targetSampleRate > 0.0 ? written / targetSampleRate : 0.0;

    // Write 16-bit PCM WAV (the project's normal audio file format).
    wavTarget.deleteFile();
    std::unique_ptr<juce::OutputStream> outStream (wavTarget.createOutputStream());

    if (outStream != nullptr)
    {
        juce::WavAudioFormat wavFormat;
        const auto options = juce::AudioFormatWriterOptions()
                                 .withSampleRate (targetSampleRate)
                                 .withNumChannels (2)
                                 .withBitsPerSample (16);

        if (auto writer = wavFormat.createWriterFor (outStream, options))
        {
            const float* channels[2] = { audio.getReadPointer (0),
                                         audio.getReadPointer (1) };
            const bool ok = writer->writeFromFloatArrays (channels, 2, written);

            if (progress01 != nullptr)
                progress01->store (1.0f);

            if (ok)
                return true;
        }
    }

    errorOut = "could not write the WAV file";
    return false;
}
