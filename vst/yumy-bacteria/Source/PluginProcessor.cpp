#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Params.h"
#include <algorithm>
#include <cmath>

using namespace juce;

AudioProcessorValueTreeState::ParameterLayout YumyBacteriaProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto roundText = [] (int v, int) { return String (v); };
    (void) roundText;
    for (int i = 0; i < yb::NUM_P; ++i)
    {
        String id (yb::kParamNames[i]); id = id.replaceCharacter (' ', '_');
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, yb::kParamNames[i], 0, 127, yb::kParamDefaults[i]));
    }
    for (int i = 0; i < yb::NUM_O; ++i)
    {
        const auto& m = yb::kOptMeta[i];
        const ParameterID id { String (yb::kOptNames[i]), 1 };
        if (m.isBool) layout.add (std::make_unique<AudioParameterBool> (id, m.label, yb::kOptDefaults[i] != 0));
        else layout.add (std::make_unique<AudioParameterChoice> (id, m.label, StringArray::fromTokens (m.choices, "|", ""), yb::kOptDefaults[i]));
    }
    for (int i = 0; i < 3; ++i)
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "ld" + String (i + 1), 1 }, "LFO" + String (i + 1) + " Destination",
                                                            StringArray::fromTokens (yb::kDests, "|", ""), 0));
    for (int s = 0; s < 6; ++s)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mxs" + String (s + 1), 1 }, "Matrix " + String (s + 1) + " Source",
                                                            StringArray::fromTokens (yb::kSources, "|", ""), 0));
        for (int j = 0; j < 3; ++j)
        {
            layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mxd" + String (s + 1) + "_" + String (j + 1), 1 },
                                                                "Matrix " + String (s + 1) + " Dest " + String (j + 1), StringArray::fromTokens (yb::kDests, "|", ""), 0));
            layout.add (std::make_unique<AudioParameterInt> (ParameterID { "mxa" + String (s + 1) + "_" + String (j + 1), 1 },
                                                             "Matrix " + String (s + 1) + " Amount " + String (j + 1), -100, 100, 0));
        }
    }
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "hsync", 1 }, "Host Tempo Sync", false));
    return layout;
}

YumyBacteriaProcessor::YumyBacteriaProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", AudioChannelSet::stereo(), true)
                          .withInput ("Vocoder Sidechain", AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "YuMyBacteria", createLayout())
{
    for (int i = 0; i < yb::NUM_P; ++i)
    {
        String id (yb::kParamNames[i]); id = id.replaceCharacter (' ', '_');
        pRaw[i] = apvts.getRawParameterValue (id);
    }
    for (int i = 0; i < yb::NUM_O; ++i) oRaw[i] = apvts.getRawParameterValue (yb::kOptNames[i]);
    for (int i = 0; i < 3; ++i) ldRaw[i] = apvts.getRawParameterValue ("ld" + String (i + 1));
    for (int s = 0; s < 6; ++s)
    {
        mxSrc[s] = apvts.getRawParameterValue ("mxs" + String (s + 1));
        for (int j = 0; j < 3; ++j)
        {
            mxDst[s][j] = apvts.getRawParameterValue ("mxd" + String (s + 1) + "_" + String (j + 1));
            mxAmt[s][j] = apvts.getRawParameterValue ("mxa" + String (s + 1) + "_" + String (j + 1));
        }
    }
    hostSyncRaw = apvts.getRawParameterValue ("hsync");
}

bool YumyBacteriaProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (l.getMainOutputChannelSet() != AudioChannelSet::stereo()) return false;
    for (auto& in : l.inputBuses)
        if (! in.isDisabled() && in != AudioChannelSet::stereo() && in != AudioChannelSet::mono()) return false;
    return true;
}

void YumyBacteriaProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    if (! core || std::abs (sampleRate - sr) > 0.5)
    {
        core = std::make_unique<yb::Core> (sr);
        Random r; core->seed ((uint32_t) r.nextInt());
    }
    sampleRate = sr;
    blockCap = std::max (samplesPerBlock, 512);
    engL.assign ((size_t) blockCap, 0.0f); engR.assign ((size_t) blockCap, 0.0f); modBuf.assign ((size_t) blockCap, 0.0f);
    bendV = modW = aftV = 0; wasPlaying = false;
    readParams();
    core->wheels (0, 0, 0);
    core->panic();
}

void YumyBacteriaProcessor::readParams()
{
    for (int i = 0; i < yb::NUM_P; ++i) core->setParam (i, (double) pRaw[i]->load());
    for (int i = 0; i < yb::NUM_O; ++i) core->setOpt (i, (int) std::lround (oRaw[i]->load()));
    for (int i = 0; i < 3; ++i) core->setLfoDest (i, (int) std::lround (ldRaw[i]->load()));
    for (int s = 0; s < 6; ++s)
    {
        yb::MxSlot m; m.src = (int) std::lround (mxSrc[s]->load());
        for (int j = 0; j < 3; ++j) { m.d[j] = (int) std::lround (mxDst[s][j]->load()); m.a[j] = std::lround (mxAmt[s][j]->load()) / 100.0; }
        core->setMatrix (s, m);
    }
}

void YumyBacteriaProcessor::handleMidi (const MidiMessage& m)
{
    if (m.isNoteOn()) core->noteOn (m.getNoteNumber(), std::max (0.05, m.getVelocity() / 127.0));
    else if (m.isNoteOff()) core->noteOff (m.getNoteNumber());
    else if (m.isPitchWheel()) { bendV = jlimit (-1.0, 1.0, (m.getPitchWheelValue() - 8192) / 8192.0); core->wheels (bendV, modW, aftV); }
    else if (m.isChannelPressure()) { aftV = m.getChannelPressureValue() / 127.0; core->wheels (bendV, modW, aftV); }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber(), v = m.getControllerValue();
        if (cc == 1) { modW = v / 127.0; core->wheels (bendV, modW, aftV); }
        else if (cc == 64) core->sustain (v >= 64);
        else if (cc == 120 || cc == 123) core->panic();
    }
    else if (m.isProgramChange()) { const int p = m.getProgramChangeNumber(); if (p < getNumPrograms()) setCurrentProgram (p); }
}

void YumyBacteriaProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int N = buffer.getNumSamples();
    auto out = getBusBuffer (buffer, false, 0);
    for (int c = 0; c < out.getNumChannels(); ++c) out.clear (c, 0, N);
    if (! core) return;
    readParams();

    // host tempo / transport
    bool playing = false;
    double bpm = 0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto b = pos->getBpm()) bpm = *b;
        }
    const bool hsync = hostSyncRaw->load() > 0.5f;
    core->setBpmOverride (hsync && bpm > 0 ? bpm : 0);
    // DAW stopped while host-synced: stop the arpeggiator / latched notes (never leave sound behind)
    if (hsync && wasPlaying && ! playing && core->arpRunning()) core->panic();
    wasPlaying = playing;

    // vocoder modulator: sidechain input (mono mix)
    haveMod = false;
    if (auto* inBus = getBus (true, 0); inBus != nullptr && inBus->isEnabled())
    {
        auto in = getBusBuffer (buffer, true, 0);
        const int nc = in.getNumChannels();
        if (nc > 0)
        {
            if ((int) modBuf.size() < N) modBuf.assign ((size_t) N, 0.0f);
            const float* c0 = in.getReadPointer (0);
            for (int i = 0; i < N; ++i) modBuf[(size_t) i] = c0[i];
            for (int c = 1; c < nc; ++c) { const float* cc = in.getReadPointer (c); for (int i = 0; i < N; ++i) modBuf[(size_t) i] += cc[i]; }
            if (nc > 1) { const float g = 1.0f / (float) nc; for (int i = 0; i < N; ++i) modBuf[(size_t) i] *= g; }
            haveMod = true;
        }
    }

    float* oL = out.getWritePointer (0);
    float* oR = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;
    auto renderRange = [&] (int start, int len)
    {
        while (len > 0)
        {
            const int n = std::min (len, blockCap);
            std::fill (engL.begin(), engL.begin() + n, 0.0f); std::fill (engR.begin(), engR.begin() + n, 0.0f);
            core->render (engL.data(), engR.data(), n, haveMod ? modBuf.data() + start : nullptr);
            for (int i = 0; i < n; ++i) { oL[start + i] = engL[(size_t) i]; if (oR) oR[start + i] = engR[(size_t) i]; }
            start += n; len -= n;
        }
    };
    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = std::min (std::max (meta.samplePosition, pos), N);
        if (t > pos) { renderRange (pos, t - pos); pos = t; }
        handleMidi (meta.getMessage());
    }
    if (pos < N) renderRange (pos, N - pos);
}

// ------------------------------------------------------------------ programs / state
const String YumyBacteriaProcessor::getProgramName (int index)
{
    return (index >= 0 && index < YB_NUM_FACTORY) ? String (yb::kFactory[index].name) : String();
}

void YumyBacteriaProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= YB_NUM_FACTORY) return;
    currentProgram = index;
    const auto& p = yb::kFactory[index];
    auto setP = [this] (const String& id, float real)
    {
        if (auto* q = apvts.getParameter (id)) q->setValueNotifyingHost (q->convertTo0to1 (real));
    };
    for (int i = 0; i < yb::NUM_P; ++i) setP (String (yb::kParamNames[i]).replaceCharacter (' ', '_'), (float) p.k[i]);
    for (int i = 0; i < yb::NUM_O; ++i) setP (yb::kOptNames[i], (float) p.o[i]);
    for (int i = 0; i < 3; ++i) setP ("ld" + String (i + 1), (float) p.ld[i]);
    for (int s = 0; s < 6; ++s)
    {
        setP ("mxs" + String (s + 1), (float) p.mx[s][0]);
        for (int j = 0; j < 3; ++j)
        {
            setP ("mxd" + String (s + 1) + "_" + String (j + 1), (float) p.mx[s][1 + j]);
            setP ("mxa" + String (s + 1) + "_" + String (j + 1), (float) p.mx[s][4 + j]);
        }
    }
}

void YumyBacteriaProcessor::getStateInformation (MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void YumyBacteriaProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (ValueTree::fromXml (*xml));
            currentProgram = jlimit (0, YB_NUM_FACTORY - 1, (int) apvts.state.getProperty ("program", 0));
        }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new YumyBacteriaProcessor(); }
