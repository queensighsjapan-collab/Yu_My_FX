#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

using namespace juce;

// ------------------------------------------------------------------ parameter layout
AudioProcessorValueTreeState::ParameterLayout YM2000Processor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;
    std::vector<std::pair<String, std::unique_ptr<AudioProcessorParameterGroup>>> groups;
    auto group = [&] (const String& name) -> AudioProcessorParameterGroup&
    {
        for (auto& g : groups) if (g.first == name) return *g.second;
        groups.emplace_back (name, std::make_unique<AudioProcessorParameterGroup> (name.replaceCharacter (' ', '_'), name, "|"));
        return *groups.back().second;
    };
    auto roundText = [] (float v, int) { return String (roundToInt (v)); };

    for (const auto& p : ym::kParams)
    {
        const ParameterID pid { p.id, 1 };
        std::unique_ptr<RangedAudioParameter> param;
        switch (p.kind)
        {
            case ym::Kind::Float:
                param = std::make_unique<AudioParameterFloat> (pid, p.name, NormalisableRange<float> (p.mn, p.mx), p.def,
                                                               AudioParameterFloatAttributes().withStringFromValueFunction (roundText));
                break;
            case ym::Kind::Int:
                param = std::make_unique<AudioParameterInt> (pid, p.name, (int) p.mn, (int) p.mx, (int) p.def);
                break;
            case ym::Kind::Bool:
                param = std::make_unique<AudioParameterBool> (pid, p.name, p.def > 0.5f);
                break;
            case ym::Kind::Choice:
                param = std::make_unique<AudioParameterChoice> (pid, p.name, StringArray::fromTokens (p.choices, "|", ""), (int) p.def);
                break;
        }
        group (p.section).addChild (std::move (param));
    }
    for (int i = 0; i < 16; ++i)
    {
        group ("MOD SEQ").addChild (std::make_unique<AudioParameterInt> (ParameterID { "seq" + String (i + 1), 1 },
                                                                         "Mod Seq Step " + String (i + 1), 0, 127, ym::kSeqDefaults[i]));
        group ("VOCODER LEVEL").addChild (std::make_unique<AudioParameterInt> (ParameterID { "vlev" + String (i + 1), 1 },
                                                                               "Vocoder Level Ch " + String (i + 1), 0, 127, 100));
        group ("VOCODER PAN").addChild (std::make_unique<AudioParameterInt> (ParameterID { "vpan" + String (i + 1), 1 },
                                                                             "Vocoder Pan Ch " + String (i + 1), 0, 127, 64));
    }
    for (auto& g : groups) layout.add (std::move (g.second));
    return layout;
}

// ------------------------------------------------------------------ construction
YM2000Processor::YM2000Processor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", AudioChannelSet::stereo(), true)
                          .withInput ("Audio In / Sidechain", AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "YM2000", createLayout())
{
    for (const auto& p : ym::kParams)
    {
        const int idx = ym::engineParamIndex (p.id);
        if (idx >= 0) engineBindings.push_back ({ apvts.getRawParameterValue (p.id), idx });
    }
    for (int i = 0; i < 16; ++i)
    {
        seqRaw[i] = apvts.getRawParameterValue ("seq" + String (i + 1));
        vlevRaw[i] = apvts.getRawParameterValue ("vlev" + String (i + 1));
        vpanRaw[i] = apvts.getRawParameterValue ("vpan" + String (i + 1));
    }
    auto R = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    fr = { R ("vol"), R ("ai1"), R ("ai2"), R ("vmode"), R ("vfs"), R ("vcl"), R ("mft"), R ("mfs"), R ("mfd"), R ("dt"), R ("dtm"), R ("ddp"),
           R ("eqhf"), R ("eqhg"), R ("eqlf"), R ("eqlg"),
           R ("arpon"), R ("arplt"), R ("arpt"), R ("arpr"), R ("arpg"), R ("arptm"), R ("msq"), R ("msk") };
    for (int i = 0; i < 16; ++i) seqVals[i] = ym::kSeqDefaults[i];
    keyboardState.addListener (&midiCollector);
}

bool YM2000Processor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (l.getMainOutputChannelSet() != AudioChannelSet::stereo()) return false;
    for (auto& in : l.inputBuses)
        if (! in.isDisabled() && in != AudioChannelSet::stereo() && in != AudioChannelSet::mono()) return false;
    return true;
}

void YM2000Processor::prepareToPlay (double sr, int samplesPerBlock)
{
    if (! engine || std::abs (sampleRate - sr) > 0.5)
        engine = std::make_unique<ym::Engine> (sr);
    sampleRate = sr;
    midiCollector.reset (sr);
    blockCap = std::max (samplesPerBlock, 512);
    for (auto* v : { &engL, &engR, &engIn, &srcMono, &outL, &outR }) v->assign ((size_t) blockCap, 0.0f);
    arpHeld.reserve (256); arpCur.reserve (256); pool.reserve (512);
    fx.prepare (sr, blockCap);
    setLatencySamples ((int) std::lround (0.006 * sr)); // limiter look-ahead
    // reset performance state
    phys.reset(); sustained.reset(); sus = false;
    arpHeld.clear(); arpCur.clear(); arpIdx = 0; seqIdx = 0;
    clockRunning = false; clockCountdown = 0; arpOffCountdown = -1;
    lastArpOn = fr.arpon->load() > 0.5f; lastArpLatch = fr.arplt->load() > 0.5f; lastMsq = fr.msq->load() > 0.5f;
    engine->panic();
}

// ------------------------------------------------------------------ notes, arpeggiator, mod sequence
void YM2000Processor::engOn (int note, int vel) { if (engine) engine->noteOn (note, vel / 127.0); }
void YM2000Processor::engOff (int note) { if (engine) engine->noteOff (note); }

bool YM2000Processor::clockWanted() const { return (arpOn && ! arpHeld.empty()) || seqOn; }

void YM2000Processor::clockStart()
{
    if (clockRunning) return;
    clockRunning = true; clockCountdown = 0;
}

void YM2000Processor::arpStop()
{
    arpOffCountdown = -1;
    for (int n : arpCur) engOff (n);
    arpCur.clear(); arpIdx = 0;
}

void YM2000Processor::arpStep (double stepSamples)
{
    arpOffCountdown = -1;
    for (int n : arpCur) engOff (n);
    arpCur.clear();
    if (! arpOn || arpHeld.empty()) { arpIdx = 0; return; }
    std::sort (arpHeld.begin(), arpHeld.end());
    const int rng_ = std::max (1, arpRange);
    pool.clear();
    for (int r = 0; r < rng_; ++r)
        for (int n : arpHeld) if (n + 12 * r <= 127) pool.push_back (n + 12 * r);
    if (arpType == 5)
        for (int n : arpHeld) arpCur.push_back (n);
    else
    {
        const int n = (int) pool.size();
        if (n == 0) return;
        int i;
        if (arpType == 0) { i = arpIdx % n; ++arpIdx; }
        else if (arpType == 1) { i = (n - 1) - (arpIdx % n); ++arpIdx; }
        else if (arpType == 2 || arpType == 3)
        {
            const int seq = arpType == 2 ? std::max (1, n * 2 - 2) : n * 2, k = arpIdx % seq;
            i = k < n ? k : (arpType == 2 ? seq - k : seq - 1 - k);
            ++arpIdx;
        }
        else i = rng.nextInt (n);
        arpCur.push_back (pool[(size_t) i]);
    }
    for (int n : arpCur) engOn (n, arpVel);
    arpOffCountdown = stepSamples * (0.1 + 0.88 * arpGate / 127.0);
}

void YM2000Processor::seqStep()
{
    if (! seqOn) { uiSeqPos.store (-1); return; }
    uiSeqPos.store (seqIdx);
    if (engine) engine->set (ym::seqv, seqVals[seqIdx]);
    seqIdx = (seqIdx + 1) % 16;
}

void YM2000Processor::clockTick()
{
    const double step = sampleRate * 15.0 / std::max (1.0, arpTempo); // one 16th note
    arpStep (step);
    seqStep();
    if (! clockWanted()) { clockRunning = false; arpStop(); uiSeqPos.store (-1); return; }
    clockCountdown += step;
}

void YM2000Processor::noteOn (int note, int vel)
{
    if (phys.test ((size_t) note)) return;
    sustained.reset ((size_t) note);
    const bool wasEmpty = phys.none();
    phys.set ((size_t) note);
    if (arpOn)
    {
        if (arpLatch && wasEmpty) arpHeld.clear();
        if (std::find (arpHeld.begin(), arpHeld.end(), note) == arpHeld.end()) arpHeld.push_back (note);
        arpVel = vel; clockStart();
    }
    else engOn (note, vel);
    if (seqOn && seqKeySync && wasEmpty) seqIdx = 0;
}

void YM2000Processor::noteOff (int note)
{
    if (! phys.test ((size_t) note)) return;
    phys.reset ((size_t) note);
    if (arpOn)
    {
        if (! arpLatch && ! sus) arpHeld.erase (std::remove (arpHeld.begin(), arpHeld.end(), note), arpHeld.end());
    }
    else if (sus) sustained.set ((size_t) note);
    else engOff (note);
}

void YM2000Processor::pedal (bool on)
{
    if (on == sus) return;
    sus = on;
    if (on) return;
    for (int n = 0; n < 128; ++n)
        if (sustained.test ((size_t) n) && ! isHeld (n)) engOff (n);
    sustained.reset();
    if (arpOn && ! arpLatch)
        arpHeld.erase (std::remove_if (arpHeld.begin(), arpHeld.end(), [this] (int n) { return ! isHeld (n); }), arpHeld.end());
}

void YM2000Processor::allNotesOff()
{
    for (int n = 0; n < 128; ++n) if (phys.test ((size_t) n)) noteOff (n);
    sus = false; sustained.reset(); arpHeld.clear(); arpStop();
    if (engine) engine->panic();
}

void YM2000Processor::handleMidi (const MidiMessage& m)
{
    if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getVelocity());
    else if (m.isNoteOff()) noteOff (m.getNoteNumber());
    else if (m.isPitchWheel()) { bendVal = jlimit (-1.0, 1.0, (m.getPitchWheelValue() - 8192) / 8192.0); uiBend.store ((float) bendVal); }
    else if (m.isController())
    {
        const int c = m.getControllerNumber(), v = m.getControllerValue();
        if (c == 1) { modVal = v / 127.0; uiMod.store ((float) modVal); }
        else if (c == 64) pedal (v >= 64);
        else if (c == 120 || c == 123) allNotesOff();
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff()) allNotesOff();
    else if (m.isProgramChange())
    {
        const int p = m.getProgramChangeNumber();
        if (p < ym::kNumFactory) { pendingProgram.store (p); triggerAsyncUpdate(); }
    }
}

// ------------------------------------------------------------------ parameters -> engine / fx
void YM2000Processor::readParams()
{
    for (const auto& b : engineBindings) engine->set (b.engIdx, b.raw->load());
    engine->set (ym::msd, 0);
    engine->set (ym::bend, bendVal);
    engine->set (ym::mod, modVal);

    arpOn = fr.arpon->load() > 0.5f; arpLatch = fr.arplt->load() > 0.5f;
    arpType = (int) fr.arpt->load(); arpRange = (int) fr.arpr->load();
    arpGate = fr.arpg->load(); arpTempo = fr.arptm->load();
    seqOn = fr.msq->load() > 0.5f; seqKeySync = fr.msk->load() > 0.5f;
    for (int i = 0; i < 16; ++i) seqVals[i] = (int) seqRaw[i]->load();

    if (arpOn != lastArpOn)
    {
        if (! arpOn)
        {
            arpStop(); arpHeld.clear();
            for (int n = 0; n < 128; ++n) if (phys.test ((size_t) n)) engOn (n, 100);
        }
        else
        {
            for (int n = 0; n < 128; ++n)
                if (phys.test ((size_t) n))
                {
                    engOff (n);
                    if (std::find (arpHeld.begin(), arpHeld.end(), n) == arpHeld.end()) arpHeld.push_back (n);
                }
            if (! arpHeld.empty()) clockStart();
        }
        lastArpOn = arpOn;
    }
    if (arpLatch != lastArpLatch)
    {
        if (! arpLatch && phys.none()) arpHeld.clear();
        lastArpLatch = arpLatch;
    }
    if (seqOn != lastMsq)
    {
        if (seqOn) clockStart();
        lastMsq = seqOn;
    }

    fxp.vol = fr.vol->load(); fxp.ai1 = fr.ai1->load(); fxp.ai2 = fr.ai2->load();
    fxp.vmode = (int) fr.vmode->load(); fxp.vfs = fr.vfs->load(); fxp.vcl = fr.vcl->load();
    fxp.mft = (int) fr.mft->load(); fxp.mfs = fr.mfs->load(); fxp.mfd = fr.mfd->load();
    fxp.dt = (int) fr.dt->load(); fxp.dtm = fr.dtm->load(); fxp.ddp = fr.ddp->load();
    fxp.eqhf = fr.eqhf->load(); fxp.eqhg = fr.eqhg->load(); fxp.eqlf = fr.eqlf->load(); fxp.eqlg = fr.eqlg->load();
    for (size_t i = 0; i < 16; ++i) { fxp.vlev[i] = vlevRaw[i]->load(); fxp.vpan[i] = vpanRaw[i]->load(); }
}

// ------------------------------------------------------------------ audio
void YM2000Processor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (! engine) { buffer.clear(); return; }
    if (n > blockCap)
    {
        blockCap = n;
        for (auto* v : { &engL, &engR, &engIn, &srcMono, &outL, &outR }) v->assign ((size_t) blockCap, 0.0f);
    }

    // audio input (vocoder modulator / OSC1 AUDIO IN), read before the shared channels are overwritten
    bool haveIn = false;
    if (getBusCount (true) > 0 && getBus (true, 0)->isEnabled())
    {
        auto in = getBusBuffer (buffer, true, 0);
        const int chs = in.getNumChannels();
        if (chs > 0)
        {
            const float* a = in.getReadPointer (0);
            const float* b = chs > 1 ? in.getReadPointer (1) : nullptr;
            for (int i = 0; i < n; ++i) srcMono[(size_t) i] = b ? 0.5f * (a[i] + b[i]) : a[i];
            haveIn = true;
        }
    }
    for (int c = 0; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    midiCollector.removeNextBlockOfMessages (midi, n); // notes / wheels played on the GUI

    readParams();
    fx.setParams (fxp, n);
    fx.engineInput (haveIn ? srcMono.data() : nullptr, engIn.data(), n);

    int pos = 0;
    auto it = midi.begin();
    const auto end = midi.end();
    while (pos < n)
    {
        while (it != end && (*it).samplePosition <= pos) { handleMidi ((*it).getMessage()); ++it; }
        if (arpOffCountdown >= 0 && arpOffCountdown <= 1e-9)
        {
            for (int nt : arpCur) engOff (nt);
            arpCur.clear(); arpOffCountdown = -1;
        }
        while (clockRunning && clockCountdown <= 1e-9) clockTick();

        int next = n;
        if (it != end) next = std::min (next, std::max (pos + 1, (*it).samplePosition));
        if (clockRunning) next = std::min (next, pos + (int) std::max (1.0, std::ceil (clockCountdown)));
        if (arpOffCountdown >= 0) next = std::min (next, pos + (int) std::max (1.0, std::ceil (arpOffCountdown)));
        const int len = next - pos;
        engine->process (engL.data() + pos, engR.data() + pos, len, engIn.data() + pos);
        if (clockRunning) clockCountdown -= len;
        if (arpOffCountdown >= 0) arpOffCountdown -= len;
        pos = next;
    }

    fx.process (engL.data(), engR.data(), haveIn ? srcMono.data() : nullptr, outL.data(), outR.data(), n);

    auto out = getBusBuffer (buffer, false, 0);
    if (out.getNumChannels() > 0) out.copyFrom (0, 0, outL.data(), n);
    if (out.getNumChannels() > 1) out.copyFrom (1, 0, outR.data(), n);
}

// ------------------------------------------------------------------ programs and state
const String YM2000Processor::getProgramName (int index)
{
    return index >= 0 && index < ym::kNumFactory ? String (ym::kFactory[index].name) : String();
}

void YM2000Processor::loadFactory (int index)
{
    const auto& f = ym::kFactory[index];
    auto setNorm = [this] (const String& id, float v)
    {
        if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    for (const auto& p : ym::kParams)
    {
        float v = p.def;
        for (int k = 0; k < f.n; ++k) if (std::strcmp (f.v[k].id, p.id) == 0) v = f.v[k].v;
        setNorm (p.id, v);
    }
    for (int i = 0; i < 16; ++i)
    {
        setNorm ("seq" + String (i + 1), (float) ym::kSeqDefaults[i]);
        setNorm ("vlev" + String (i + 1), 100.0f);
        setNorm ("vpan" + String (i + 1), 64.0f);
    }
}

void YM2000Processor::setCurrentProgram (int index)
{
    if (index < 0 || index >= ym::kNumFactory) return;
    currentProgram = index;
    loadFactory (index);
}

void YM2000Processor::handleAsyncUpdate()
{
    const int p = pendingProgram.exchange (-1);
    if (p >= 0) setCurrentProgram (p);
}

void YM2000Processor::getStateInformation (MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void YM2000Processor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = ValueTree::fromXml (*xml);
            currentProgram = jlimit (0, ym::kNumFactory - 1, (int) tree.getProperty ("program", 0));
            apvts.replaceState (tree);
        }
}



AudioProcessorEditor* YM2000Processor::createEditor() { return new YM2000Editor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new YM2000Processor(); }
