#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
YMEchoProcessor::YMEchoProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    timeParam     = apvts.getRawParameterValue ("time");
    feedbackParam = apvts.getRawParameterValue ("feedback");
    mixParam      = apvts.getRawParameterValue ("mix");
    pingPongParam = apvts.getRawParameterValue ("pingpong");
}

YMEchoProcessor::~YMEchoProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout YMEchoProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "time", "Time",
        juce::NormalisableRange<float> (10.0f, 1500.0f, 0.1f),
        375.0f, "ms"));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "feedback", "Feedback",
        juce::NormalisableRange<float> (0.0f, 0.85f, 0.01f),
        0.35f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
        0.30f));

    params.push_back (std::make_unique<juce::AudioParameterBool>(
        "pingpong", "Ping-Pong", false));

    return { params.begin(), params.end() };
}

//==============================================================================
void YMEchoProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    const auto numChannels = (juce::uint32) juce::jmin (2, getTotalNumInputChannels());

    // 2 seconds of delay is the ceiling (the time parameter tops out at 1.5 s).
    delayLine.setMaximumDelayInSamples ((int) std::ceil (sampleRate * 2.0) + 8);

    juce::dsp::ProcessSpec spec { sampleRate,
                                  (juce::uint32) juce::jmax (1, samplesPerBlock),
                                  numChannels };
    delayLine.prepare (spec);
    delayLine.reset();

    lowpassState[0] = lowpassState[1] = 0.0f;

    smoothedDelaySamples.reset (sampleRate, 0.05);
    const auto initialMs = timeParam->load();
    smoothedDelaySamples.setCurrentAndTargetValue ((float) (initialMs * 0.001 * sampleRate));
}

void YMEchoProcessor::releaseResources()
{
}

bool YMEchoProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    // An effect: what comes in must match what goes out.
    if (layouts.getMainInputChannelSet() != out)
        return false;

    return true;
}

//==============================================================================
void YMEchoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numChannels = juce::jmin (buffer.getNumChannels(),
                                         getTotalNumInputChannels());
    const auto numSamples  = buffer.getNumSamples();

    if (numChannels <= 0)
        return;

    const float feedback  = feedbackParam->load();
    const float mix       = mixParam->load();
    const bool  pingPong  = pingPongParam->load() > 0.5f;
    const float maxDelay  = (float) (currentSampleRate * 2.0);

    smoothedDelaySamples.setTargetValue (juce::jlimit (1.0f, maxDelay,
                                         timeParam->load() * 0.001f * (float) currentSampleRate));

    auto* channelData0 = buffer.getWritePointer (0);
    auto* channelData1 = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    if (channelData1 != nullptr)
    {
        // Stereo (ping-pong capable) path.
        for (int i = 0; i < numSamples; ++i)
        {
            const float delay = smoothedDelaySamples.getNextValue();

            const float inL = channelData0[i];
            const float inR = channelData1[i];

            const float delL = delayLine.popSample (0, delay);
            const float delR = delayLine.popSample (1, delay);

            const float fbL = dampOnePole (0, delL);
            const float fbR = dampOnePole (1, delR);

            delayLine.pushSample (0, inL + (pingPong ? fbR : fbL) * feedback);
            delayLine.pushSample (1, inR + (pingPong ? fbL : fbR) * feedback);

            channelData0[i] = inL + delL * mix;
            channelData1[i] = inR + delR * mix;
        }
    }
    else
    {
        // Mono path.
        for (int i = 0; i < numSamples; ++i)
        {
            const float delay = smoothedDelaySamples.getNextValue();

            const float in  = channelData0[i];
            const float del = delayLine.popSample (0, delay);
            const float fb  = dampOnePole (0, del);

            delayLine.pushSample (0, in + fb * feedback);

            channelData0[i] = in + del * mix;
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* YMEchoProcessor::createEditor()
{
    return new YMEchoEditor (*this);
}

//==============================================================================
void YMEchoProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void YMEchoProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new YMEchoProcessor();
}
