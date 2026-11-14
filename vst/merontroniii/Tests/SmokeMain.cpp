//==============================================================================
// MeronTronIII headless smoke harness.
//
// Usage: MeronTronIIISmoke <path-to-MeronTronIII.sf2> [editor-png-out]
//
//   1. font loads, all five tape sets map
//   2. silence without notes
//   3. every preset x every key 55..89 sounds (incl. restored Flute 70 /
//      Strings 86), peaks within headroom
//   4. four-note chord headroom per preset
//   5. note-off release quiets the output
//   6. HALF SPEED drops the pitch an octave (FFT band check, Flute A3)
//   7. MIDI program change selects the tape set (panel order)
//   8. GUI note queue (the standalone / preview-keyboard route) sounds
//   9. editor constructs at 1120x660 and snapshots non-blank
//==============================================================================

#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace
{
    constexpr int kSampleRate = 48000;
    constexpr int kBlockSize = 512;
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("[%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
        if (! ok) ++failures;
    }

    std::unique_ptr<MeronTronIIIProcessor> makeProc()
    {
        auto proc = std::make_unique<MeronTronIIIProcessor>();
        proc->setRateAndBufferSizeDetails (kSampleRate, kBlockSize);
        proc->prepareToPlay (kSampleRate, kBlockSize);
        return proc;
    }

    bool waitReady (MeronTronIIIProcessor& proc, double timeoutSec = 60.0)
    {
        const auto deadline = juce::Time::currentTimeMillis() + (juce::int64) (timeoutSec * 1000.0);
        while (juce::Time::currentTimeMillis() < deadline)
        {
            if (proc.isFontReady())
                return true;
            if (proc.getFontStatus() == MeronTronIIIProcessor::FontStatus::notFound
                || proc.getFontStatus() == MeronTronIIIProcessor::FontStatus::error)
                return false;
            juce::Thread::sleep (50);
        }
        return false;
    }

    void setRaw (MeronTronIIIProcessor& proc, const char* id, float v)
    {
        if (auto* p = proc.apvts.getRawParameterValue (id))
            p->store (v);
    }
    float getRaw (MeronTronIIIProcessor& proc, const char* id)
    {
        auto* p = proc.apvts.getRawParameterValue (id);
        return p != nullptr ? p->load() : 0.0f;
    }

    using Injector = std::function<void (int, juce::MidiBuffer&)>;

    std::vector<float> runCapture (MeronTronIIIProcessor& proc, double seconds, Injector inject = nullptr)
    {
        const int totalBlocks = (int) std::ceil (seconds * kSampleRate / kBlockSize);
        std::vector<float> out;
        out.reserve ((size_t) totalBlocks * kBlockSize);
        juce::AudioBuffer<float> buffer (2, kBlockSize);
        for (int b = 0; b < totalBlocks; ++b)
        {
            buffer.clear();
            juce::MidiBuffer midi;
            if (inject != nullptr)
                inject (b, midi);
            proc.processBlock (buffer, midi);
            for (int i = 0; i < kBlockSize; ++i)
                out.push_back (buffer.getSample (0, i));
        }
        return out;
    }

    double windowRms (const std::vector<float>& d, double t0, double t1)
    {
        const int i0 = juce::jlimit (0, (int) d.size(), (int) (t0 * kSampleRate));
        const int i1 = juce::jlimit (i0, (int) d.size(), (int) (t1 * kSampleRate));
        if (i1 <= i0) return 0.0;
        double s = 0.0;
        for (int i = i0; i < i1; ++i) s += (double) d[(size_t) i] * d[(size_t) i];
        return std::sqrt (s / (i1 - i0));
    }

    double windowPeak (const std::vector<float>& d, double t0, double t1)
    {
        const int i0 = juce::jlimit (0, (int) d.size(), (int) (t0 * kSampleRate));
        const int i1 = juce::jlimit (i0, (int) d.size(), (int) (t1 * kSampleRate));
        double p = 0.0;
        for (int i = i0; i < i1; ++i) p = juce::jmax (p, (double) std::abs (d[(size_t) i]));
        return p;
    }

    double bandEnergy (const std::vector<float>& d, double t0, double f0, double f1)
    {
        constexpr int order = 14, size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> fd ((size_t) size * 2, 0.0f);
        const int start = (int) (t0 * kSampleRate);
        for (int i = 0; i < size; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (size - 1));
            fd[(size_t) i] = ((start + i) < (int) d.size() ? d[(size_t) (start + i)] : 0.0f) * w;
        }
        fft.performRealOnlyForwardTransform (fd.data());
        const double binHz = (double) kSampleRate / size;
        double e = 0.0;
        for (int k = (int) (f0 / binHz); k <= (int) (f1 / binHz) && 2 * k + 1 < (int) fd.size(); ++k)
            e += (double) fd[(size_t) (2 * k)] * fd[(size_t) (2 * k)] + (double) fd[(size_t) (2 * k + 1)] * fd[(size_t) (2 * k + 1)];
        return e;
    }
}

//==============================================================================
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc < 2)
    {
        std::printf ("usage: MeronTronIIISmoke <MeronTronIII.sf2> [editor.png]\n");
        return 2;
    }
    setenv ("MERONTRONIII_FONT", argv[1], 1);
    std::printf ("MeronTronIII smoke test (sr=%d, block=%d)\n", kSampleRate, kBlockSize);

    //-- 1. font load -------------------------------------------------------
    auto proc = makeProc();
    const bool ready = waitReady (*proc);
    std::printf ("       font status=%d msg=%s\n", (int) proc->getFontStatus(),
                 proc->getFontMessage().toRawUTF8());
    check (ready, "1a. font loads and becomes ready");
    bool allMapped = true;
    for (int ui = 0; ui < 5; ++ui)
        if (proc->fontPresetForUi (ui) < 0)
            allMapped = false;
    check (allMapped, "1b. all five tape sets map to font presets");

    //-- 2. silence -----------------------------------------------------------
    {
        auto out = runCapture (*proc, 0.5);
        check (windowRms (out, 0.0, 0.5) < 1.0e-4, "2. no note -> near-silence");
    }

    //-- 3. every preset x key ---------------------------------------------------
    double worstPeakSingle = 0.0;
    int quietCount = 0;
    juce::String quietList;
    for (int ui = 0; ui < 5; ++ui)
    {
        setRaw (*proc, "preset", (float) ui);
        for (int note = 55; note <= 89; ++note)
        {
            auto out = runCapture (*proc, 0.30, [note] (int b, juce::MidiBuffer& m)
            {
                if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 110), 0);
            });
            const double rms = windowRms (out, 0.08, 0.28);
            worstPeakSingle = juce::jmax (worstPeakSingle, windowPeak (out, 0.0, 0.30));
            if (rms < 0.002)
            {
                ++quietCount;
                quietList += " ui" + juce::String (ui) + "/n" + juce::String (note);
            }
            // release the note before the next capture
            runCapture (*proc, 0.05, [note] (int b, juce::MidiBuffer& m)
            {
                if (b == 0) m.addEvent (juce::MidiMessage::noteOff (1, note), 0);
            });
        }
    }
    std::printf ("       sweep: quiet=%d%s worstSinglePeak=%.3f\n", quietCount,
                 quietList.toRawUTF8(), worstPeakSingle);
    check (quietCount == 0, "3a. all 175 preset/key combinations sound (incl. Flute 70, Strings 86)");
    check (worstPeakSingle < 0.99, "3b. single-note peaks within headroom");

    //-- 4. chord headroom -------------------------------------------------------
    double worstChord = 0.0;
    for (int ui = 0; ui < 5; ++ui)
    {
        setRaw (*proc, "preset", (float) ui);
        auto out = runCapture (*proc, 1.2, [] (int b, juce::MidiBuffer& m)
        {
            if (b == 0)
                for (int n : { 55, 60, 65, 72 })
                    m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 110), 0);
        });
        worstChord = juce::jmax (worstChord, windowPeak (out, 0.0, 1.2));
        runCapture (*proc, 0.1, [] (int b, juce::MidiBuffer& m)
        {
            if (b == 0) m.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        });
    }
    std::printf ("       worst chord peak=%.3f\n", worstChord);
    check (worstChord < 0.99, "4. four-note chord peaks within headroom");

    //-- 5. release ------------------------------------------------------------------
    {
        setRaw (*proc, "preset", 1.0f);   // Flute
        setRaw (*proc, "reverb", 0.0f);
        auto out = runCapture (*proc, 3.0, [] (int b, juce::MidiBuffer& m)
        {
            if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110), 0);
            if (b == 47) m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);   // ~0.5 s
        });
        const double during = windowRms (out, 0.2, 0.45);
        const double after = windowRms (out, 2.4, 2.9);
        std::printf ("       release: rmsDuring=%.4f rmsAfter=%.6f\n", during, after);
        check (during > 0.01, "5a. note sounds while held");
        check (after < 1.0e-3, "5b. output quiet after note-off + release");
        setRaw (*proc, "reverb", 0.30f);
    }

    //-- 6. HALF SPEED ------------------------------------------------------------------
    {
        setRaw (*proc, "preset", 1.0f);   // Flute
        setRaw (*proc, "halfSpeed", 0.0f);
        auto normal = runCapture (*proc, 1.6, [] (int b, juce::MidiBuffer& m)
        {
            if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 110), 0);
        });
        runCapture (*proc, 0.1, [] (int b, juce::MidiBuffer& m)
        {
            if (b == 0) m.addEvent (juce::MidiMessage::noteOff (1, 69), 0);
        });
        setRaw (*proc, "halfSpeed", 1.0f);
        auto half = runCapture (*proc, 1.6, [] (int b, juce::MidiBuffer& m)
        {
            if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 110), 0);
        });
        runCapture (*proc, 0.1, [] (int b, juce::MidiBuffer& m)
        {
            if (b == 0) m.addEvent (juce::MidiMessage::noteOff (1, 69), 0);
        });
        setRaw (*proc, "halfSpeed", 0.0f);
        const double eN440 = bandEnergy (normal, 0.8, 400.0, 480.0);
        const double eN220 = bandEnergy (normal, 0.8, 190.0, 250.0);
        const double eH440 = bandEnergy (half, 0.8, 400.0, 480.0);
        const double eH220 = bandEnergy (half, 0.8, 190.0, 250.0);
        std::printf ("       half-speed: normal 440band=%.1f 220band=%.1f | half 440band=%.1f 220band=%.1f\n",
                     eN440, eN220, eH440, eH220);
        check (eN440 > eN220, "6a. normal speed: energy centred at 440 Hz band");
        check (eH220 > eH440, "6b. HALF SPEED: energy moves to the 220 Hz band");
    }

    //-- 7. program change ------------------------------------------------------------------
    {
        setRaw (*proc, "preset", 0.0f);
        runCapture (*proc, 0.2, [] (int b, juce::MidiBuffer& m)
        {
            if (b == 0) m.addEvent (juce::MidiMessage::programChange (1, 3), 0);
        });
        const float afterPc = getRaw (*proc, "preset");
        std::printf ("       program change 3 -> preset=%.0f\n", (double) afterPc);
        check (std::abs (afterPc - 3.0f) < 0.01f, "7. program change 3 selects Brass (panel order)");
        runCapture (*proc, 0.2, [] (int b, juce::MidiBuffer& m)
        {
            if (b == 0) m.addEvent (juce::MidiMessage::programChange (1, 0), 0);
        });
        check (std::abs (getRaw (*proc, "preset")) < 0.01f, "7b. program change 0 selects Strings");
    }

    //-- 8. GUI note queue ---------------------------------------------------------------------
    {
        setRaw (*proc, "preset", 2.0f);   // Choir
        proc->noteOnFromGui (64, 0.9f);
        auto out = runCapture (*proc, 0.8);
        const double rms = windowRms (out, 0.1, 0.7);
        proc->noteOffFromGui (64);
        runCapture (*proc, 0.8);
        std::printf ("       gui queue: rms=%.4f held=%d\n", rms, proc->getHeldNoteCount());
        check (rms > 0.01, "8. GUI-triggered note sounds (preview keyboard route)");
    }

    //-- 9. editor --------------------------------------------------------------------------------
    if (argc >= 3)
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
        check (ed != nullptr && ed->getWidth() == 1120 && ed->getHeight() == 660,
               "9a. editor constructs at 1120x660");
        if (ed != nullptr)
        {
            // let the load-state settle into the LCD, then snapshot
            juce::Thread::sleep (300);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 1.0f);
            double mean = 0.0, sq = 0.0;
            const int px = img.getWidth() * img.getHeight();
            for (int y = 0; y < img.getHeight(); y += 7)
                for (int x = 0; x < img.getWidth(); x += 7)
                {
                    const auto c = img.getPixelAt (x, y);
                    const double v = (c.getRed() + c.getGreen() + c.getBlue()) / 765.0;
                    mean += v; sq += v * v;
                }
            const int n = (img.getWidth() / 7 + 1) * (img.getHeight() / 7 + 1);
            mean /= n; sq = std::sqrt (sq / n - mean * mean);
            std::printf ("       editor snapshot: mean=%.3f std=%.3f (%d px)\n", mean, sq, px);
            check (sq > 0.05, "9b. editor snapshot is non-blank (layout renders)");
            juce::File outFile (argv[2]);
            outFile.deleteFile();
            if (auto stream = outFile.createOutputStream())
            {
                juce::PNGImageFormat png;
                png.writeImageToStream (img, *stream);
            }
        }
    }

    std::printf (failures == 0 ? "SMOKE OK (all checks passed)\n" : "SMOKE FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}


