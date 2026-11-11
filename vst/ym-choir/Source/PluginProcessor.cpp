#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
YMChoirProcessor::YMChoirProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    voicesParam  = apvts.getRawParameterValue ("voices");
    detuneParam  = apvts.getRawParameterValue ("detune");
    timingParam  = apvts.getRawParameterValue ("timing");
    vibratoParam = apvts.getRawParameterValue ("vibrato");
    spreadParam  = apvts.getRawParameterValue ("spread");
    toneParam    = apvts.getRawParameterValue ("tone");
    mixParam     = apvts.getRawParameterValue ("mix");
    outputParam  = apvts.getRawParameterValue ("output");
    bypassParam   = apvts.getRawParameterValue ("bypass");
    bypassCcParam = apvts.getRawParameterValue ("bypassCc");

    bypassBoolParam  = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
    bypassCcIntParam = dynamic_cast<juce::AudioParameterInt*>  (apvts.getParameter ("bypassCc"));
}

YMChoirProcessor::~YMChoirProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout YMChoirProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        "voices", "Voices", 1, maxVoices, 4));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "detune", "Detune",
        juce::NormalisableRange<float> (0.0f, 60.0f, 0.1f),
        18.0f, "cents"));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "timing", "Timing",
        juce::NormalisableRange<float> (0.0f, 30.0f, 0.1f),
        12.0f, "ms"));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "vibrato", "Vibrato",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
        0.4f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "spread", "Spread",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
        0.7f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "tone", "Tone",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
        0.45f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "output", "Output",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f),
        0.0f, "dB"));

    // House standard (かいさん, 2026-11-10): every YM effect's ON/OFF
    // must be MIDI-CC controllable. "bypass" is also returned from
    // getBypassParameter() so host bypass buttons drive the same
    // switch; bypassCc (default 65) flips it with pedal-style values
    // (>= 64 = effect ON, < 64 = bypassed). CC number 0 means "Off"
    // (no CC assigned); CC 0 itself is therefore never a working
    // assignment.
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "bypass", "Bypass", false));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        "bypassCc", "Bypass CC", 0, 127, 65));

    return { params.begin(), params.end() };
}

//==============================================================================
void YMChoirProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;

    ring.assign ((size_t) std::ceil (bufferSeconds * currentSampleRate), 0.0f);
    ringSize = (int) ring.size();
    writeHead = 0;

    windowSamples = juce::jmax (256, (int) std::lround (
        referenceWindowSamples * currentSampleRate / referenceRate));
    setLatencySamples (windowSamples);

    for (int i = 0; i < maxVoices; ++i)
    {
        auto& voice = voices[(size_t) i];
        voice.phase = phasePattern[(size_t) i];
        voice.vibPhase = (double) i * 2.399963;   // golden-angle spread
        voice.lpState = 0.0f;
        voice.level = 0.0f;

        // Fixed vibrato LFO rate, spread 3.5..6.5 Hz across the slots.
        const double rateHz = 3.5 + 3.0 * (double) i / (double) (maxVoices - 1);
        voice.vibInc = juce::MathConstants<double>::twoPi * rateHz / currentSampleRate;
    }

    smoothDetune.reset (currentSampleRate, 0.05);
    smoothTiming.reset (currentSampleRate, 0.05);
    smoothVibrato.reset (currentSampleRate, 0.05);
    smoothSpread.reset (currentSampleRate, 0.05);
    smoothTone.reset (currentSampleRate, 0.05);
    smoothMix.reset (currentSampleRate, 0.03);
    smoothOutGain.reset (currentSampleRate, 0.03);
    smoothBypass.reset (currentSampleRate, 0.015);

    smoothDetune.setCurrentAndTargetValue (detuneParam->load());
    smoothTiming.setCurrentAndTargetValue (timingParam->load());
    smoothVibrato.setCurrentAndTargetValue (vibratoParam->load());
    smoothSpread.setCurrentAndTargetValue (spreadParam->load());
    smoothTone.setCurrentAndTargetValue (toneParam->load());
    smoothMix.setCurrentAndTargetValue (mixParam->load());
    smoothOutGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (outputParam->load()));
    smoothBypass.setCurrentAndTargetValue (
        bypassParam->load() > 0.5f ? 0.0f : 1.0f);   // start settled, no fade-in
}

void YMChoirProcessor::releaseResources()
{
    for (auto& voice : voices)
        voice.level = 0.0f;
}

bool YMChoirProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::stereo())
        return false;

    return in == juce::AudioChannelSet::stereo()
        || in == juce::AudioChannelSet::mono();
}

//==============================================================================
float YMChoirProcessor::readRing (double delaySamples) const noexcept
{
    // Read the sample "delaySamples" behind the write head (the slot
    // most recently written), with linear interpolation.
    double pos = (double) writeHead - delaySamples;

    while (pos < 0.0)
        pos += (double) ringSize;

    const int i0 = (int) pos;
    const float frac = (float) (pos - (double) i0);
    const int i1 = (i0 + 1 >= ringSize) ? 0 : i0 + 1;

    return ring[(size_t) i0] * (1.0f - frac) + ring[(size_t) i1] * frac;
}

void YMChoirProcessor::updateVoiceParameters (int activeVoices, float detuneCents,
                                              float timingMs, float spread, float tone)
{
    // Global lowpass tilt: TONE -1..+1 -> 4..16 kHz (exponential).
    const double globalFc = 8000.0 * std::pow (2.0, (double) tone);
    const float norm = 1.0f / std::sqrt ((float) juce::jmax (1, activeVoices));

    for (int i = 0; i < maxVoices; ++i)
    {
        auto& voice = voices[(size_t) i];

        // Position across the field, centre outwards, in [-1, +1].
        const float posNorm = activeVoices <= 1
            ? 0.0f
            : (2.0f * (float) i / (float) (activeVoices - 1) - 1.0f);

        // A single added voice is a pure unison thickener (no static
        // detune of its own; the vibrato still gives it life).
        voice.baseCents = (activeVoices <= 1)
            ? 0.0f
            : detunePattern[(size_t) i] * detuneCents;

        voice.timingSamples = (double) timingPattern[(size_t) i]
                            * (double) timingMs * 0.001 * currentSampleRate;

        const float pan = posNorm * spread;
        const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        voice.panL = std::cos (angle);
        voice.panR = std::sin (angle);

        // Centre voices slightly louder; overall 1/sqrt(N) normalising.
        voice.gain = (1.0f - 0.25f * std::abs (posNorm)) * norm;

        // Edge voices slightly darker, under the global tilt.
        const double fc = juce::jlimit (400.0, currentSampleRate * 0.45,
                                        globalFc * (1.0 - 0.35 * (double) std::abs (posNorm)));
        voice.lpCoeff = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi
                                                 * fc / currentSampleRate));
    }
}

//==============================================================================
void YMChoirProcessor::setBypassCcNumber (int ccNumber)
{
    if (bypassCcIntParam != nullptr)
        *bypassCcIntParam = juce::jlimit (0, 127, ccNumber);
}

void YMChoirProcessor::setBypassed (bool shouldBeBypassed)
{
    if (bypassBoolParam != nullptr)
        *bypassBoolParam = shouldBeBypassed;
}

void YMChoirProcessor::handleMidiMessages (const juce::MidiBuffer& midiMessages)
{
    // Omni: the channel is ignored. Events are applied in buffer
    // order, so within one block the last matching controller wins.
    // All writes go through the parameters themselves
    // (setValueNotifyingHost via the typed operator=), the accepted
    // pattern for MIDI-driven parameter changes: no locks, no
    // allocation, and the GUI / host see the same value the DSP reads.
    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();

        if (! message.isController())
            continue;

        const int ccNumber = message.getControllerNumber();

        // MIDI Learn has priority: the next controller is captured as
        // the bypass assignment and consumed. CC 0 can never be
        // learned, because 0 is the "Off" value of the assignment.
        if (getLearnTarget() != LearnTarget::none && ccNumber >= 1)
        {
            setBypassCcNumber (ccNumber);
            setLearnTarget (LearnTarget::none);
            continue;
        }

        const int bypassCc = bypassCcParam != nullptr ? (int) bypassCcParam->load() : 0;

        if (bypassCc >= 1 && ccNumber == bypassCc && bypassBoolParam != nullptr)
            *bypassBoolParam = (message.getControllerValue() < 64);   // >= 64 = effect ON
    }
}

//==============================================================================
void YMChoirProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                     juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // MIDI first, so a CC in this block already steers this block.
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
    smoothDetune.setTargetValue (detuneParam->load());
    smoothTiming.setTargetValue (timingParam->load());
    smoothVibrato.setTargetValue (vibratoParam->load());
    smoothSpread.setTargetValue (spreadParam->load());
    smoothTone.setTargetValue (toneParam->load());
    smoothMix.setTargetValue (mixParam->load());
    smoothOutGain.setTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));

    // Voice character uses one block-constant set of values (the
    // smoothers still ramp between blocks); mix / output are smoothed
    // per sample below.
    const int activeVoices = juce::jlimit (1, maxVoices, (int) voicesParam->load());
    const float detuneCents = smoothDetune.skip (numSamples);
    const float timingMs    = smoothTiming.skip (numSamples);
    const float spread      = smoothSpread.skip (numSamples);
    const float tone        = smoothTone.skip (numSamples);
    const float vibratoDepthCents = smoothVibrato.skip (numSamples) * 15.0f;

    updateVoiceParameters (activeVoices, detuneCents, timingMs, spread, tone);

    // Bypass: smoothBypass ramps 1 (effect) <-> 0 (bypassed). While
    // fully bypassed and settled, voices are not rendered (no DSP
    // CPU) - but the ring keeps being written below, so the choir
    // never sings stale audio when the effect comes back.
    const bool bypassed = bypassParam->load() > 0.5f;
    smoothBypass.setTargetValue (bypassed ? 0.0f : 1.0f);
    const bool renderVoices = ! bypassed || smoothBypass.isSmoothing();

    // Per-sample level ramp for voices fading in/out on VOICES changes
    // (~30 ms to full), so adding or removing a singer never clicks.
    const float levelStep = 1.0f / (0.030f * (float) currentSampleRate);
    const double window = (double) windowSamples;

    for (int i = 0; i < numSamples; ++i)
    {
        const float dryL = inL[i];
        const float dryR = inR[i];

        ring[(size_t) writeHead] = (dryL + dryR) * 0.5f;

        float wetL = 0.0f, wetR = 0.0f;

        if (renderVoices)
        {
            for (int v = 0; v < maxVoices; ++v)
            {
                auto& voice = voices[(size_t) v];

                const float target = v < activeVoices ? 1.0f : 0.0f;

                if (voice.level < target)
                    voice.level = juce::jmin (target, voice.level + levelStep);
                else if (voice.level > target)
                    voice.level = juce::jmax (target, voice.level - levelStep);

                if (voice.level <= 0.0005f)
                    continue;

                // Vibrato -> instantaneous pitch ratio for this voice.
                voice.vibPhase += voice.vibInc;

                if (voice.vibPhase >= juce::MathConstants<double>::twoPi)
                    voice.vibPhase -= juce::MathConstants<double>::twoPi;

                const double cents = (double) voice.baseCents
                                   + std::sin (voice.vibPhase) * (double) vibratoDepthCents;
                const double ratio = std::pow (2.0, cents / 1200.0);

                // Dual-tap crossfading delay-line shifter: the phase
                // sawtooth advances at (ratio - 1) / window, the tap
                // delay is (1 - phase) * window so a rising phase
                // shortens the delay and raises the pitch.
                voice.phase += (ratio - 1.0) / window;

                if (voice.phase >= 1.0)
                    voice.phase -= 1.0;
                else if (voice.phase < 0.0)
                    voice.phase += 1.0;

                const double phase2 = voice.phase < 0.5 ? voice.phase + 0.5
                                                        : voice.phase - 0.5;
                const double delay1 = (1.0 - voice.phase) * window + voice.timingSamples;
                const double delay2 = (1.0 - phase2) * window + voice.timingSamples;

                const double s = std::sin (voice.phase * juce::MathConstants<double>::pi);
                const double c = std::cos (voice.phase * juce::MathConstants<double>::pi);
                const float w1 = (float) (s * s);   // sin^2(pi * phase)
                const float w2 = (float) (c * c);   // sin^2(pi * phase2)

                float sig = readRing (delay1) * w1 + readRing (delay2) * w2;

                voice.lpState += voice.lpCoeff * (sig - voice.lpState);
                sig = voice.lpState * voice.level * voice.gain;

                wetL += sig * voice.panL;
                wetR += sig * voice.panR;
            }
        }

        if (++writeHead >= ringSize)
            writeHead = 0;

        const float mix = smoothMix.getNextValue();
        const float outGain = smoothOutGain.getNextValue();

        const float effectL = dryL * (1.0f - mix) * outGain + wetL * mix * outGain;
        const float effectR = dryR * (1.0f - mix) * outGain + wetR * mix * outGain;

        // Equal-power crossfade between the effect output and the raw
        // dry input (bypass is a true unity passthrough: MIX and
        // OUTPUT do not apply while bypassed).
        const float blend = smoothBypass.getNextValue();
        const float angle = blend * juce::MathConstants<float>::halfPi;
        const float gainEffect = std::sin (angle);
        const float gainDry = std::cos (angle);

        outL[i] = effectL * gainEffect + dryL * gainDry;
        outR[i] = effectR * gainEffect + dryR * gainDry;
    }
}

//==============================================================================
juce::AudioProcessorEditor* YMChoirProcessor::createEditor()
{
    return new YMChoirEditor (*this);
}

//==============================================================================
void YMChoirProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void YMChoirProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new YMChoirProcessor();
}
