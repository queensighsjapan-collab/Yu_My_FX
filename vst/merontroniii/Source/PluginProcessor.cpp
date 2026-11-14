#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
const juce::StringArray& MeronTronIIIProcessor::tapeSetNames()
{
    static const juce::StringArray names { "Strings", "Flute", "Choir", "Brass", "Violin" };
    return names;
}

juce::AudioProcessorValueTreeState::ParameterLayout MeronTronIIIProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "preset", 1 }, "Tape Set", tapeSetNames(), 0));

    auto f01 = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), def);
    };

    params.push_back (f01 ("volume",  "Volume",  0.78f));
    params.push_back (f01 ("tone",    "Tone",    0.55f));
    params.push_back (f01 ("attack",  "Attack",  0.12f));
    params.push_back (f01 ("release", "Release", 0.40f));
    params.push_back (f01 ("reverb",  "Reverb",  0.30f));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "halfSpeed", 1 }, "Half Speed", false));

    return { params.begin(), params.end() };
}

float MeronTronIIIProcessor::rawParam (const char* id) const
{
    const auto* v = apvts.getRawParameterValue (id);
    return v != nullptr ? v->load() : 0.0f;
}

//==============================================================================
MeronTronIIIProcessor::MeronTronIIIProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MeronTronIII", createParameterLayout())
{
    loadFontAsync();
}

MeronTronIIIProcessor::~MeronTronIIIProcessor() = default;

//==============================================================================
juce::File MeronTronIIIProcessor::findFontFile() const
{
    const juce::String fileName = "MeronTronIII.sf2";

    if (const auto env = juce::SystemStats::getEnvironmentVariable ("MERONTRONIII_FONT", ""); env.isNotEmpty())
    {
        const juce::File f (env);
        if (f.existsAsFile())
            return f;
    }

    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    juce::Array<juce::File> candidates;
    candidates.add (exe.getParentDirectory().getChildFile (fileName));                    // standalone: next to the exe
    candidates.add (exe.getParentDirectory()                                               // VST3 bundle: Contents/Resources
                        .getParentDirectory().getChildFile ("Resources").getChildFile (fileName));
    candidates.add (exe.getParentDirectory().getParentDirectory().getChildFile (fileName)); // one level up (dev builds)
    candidates.add (juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                        .getParentDirectory().getChildFile (fileName));
    candidates.add (juce::File::getCurrentWorkingDirectory().getChildFile (fileName));

    for (const auto& f : candidates)
        if (f.existsAsFile())
            return f;

    return {};
}

void MeronTronIIIProcessor::loadFontAsync()
{
    std::thread ([this]
    {
        const auto file = findFontFile();
        if (! file.existsAsFile())
        {
            fontMessage = "FONT NOT FOUND - PLACE MeronTronIII.sf2 NEXT TO THE PLUGIN";
            fontStatus.store (FontStatus::notFound);
            return;
        }

        auto loaded = std::make_unique<Sf2Font>();
        if (! loaded->loadFromFile (file))
        {
            fontMessage = "FONT ERROR: " + loaded->getError().toUpperCase();
            fontStatus.store (FontStatus::error);
            return;
        }

        // Map the panel order (Strings/Flute/Choir/Brass/Violin) onto font
        // presets by program number (Strings 5, Flute 1, Choir 2, Brass 4,
        // Violin 3), falling back to name matching.
        static const int programs[5] = { 5, 1, 2, 4, 3 };
        for (int ui = 0; ui < 5; ++ui)
        {
            int idx = loaded->findPresetByProgram (programs[ui]);
            if (idx < 0)
            {
                for (int i = 0; i < loaded->getNumPresets(); ++i)
                    if (loaded->getPresetName (i).containsIgnoreCase (tapeSetNames()[ui]))
                    { idx = i; break; }
            }
            uiToFontPreset[(size_t) ui] = idx;
        }

        font = std::move (loaded);
        fontMessage = "READY";
        fontStatus.store (FontStatus::ready);
    }).detach();
}

int MeronTronIIIProcessor::fontPresetForUi (int uiIndex) const
{
    if (uiIndex < 0 || uiIndex >= 5)
        return -1;
    return uiToFontPreset[(size_t) uiIndex];
}

//==============================================================================
void MeronTronIIIProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    reverb.setSampleRate (sampleRate);
    lastToneValue = -1.0f;
    lastReverbValue = -1.0f;
    toneStateL = toneStateR = 0.0f;
    for (auto& v : voices)
        v.active = false;
    heldNotes.store (0);
    anyActive.store (false);
}

void MeronTronIIIProcessor::releaseResources() {}

bool MeronTronIIIProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

//==============================================================================
int MeronTronIIIProcessor::getCurrentProgram()
{
    return juce::jlimit (0, 4, (int) rawParam ("preset"));
}

void MeronTronIIIProcessor::setCurrentProgram (int index)
{
    if (auto* p = apvts.getParameter ("preset"))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (0, 4, index)));
}

const juce::String MeronTronIIIProcessor::getProgramName (int index)
{
    return "MERON " + tapeSetNames()[juce::jlimit (0, 4, index)].toUpperCase();
}

void MeronTronIIIProcessor::applyPendingProgramChange()
{
    const int p = pendingProgram.exchange (-1);
    if (p < 0)
        return;
    // Panel order 0..4; program 5 (SF2 numbering top) also lands on Strings.
    setCurrentProgram (p <= 4 ? p : 0);
}

//==============================================================================
void MeronTronIIIProcessor::noteOnFromGui (int midiNote, float velocity)
{
    int s1, s2, f1, f2;
    guiFifo.prepareToWrite (1, s1, f1, s2, f2);
    if (f1 > 0)
    {
        guiQueue[(size_t) s1] = (juce::uint32) ((1 << 24) | ((midiNote & 0xFF) << 8)
                                                | (juce::uint8) juce::jlimit (0, 127, juce::roundToInt (velocity * 127.0f)));
        guiFifo.finishedWrite (1);
    }
}

void MeronTronIIIProcessor::noteOffFromGui (int midiNote)
{
    int s1, s2, f1, f2;
    guiFifo.prepareToWrite (1, s1, f1, s2, f2);
    if (f1 > 0)
    {
        guiQueue[(size_t) s1] = (juce::uint32) ((midiNote & 0xFF) << 8);
        guiFifo.finishedWrite (1);
    }
}

//==============================================================================
void MeronTronIIIProcessor::startVoice (int note, float velocity)
{
    if (! isFontReady() || font == nullptr)
        return;

    const int ui = juce::jlimit (0, 4, (int) rawParam ("preset"));
    const int fp = fontPresetForUi (ui);
    if (fp < 0)
        return;

    const auto* ref = font->getSample (fp, note);
    if (ref == nullptr || ref->length <= 8)
        return;

    // Retrigger: retire any voice already playing this note.
    for (auto& v : voices)
        if (v.active && v.note == note && ! v.releasing)
            killVoice (v, 48);

    Voice* slot = nullptr;
    for (int i = 0; i < maxVoices; ++i)
    {
        auto& cand = voices[(size_t) ((voiceCursor + i) % maxVoices)];
        if (! cand.active)
        {
            slot = &cand;
            voiceCursor = (voiceCursor + i + 1) % maxVoices;
            break;
        }
    }
    if (slot == nullptr)   // steal the oldest slot
    {
        slot = &voices[(size_t) voiceCursor];
        voiceCursor = (voiceCursor + 1) % maxVoices;
    }

    const double attackSec = 0.002 + 2.0 * std::pow ((double) rawParam ("attack"), 2.0);
    const bool half = rawParam ("halfSpeed") > 0.5f;

    slot->active = true;
    slot->releasing = false;
    slot->note = note;
    slot->fontPreset = fp;
    slot->ref = ref;
    slot->pos = 0.0;
    slot->step = std::pow (2.0, (note - ref->rootKey) / 12.0)
               * (half ? 0.5 : 1.0)
               * ((double) ref->sampleRate / currentSampleRate);
    slot->velGain = juce::jlimit (0.05f, 1.0f, velocity);
    slot->env = 0.0f;
    slot->attackLen = juce::jmax (1, (int) (attackSec * currentSampleRate));
    slot->attackPos = 0;
    slot->releaseLen = 1;
    slot->releasePos = 0;

    lastNote.store (note);
    heldNotes.fetch_add (1);
    if (note >= 55 && note <= 89)
        heldMask.fetch_or (juce::uint64 (1) << (note - 55));
}

void MeronTronIIIProcessor::stopVoice (int note)
{
    const double releaseSec = 0.01 + 3.0 * std::pow ((double) rawParam ("release"), 2.0);
    bool any = false;
    for (auto& v : voices)
    {
        if (v.active && v.note == note && ! v.releasing)
        {
            v.releasing = true;
            v.releaseFrom = v.env;
            v.releaseLen = juce::jmax (8, (int) (releaseSec * currentSampleRate));
            v.releasePos = 0;
            any = true;
        }
    }
    if (any)
    {
        heldNotes.fetch_sub (1);
        if (note >= 55 && note <= 89)
            heldMask.fetch_and (~ (juce::uint64 (1) << (note - 55)));
    }
}

void MeronTronIIIProcessor::killVoice (Voice& v, int fadeSamples)
{
    v.releasing = true;
    v.releaseFrom = v.env;
    v.releaseLen = juce::jmax (1, fadeSamples);
    v.releasePos = 0;
}

float MeronTronIIIProcessor::renderVoiceSample (Voice& v)
{
    const auto* data = font->getSampleData();
    const int len = v.ref->length;
    const int i0 = (int) v.pos;

    if (i0 >= len - 4)
    {
        v.active = false;
        return 0.0f;
    }

    // Catmull-Rom interpolation over the mono tape. `idx` is relative to
    // the sample; clamp the ABSOLUTE pool index to this sample's range.
    auto at = [&] (int idx) -> float
    {
        idx = juce::jlimit (v.ref->start, v.ref->start + len - 1, idx + v.ref->start);
        return (float) data[idx] * (1.0f / 32768.0f);
    };
    const float x0 = at (i0 - 1), x1 = at (i0), x2 = at (i0 + 1), x3 = at (i0 + 2);
    const float t = (float) (v.pos - (double) i0);
    const float sample = 0.5f * ((2.0f * x1) + (-x0 + x2) * t
                                 + (2.0f * x0 - 5.0f * x1 + 4.0f * x2 - x3) * t * t
                                 + (-x0 + 3.0f * x1 - 3.0f * x2 + x3) * t * t * t);

    // Envelope
    if (v.releasing)
    {
        const float k = 1.0f - (float) (++v.releasePos) / (float) v.releaseLen;
        v.env = v.releaseFrom * k * k;
        if (v.releasePos >= v.releaseLen)
        {
            v.active = false;
            return 0.0f;
        }
    }
    else if (v.attackPos < v.attackLen)
    {
        const float k = (float) (++v.attackPos) / (float) v.attackLen;
        v.env = 1.0f - (1.0f - k) * (1.0f - k);
    }
    else
    {
        v.env = 1.0f;
    }

    v.pos += v.step;
    return sample * v.env * v.velGain;
}

//==============================================================================
void MeronTronIIIProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    applyPendingProgramChange();

    // Collect note events: GUI queue at offset 0, then host MIDI in order.
    struct Ev { int offset; bool on; int note; float vel; };
    std::vector<Ev> events;

    {
        int s1, f1, s2, f2;
        const int ready = guiFifo.getNumReady();
        guiFifo.prepareToRead (ready, s1, f1, s2, f2);
        auto drain = [&] (int start, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                const auto code = guiQueue[(size_t) (start + i)];
                const bool on = (code >> 24) == 1;
                const int note = (int) ((code >> 8) & 0xFF);
                const float vel = (float) (code & 0xFF) / 127.0f;
                events.push_back ({ 0, on, note, on ? juce::jmax (0.05f, vel) : 0.0f });
            }
        };
        drain (s1, f1);
        drain (s2, f2);
        guiFifo.finishedRead (f1 + f2);
    }

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            events.push_back ({ meta.samplePosition, true, m.getNoteNumber(), m.getFloatVelocity() });
        else if (m.isNoteOff() || (m.isNoteOn() && m.getVelocity() == 0))
            events.push_back ({ meta.samplePosition, false, m.getNoteNumber(), 0.0f });
        else if (m.isProgramChange())
            pendingProgram.store (m.getProgramChangeNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            for (auto& v : voices)
                if (v.active && ! v.releasing)
                    stopVoice (v.note);
    }
    applyPendingProgramChange();

    const int numSamples = buffer.getNumSamples();
    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : outL;

    // Master parameters (per block).
    const float volume = rawParam ("volume");
    const float masterGain = std::pow (volume, 1.5f) * 1.05f;

    const float tone = rawParam ("tone");
    if (std::abs (tone - lastToneValue) > 0.0005f)
    {
        lastToneValue = tone;
        const double cutoff = 700.0 * std::pow (19000.0 / 700.0, (double) tone);
        toneCoeff = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * cutoff / currentSampleRate));
    }

    const float reverbAmt = rawParam ("reverb");
    if (std::abs (reverbAmt - lastReverbValue) > 0.0005f)
    {
        lastReverbValue = reverbAmt;
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.62f;
        rp.damping = 0.48f;
        rp.width = 0.75f;
        rp.wetLevel = reverbAmt * 0.55f;
        rp.dryLevel = 1.0f - reverbAmt * 0.22f;
        rp.freezeMode = 0.0f;
        reverb.setParameters (rp);
    }

    size_t evIdx = 0;
    bool anyNow = false;

    for (int i = 0; i < numSamples; ++i)
    {
        while (evIdx < events.size() && events[evIdx].offset <= i)
        {
            const auto& e = events[evIdx++];
            if (e.on) startVoice (e.note, e.vel);
            else stopVoice (e.note);
        }

        float s = 0.0f;
        if (isFontReady())
            for (auto& v : voices)
                if (v.active)
                {
                    s += renderVoiceSample (v);
                    if (v.active) anyNow = true;
                }

        // Tone: gentle one-pole low-pass (the tapes' own hiss tamer).
        toneStateL += toneCoeff * (s - toneStateL);
        outL[i] = toneStateL;
        if (outR != outL)
        {
            toneStateR += toneCoeff * (s - toneStateR);
            outR[i] = toneStateR;
        }
    }

    while (evIdx < events.size())
    {
        const auto& e = events[evIdx++];
        if (e.on) startVoice (e.note, e.vel);
        else stopVoice (e.note);
    }

    anyActive.store (anyNow);

    if (buffer.getNumChannels() > 1)
        reverb.processStereo (outL, outR, numSamples);
    else
        reverb.processMono (outL, numSamples);

    buffer.applyGain (masterGain);
}

//==============================================================================
void MeronTronIIIProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
}

void MeronTronIIIProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessorEditor* MeronTronIIIProcessor::createEditor()
{
    return new MeronTronIIIEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MeronTronIIIProcessor();
}
