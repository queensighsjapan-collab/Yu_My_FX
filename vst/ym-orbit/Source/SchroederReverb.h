#pragma once

#include <JuceHeader.h>

//==============================================================================
/** A small stereo room reverb in the classic Schroeder topology, written
    from scratch for YM Orbit: four parallel feedback comb filters followed
    by two series allpass filters per channel. The right channel uses
    slightly different lengths so the two tails decorrelate.

    The comb loops contain a one-pole lowpass (damping), so the tail gets
    darker as it decays, and the comb feedback is fixed at 0.80 — safely
    below the 0.84 stability clamp YM Orbit designs around.

    This is a deliberately simple "small room" — it exists to give the
    spatialised source a sense of place, not to emulate a concert hall.
*/
class SchroederReverb
{
public:
    SchroederReverb() = default;

    void prepare (double sampleRate)
    {
        // Textbook Schroeder/Moorer-style incommensurate lengths, chosen
        // for this plugin (reference rate 44.1 kHz, scaled to the host).
        static const int combBase[4]   = { 1051, 1139, 1217, 1307 };
        static const int allpassBase[2] = { 503, 341 };
        static const int combSpread    = 19;   // right-channel detune
        static const int allpassSpread = 13;

        const double scale = sampleRate / 44100.0;

        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < 4; ++i)
                combs[ch][i].prepare ((int) std::lround ((combBase[i] + (ch == 1 ? combSpread : 0)) * scale));

            for (int i = 0; i < 2; ++i)
                allpasses[ch][i].prepare ((int) std::lround ((allpassBase[i] + (ch == 1 ? allpassSpread : 0)) * scale));
        }
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            for (auto& comb : combs[ch])     comb.reset();
            for (auto& ap   : allpasses[ch]) ap.reset();
        }
    }

    /** Runs one stereo sample through the reverb; returns the wet pair. */
    void processSample (float inL, float inR, float& wetL, float& wetR)
    {
        wetL = runChannel (0, inL);
        wetR = runChannel (1, inR);
    }

private:
    struct Comb
    {
        void prepare (int length)
        {
            buffer.assign ((size_t) juce::jmax (1, length), 0.0f);
            index = 0;
            store = 0.0f;
        }

        void reset()
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            index = 0;
            store = 0.0f;
        }

        float process (float input)
        {
            const float out = buffer[index];

            // One-pole lowpass inside the loop: the tail darkens over time.
            store += damping * (out - store);
            buffer[index] = input + store * feedback;

            if (++index >= buffer.size())
                index = 0;

            return out;
        }

        std::vector<float> buffer;
        size_t index = 0;
        float store = 0.0f;
        float feedback = 0.80f;   // <= 0.84 by design (stability clamp)
        float damping  = 0.30f;
    };

    struct Allpass
    {
        void prepare (int length)
        {
            buffer.assign ((size_t) juce::jmax (1, length), 0.0f);
            index = 0;
        }

        void reset()
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            index = 0;
        }

        float process (float input)
        {
            const float delayed = buffer[index];
            const float out = delayed - gain * input;
            buffer[index] = input + gain * delayed;

            if (++index >= buffer.size())
                index = 0;

            return out;
        }

        std::vector<float> buffer;
        size_t index = 0;
        float gain = 0.5f;
    };

    float runChannel (int ch, float input)
    {
        float sum = 0.0f;

        for (auto& comb : combs[ch])
            sum += comb.process (input);

        sum *= 0.25f;

        for (auto& ap : allpasses[ch])
            sum = ap.process (sum);

        return sum;
    }

    Comb    combs[2][4];
    Allpass allpasses[2][2];

    JUCE_DECLARE_NON_COPYABLE (SchroederReverb)
};
