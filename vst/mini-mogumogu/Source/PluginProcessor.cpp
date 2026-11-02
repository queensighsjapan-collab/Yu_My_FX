#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cstdio>

using namespace juce;

namespace
{
String stepId (int i, const char* what) { char b[32]; std::snprintf (b, sizeof b, "s%02d%s", i + 1, what); return b; }
}

AudioProcessorValueTreeState::ParameterLayout MoguProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    for (int i = 0; i < mm::kNumSpecs; ++i)
    {
        const auto& s = mm::kSpecs[i];
        if (s.opts != nullptr)
        {
            StringArray a; a.addTokens (s.opts, "|", "");
            v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { s.id, 1 }, s.name, a, (int) s.def));
        }
        else
        {
            NormalisableRange<float> r (s.mn, s.mx);
            if (s.log)
            {
                const float mn = s.mn, mx = s.mx;
                r = NormalisableRange<float> (mn, mx,
                    [mn, mx] (float, float, float n) { return mn * std::pow (mx / mn, n); },
                    [mn, mx] (float, float, float x) { return std::log (x / mn) / std::log (mx / mn); },
                    [mn, mx] (float, float, float x) { return jlimit (mn, mx, x); });
            }
            else if (s.step > 0) r = NormalisableRange<float> (s.mn, s.mx, s.step);
            v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { s.id, 1 }, s.name, r, s.def));
        }
    }
    for (int i = 0; i < 16; ++i)
    {
        const String n = "Step " + String (i + 1).paddedLeft ('0', 2);
        v.push_back (std::make_unique<AudioParameterBool> (ParameterID { stepId (i, "on"), 1 }, n + " On", mm::kDefOn[i] != 0));
        v.push_back (std::make_unique<AudioParameterInt> (ParameterID { stepId (i, "note"), 1 }, n + " Note", 24, 72, mm::kDefNote[i]));
        v.push_back (std::make_unique<AudioParameterBool> (ParameterID { stepId (i, "tie"), 1 }, n + " Tie", i == 6));
    }
    return { std::make_move_iterator (v.begin()), std::make_move_iterator (v.end()) };
}

MoguProcessor::MoguProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MiniMogumogu", createLayout())
{
    for (int i = 0; i < mm::kNumSpecs; ++i) raw[(size_t) i] = apvts.getRawParameterValue (mm::kSpecs[i].id);
    for (int i = 0; i < 16; ++i)
    {
        stOn[i] = apvts.getRawParameterValue (stepId (i, "on"));
        stNote[i] = apvts.getRawParameterValue (stepId (i, "note"));
        stTie[i] = apvts.getRawParameterValue (stepId (i, "tie"));
    }
    apvts.addParameterListener ("rvSize", this);
    evs.reserve (8192);
    held.reserve (32);
}

MoguProcessor::~MoguProcessor()
{
    stopTimer();
    apvts.removeParameterListener ("rvSize", this);
}

bool MoguProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return (o == AudioChannelSet::stereo() || o == AudioChannelSet::mono()) && l.getMainInputChannelSet().isDisabled();
}

float MoguProcessor::value (const char* id) const { return R (id).load(); }

void MoguProcessor::setValue (const char* id, float real)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (real));
}

void MoguProcessor::parameterChanged (const String& id, float)
{
    if (id == "rvSize") { irDirty.store (true); MessageManager::callAsync ([this] { startTimer (180); }); }
}

void MoguProcessor::timerCallback()
{
    stopTimer();
    if (irDirty.exchange (false)) buildIR();
}

void MoguProcessor::buildIR()
{
    // same impulse response recipe as the browser version (decaying low-passed noise) with the convolver's normalisation applied
    const float size = R ("rvSize").load();
    irBuiltSize = size;
    const int len = (int) std::floor (sampleRate * (0.4 + size * 3.6));
    AudioBuffer<float> ir (2, len);
    Random rnd (0x6d6f6775 + (int) (size * 1000));
    double sumSq = 0;
    for (int ch = 0; ch < 2; ++ch)
    {
        float* d = ir.getWritePointer (ch);
        float lp = 0;
        for (int i = 0; i < len; ++i)
        {
            const float t = (float) i / (float) len;
            const float w = (rnd.nextFloat() * 2.f - 1.f) * std::pow (1.f - t, 2.6f);
            const float a = 0.15f + 0.8f * (1.f - t);
            lp += a * (w - lp);
            d[i] = lp;
            sumSq += (double) lp * lp;
        }
    }
    double power = std::sqrt (sumSq / (2.0 * len));
    if (! std::isfinite (power) || power < 0.000125) power = 0.000125;
    const float scale = (float) ((1.0 / power) * std::pow (10.0, -58.0 * 0.05) * (44100.0 / sampleRate));
    ir.applyGain (scale);
    rvLen = len;
    conv.loadImpulseResponse (std::move (ir), sampleRate, dsp::Convolution::Stereo::yes, dsp::Convolution::Trim::no, dsp::Convolution::Normalise::no);
}

void MoguProcessor::prepareToPlay (double sr, int block)
{
    sampleRate = sr;
    engine.prepare (sr);
    const int maxB = std::max (block, 512);
    dsp::ProcessSpec spec { sr, (uint32) maxB, 2 };
    conv.prepare (spec);
    buildIR();
    wet.setSize (2, maxB); scratch.setSize (2, maxB);
    rvTail = 0; rvWasOn = false; rvGain = R ("rvMix").load() * 1.1f;
    held.clear(); curNote = -1; seqWasOn = false; prevTie = false; pendingOff = -1; curStep = -1;
    absPos = 0;
}

void MoguProcessor::setCurrentProgram (int i)
{
    if (i < 0 || i >= mm::kNumPresets) return;
    program = i;
    const auto& pr = mm::kPresets[i];
    for (int k = 0; k < mm::kNumSpecs; ++k)
    {
        const auto& s = mm::kSpecs[k];
        if (! std::strcmp (s.id, "vol") || ! std::strcmp (s.id, "run") || ! std::strcmp (s.id, "hostSync")) continue;
        float v = s.def;
        for (int j = 0; j < pr.n; ++j) if (! std::strcmp (pr.kv[j].k, s.id)) v = pr.kv[j].v;
        setValue (s.id, v);
    }
}

void MoguProcessor::randomizeSteps()
{
    static const int scale[7] = { 0, 0, 3, 5, 7, 10, 12 };
    Random r;
    for (int i = 0; i < 16; ++i)
    {
        auto set = [this] (const String& id, float real) { if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (real)); };
        set (stepId (i, "on"), r.nextFloat() < 0.78f ? 1.f : 0.f);
        set (stepId (i, "note"), (float) (36 + scale[r.nextInt (7)] + (r.nextFloat() < 0.2f ? 12 : 0)));
        set (stepId (i, "tie"), r.nextFloat() < 0.12f ? 1.f : 0.f);
    }
}

void MoguProcessor::resetSteps()
{
    for (int i = 0; i < 16; ++i)
    {
        auto set = [this] (const String& id, float real) { if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (real)); };
        set (stepId (i, "on"), (float) mm::kDefOn[i]);
        set (stepId (i, "note"), (float) mm::kDefNote[i]);
        set (stepId (i, "tie"), i == 6 ? 1.f : 0.f);
    }
}

void MoguProcessor::fillParams (mm::Params& p, double bpm) const
{
    auto F = [this] (const char* id) { return R (id).load(); };
    auto I = [this] (const char* id) { return (int) (R (id).load() + 0.5f); };
    p.vol = F ("vol"); p.tune = F ("tune"); p.glide = F ("glide"); p.multi = I ("multi"); p.bendRange = I ("bendRange");
    p.range[0] = I ("o1range"); p.range[1] = I ("o2range"); p.range[2] = I ("o3range");
    p.wave[0] = I ("o1wave"); p.wave[1] = I ("o2wave"); p.wave[2] = I ("o3wave");
    p.otune[1] = F ("o2tune"); p.otune[2] = F ("o3tune"); p.o3kbd = I ("o3kbd");
    p.m1 = F ("m1"); p.m2 = F ("m2"); p.m3 = F ("m3"); p.mN = F ("mN"); p.noiseType = I ("noiseType");
    p.cutoff = F ("cutoff"); p.emph = F ("emph"); p.contour = F ("contour"); p.fdrive = F ("fdrive"); p.kbd = I ("kbd");
    p.fA = F ("fA"); p.fD = F ("fD"); p.fS = F ("fS"); p.aA = F ("aA"); p.aD = F ("aD"); p.aS = F ("aS"); p.rel = I ("rel");
    p.modSrc = I ("modSrc"); p.modRate = F ("modRate"); p.modPitch = F ("modPitch"); p.modFilt = F ("modFilt");
    p.drive = F ("drive"); p.chRate = F ("chRate"); p.chDepth = F ("chDepth"); p.chMix = F ("chMix");
    p.dlTime = F ("dlTime"); p.dlFb = F ("dlFb"); p.dlTone = F ("dlTone"); p.dlMix = F ("dlMix"); p.dlSync = I ("dlSync");
    p.rvSize = F ("rvSize"); p.rvMix = F ("rvMix");
    p.bpm = bpm;
}

int MoguProcessor::seqPick (long long n, int len)
{
    const int dir = (int) (R ("sqDir").load() + 0.5f);
    if (len < 1) return 0;
    if (dir == 0) return (int) (n % len);
    if (dir == 1) return len - 1 - (int) (n % len);
    if (dir == 2)
    {
        if (len < 2) return 0;
        const int cyc = 2 * len - 2, m = (int) (n % cyc);
        return m < len ? m : cyc - m;
    }
    uint32_t h = (uint32_t) (n * 2654435761ull) ^ 0x9e3779b9u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    return (int) (h % (uint32_t) len);
}

// one sequencer step starting at sample `pos` of the block
void MoguProcessor::seqFire (int pos, int idx, long long startAbs, double durSamples)
{
    curStep.store (idx);
    const bool multi = R ("multi").load() > 0.5f;
    if (stOn[idx]->load() > 0.5f)
    {
        const int note = (int) (stNote[idx]->load() + 0.5f) + trans.load();
        evs.push_back ({ pos, 5, note, (! prevTie || multi) ? 1.f : 0.f });
        if (stTie[idx]->load() < 0.5f)
        {
            const long long off = startAbs + (long long) std::llround (durSamples * R ("sqGate").load());
            if (off < absPos + curBlockN) evs.push_back ({ (int) std::max<long long> (pos, off - absPos), 6, 0, 0.f });
            else pendingOff = off;
            prevTie = false;
        }
        else prevTie = true;
    }
    else
    {
        if (prevTie) evs.push_back ({ pos, 6, 0, 0.f });
        prevTie = false;
    }
}

void MoguProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), nch = buffer.getNumChannels();
    if (nch == 0 || n == 0) return;
    curBlockN = n;
    if (wet.getNumSamples() < n) { wet.setSize (2, n, false, false, true); scratch.setSize (2, n, false, false, true); }

    // ---- clock source
    bool hostOk = false, hostPlaying = false; double ppq = 0, bpm = R ("sqBpm").load();
    if (R ("hostSync").load() > 0.5f)
        if (auto* ph = getPlayHead())
            if (auto pi = ph->getPosition())
                if (auto q = pi->getPpqPosition())
                    if (auto b = pi->getBpm())
                        if (*b >= 1.0) { hostOk = true; ppq = *q; bpm = *b; hostPlaying = pi->getIsPlaying(); }
    hostDriven.store (hostOk);
    const bool seqOn = hostOk ? hostPlaying : R ("run").load() > 0.5f;
    running.store (seqOn);

    mm::Params prm;
    fillParams (prm, bpm);
    engine.setParams (prm);

    // ---- events
    evs.clear();
    keyState.processNextMidiBuffer (midi, 0, n, true);
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage(); const int pos = jlimit (0, n - 1, meta.samplePosition);
        if (m.isNoteOn()) evs.push_back ({ pos, 0, m.getNoteNumber(), m.getFloatVelocity() });
        else if (m.isNoteOff()) evs.push_back ({ pos, 1, m.getNoteNumber(), 0.f });
        else if (m.isPitchWheel()) evs.push_back ({ pos, 2, 0, (m.getPitchWheelValue() - 8192) / 8192.f });
        else if (m.isController())
        {
            const int cc = m.getControllerNumber(); const float x = m.getControllerValue() / 127.f;
            if (cc == 1) evs.push_back ({ pos, 3, 0, x });
            else if (cc == 120 || cc == 123) evs.push_back ({ pos, 4, 0, 0.f });
            else
            {
                const char* id = cc == 5 ? "glide" : cc == 7 ? "vol" : cc == 71 ? "emph" : cc == 72 ? "aD" : cc == 73 ? "aA"
                               : cc == 74 ? "cutoff" : cc == 75 ? "contour" : cc == 91 ? "rvMix" : cc == 93 ? "chMix" : nullptr;
                if (id) if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (x);
            }
        }
    }
    midi.clear();
    if (uiBendDirty.exchange (false)) evs.push_back ({ 0, 2, 0, uiBend.load() });
    if (uiWheelDirty.exchange (false)) evs.push_back ({ 0, 3, 0, uiWheel.load() });
    if (panicReq.exchange (false)) evs.push_back ({ 0, 4, 0, 0.f });

    // ---- sequencer
    const int len = jlimit (1, 16, (int) (R ("sqLen").load() + 0.5f));
    const double stepBeats = 1.0 / std::array<double, 3> { 2.0, 4.0, 8.0 }[(size_t) jlimit (0, 2, (int) (R ("sqRate").load() + 0.5f))];
    const double durSamples = stepBeats * 60.0 / bpm * sampleRate;
    const double swing = R ("sqSwing").load();
    if (seqOn && ! seqWasOn)
    {
        evs.push_back ({ 0, 4, 0, 0.f });                    // like the browser version: start from silence
        prevTie = false; pendingOff = -1; seqPP = 1; seqCount = 0;
        seqIdx = (int) (R ("sqDir").load() + 0.5f) == 1 ? len - 1 : 0;
        seqNext = (double) absPos;
    }
    if (! seqOn && seqWasOn)
    {
        evs.push_back ({ 0, 6, 0, 0.f });
        prevTie = false; pendingOff = -1; curStep.store (-1);
    }
    seqWasOn = seqOn;
    if (seqOn)
    {
        if (pendingOff >= 0 && pendingOff < absPos + n) { evs.push_back ({ (int) std::max<long long> (0, pendingOff - absPos), 6, 0, 0.f }); pendingOff = -1; }
        if (hostOk)
        {
            const double perSample = bpm / 60.0 / sampleRate, ppq1 = ppq + perSample * n;
            for (long long k = (long long) std::floor (ppq / stepBeats) - 1; k <= (long long) std::floor (ppq1 / stepBeats); ++k)
            {
                if (k < 0) continue;
                const double start = (double) k * stepBeats + ((k & 1) ? swing * stepBeats * 0.5 : 0.0);
                if (start < ppq || start >= ppq1) continue;
                const int pos = jlimit (0, n - 1, (int) ((start - ppq) / perSample));
                seqFire (pos, seqPick (k, len), absPos + pos, durSamples);
            }
        }
        else
        {
            while (seqNext < (double) (absPos + n))
            {
                const double t = seqNext + ((seqCount & 1) ? swing * durSamples * 0.5 : 0.0);
                const int pos = jlimit (0, n - 1, (int) (t - (double) absPos));
                seqFire (pos, std::min (seqIdx, len - 1), (long long) t, durSamples);
                seqNext += durSamples; ++seqCount;
                const int dir = (int) (R ("sqDir").load() + 0.5f);
                if (dir == 0) seqIdx = (seqIdx + 1) % len;
                else if (dir == 1) seqIdx = (seqIdx - 1 + len) % len;
                else if (dir == 2) { if (len < 2) seqIdx = 0; else { if (seqIdx + seqPP >= len || seqIdx + seqPP < 0) seqPP = -seqPP; seqIdx += seqPP; } }
                else { rndState = rndState * 1664525u + 1013904223u; seqIdx = (int) ((rndState >> 8) % (uint32_t) len); }
                if (seqIdx >= len) seqIdx = 0;
            }
        }
    }
    std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.pos < b.pos; });

    // ---- render
    float* L = scratch.getWritePointer (0);
    float* Rr = scratch.getWritePointer (1);
    int cursor = 0;
    auto handle = [this, seqOn] (const Ev& e)
    {
        const bool multi = R ("multi").load() > 0.5f;
        switch (e.type)
        {
            case 0:
                if (seqOn && R ("sqKey").load() > 0.5f) { trans.store (e.a - 48); break; }
                {
                    const bool first = held.empty();
                    held.erase (std::remove (held.begin(), held.end(), e.a), held.end());
                    held.push_back (e.a); curNote = e.a;
                    engine.noteOn (e.a, e.b > 0.f ? e.b : 0.8f, first || multi);
                }
                break;
            case 1:
                if (seqOn && R ("sqKey").load() > 0.5f) break;
                if (std::find (held.begin(), held.end(), e.a) == held.end()) break;
                {
                    const bool wasCur = curNote == e.a;
                    held.erase (std::remove (held.begin(), held.end(), e.a), held.end());
                    if (held.empty()) { curNote = -1; engine.noteOff(); }
                    else if (wasCur) { curNote = held.back(); engine.noteOn (curNote, 0.8f, multi); }
                }
                break;
            case 2: engine.setBend (e.b); bendNow.store (e.b); break;
            case 3: engine.setModWheel (e.b); wheelNow.store (e.b); break;
            case 4: held.clear(); curNote = -1; engine.noteOff(); break;
            case 5: engine.noteOn (jlimit (0, 127, e.a), 0.9f, e.b > 0.5f); break;
            case 6: engine.noteOff(); break;
        }
    };
    for (const auto& e : evs)
    {
        if (e.pos > cursor) { engine.processVoice (L + cursor, Rr + cursor, e.pos - cursor); cursor = e.pos; }
        handle (e);
    }
    if (cursor < n) engine.processVoice (L + cursor, Rr + cursor, n - cursor);

    // ---- reverb (dry + convolved * mix * 1.1, as in the browser version)
    const float rvMix = R ("rvMix").load();
    if (rvMix > 0.001f) rvTail = rvLen + n;
    if (rvTail > 0)
    {
        if (! rvWasOn) { conv.reset(); rvWasOn = true; }
        wet.copyFrom (0, 0, L, n); wet.copyFrom (1, 0, Rr, n);
        float* wl = wet.getWritePointer (0); float* wr = wet.getWritePointer (1);
        float* ptrs[2] = { wl, wr };
        dsp::AudioBlock<float> blk (ptrs, 2, (size_t) n);
        dsp::ProcessContextReplacing<float> ctx (blk);
        conv.process (ctx);
        const float k = 1.f - std::exp (-1.f / (0.01f * (float) sampleRate)), tgt = rvMix * 1.1f;
        for (int i = 0; i < n; ++i)
        {
            rvGain += (tgt - rvGain) * k;
            L[i] += wl[i] * rvGain; Rr[i] += wr[i] * rvGain;
        }
        rvTail -= n;
        if (rvTail <= 0) rvWasOn = false;
    }
    else rvGain = rvMix * 1.1f;

    engine.processMaster (L, Rr, n);

    if (nch == 1) { float* o = buffer.getWritePointer (0); for (int i = 0; i < n; ++i) o[i] = 0.5f * (L[i] + Rr[i]); }
    else { buffer.copyFrom (0, 0, L, n); buffer.copyFrom (1, 0, Rr, n); for (int c = 2; c < nch; ++c) buffer.clear (c, 0, n); }
    { int w = scopeW.load(); for (int i = 0; i < n; ++i) { scopeBuf[(size_t) w] = 0.5f * (L[i] + Rr[i]); w = (w + 1) % kScope; } scopeW.store (w); }
    absPos += n;
}

void MoguProcessor::getStateInformation (MemoryBlock& dest)
{
    auto xml = apvts.copyState().createXml();
    if (xml == nullptr) return;
    xml->setAttribute ("program", program);
    copyXmlToBinary (*xml, dest);
}

void MoguProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    program = xml->getIntAttribute ("program", 0);
    apvts.replaceState (ValueTree::fromXml (*xml));
    irDirty.store (true);
    MessageManager::callAsync ([this] { startTimer (50); });
}

AudioProcessorEditor* MoguProcessor::createEditor() { return new MoguEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MoguProcessor(); }
