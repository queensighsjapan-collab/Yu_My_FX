//==============================================================================
// YM Amp headless smoke harness.
//
// Usage: YMAmpSmoke <path-to-EVH-5150-A2.nam> [workdir]
//
// Exercises the real processor without a host:
//   1. default factory model (Plexi) loads asynchronously
//   2. the bundled EVH 5150 III A2 (SlimmableContainer) model loads
//   3. sine + noise burst at 48 kHz -> finite, non-silent output
//   4. MIDI CC 65 bypass toggles the parameter and crossfades to dry
//   5. same signal at 44.1 kHz (resampled NAM path) -> finite, non-silent
//   6. FILE player path: a generated WAV is decoded, played through
//      the chain with silent live input -> non-silent output
// Prints measured numbers and exits non-zero on any failure.
//==============================================================================

#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"

#include <cmath>
#include <cstdio>

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("[%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());

        if (! ok)
            ++failures;
    }

    struct Measure
    {
        double rms = 0.0;
        double peak = 0.0;
        double inputRms = 0.0;
        bool allFinite = true;
        long subnormals = 0;
    };

    // Runs `seconds` of a 220 Hz sine (amplitude 0.25) with a short
    // noise burst in the first half, in blocks of `blockSize`. RMS is
    // measured over the second half (steady state).
    Measure runSignal (YMAmpProcessor& proc, double seconds, int blockSize,
                       juce::MidiBuffer firstBlockMidi = {})
    {
        const double sr = proc.getSampleRate();
        const int totalSamples = (int) (seconds * sr);
        const int measureFrom = totalSamples / 2;

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer emptyMidi;

        double phase = 0.0;
        const double phaseInc = 2.0 * juce::MathConstants<double>::pi * 220.0 / sr;
        juce::uint32 noiseState = 22222;

        double sumSq = 0.0, sumInSq = 0.0;
        int measured = 0;
        Measure result;

        bool firstBlock = true;

        for (int done = 0; done < totalSamples; done += blockSize)
        {
            const int n = juce::jmin (blockSize, totalSamples - done);

            for (int i = 0; i < n; ++i)
            {
                const int index = done + i;
                float sample = (float) (std::sin (phase) * 0.25);
                phase += phaseInc;

                if (index > totalSamples * 3 / 10 && index < totalSamples * 4 / 10)
                {
                    noiseState = noiseState * 1664525u + 1013904223u;
                    const float noise = ((float) ((noiseState >> 9) & 0x7fff) / 16384.0f - 1.0f) * 0.1f;
                    sample += noise;
                }

                buffer.setSample (0, i, sample);
                buffer.setSample (1, i, sample);
            }

            auto& midi = firstBlock ? firstBlockMidi : emptyMidi;
            proc.processBlock (buffer, midi);
            firstBlock = false;

            for (int i = 0; i < n; ++i)
            {
                const int index = done + i;
                const float out = buffer.getSample (0, i);

                if (! std::isfinite (out))
                    result.allFinite = false;

                if (std::fpclassify (out) == FP_SUBNORMAL)
                    ++result.subnormals;

                if (index >= measureFrom)
                {
                    sumSq += (double) out * out;
                    const float inSample = (float) (std::sin (phase - phaseInc * (double) (n - i)) * 0.25);
                    sumInSq += (double) inSample * inSample;
                    ++measured;
                }

                result.peak = juce::jmax (result.peak, (double) std::abs (out));
            }
        }

        if (measured > 0)
        {
            result.rms = std::sqrt (sumSq / measured);
            result.inputRms = std::sqrt (sumInSq / measured);
        }

        return result;
    }

    bool writeTestWav (const juce::File& file)
    {
        const double sr = 48000.0;
        const int numSamples = (int) sr;
        juce::AudioBuffer<float> data (2, numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            const float s = (float) (std::sin (2.0 * juce::MathConstants<double>::pi * 330.0
                                               * (double) i / sr) * 0.3);
            data.setSample (0, i, s);
            data.setSample (1, i, s);
        }

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());

        if (stream == nullptr)
            return false;

        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.release(), sr, 2, 16, {}, 0));

        if (writer == nullptr)
            return false;

        return writer->writeFromAudioSampleBuffer (data, 0, numSamples);
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 2)
    {
        std::printf ("usage: YMAmpSmoke <evh-5150-a2.nam> [workdir]\n");
        return 2;
    }

    const juce::File evhFile { juce::String (argv[1]) };
    const juce::File workDir = argc > 2 ? juce::File (juce::String (argv[2]))
                                        : juce::File::getCurrentWorkingDirectory();

    std::printf ("YM Amp smoke test\n  EVH model: %s\n", evhFile.getFullPathName().toRawUTF8());
    check (evhFile.existsAsFile(), "EVH model file exists");

    auto proc = std::make_unique<YMAmpProcessor>();

    auto prepare = [&proc] (double sr, int blockSize)
    {
        proc->setRateAndBufferSizeDetails (sr, blockSize);
        proc->prepareToPlay (sr, blockSize);
    };

    //-- 1. the constructor's default (Plexi) factory load --------------
    prepare (48000.0, 512);

    bool defaultLoaded = false;

    for (int i = 0; i < 100 && ! defaultLoaded; ++i)
    {
        juce::Thread::sleep (50);
        juce::AudioBuffer<float> silence (2, 512);
        silence.clear(); // AudioBuffer does NOT zero its memory: an
                         // uncleared buffer feeds heap garbage into
                         // the chain (and it lingers in the cab IR's
                         // convolution history).
        juce::MidiBuffer midi;
        proc->processBlock (silence, midi);
        defaultLoaded = proc->isModelLoaded();
    }

    check (defaultLoaded, "default factory model loaded (async)");
    std::printf ("       default model display: %s\n",
                 proc->getModelDisplayName().toRawUTF8());
    std::printf ("       status: %s\n", proc->getStatusText().toRawUTF8());

    //-- 2. EVH 5150 III A2 model ---------------------------------------
    proc->loadModelBlocking (evhFile);
    std::printf ("       status after EVH load: %s\n", proc->getStatusText().toRawUTF8());
    check (proc->isModelLoaded(), "EVH A2 (SlimmableContainer) model loaded");
    check (proc->getModelDisplayName() == "EVH_5150_III_AmpOnly.nam",
           "EVH model display name matches");
    std::printf ("       model sub-display: %s\n", proc->getModelDisplaySub().toRawUTF8());

    //-- 3. 48 kHz signal -------------------------------------------------
    auto r48 = runSignal (*proc, 1.5, 512);
    std::printf ("       48k: rms=%.5f peak=%.4f finite=%d subnormals=%ld\n",
                 r48.rms, r48.peak, (int) r48.allFinite, r48.subnormals);
    check (r48.allFinite, "48k output finite");
    check (r48.peak < 2.0, "48k output peak sane (< 2.0)");
    check (r48.rms > 0.01, "48k output non-silent with model active");
    check (r48.subnormals == 0, "48k output has no subnormals");

    //-- 4. MIDI CC 65 bypass --------------------------------------------
    auto* bypassRaw = proc->apvts.getRawParameterValue ("bypass");

    juce::MidiBuffer ccOff;
    ccOff.addEvent (juce::MidiMessage::controllerEvent (1, 65, 0), 0);
    auto rBypass = runSignal (*proc, 1.0, 512, ccOff);
    std::printf ("       bypass via CC65=0: param=%.0f rmsOut=%.5f rmsIn=%.5f ratio=%.3f\n",
                 (double) bypassRaw->load(), rBypass.rms, rBypass.inputRms,
                 rBypass.inputRms > 0 ? rBypass.rms / rBypass.inputRms : 0.0);
    check (bypassRaw->load() > 0.5f, "CC65 value 0 engages bypass parameter");
    check (rBypass.allFinite && rBypass.inputRms > 0
               && std::abs (rBypass.rms / rBypass.inputRms - 1.0) < 0.05,
           "bypassed output matches dry input within 5%");

    juce::MidiBuffer ccOn;
    ccOn.addEvent (juce::MidiMessage::controllerEvent (1, 65, 127), 0);
    juce::AudioBuffer<float> oneBlock (2, 512);
    oneBlock.clear();
    proc->processBlock (oneBlock, ccOn);
    check (bypassRaw->load() < 0.5f, "CC65 value 127 releases bypass parameter");

    //-- 5. 44.1 kHz (resampled NAM path) ----------------------------------
    proc->releaseResources();
    prepare (44100.0, 512);

    // The model swap was dropped by releaseResources(); reload.
    proc->loadModelBlocking (evhFile);
    check (proc->isModelLoaded(), "EVH model reloaded for 44.1k run");

    auto r441 = runSignal (*proc, 1.5, 512);
    std::printf ("       44.1k: rms=%.5f peak=%.4f finite=%d subnormals=%ld latency=%d\n",
                 r441.rms, r441.peak, (int) r441.allFinite, r441.subnormals,
                 proc->getLatencySamples());
    check (r441.allFinite, "44.1k output finite");
    check (r441.peak < 2.0, "44.1k output peak sane (< 2.0)");
    check (r441.rms > 0.01, "44.1k output non-silent (resampled NAM path)");
    check (r441.subnormals == 0, "44.1k output has no subnormals");

    //-- 6. FILE player ------------------------------------------------------
    proc->releaseResources();
    prepare (48000.0, 512);
    proc->loadModelBlocking (evhFile);

    const auto testWav = workDir.getChildFile ("ymamp_smoke_stem.wav");
    check (writeTestWav (testWav), "test WAV written");

    proc->loadAudioFileBlocking (testWav);
    check (proc->hasAudioFile(), "audio file decoded");

    if (auto* srcParam = proc->apvts.getParameter ("srcSelect"))
        srcParam->setValueNotifyingHost (srcParam->convertTo0to1 (1.0f));

    proc->setFilePlaying (true);

    // Silent live input; all output must come from the file player.
    juce::AudioBuffer<float> silenceIn (2, 512);
    juce::MidiBuffer emptyMidi;
    double fileSumSq = 0.0;
    double filePeak = 0.0;
    bool fileFinite = true;
    int fileMeasured = 0;

    for (int block = 0; block < 60; ++block)
    {
        silenceIn.clear();
        proc->processBlock (silenceIn, emptyMidi);

        if (block >= 20)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float out = silenceIn.getSample (0, i);

                if (! std::isfinite (out))
                    fileFinite = false;

                fileSumSq += (double) out * out;
                filePeak = juce::jmax (filePeak, (double) std::abs (out));
                ++fileMeasured;
            }
        }
    }

    const double fileRms = fileMeasured > 0 ? std::sqrt (fileSumSq / fileMeasured) : 0.0;
    std::printf ("       file player: rms=%.5f peak=%.4f finite=%d posNorm=%.3f\n",
                 fileRms, filePeak, (int) fileFinite, (double) proc->getFilePositionNorm());
    check (fileFinite, "file-player output finite");
    check (fileRms > 0.01, "file-player path outputs signal through the chain");
    check (proc->getFilePositionNorm() > 0.05f, "file playhead advanced");

    std::printf (failures == 0 ? "\nSMOKE OK\n" : "\nSMOKE FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
