// Headless smoke test of the whole plug-in: MIDI -> engine -> output, presets, DAW clock, "no stuck sound" guards (DESIGN_RULES.md), state.
#include "../Source/PluginProcessor.h"
#include <cmath>
#include <cstdio>
#include <random>

static float rms (const juce::AudioBuffer<float>& b) { return b.getRMSLevel (0, 0, b.getNumSamples()); }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    YumyBacteriaProcessor p;
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
    auto allOff = [] { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::allNotesOff (1), 0); return m; };
    auto setP = [&] (const char* id, float real) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (real)); };

    float pk = 0;
    check (render (20, {}) < 1e-6f, "silence without notes");
    float l = render (60, on (60, 100), &pk);
    check (l > 0.01f && pk < 1.5f, "note on produces sound");
    render (1, off (60));
    l = render (400, {});
    check (l < 0.001f, "note off decays to silence");

    // ---- all factory programs: finite, bounded, audible
    for (int i = 0; i < p.getNumPrograms(); ++i)
    {
        p.setCurrentProgram (i);
        render (2, {});
        juce::MidiBuffer chord; for (int n : { 48, 55, 60 }) chord.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
        l = render (120, chord, &pk);
        const bool ok = std::isfinite (l) && std::isfinite (pk) && pk < 2.0f && l > 0.001f;
        if (! ok) std::printf ("     program %d %s rms=%.5f peak=%.3f\n", i, p.getProgramName (i).toRawUTF8(), l, pk);
        check (ok, ("program " + p.getProgramName (i) + " is finite, bounded and audible").toRawUTF8());
        render (1, allOff()); render (200, {});
    }
    p.setCurrentProgram (0);

    // ---- GUARD 1: DAW playing, HOST SYNC on, ARP on, no keys -> silence
    struct MockHead : juce::AudioPlayHead
    {
        double bpm = 120; bool playing = true;
        juce::Optional<PositionInfo> getPosition() const override { PositionInfo pi; pi.setBpm (bpm); pi.setIsPlaying (playing); pi.setPpqPosition (0); return pi; }
    } head;
    p.setPlayHead (&head);
    setP ("hsync", 1.0f); setP ("arp", 1.0f);
    check (render (200, {}) < 1e-6f, "GUARD DAW playing + HOST SYNC + ARP ON, no keys -> silence");
    // arpeggiator retriggers while keys are held
    {
        setP ("Amp_Decay", 40); setP ("Amp_Sustain", 20);
        juce::MidiBuffer chord; for (int n : { 48, 52, 55 }) chord.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
        juce::AudioBuffer<float> buf (2, bs); std::vector<float> env;
        for (int i = 0; i < 400; ++i) { buf.clear(); juce::MidiBuffer m; if (i == 0) m = chord; p.processBlock (buf, m); env.push_back (rms (buf)); }
        int peaks = 0; for (size_t i = 2; i + 1 < env.size(); ++i) if (env[i] > env[i - 1] * 1.5f && env[i] > 0.003f) ++peaks;
        std::printf ("     arp onsets detected: %d\n", peaks);
        check (peaks >= 8, "arpeggiator retriggers repeatedly");
    }
    // ---- GUARD 2/3: DAW stop and All Notes Off leave no sound
    head.playing = false;
    render (4, {});
    check (render (600, {}) < 0.0005f, "GUARD DAW stopped while host-synced arp is running -> silence");
    head.playing = true;
    render (1, allOff());
    check (render (600, {}) < 0.0005f, "GUARD All Notes Off -> silence");
    setP ("arp", 0.0f); setP ("hsync", 0.0f);
    p.setPlayHead (nullptr);

    // ---- GUARD 4: random MIDI / program / transport chaos always ends silent
    {
        std::mt19937 g (12345); p.setPlayHead (&head);
        for (int round = 0; round < 6; ++round)
        {
            setP ("hsync", (float) (g() & 1)); setP ("arp", (float) (g() & 1)); setP ("arpHold", (float) (g() & 1));
            p.setCurrentProgram ((int) (g() % (unsigned) p.getNumPrograms()));
            juce::AudioBuffer<float> buf (2, bs);
            for (int i = 0; i < 300; ++i)
            {
                juce::MidiBuffer m;
                const int k = (int) (g() % 8);
                if (k == 0) m.addEvent (juce::MidiMessage::noteOn (1, 36 + (int) (g() % 48), (juce::uint8) (30 + g() % 90)), (int) (g() % bs));
                else if (k == 1) m.addEvent (juce::MidiMessage::noteOff (1, 36 + (int) (g() % 48)), (int) (g() % bs));
                else if (k == 2) m.addEvent (juce::MidiMessage::controllerEvent (1, 64, (int) (g() % 128)), (int) (g() % bs));
                else if (k == 3) m.addEvent (juce::MidiMessage::pitchWheel (1, (int) (g() % 16384)), (int) (g() % bs));
                if ((g() % 40) == 0) head.playing = ! head.playing;
                buf.clear(); p.processBlock (buf, m);
                if (! std::isfinite (rms (buf)) || buf.getMagnitude (0, bs) > 3.0f) { check (false, "chaos: non-finite or huge output"); i = 1000; }
            }
            head.playing = true;
            render (1, allOff()); render (1, {});
            juce::MidiBuffer pedalUp; pedalUp.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0); render (1, pedalUp);
            render (1, allOff());
            setP ("arp", 0.0f); setP ("arpHold", 0.0f);
            const float tail = render (1500, {});
            char msg[96]; std::snprintf (msg, sizeof msg, "GUARD chaos round %d ends silent (tail rms %.6f)", round, tail);
            check (tail < 0.001f, msg);
        }
        p.setPlayHead (nullptr); setP ("hsync", 0.0f);
    }

    // ---- state round trip
    {
        p.setCurrentProgram (8);
        setP ("Cutoff", 33); setP ("Unison", 4);
        juce::MemoryBlock mb; p.getStateInformation (mb);
        p.setCurrentProgram (3); setP ("Cutoff", 100);
        p.setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::lround (p.apvts.getRawParameterValue ("Cutoff")->load()) == 33 && p.getCurrentProgram() == 8, "state save / restore");
    }

    std::printf ("%s (%d failures)\n", fails ? "SOME TESTS FAILED" : "ALL OK", fails);
    return fails ? 1 : 0;
}
