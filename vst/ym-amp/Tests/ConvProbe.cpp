//==============================================================================
// Minimal juce::dsp::Convolution behaviour probe (diagnostic only).
//
// Question: does processing after loadImpulseResponse ever emit
// garbage samples, and under which conditions?
//   mode 0: file load, process immediately
//   mode 1: file load, sleep 500 ms, process
//   mode 2: file load, 8 silent prime blocks, process
//   mode 3: file load, process 24 blocks, starve 150 ms, process
//   mode 4: memory load, process 24 blocks, starve 150 ms, process
//   mode 5: memory load, process immediately
// Prints how many of 12 trials produced a peak > 2.0 per mode.
//==============================================================================

#include <JuceHeader.h>

#include <cmath>
#include <cstdio>

namespace
{
    double runOnce (int mode, const juce::File& irFile)
    {
        const bool useMemory = mode >= 4;
        const bool starve = mode == 3 || mode == 4;

        juce::dsp::Convolution conv;
        juce::dsp::ProcessSpec spec { 48000.0,
                                      (juce::uint32) (mode == 6 ? 8192 : 512), 2 };
        conv.reset();
        conv.prepare (spec);

        juce::MemoryBlock mem;

        if (useMemory)
        {
            irFile.loadFileAsData (mem);
            conv.loadImpulseResponse (mem.getData(), mem.getSize(),
                                      juce::dsp::Convolution::Stereo::yes,
                                      juce::dsp::Convolution::Trim::yes, 0,
                                      juce::dsp::Convolution::Normalise::no);
        }
        else
        {
            conv.loadImpulseResponse (irFile,
                                      juce::dsp::Convolution::Stereo::yes,
                                      juce::dsp::Convolution::Trim::yes, 0,
                                      juce::dsp::Convolution::Normalise::no);
        }

        juce::AudioBuffer<float> buf (2, 512);
        double phase = 0.0;

        auto feedSine = [&]
        {
            for (int i = 0; i < 512; ++i)
            {
                const float s = (float) (std::sin (phase) * 0.25);
                phase += 2.0 * juce::MathConstants<double>::pi * 220.0 / 48000.0;
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
        };

        auto processBuf = [&]
        {
            juce::dsp::AudioBlock<float> block (buf);
            juce::dsp::ProcessContextReplacing<float> ctx (block);
            conv.process (ctx);
        };

        if (mode == 1)
            juce::Thread::sleep (500);

        if (mode == 2)
        {
            buf.clear();

            for (int i = 0; i < 8; ++i)
                processBuf();
        }

        if (starve)
        {
            for (int i = 0; i < 24; ++i)
            {
                feedSine();
                processBuf();
            }

            juce::Thread::sleep (150);
        }

        double peak = 0.0;

        for (int b = 0; b < 6; ++b)
        {
            feedSine();
            processBuf();

            for (int i = 0; i < 512; ++i)
                peak = juce::jmax (peak, (double) std::abs (buf.getSample (0, i)));
        }

        return peak;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;

    const juce::File irFile { juce::String (argv[1]) };
    const char* names[7] = { "0 file immediate", "1 file sleep", "2 file prime",
                             "3 file starve", "4 memory starve", "5 memory immediate",
                             "6 maxBlock8192" };

    for (int mode = 0; mode < 7; ++mode)
    {
        int bad = 0;
        double worst = 0.0;

        for (int trial = 0; trial < 12; ++trial)
        {
            const double peak = runOnce (mode, irFile);
            worst = juce::jmax (worst, peak);

            if (peak > 2.0)
                ++bad;
        }

        std::printf ("mode %s: garbage in %d/12 trials, worst peak %.3f\n",
                     names[mode], bad, worst);
    }

    return 0;
}
