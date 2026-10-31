// Headless smoke test of the whole plug-in: parameters, internal clock, DAW clock, edit view, state.
#include "../Source/PluginProcessor.h"
#include <cmath>
#include <cstdio>

static int fails = 0;
static void check (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++fails; }

struct MockHead : juce::AudioPlayHead
{
    double bpm = 120, ppq = 0; bool playing = true, hasPos = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        if (! hasPos) return {};
        PositionInfo pi; pi.setBpm (bpm); pi.setIsPlaying (playing); pi.setPpqPosition (ppq); return pi;
    }
};

static void setParam (YM303Processor& p, const char* id, float real)
{
    auto* q = p.apvts.getParameter (id);
    q->setValueNotifyingHost (q->convertTo0to1 (real));
}
static float getParam (YM303Processor& p, const char* id)
{
    auto* q = p.apvts.getParameter (id);
    return q->convertFrom0to1 (q->getValue());
}
static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000; const int bs = 256;
    auto make = [&] () { auto p = std::make_unique<YM303Processor>(); p->setPlayConfigDetails (0, 2, sr, bs); p->prepareToPlay (sr, bs); return p; };
    auto pp = make(); auto& p = *pp;
    juce::AudioBuffer<float> buf (2, bs);
    juce::MidiBuffer midi;
    auto render = [&] (int blocks, MockHead* head = nullptr, float* peak = nullptr, bool advance = true)
    {
        p.setPlayHead (head);
        double acc = 0; float pk = 0; bool finite = true;
        for (int i = 0; i < blocks; ++i)
        {
            buf.clear(); p.processBlock (buf, midi);
            acc += buf.getRMSLevel (0, 0, bs); pk = std::max (pk, buf.getMagnitude (0, bs));
            for (int k = 0; k < bs; ++k) if (! std::isfinite (buf.getSample (0, k))) finite = false;
            if (head && advance && head->playing) head->ppq += bs * head->bpm / 60.0 / sr;
        }
        if (peak) *peak = pk;
        if (! finite) std::printf ("     NON-FINITE OUTPUT\n");
        return (float) (acc / blocks);
    };

    float pk = 0;
    check (p.getNumParameters() > 60, "parameters exist (sound, sequencer, 16 steps)");
    check (render (40) < 1e-6f, "silent while not running");

    // ---- internal clock (RUN + TEMPO), no host position
    setParam (p, "run", 1);
    float l = render (200, nullptr, &pk);
    std::printf ("     rms %.4f peak %.3f\n", l, pk);
    check (l > 0.02f && pk < 1.0f, "RUN plays the pattern");
    setParam (p, "run", 0);
    render (20);
    check (render (40) < 1e-4f, "stop falls silent");

    // ---- DAW clock
    MockHead head;
    head.playing = false; head.ppq = 0;
    check (render (40, &head) < 1e-6f, "DAW stopped: silent (HOST SYNC on)");
    setParam (p, "run", 1);
    check (render (40, &head) < 1e-6f, "DAW stopped: RUN is ignored while HOST SYNC is on");
    head.playing = true; head.ppq = 0;
    l = render (200, &head, &pk);
    check (l > 0.02f && pk < 1.0f, "DAW playing: sequence plays");
    head.playing = false; render (20, &head);
    check (render (40, &head) < 1e-4f, "DAW stop: falls silent");
    setParam (p, "hostsync", 0);
    head.playing = false;
    l = render (200, &head);
    check (l > 0.02f, "HOST SYNC off: RUN + TEMPO play even if the DAW is stopped");
    setParam (p, "run", 0); setParam (p, "hostsync", 1); render (30);

    // ---- sample-accurate start: the first note starts on the first sample when the DAW starts at ppq 0
    {
        auto q = make();
        MockHead h; h.ppq = 0; q->setPlayHead (&h);
        juce::AudioBuffer<float> b (2, bs); b.clear(); juce::MidiBuffer m; q->processBlock (b, m);
        int first = -1; for (int i = 0; i < bs; ++i) if (std::fabs (b.getSample (0, i)) > 1e-4f) { first = i; break; }
        std::printf ("     first audible sample %d\n", first);
        check (first >= 0 && first < 20, "note starts at the first sample of the DAW start");
    }

    // ---- edit view: parameters <-> pattern data
    p.setPlayHead (nullptr);
    check ((int) getParam (p, "s01_type") == 2 && (int) getParam (p, "s03_oct") == 2 && getParam (p, "s03_sld") > 0.5f, "step parameters show pattern I-1 A");
    setParam (p, "s01_type", 0);
    check (p.bank.getStep (0, 0, 0).type == ym303::Rest, "editing a step parameter writes into the pattern");
    setParam (p, "s02_type", 2 + 7);   // G
    setParam (p, "s02_oct", 0);        // down
    setParam (p, "s02_acc", 1);
    {
        const auto s = p.bank.getStep (0, 0, 1);
        check (s.type == ym303::Note && s.note == 7 && s.dn && ! s.up && s.acc, "note / octave / accent write-through");
    }
    setParam (p, "len", 12);
    check (p.bank.getLen (0, 0) == 12, "LENGTH writes into the pattern");
    setParam (p, "pattern", 2); pump (120);
    check ((int) getParam (p, "len") == 16 && (int) getParam (p, "s01_type") == 2 && getParam (p, "s01_acc") > 0.5f, "selecting pattern I-2 loads its steps into the parameters");
    check (p.bank.getLen (0, 0) == 12 && p.bank.getStep (0, 0, 1).note == 7, "pattern I-1 keeps the edits");
    setParam (p, "section", 1); pump (120);
    check ((int) getParam (p, "s02_type") == 2 + 0 && (int) getParam (p, "s04_type") == 2 + 0, "selecting section B shows section B of I-2");
    setParam (p, "section", 0); setParam (p, "pattern", 1); pump (120);
    check ((int) getParam (p, "len") == 12 && (int) getParam (p, "s02_oct") == 0, "going back shows the edited pattern");

    // ---- track mode
    setParam (p, "playmode", 1); setParam (p, "run", 1); setParam (p, "hostsync", 0);
    l = render (300, nullptr, &pk);
    check (l > 0.02f && pk < 1.0f, "TRACK mode plays");
    setParam (p, "run", 0); render (30);

    // ---- stress: extreme settings stay finite and bounded
    for (auto* id : { "reso", "envmod", "accent", "volume", "cutoff", "decay" }) setParam (p, id, 10);
    setParam (p, "wave", 1); setParam (p, "tempo", 200); setParam (p, "playmode", 0); setParam (p, "run", 1);
    l = render (800, nullptr, &pk);
    std::printf ("     extreme: rms %.3f peak %.3f\n", l, pk);
    check (std::isfinite (l) && pk < 1.0f, "all knobs at maximum: finite and below 0 dBFS");
    setParam (p, "run", 0); render (30);

    // ---- state round trip
    {
        setParam (p, "cutoff", 4.4f); setParam (p, "wave", 1); setParam (p, "group", 2); setParam (p, "pattern", 3); pump (120);
        setParam (p, "s05_type", 2 + 11); setParam (p, "s05_acc", 1); setParam (p, "len", 9);
        const int tr[] = { 5, 6, 7, 31 }; p.bank.setTrack (tr, 4);
        juce::MemoryBlock mb; p.getStateInformation (mb);
        auto q = make();
        q->setStateInformation (mb.getData(), (int) mb.getSize()); pump (120);
        check (q->bank.serialize() == p.bank.serialize(), "state: all patterns and the track restore");
        check (std::fabs (getParam (*q, "cutoff") - 4.4f) < 0.01f && (int) getParam (*q, "wave") == 1, "state: knobs restore");
        check ((int) getParam (*q, "group") == 2 && (int) getParam (*q, "pattern") == 3, "state: selection restores");
        check ((int) getParam (*q, "s05_type") == 2 + 11 && getParam (*q, "s05_acc") > 0.5f && (int) getParam (*q, "len") == 9, "state: edit view shows the restored pattern");
        juce::MemoryBlock junk; junk.append ("garbage", 7);
        q->setStateInformation (junk.getData(), (int) junk.getSize());
        check (q->bank.serialize() == p.bank.serialize(), "state: garbage input is ignored");
    }
    // ---- bank (de)serialisation
    {
        ym303::Bank b; auto s = b.serialize();
        ym303::Bank c; c.clearAll();
        check (c.deserialize (s) && c.serialize() == s, "bank serialisation round trip");
        check (! c.deserialize (s.substr (0, s.size() - 3)) && ! c.deserialize ("zz"), "bank rejects malformed data");
    }
    std::printf (fails ? "%d FAILED\n" : "all plug-in tests passed\n", fails);
    return fails ? 2 : 0;
}
