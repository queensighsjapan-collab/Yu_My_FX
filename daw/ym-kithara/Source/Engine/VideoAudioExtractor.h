#pragma once

#include <JuceHeader.h>
#include <atomic>

//==============================================================================
/**
    VideoAudioExtraction — decode a video file's own audio stream to
    a WAV file (M6.2).

    Kept free of any engine/UI dependency so the headless smoke
    harness can call the core directly. The engine wraps it with the
    session-side work (sources, tracks, aligned clips).
*/
namespace VideoAudioExtraction
{
    /**
        Decodes the first audio stream of `videoFile` via FFmpeg,
        resamples it to stereo at `targetSampleRate` and writes a
        16-bit PCM WAV to `wavTarget` (overwritten if it exists).

        Returns false when the file cannot be opened, has no audio
        stream (`noAudioStream` is then set true) or decoding fails;
        `errorOut` explains. `progress01` (optional) runs 0..1 against
        the stream's duration where known; `shouldCancel` (optional)
        aborts promptly, in which case no file is written.
    */
    bool decodeToWav (const juce::File& videoFile, const juce::File& wavTarget,
                      double targetSampleRate,
                      double& durationSecondsOut, bool& noAudioStream,
                      juce::String& errorOut,
                      std::atomic<float>* progress01 = nullptr,
                      std::atomic<bool>* shouldCancel = nullptr);
}
