#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
YMMirageProcessor::YMMirageProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    sizeParam    = apvts.getRawParameterValue ("sizeMs");
    densityParam = apvts.getRawParameterValue ("density");
    pitchParam   = apvts.getRawParameterValue ("pitch");
    spreadParam  = apvts.getRawParameterValue ("spread");
    smearParam   = apvts.getRawParameterValue ("smear");
    freezeParam  = apvts.getRawParameterValue ("freeze");
    mixParam     = apvts.getRawParameterValue ("mix");
    outputParam  = apvts.getRawParameterValue ("output");
    pitchCcParam  = apvts.getRawParameterValue ("pitchCc");
    freezeCcParam = apvts.getRawParameterValue ("freezeCc");
    bypassParam   = apvts.getRawParameterValue ("bypass");
    bypassCcParam = apvts.getRawParameterValue ("bypassCc");

    pitchFloatParam  = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("pitch"));
    freezeBoolParam  = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("freeze"));
    pitchCcIntParam  = dynamic_cast<juce::AudioParameterInt*>   (apvts.getParameter ("pitchCc"));
    freezeCcIntParam = dynamic_cast<juce::AudioParameterInt*>   (apvts.getParameter ("freezeCc"));
    bypassBoolParam  = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("bypass"));
    bypassCcIntParam = dynamic_cast<juce::AudioParameterInt*>   (apvts.getParameter ("bypassCc"));
}

YMMirageProcessor::~YMMirageProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout YMMirageProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "sizeMs", "Size",
        juce::NormalisableRange<float> (10.0f, 200.0f, 0.1f),
        80.0f, "ms"));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "density", "Density",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
        0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "pitch", "Pitch",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f),
        0.0f, "st"));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "spread", "Spread",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
        0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "smear", "Smear",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
        0.3f));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "freeze", "Freeze", false));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
        0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "output", "Output",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f),
        0.0f, "dB"));

    // v1.1 additions, appended after the v1.0 parameters so existing
    // states load identically. CC number 0 means "Off" (no CC assigned);
    // CC 0 itself is therefore never a working assignment.
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        "pitchCc", "Pitch CC", 0, 127, 1));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        "freezeCc", "Freeze CC", 0, 127, 64));

    // House standard (かいさん, 2026-11-10): every YM effect's ON/OFF
    // must be MIDI-CC controllable. "bypass" is also returned from
    // getBypassParameter() so host bypass buttons drive the same
    // switch; bypassCc (default 65) flips it with pedal-style values
    // (>= 64 = effect ON, < 64 = bypassed).
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "bypass", "Bypass", false));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        "bypassCc", "Bypass CC", 0, 127, 65));

    return { params.begin(), params.end() };
}

//==============================================================================
int YMMirageProcessor::computeLatencySamples (float sizeMs, float pitchSt) const noexcept
{
    // The wet path reads behind the write head by (grain length x rate),
    // i.e. exactly the span a max-length grain consumes, plus the 2 ms
    // safety margin spawnGrain() adds. Report that so the host's delay
    // compensation aligns the dry path with the cloud's anchor.
    const double sizeSamples = (double) sizeMs * 0.001 * currentSampleRate;
    const double rate = std::pow (2.0, (double) pitchSt / 12.0);
    return (int) std::ceil (sizeSamples * rate) + (int) std::ceil (0.002 * currentSampleRate);
}

void YMMirageProcessor::refreshLatency()
{
    const int wanted = computeLatencySamples (sizeParam->load(), pitchParam->load());

    if (wanted != lastLatencySamples)
    {
        lastLatencySamples = wanted;
        setLatencySamples (wanted);
    }
}

void YMMirageProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    ring.setSize (2, (int) std::ceil (bufferSeconds * currentSampleRate));
    ring.clear();
    ringSize = ring.getNumSamples();
    writeHead = 0;
    ringL = ring.getWritePointer (0);
    ringR = ring.getWritePointer (1);

    for (auto& grain : grains)
        grain.active = false;

    spawnAccumulator = 0.0;
    random.setSeed ((juce::int64) (juce::Time::currentTimeMillis() & 0x7fffffff));

    smoothSize.reset (currentSampleRate, 0.05);
    smoothDensity.reset (currentSampleRate, 0.05);
    smoothPitch.reset (currentSampleRate, 0.05);
    smoothSpread.reset (currentSampleRate, 0.05);
    smoothSmear.reset (currentSampleRate, 0.05);
    smoothMix.reset (currentSampleRate, 0.03);
    smoothOutGain.reset (currentSampleRate, 0.03);
    smoothBypass.reset (currentSampleRate, 0.015);

    smoothSize.setCurrentAndTargetValue (sizeParam->load());
    smoothDensity.setCurrentAndTargetValue (densityParam->load());
    smoothPitch.setCurrentAndTargetValue (pitchParam->load());
    smoothSpread.setCurrentAndTargetValue (spreadParam->load());
    smoothSmear.setCurrentAndTargetValue (smearParam->load());
    smoothMix.setCurrentAndTargetValue (mixParam->load());
    smoothOutGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (outputParam->load()));
    smoothBypass.setCurrentAndTargetValue (
        bypassParam->load() > 0.5f ? 0.0f : 1.0f);   // start settled, no fade-in

    lastLatencySamples = -1;
    refreshLatency();
}

void YMMirageProcessor::releaseResources()
{
    for (auto& grain : grains)
        grain.active = false;
}

bool YMMirageProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::stereo())
        return false;

    return in == juce::AudioChannelSet::stereo()
        || in == juce::AudioChannelSet::mono();
}

//==============================================================================
float YMMirageProcessor::readMid (double pos) const noexcept
{
    // pos is always kept inside [0, ringSize).
    const int i0 = (int) pos;
    const float frac = (float) (pos - (double) i0);
    const int i1 = (i0 + 1 >= ringSize) ? 0 : i0 + 1;
    const float inv = 1.0f - frac;

    const float mid = (ringL[i0] + ringR[i0]) * inv
                    + (ringL[i1] + ringR[i1]) * frac;
    return mid * 0.5f;
}

void YMMirageProcessor::spawnGrain (float sizeMs, float pitchSt, float spread, float smear)
{
    Grain* slot = nullptr;

    for (auto& grain : grains)
    {
        if (! grain.active)
        {
            slot = &grain;
            break;
        }
    }

    if (slot == nullptr)
        return;   // pool full: drop this spawn (CPU guard)

    const double sizeSamples = (double) sizeMs * 0.001 * currentSampleRate;
    const double baseRate = std::pow (2.0, (double) pitchSt / 12.0);

    // Random detune: up to +-1% x SPREAD, per the design note.
    const double detune = 1.0 + (double) (random.nextFloat() * 2.0f - 1.0f)
                              * (double) spread * 0.01;
    const double rate = baseRate * detune;

    // The grain consumes (length x rate) samples of input over its life,
    // so its anchor must sit at least that far behind the write head,
    // plus a small safety margin: it can never read fresh, unwritten or
    // just-overwritten material, and never overtake the write head.
    const double need = sizeSamples * rate;
    const double safety = 0.002 * currentSampleRate;
    double behind = need + safety;

    // SMEAR: random extra history, up to +2 s, clamped so the anchor
    // always stays inside the ring with room for the grain's span.
    const double maxBehind = (double) ringSize - need - safety;
    double extra = (double) random.nextFloat() * (double) smear * 2.0 * currentSampleRate;

    if (behind + extra > maxBehind)
        extra = juce::jmax (0.0, maxBehind - behind);

    double start = (double) writeHead - behind - extra;

    while (start < 0.0)
        start += (double) ringSize;

    // Random equal-power pan within +-SPREAD.
    const float pan = (random.nextFloat() * 2.0f - 1.0f) * spread;
    const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;

    slot->readPos = start;
    slot->rate = rate;
    slot->length = juce::jmax (16, (int) std::lround (sizeSamples));
    slot->age = 0;
    slot->windowPhaseInc = juce::MathConstants<float>::pi / (float) slot->length;
    slot->gainL = std::cos (angle);
    slot->gainR = std::sin (angle);
    slot->active = true;
}

//==============================================================================
void YMMirageProcessor::setPitchCcNumber (int ccNumber)
{
    if (pitchCcIntParam != nullptr)
        *pitchCcIntParam = juce::jlimit (0, 127, ccNumber);
}

void YMMirageProcessor::setFreezeCcNumber (int ccNumber)
{
    if (freezeCcIntParam != nullptr)
        *freezeCcIntParam = juce::jlimit (0, 127, ccNumber);
}

void YMMirageProcessor::setBypassCcNumber (int ccNumber)
{
    if (bypassCcIntParam != nullptr)
        *bypassCcIntParam = juce::jlimit (0, 127, ccNumber);
}

void YMMirageProcessor::setBypassed (bool shouldBeBypassed)
{
    if (bypassBoolParam != nullptr)
        *bypassBoolParam = shouldBeBypassed;
}

void YMMirageProcessor::handleMidiMessages (const juce::MidiBuffer& midiMessages)
{
    // Omni: the channel is ignored. Events are applied in buffer order,
    // so within one block the last matching controller wins. All writes
    // go through the parameters themselves (setValueNotifyingHost via
    // the typed operator=), which is the accepted pattern for
    // MIDI-driven parameter changes: no locks, no allocation, and the
    // GUI / host see the same value the DSP reads.
    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();

        if (! message.isController())
            continue;

        const int ccNumber = message.getControllerNumber();

        // MIDI Learn has priority: the next controller is captured as
        // the assignment for the armed target and consumed - it does
        // not also drive the parameter. CC 0 can never be learned,
        // because 0 is the "Off" value of the assignment parameters.
        const auto target = getLearnTarget();

        if (target != LearnTarget::none && ccNumber >= 1)
        {
            if (target == LearnTarget::pitch)
                setPitchCcNumber (ccNumber);
            else if (target == LearnTarget::freeze)
                setFreezeCcNumber (ccNumber);
            else
                setBypassCcNumber (ccNumber);

            setLearnTarget (LearnTarget::none);
            continue;
        }

        const int value = message.getControllerValue();
        const int pitchCc  = pitchCcParam  != nullptr ? (int) pitchCcParam->load()  : 0;
        const int freezeCc = freezeCcParam != nullptr ? (int) freezeCcParam->load() : 0;
        const int bypassCc = bypassCcParam != nullptr ? (int) bypassCcParam->load() : 0;

        if (pitchCc >= 1 && ccNumber == pitchCc && pitchFloatParam != nullptr)
        {
            // Linear map 0..127 -> -12..+12 st, with a centre dead-zone
            // (62..65) that lands on exactly 0 st. The pitch parameter's
            // ~50 ms DSP smoothing glides the change from here.
            float semitones = 0.0f;

            if (value < 62 || value > 65)
                semitones = -12.0f + 24.0f * ((float) value / 127.0f);

            *pitchFloatParam = semitones;
        }

        if (freezeCc >= 1 && ccNumber == freezeCc && freezeBoolParam != nullptr)
            *freezeBoolParam = (value >= 64);   // sustain-pedal semantics

        if (bypassCc >= 1 && ccNumber == bypassCc && bypassBoolParam != nullptr)
            *bypassBoolParam = (value < 64);    // >= 64 = effect ON
    }
}

//==============================================================================
void YMMirageProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                      juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // MIDI first, so a CC in this block already steers this block's
    // parameter reads below (and Learn works even before prepareToPlay).
    handleMidiMessages (midiMessages);

    const int numSamples = buffer.getNumSamples();
    const int numInChannels = getTotalNumInputChannels();

    if (ringSize == 0 || numSamples == 0)
        return;

    auto* inL = buffer.getReadPointer (0);
    auto* inR = buffer.getReadPointer (numInChannels > 1 ? 1 : 0);
    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getWritePointer (1);

    //--- smoothed control values ---------------------------------------------
    smoothSize.setTargetValue (sizeParam->load());
    smoothDensity.setTargetValue (densityParam->load());
    smoothPitch.setTargetValue (pitchParam->load());
    smoothSpread.setTargetValue (spreadParam->load());
    smoothSmear.setTargetValue (smearParam->load());
    smoothMix.setTargetValue (mixParam->load());
    smoothOutGain.setTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));

    // Spawn parameters use one block-constant value (the smoothers still
    // ramp between blocks); mix / output are smoothed per sample below.
    const float sizeMs  = smoothSize.skip (numSamples);
    const float density = smoothDensity.skip (numSamples);
    const float pitchSt = smoothPitch.skip (numSamples);
    const float spread  = smoothSpread.skip (numSamples);
    const float smear   = smoothSmear.skip (numSamples);

    const bool frozen = freezeParam->load() > 0.5f;

    // Bypass: smoothBypass ramps 1 (effect) <-> 0 (bypassed). While
    // fully bypassed and settled, grains are not spawned or rendered
    // at all (no DSP CPU) - but the write head above keeps running,
    // so the ring never holds stale audio when the effect comes back.
    const bool bypassed = bypassParam->load() > 0.5f;
    smoothBypass.setTargetValue (bypassed ? 0.0f : 1.0f);
    const bool renderGrains = ! bypassed || smoothBypass.isSmoothing();

    const float spawnRate = grainsPerSecond (density);
    const double spawnPerSample = (double) spawnRate / currentSampleRate;

    // Incoherent-sum normalisation: expected overlap is spawnRate x grain
    // length, and random-phase grains add as sqrt(N), so divide it back out.
    const float overlap = juce::jmax (1.0f, spawnRate * sizeMs * 0.001f);
    const float wetNorm = 1.0f / std::sqrt (overlap);

    for (int i = 0; i < numSamples; ++i)
    {
        const float dryL = inL[i];
        const float dryR = inR[i];

        if (! frozen)
        {
            ringL[writeHead] = dryL;
            ringR[writeHead] = dryR;

            if (++writeHead >= ringSize)
                writeHead = 0;
        }

        float wetL = 0.0f, wetR = 0.0f;

        if (renderGrains)
        {
            spawnAccumulator += spawnPerSample;

            while (spawnAccumulator >= 1.0)
            {
                spawnAccumulator -= 1.0;
                spawnGrain (sizeMs, pitchSt, spread, smear);
            }

            for (auto& grain : grains)
            {
                if (! grain.active)
                    continue;

                const float s = readMid (grain.readPos);
                const double phase = (double) grain.age * (double) grain.windowPhaseInc;
                const float w = (float) std::sin (phase);
                const float windowed = s * w * w;

                wetL += windowed * grain.gainL;
                wetR += windowed * grain.gainR;

                grain.readPos += grain.rate;

                if (grain.readPos >= (double) ringSize)
                    grain.readPos -= (double) ringSize;

                if (++grain.age >= grain.length)
                    grain.active = false;
            }
        }
        else
        {
            spawnAccumulator = 0.0;
        }

        const float mix = smoothMix.getNextValue();
        const float outGain = smoothOutGain.getNextValue();
        const float wetGain = mix * wetNorm * outGain;
        const float dryGain = (1.0f - mix) * outGain;

        const float effectL = dryL * dryGain + wetL * wetGain;
        const float effectR = dryR * dryGain + wetR * wetGain;

        // Equal-power crossfade between the effect output and the raw
        // dry input (bypass is a true unity passthrough: MIX and OUTPUT
        // do not apply while bypassed).
        const float blend = smoothBypass.getNextValue();
        const float angle = blend * juce::MathConstants<float>::halfPi;
        const float gainEffect = std::sin (angle);
        const float gainDry = std::cos (angle);

        outL[i] = effectL * gainEffect + dryL * gainDry;
        outR[i] = effectR * gainEffect + dryR * gainDry;
    }

    refreshLatency();
}

//==============================================================================
juce::AudioProcessorEditor* YMMirageProcessor::createEditor()
{
    return new YMMirageEditor (*this);
}

//==============================================================================
void YMMirageProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void YMMirageProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new YMMirageProcessor();
}
