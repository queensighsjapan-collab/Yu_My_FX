#include "PluginProcessor.h"
#include <cstdio>

using namespace juce;

namespace
{
const char* kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "C+" };

String stepId (int i, const char* what) { char b[32]; std::snprintf (b, sizeof b, "s%02d_%s", i + 1, what); return b; }

StringArray typeChoices()
{
    StringArray a { "Rest", "Tie" };
    for (auto* n : kNoteNames) a.add (n);
    return a;
}
}

AudioProcessorValueTreeState::ParameterLayout YM303Processor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    auto knob = [&] (const char* id, const char* name, float def)
    {
        v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 10.0f, 0.01f), def,
                                                            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return String (x, 1); })));
    };
    knob ("tuning",  "Tuning",       5.0f);
    knob ("cutoff",  "Cut Off Freq", 3.0f);
    knob ("reso",    "Resonance",    7.2f);
    knob ("envmod",  "Env Mod",      6.2f);
    knob ("decay",   "Decay",        3.0f);
    knob ("accent",  "Accent",       6.2f);
    knob ("volume",  "Volume",       7.5f);
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "wave", 1 }, "Waveform", StringArray { "Saw", "Square" }, 0));

    v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "tempo", 1 }, "Tempo", NormalisableRange<float> (60.0f, 200.0f, 0.1f), 130.0f));
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "run", 1 }, "Run", false));
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "hostsync", 1 }, "Host Sync", true));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "playmode", 1 }, "Play Mode", StringArray { "Pattern", "Track" }, 0));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "group", 1 }, "Patt. Group", StringArray { "I", "II", "III", "IV" }, 0));
    v.push_back (std::make_unique<AudioParameterInt> (ParameterID { "pattern", 1 }, "Pattern", 1, 8, 1));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "section", 1 }, "Play Section", StringArray { "A", "B", "A>B" }, 0));
    v.push_back (std::make_unique<AudioParameterInt> (ParameterID { "len", 1 }, "Length", 1, 16, 16));

    for (int i = 0; i < ym303::kSteps; ++i)
    {
        const String n = "Step " + String (i + 1).paddedLeft ('0', 2);
        v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { stepId (i, "type"), 1 }, n + " Note", typeChoices(), 0));
        v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { stepId (i, "oct"), 1 }, n + " Octave", StringArray { "Down", "Normal", "Up" }, 1));
        v.push_back (std::make_unique<AudioParameterBool> (ParameterID { stepId (i, "acc"), 1 }, n + " Accent", false));
        v.push_back (std::make_unique<AudioParameterBool> (ParameterID { stepId (i, "sld"), 1 }, n + " Slide", false));
    }
    return { std::make_move_iterator (v.begin()), std::make_move_iterator (v.end()) };
}

YM303Processor::YM303Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "YM303", createLayout())
{
    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pTuning = raw ("tuning"); pCutoff = raw ("cutoff"); pReso = raw ("reso"); pEnvmod = raw ("envmod"); pDecay = raw ("decay");
    pAccent = raw ("accent"); pVolume = raw ("volume"); pWave = raw ("wave"); pTempo = raw ("tempo"); pRun = raw ("run");
    pHostSync = raw ("hostsync"); pPlayMode = raw ("playmode"); pGroup = raw ("group"); pPattern = raw ("pattern"); pSection = raw ("section");

    for (auto* id : { "group", "pattern", "section", "len" }) apvts.addParameterListener (id, this);
    for (int i = 0; i < ym303::kSteps; ++i)
        for (auto* w : { "type", "oct", "acc", "sld" }) apvts.addParameterListener (stepId (i, w), this);

    engine = std::make_unique<ym303::Engine> (sampleRate);
    seq.prepare (sampleRate);
    refreshEditView();
}

YM303Processor::~YM303Processor()
{
    cancelPendingUpdate();
}

bool YM303Processor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return (o == AudioChannelSet::stereo() || o == AudioChannelSet::mono()) && l.getMainInputChannelSet().isDisabled();
}

void YM303Processor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    engine = std::make_unique<ym303::Engine> (sr);
    seq.prepare (sr);
}

int YM303Processor::selectedPattern() const
{
    return jlimit (0, 3, (int) pGroup->load()) * 8 + jlimit (1, 8, (int) pPattern->load()) - 1;
}

int YM303Processor::editSection() const { return (int) pSection->load() == 1 ? 1 : 0; }   // A and A>B edit section A

void YM303Processor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    midi.clear();                                   // MIDI is not used in this version (the built-in sequencer plays)
    const int n = buffer.getNumSamples(), nch = buffer.getNumChannels();
    if (nch == 0 || n == 0 || engine == nullptr) return;

    auto& p = engine->p;
    p.tuning = pTuning->load() / 10.0;  p.cutoff = pCutoff->load() / 10.0;  p.reso = pReso->load() / 10.0;
    p.envmod = pEnvmod->load() / 10.0;  p.decay = pDecay->load() / 10.0;    p.accent = pAccent->load() / 10.0;
    p.volume = pVolume->load() / 10.0;  p.wave = pWave->load() > 0.5f ? 1 : 0;

    ym303::Selection sel;
    sel.mode = pPlayMode->load() > 0.5f ? 1 : 0;
    sel.pat = selectedPattern();
    sel.sec = jlimit (0, 2, (int) pSection->load());

    // clock: the DAW transport when HOST SYNC is on and the host reports a position, otherwise RUN + TEMPO
    bool hostOk = false, playing = false; double ppq = 0, bpm = 0;
    if (pHostSync->load() > 0.5f)
        if (auto* ph = getPlayHead())
            if (auto pi = ph->getPosition())
                if (auto q = pi->getPpqPosition())
                    if (auto b = pi->getBpm())
                        if (*b >= 1.0) { hostOk = true; ppq = *q; bpm = *b; playing = pi->getIsPlaying(); }

    events.clear();
    if (hostOk)
    {
        if (playing) seq.runHost (n, ppq, bpm, sel, events); else seq.idle (n, events);
    }
    else if (pRun->load() > 0.5f) seq.runInternal (n, (double) pTempo->load(), sel, events);
    else seq.idle (n, events);

    float* out = buffer.getWritePointer (0);
    int cursor = 0;
    for (int i = 0; i < events.size(); ++i)
    {
        const auto& e = events[i];
        const int at = jlimit (cursor, n, e.offset);
        if (at > cursor) { engine->process (out + cursor, at - cursor); cursor = at; }
        if (e.on) engine->noteOn (e.note, e.accent, e.slide); else engine->noteOff();
    }
    if (cursor < n) engine->process (out + cursor, n - cursor);
    // The browser version has a compressor on its output; here a soft knee keeps extreme settings below 0 dBFS.
    // Signals below 0.8 pass unchanged, so normal playing is not affected.
    for (int i = 0; i < n; ++i)
    {
        const float a = std::abs (out[i]);
        if (a > 0.8f) out[i] = std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f), out[i]);
    }
    for (int c = 1; c < nch; ++c) buffer.copyFrom (c, 0, buffer, 0, 0, n);
}

// ------------------------------------------------------------------ edit view <-> pattern data
// The 16 step parameters (plus LENGTH) always show the pattern section that is currently selected.
// Moving a step parameter writes into that section; changing the selection reloads the parameters.
void YM303Processor::parameterChanged (const String& id, float v)
{
    if (refreshing.load()) return;
    if (id == "group" || id == "pattern" || id == "section") { triggerAsyncUpdate(); return; }

    const int pat = selectedPattern(), sec = editSection();
    if (id == "len") { bank.setLen (pat, sec, (int) v); return; }
    if (id.length() >= 7 && id[0] == 's' && id[3] == '_')
    {
        const int i = id.substring (1, 3).getIntValue() - 1;
        if (i < 0 || i >= ym303::kSteps) return;
        auto s = bank.getStep (pat, sec, i);
        const auto what = id.substring (4);
        if (what == "type")
        {
            const int c = (int) v;
            if (c == 0) s.type = ym303::Rest; else if (c == 1) s.type = ym303::Tie; else { s.type = ym303::Note; s.note = jlimit (0, 12, c - 2); }
        }
        else if (what == "oct") { s.dn = (int) v == 0; s.up = (int) v == 2; }
        else if (what == "acc") s.acc = v > 0.5f;
        else if (what == "sld") s.sld = v > 0.5f;
        bank.setStep (pat, sec, i, s);
    }
}

void YM303Processor::handleAsyncUpdate() { refreshEditView(); }

void YM303Processor::refreshEditView()
{
    const int pat = selectedPattern(), sec = editSection();
    refreshing = true;
    auto set = [this] (const String& id, float real)
    {
        if (auto* p = apvts.getParameter (id))
            if (std::abs (p->convertFrom0to1 (p->getValue()) - real) > 0.001f) p->setValueNotifyingHost (p->convertTo0to1 (real));
    };
    set ("len", (float) bank.getLen (pat, sec));
    for (int i = 0; i < ym303::kSteps; ++i)
    {
        const auto s = bank.getStep (pat, sec, i);
        set (stepId (i, "type"), s.type == ym303::Rest ? 0.0f : s.type == ym303::Tie ? 1.0f : (float) (2 + s.note));
        set (stepId (i, "oct"), s.dn ? 0.0f : s.up ? 2.0f : 1.0f);
        set (stepId (i, "acc"), s.acc ? 1.0f : 0.0f);
        set (stepId (i, "sld"), s.sld ? 1.0f : 0.0f);
    }
    refreshing = false;
}

// ------------------------------------------------------------------ state
void YM303Processor::getStateInformation (MemoryBlock& dest)
{
    auto xml = apvts.copyState().createXml();
    if (xml == nullptr) return;
    auto* b = xml->createNewChildElement ("BANK");
    b->setAttribute ("version", 1);
    b->setAttribute ("data", String (bank.serialize()));
    copyXmlToBinary (*xml, dest);
}

void YM303Processor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    std::string bankData;
    if (auto* b = xml->getChildByName ("BANK"))
    {
        bankData = b->getStringAttribute ("data").toStdString();
        xml->removeChildElement (b, true);
    }
    cancelPendingUpdate();
    refreshing = true;                                  // the stored step values must not be written into another slot
    apvts.replaceState (ValueTree::fromXml (*xml));
    refreshing = false;
    if (! bankData.empty()) bank.deserialize (bankData);
    refreshEditView();
}

AudioProcessorEditor* YM303Processor::createEditor() { return new GenericAudioProcessorEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new YM303Processor(); }
