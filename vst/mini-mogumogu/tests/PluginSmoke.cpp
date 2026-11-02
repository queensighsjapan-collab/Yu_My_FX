// Headless smoke test of the whole plug-in: parameters, MIDI, internal clock, DAW clock, presets, reverb, state.
#include "../Source/PluginProcessor.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int fails = 0;
static void check (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++fails; }

struct MockHead : juce::AudioPlayHead
{
    double bpm = 120, ppq = 0; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo pi; pi.setBpm (bpm); pi.setIsPlaying (playing); pi.setPpqPosition (ppq); return pi;
    }
};

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000; const int bs = 256;
    auto make = [&] () { auto p = std::make_unique<MoguProcessor>(); p->setPlayConfigDetails (0, 2, sr, bs); p->prepareToPlay (sr, bs); return p; };
    auto pp = make(); auto& p = *pp;
    juce::AudioBuffer<float> buf (2, bs);
    long long onsets = 0;
    auto render = [&] (int blocks, MockHead* head = nullptr, juce::MidiBuffer* inMidi = nullptr, float* peak = nullptr)
    {
        p.setPlayHead (head);
        double acc = 0; float pk = 0; bool finite = true;
        for (int i = 0; i < blocks; ++i)
        {
            juce::MidiBuffer m; if (inMidi && i == 0) m = *inMidi;
            buf.clear(); p.processBlock (buf, m);
            acc += buf.getRMSLevel (0, 0, bs); pk = std::max (pk, buf.getMagnitude (0, bs));
            for (int k = 0; k < bs; ++k) if (! std::isfinite (buf.getSample (0, k)) || std::abs (buf.getSample (0, k)) > 4.f) finite = false;
            if (head && head->playing) head->ppq += bs * head->bpm / 60.0 / sr;
        }
        if (peak) *peak = pk;
        if (! finite) { std::printf ("     NON-FINITE OR HUGE OUTPUT\n"); ++fails; }
        return (float) (acc / blocks);
    };
    (void) onsets;

    if (argc > 1 && std::string (argv[1]) == "rv")      // level of the reverb vs the browser version (see README): prints RMS of sustain and tails
    {
        p.setValue ("rvMix", 1.f); p.setValue ("aD", 0.2f); p.setValue ("aS", 0.7f); p.setValue ("hostSync", 0.f);
        const int total = (int) (3.0 * sr / bs); std::vector<float> o;
        for (int b = 0; b < total; ++b)
        {
            juce::MidiBuffer m; const int t = b * bs;
            if (t <= (int) (0.05 * sr) && (int) (0.05 * sr) < t + bs) m.addEvent (juce::MidiMessage::noteOn (1, 48, 0.8f), (int) (0.05 * sr) - t);
            if (t <= (int) (1.0 * sr) && (int) (1.0 * sr) < t + bs) m.addEvent (juce::MidiMessage::noteOff (1, 48), (int) (1.0 * sr) - t);
            buf.clear(); p.processBlock (buf, m); for (int k = 0; k < bs; ++k) o.push_back (buf.getSample (0, k));
        }
        auto rms = [&] (double a, double b) { double s = 0; int n = 0; for (int i = (int) (a * sr); i < (int) (b * sr); ++i, ++n) s += o[(size_t) i] * o[(size_t) i]; return std::sqrt (s / n); };
        std::printf ("sustain %.4f  tail1.2-2.0 %.4f  tail2-3 %.4f\n", rms (0.3, 1.0), rms (1.2, 2.0), rms (2.0, 3.0));
        return 0;
    }

    float pk = 0;
    check (p.getParameters().size() > 100, "parameters exist (sound + sequencer + 48 step params)");
    check (render (40) < 1e-6f, "silent while idle");

    // ---- MIDI note
    { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 48, 0.8f), 10);
      const float r = render (60, nullptr, &m, &pk); check (r > 0.01f && pk < 1.5f, "MIDI note on makes sound within range"); }
    { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOff (1, 48), 0);
      render (1, nullptr, &m); render (150); const float tail = render (100); check (tail < 1e-3f, "MIDI note off decays to silence"); }

    // ---- internal clock
    p.setValue ("hostSync", 0.f); p.setValue ("run", 1.f);
    check (render (200, nullptr, nullptr, &pk) > 0.005f, "RUN with internal clock plays the pattern");
    check (p.currentStep() >= 0 && p.seqRunning(), "current step is reported while running");
    p.setValue ("run", 0.f); render (400);
    check (render (40) < 1e-3f, "STOP silences the sequencer");

    // ---- all steps off -> silent
    for (int i = 0; i < 16; ++i) { char b[16]; std::snprintf (b, sizeof b, "s%02don", i + 1); p.setValue (b, 0.f); }
    p.setValue ("run", 1.f);
    check (render (200) < 1e-3f, "all steps off stays silent");
    p.resetSteps(); p.setValue ("run", 0.f); render (300);

    // ---- DAW clock
    p.setValue ("hostSync", 1.f);
    MockHead head; head.bpm = 120; head.ppq = 0; head.playing = false;
    check (render (60, &head) < 1e-3f, "host stopped -> silent");
    head.playing = true;
    check (render (200, &head, nullptr, &pk) > 0.005f && p.hostDriving(), "host playing -> pattern plays from the DAW clock");
    head.playing = false; render (400, &head);
    check (render (40, &head) < 1e-3f, "host stop silences");

    // ---- KEY -> TRANS
    p.setValue ("hostSync", 0.f); p.setValue ("run", 1.f); render (20);
    { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 55, 0.8f), 0); render (2, nullptr, &m); }
    check (p.transpose() == 7, "KEY>TRANS: key G sets transpose +7 while running");
    p.setValue ("run", 0.f); render (400);

    // ---- all factory presets
    bool allOk = true;
    for (int i = 0; i < p.getNumPrograms(); ++i)
    {
        p.setCurrentProgram (i);
        juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 40, 0.9f), 0);
        const float r = render (80, nullptr, &m, &pk);
        juce::MidiBuffer o; o.addEvent (juce::MidiMessage::allNotesOff (1), 0); render (1, nullptr, &o);
        render (200);
        if (! (r > 0.001f) || pk > 2.f) { allOk = false; std::printf ("     preset %d '%s': rms %.4f peak %.3f\n", i, p.getProgramName (i).toRawUTF8(), r, pk); }
    }
    check (allOk, "every factory preset sounds and stays in range");

    // ---- reverb tail
    p.setCurrentProgram (0); p.setValue ("rvMix", 1.f); p.setValue ("aD", 0.05f); pump (400);
    { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 48, 0.9f), 0); render (30, nullptr, &m);
      juce::MidiBuffer o; o.addEvent (juce::MidiMessage::noteOff (1, 48), 0); render (1, nullptr, &o);
      render (40); const float tail = render (60); check (tail > 1e-4f, "reverb tail rings after the note ends"); }
    p.setValue ("rvMix", 0.f); render (800);

    // ---- state round trip
    p.setValue ("cutoff", 777.f); p.setValue ("o2wave", 4.f); p.setValue ("s05note", 60.f); p.setValue ("s03tie", 1.f);
    juce::MemoryBlock mb; p.getStateInformation (mb);
    auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
    auto gp = [] (MoguProcessor& m, const char* id) { auto* a = m.apvts.getParameter (id); return a->convertFrom0to1 (a->getValue()); };
    check (std::abs (gp (*q, "cutoff") - 777.f) < 1.f && (int) std::lround (gp (*q, "o2wave")) == 4 && (int) std::lround (gp (*q, "s05note")) == 60 && gp (*q, "s03tie") > 0.5f,
           "state (parameters + step pattern) survives save/load");

    // ---- stress: random parameter changes while playing must not blow up
    { juce::Random r (5); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 36, 0.9f), 0);
      p.setValue ("run", 1.f); bool ok = true;
      for (int i = 0; i < 300; ++i)
      {
          const auto* s = &mm::kSpecs[r.nextInt (mm::kNumSpecs)];
          if (! std::strcmp (s->id, "vol") || ! std::strcmp (s->id, "hostSync") || ! std::strcmp (s->id, "run")) continue;
          p.setValue (s->id, s->mn + (s->mx - s->mn) * r.nextFloat());
          render (3, nullptr, i == 0 ? &m : nullptr, &pk); if (pk > 4.f) ok = false;
      }
      check (ok, "random parameter changes stay bounded"); }

    std::printf (fails ? "SMOKE TEST FAILED\n" : "SMOKE TEST OK\n");
    return fails ? 1 : 0;
}
