#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Sweep half-range in octaves for a given DEPTH (v1.1 knee).
    // Up to the knee at DEPTH = 0.66 this is exactly v1.0's 1.5 x DEPTH;
    // above it a quadratic term (zero slope at the knee, so the curve
    // is smooth) stretches the half-range to 2.0 octaves at DEPTH = 1:
    //   R(1) = 1.5 + k (1 - 0.66)^2 = 2.0  =>  k = 0.5 / 0.34^2.
    double sweepHalfRangeOctaves (double depth) noexcept
    {
        const double d = juce::jlimit (0.0, 1.0, depth);
        const double over = juce::jmax (0.0, d - 0.66);
        return 1.5 * d + (0.5 / (0.34 * 0.34)) * over * over;
    }

    // Exact logarithmic mapping for the frequency-like parameters.
    juce::NormalisableRange<float> logRange (float start, float end, float interval)
    {
        juce::NormalisableRange<float> range (
            start, end,
            [start, end] (float, float, float n) { return start * std::pow (end / start, n); },
            [start, end] (float, float, float v) { return std::log (v / start) / std::log (end / start); },
            [start, end] (float, float, float v) { return juce::jlimit (start, end, v); });
        range.interval = interval;
        return range;
    }
}

//==============================================================================
YMVortexProcessor::YMVortexProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    rateParam     = apvts.getRawParameterValue ("rate");
    depthParam    = apvts.getRawParameterValue ("depth");
    ratioParam    = apvts.getRawParameterValue ("ratio");
    depth2Param   = apvts.getRawParameterValue ("depth2");
    centreParam   = apvts.getRawParameterValue ("centre");
    feedbackParam = apvts.getRawParameterValue ("feedback");
    stagesParam   = apvts.getRawParameterValue ("stages");
    widthParam    = apvts.getRawParameterValue ("width");
    waveParam     = apvts.getRawParameterValue ("wave");
    mixParam      = apvts.getRawParameterValue ("mix");
    outputParam   = apvts.getRawParameterValue ("output");
    bypassParam   = apvts.getRawParameterValue ("bypass");
    bypassCcParam = apvts.getRawParameterValue ("bypassCc");

    bypassBoolParam  = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
    bypassCcIntParam = dynamic_cast<juce::AudioParameterInt*>  (apvts.getParameter ("bypassCc"));
}

YMVortexProcessor::~YMVortexProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout YMVortexProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "rate", "Rate", logRange (0.02f, 8.0f, 0.001f), 0.3f, "Hz"));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "depth", "Depth",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.7f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "ratio", "Ratio", logRange (0.25f, 4.0f, 0.001f), 1.618f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "depth2", "Depth 2",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.3f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "centre", "Centre", logRange (100.0f, 4000.0f, 1.0f), 800.0f, "Hz"));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "feedback", "Feedback",
        juce::NormalisableRange<float> (-0.97f, 0.97f, 0.01f), 0.3f));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "stages", "Stages", juce::StringArray { "4", "6", "8", "12" }, 1));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "width", "Width",
        juce::NormalisableRange<float> (0.0f, 180.0f, 1.0f), 90.0f, "deg"));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "wave", "Wave", juce::StringArray { "Sine", "Triangle" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "output", "Output",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, "dB"));

    // YM house standard, appended last: MIDI-controllable bypass.
    // bypassCc 0 means "Off" (no CC assigned); CC 0 itself is therefore
    // never a working assignment.
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "bypass", "Bypass", false));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        "bypassCc", "Bypass CC", 0, 127, 65));

    return { params.begin(), params.end() };
}

juce::AudioProcessorParameter* YMVortexProcessor::getBypassParameter() const
{
    return apvts.getParameter ("bypass");
}

//==============================================================================
void YMVortexProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    std::memset (apX1, 0, sizeof (apX1));
    std::memset (apY1, 0, sizeof (apY1));
    fbState[0] = fbState[1] = 0.0f;
    fbLpState[0] = fbLpState[1] = 0.0f;
    // One-pole return highpass coefficient, fc ~= 25 Hz:
    // lp += alpha (x - lp); hp = x - lp, with alpha = 1 - exp(-2 pi fc / fs).
    fbHpAlpha = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 25.0 / currentSampleRate);
    lastStageCount = -1;
    phase1 = 0.0;
    phase2 = 0.0;

    smoothRate.reset     (currentSampleRate, 0.05);
    smoothDepth.reset    (currentSampleRate, 0.05);
    smoothRatio.reset    (currentSampleRate, 0.05);
    smoothDepth2.reset   (currentSampleRate, 0.05);
    smoothCentre.reset   (currentSampleRate, 0.05);
    smoothFeedback.reset (currentSampleRate, 0.03);
    smoothWidth.reset    (currentSampleRate, 0.05);
    smoothMix.reset      (currentSampleRate, 0.03);
    smoothOutGain.reset  (currentSampleRate, 0.03);

    smoothRate.setCurrentAndTargetValue (rateParam->load());
    smoothDepth.setCurrentAndTargetValue (depthParam->load());
    smoothRatio.setCurrentAndTargetValue (ratioParam->load());
    smoothDepth2.setCurrentAndTargetValue (depth2Param->load());
    smoothCentre.setCurrentAndTargetValue (centreParam->load());
    smoothFeedback.setCurrentAndTargetValue (feedbackParam->load());
    smoothWidth.setCurrentAndTargetValue (widthParam->load());
    smoothMix.setCurrentAndTargetValue (mixParam->load());
    smoothOutGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (outputParam->load()));

    bypassStep = (float) (1.0 / (0.015 * currentSampleRate));
    bypassLevel = bypassParam->load() > 0.5f ? 0.0f : 1.0f;
}

void YMVortexProcessor::releaseResources()
{
    std::memset (apX1, 0, sizeof (apX1));
    std::memset (apY1, 0, sizeof (apY1));
    fbState[0] = fbState[1] = 0.0f;
    fbLpState[0] = fbLpState[1] = 0.0f;
}

bool YMVortexProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::stereo())
        return false;

    return in == juce::AudioChannelSet::stereo()
        || in == juce::AudioChannelSet::mono();
}

//==============================================================================
float YMVortexProcessor::lfoWave (double phaseCycles, bool triangle) const noexcept
{
    const double p = phaseCycles - std::floor (phaseCycles);   // 0..1

    if (triangle)
        return (float) (p < 0.5 ? (4.0 * p - 1.0) : (3.0 - 4.0 * p));

    return (float) std::sin (juce::MathConstants<double>::twoPi * p);
}

void YMVortexProcessor::setBypassCcNumber (int ccNumber)
{
    if (bypassCcIntParam != nullptr)
        *bypassCcIntParam = juce::jlimit (0, 127, ccNumber);
}

void YMVortexProcessor::handleMidiMessages (const juce::MidiBuffer& midiMessages)
{
    // Omni: the channel is ignored. Events are applied in buffer order,
    // so within one block the last matching controller wins. All writes
    // go through the parameters themselves (setValueNotifyingHost via
    // the typed operator=): no locks, no allocation, and the GUI / host
    // see the same value the DSP reads.
    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();

        if (! message.isController())
            continue;

        const int ccNumber = message.getControllerNumber();

        // MIDI Learn has priority: the next controller is captured as
        // the bypass assignment and consumed - it does not also toggle
        // the bypass. CC 0 can never be learned, because 0 is the "Off"
        // value of the assignment parameter.
        if (getLearnTarget() != LearnTarget::none && ccNumber >= 1)
        {
            setBypassCcNumber (ccNumber);
            setLearnTarget (LearnTarget::none);
            continue;
        }

        const int bypassCc = bypassCcParam != nullptr ? (int) bypassCcParam->load() : 0;

        if (bypassCc >= 1 && ccNumber == bypassCc && bypassBoolParam != nullptr)
        {
            // House standard: value >= 64 turns the effect ON (bypass
            // off); below 64 turns it OFF (bypassed).
            *bypassBoolParam = (message.getControllerValue() < 64);
        }
    }
}

//==============================================================================
void YMVortexProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                      juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // MIDI first, so a CC in this block already steers this block.
    handleMidiMessages (midiMessages);

    const int numSamples = buffer.getNumSamples();

    if (numSamples == 0)
        return;

    const int numInChannels = getTotalNumInputChannels();
    const double fs = currentSampleRate;

    //--- block-constant smoothed control values ------------------------------
    smoothRate.setTargetValue (rateParam->load());
    smoothDepth.setTargetValue (depthParam->load());
    smoothRatio.setTargetValue (ratioParam->load());
    smoothDepth2.setTargetValue (depth2Param->load());
    smoothCentre.setTargetValue (centreParam->load());
    smoothWidth.setTargetValue (widthParam->load());
    smoothFeedback.setTargetValue (feedbackParam->load());
    smoothMix.setTargetValue (mixParam->load());
    smoothOutGain.setTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));

    const float rate   = smoothRate.skip (numSamples);
    const float depth  = smoothDepth.skip (numSamples);
    const float ratio  = smoothRatio.skip (numSamples);
    const float depth2 = smoothDepth2.skip (numSamples);
    const float centre = smoothCentre.skip (numSamples);
    const float widthDeg = smoothWidth.skip (numSamples);
    // (feedback / mix / output are advanced per sample via getNextValue)

    const int numStages = stageCountForIndex ((int) stagesParam->load());

    if (numStages != lastStageCount)
    {
        // Changing the cascade length: clear the cells so stale state
        // from a different geometry cannot burst through.
        std::memset (apX1, 0, sizeof (apX1));
        std::memset (apY1, 0, sizeof (apY1));
        fbState[0] = fbState[1] = 0.0f;
        fbLpState[0] = fbLpState[1] = 0.0f;
        lastStageCount = numStages;
    }

    // Stage base frequencies: geometric span of 3 octaves around CENTRE.
    double baseFreq[maxStages];

    for (int s = 0; s < numStages; ++s)
    {
        const double offset = numStages > 1
            ? 3.0 * ((double) s / (double) (numStages - 1) - 0.5)
            : 0.0;
        baseFreq[s] = (double) centre * std::pow (2.0, offset);
    }

    const bool triangle = waveParam->load() > 0.5f;
    const double phaseInc1 = (double) rate / fs;
    const double phaseInc2 = (double) rate * (double) ratio / fs;
    const double widthCycles = (double) widthDeg / 360.0;
    const float invNorm = 1.0f / (1.0f + depth2);
    const double maxFreq = 0.45 * fs;
    // v1.1: sweep half-range with the DEPTH knee (1.5xDEPTH up to 0.66,
    // smoothly up to 2.0 octaves at DEPTH = 1). Stage frequencies are
    // still clamped to maxFreq below, so the top stages stay sane when
    // CENTRE is high and the sweep pushes them towards Nyquist.
    const double sweepHalfRange = sweepHalfRangeOctaves ((double) depth);

    //--- bypass ---------------------------------------------------------------
    const bool bypassed = bypassParam->load() > 0.5f;
    const float bypassTarget = bypassed ? 0.0f : 1.0f;

    if (bypassLevel == 0.0f && bypassTarget == 0.0f)
    {
        // Fully bypassed: true pass-through (the buffer already holds
        // the input). The chain is skipped, but the LFOs keep running
        // so the sweep is mid-motion when the effect comes back.
        phase1 += phaseInc1 * numSamples;
        phase2 += phaseInc2 * numSamples;
        phase1 -= std::floor (phase1);
        phase2 -= std::floor (phase2);

        const float mL = (lfoWave (phase1, triangle)
                          + depth2 * (float) std::sin (juce::MathConstants<double>::twoPi * phase2)) * invNorm;
        const float mR = (lfoWave (phase1 + widthCycles, triangle)
                          + depth2 * (float) std::sin (juce::MathConstants<double>::twoPi * (phase2 + widthCycles))) * invNorm;
        modValueL.store (mL);
        modValueR.store (mR);
        return;
    }

    const auto* inL = buffer.getReadPointer (0);
    const auto* inR = buffer.getReadPointer (numInChannels > 1 ? 1 : 0);
    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getWritePointer (1);

    float mLastL = 0.0f, mLastR = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // Bypass crossfade ramp (~15 ms), per sample.
        if (bypassLevel < bypassTarget)
            bypassLevel = juce::jmin (bypassTarget, bypassLevel + bypassStep);
        else if (bypassLevel > bypassTarget)
            bypassLevel = juce::jmax (bypassTarget, bypassLevel - bypassStep);

        const float fbAmount = smoothFeedback.getNextValue();
        const float mix = smoothMix.getNextValue();
        const float outGain = smoothOutGain.getNextValue();

        for (int c = 0; c < 2; ++c)
        {
            const double off = c == 1 ? widthCycles : 0.0;
            const float l1 = lfoWave (phase1 + off, triangle);
            const float l2 = (float) std::sin (juce::MathConstants<double>::twoPi * (phase2 + off));
            const float m = (l1 + depth2 * l2) * invNorm;

            // Sweep: all stage centres scale by 2^(half-range x m).
            const double mult = std::pow (2.0, sweepHalfRange * (double) m);

            const float x = c == 0 ? inL[i] : inR[i];

            // Chain input: dry plus the feedback return. The return is
            // soft-clipped by a gain-staged tanh, 2*tanh(x/2) (v1.1):
            // v1.0's plain tanh(x) saturated on the loop signal itself,
            // capping the resonant peaks (and filling the notches with
            // intermodulation) so the feedback stopped getting stronger
            // much past 0.7. The staged version stays near-linear to
            // |x| ~= 1 and only limits towards +-2, while its slope is
            // still <= 1 everywhere, so the loop's incremental gain
            // stays <= |fb| < 1 and it cannot run away. After clipping,
            // a ~25 Hz one-pole highpass (v1.1) stops DC / low rumble
            // recirculating for seconds at |fb| -> 0.97. The LP state
            // tracks the clipped signal every sample, even at fb = 0,
            // so raising FEEDBACK later cannot produce a stale-state
            // step.
            const float clipped = 2.0f * std::tanh (fbState[c] * 0.5f);
            fbLpState[c] += (float) (fbHpAlpha * (double) (clipped - fbLpState[c]));
            float v = x + fbAmount * (clipped - fbLpState[c]);

            for (int s = 0; s < numStages; ++s)
            {
                const double f = juce::jlimit (8.0, maxFreq, baseFreq[s] * mult);
                const double t = std::tan (juce::MathConstants<double>::pi * f / fs);
                // Sign matters: for y = a*x + x[-1] - a*y[-1] the
                // -90 degree point sits at tan(pi f / fs) = t only
                // when a = (t - 1) / (t + 1).
                const float a = (float) ((t - 1.0) / (1.0 + t));

                // One-pole allpass: y = a*x + x[-1] - a*y[-1].
                const float y = a * v + apX1[c][s] - a * apY1[c][s];
                apX1[c][s] = v;
                apY1[c][s] = y;
                v = y;
            }

            // Classic phaser sum. The feedback tap is taken HERE, from
            // the summed output, not from the raw chain end: with a
            // chain-only tap, feedback pulls the notch zeros off the
            // unit circle and the notches fill in; tapping the sum
            // keeps the zeros at H_ap = -1 (notches stay deep) and
            // turns the feedback into pure resonant peaks - the
            // classic regenerative-phaser behaviour.
            const float classic = 0.5f * (x + v);
            fbState[c] = classic;
            float y = ((1.0f - mix) * x + mix * classic) * outGain;

            // Bypass crossfade against the untouched input.
            if (bypassLevel < 1.0f)
                y = x * (1.0f - bypassLevel) + y * bypassLevel;

            if (c == 0) { outL[i] = y; mLastL = m; }
            else        { outR[i] = y; mLastR = m; }
        }

        phase1 += phaseInc1;
        if (phase1 >= 1.0) phase1 -= 1.0;
        phase2 += phaseInc2;
        if (phase2 >= 1.0) phase2 -= 1.0;
    }

    modValueL.store (mLastL);
    modValueR.store (mLastR);
}

//==============================================================================
juce::AudioProcessorEditor* YMVortexProcessor::createEditor()
{
    return new YMVortexEditor (*this);
}

//==============================================================================
void YMVortexProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void YMVortexProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new YMVortexProcessor();
}
