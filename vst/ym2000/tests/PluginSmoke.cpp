// Headless smoke test of the whole plug-in (MIDI -> engine -> FX -> output).
#include "../Source/PluginProcessor.h"
#include <cstdio>

static float rms (const juce::AudioBuffer<float>& b) { return b.getRMSLevel (0, 0, b.getNumSamples()); }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    YM2000Processor p;
    const double sr = 48000; const int bs = 256;
    p.setPlayConfigDetails (0, 2, sr, bs);
    p.prepareToPlay (sr, bs);
    int fails = 0;
    auto check = [&] (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++fails; };

    auto render = [&] (int blocks, juce::MidiBuffer first, float* peakOut = nullptr)
    {
        juce::AudioBuffer<float> buf (2, bs); double acc = 0; float pk = 0;
        for (int i = 0; i < blocks; ++i)
        {
            buf.clear(); juce::MidiBuffer m; if (i == 0) m = first;
            p.processBlock (buf, m);
            acc += rms (buf); pk = std::max (pk, buf.getMagnitude (0, bs));
        }
        if (peakOut) *peakOut = pk;
        return (float) (acc / blocks);
    };
    auto on = [] (int n, int v) { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) v), 0); return m; };
    auto off = [] (int n) { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOff (1, n), 0); return m; };

    float pk = 0;
    check (render (20, {}) < 1e-6f, "silence without notes");
    float l = render (60, on (60, 100), &pk);
    check (l > 0.01f && pk < 1.5f, "note on produces sound");
    render (1, off (60));
    std::printf ("     rms after release, blocks 1-20: %.5f\n", render (20, {}));
    std::printf ("     rms after release, blocks 21-100: %.5f\n", render (80, {}));
    l = render (200, {});
    std::printf ("     rms after release, later 200 blocks: %.6f\n", l);
    check (l < 0.001f, "note off decays to silence (release + FX tails)");

    // factory programs and parameter automation
    for (int i = 0; i < p.getNumPrograms(); ++i)
    {
        p.setCurrentProgram (i);
        render (2, {});
        l = render (80, on (48, 110), &pk);
        std::printf ("     program %d %-10s rms=%.4f peak=%.3f\n", i, p.getProgramName (i).toRawUTF8(), l, pk);
        check (std::isfinite (l) && pk < 2.0f, ("program " + p.getProgramName (i) + " is finite and bounded").toRawUTF8());
        render (1, off (48)); render (600, {});
    }

    // arpeggiator: with arp on, sustained held notes should retrigger => amplitude modulation
    p.setCurrentProgram (4); // Arp Pluck
    render (2, {});
    juce::MidiBuffer chord; for (int n : { 48, 52, 55 }) chord.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
    juce::AudioBuffer<float> buf (2, bs); std::vector<float> env;
    for (int i = 0; i < 400; ++i) { buf.clear(); juce::MidiBuffer m; if (i == 0) m = chord; p.processBlock (buf, m); env.push_back (rms (buf)); }
    int peaks = 0; for (size_t i = 2; i + 1 < env.size(); ++i) if (env[i] > env[i - 1] * 1.5f && env[i] > 0.005f) ++peaks;
    std::printf ("     arp onsets detected: %d\n", peaks);
    check (peaks >= 8, "arpeggiator retriggers repeatedly");
    juce::MidiBuffer alloff; alloff.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    render (1, alloff); render (400, {});

    // state round trip
    juce::MemoryBlock mb; p.setCurrentProgram (2); p.getStateInformation (mb);
    p.setCurrentProgram (0); p.setStateInformation (mb.getData(), (int) mb.getSize());
    check (p.getCurrentProgram() == 2, "state save/restore keeps program index");
    check (std::abs (p.apvts.getRawParameterValue ("cut")->load() - 70.0f) < 0.6f, "state restore keeps Pad cutoff");

    // vocoder with audio input enabled
    p.setCurrentProgram (6);
    if (auto* inBus = p.getBus (true, 0)) inBus->enable (true);
    p.setPlayConfigDetails (2, 2, sr, bs); p.prepareToPlay (sr, bs);
    juce::AudioBuffer<float> vb (2, bs); double acc = 0; juce::Random rnd (1);
    for (int i = 0; i < 120; ++i)
    {
        juce::MidiBuffer m; if (i == 0) m.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 110), 0);
        for (int c = 0; c < 2; ++c) for (int s = 0; s < bs; ++s) vb.setSample (c, s, (rnd.nextFloat() - 0.5f) * 0.5f);
        p.processBlock (vb, m); if (i > 20) acc += rms (vb);
    }
    check (acc / 100 > 0.001, "vocoder with noise modulator produces output");
    std::printf ("%s (%d failures)\n", fails ? "SMOKE TEST FAILED" : "SMOKE TEST OK", fails);
    return fails ? 1 : 0;
}
