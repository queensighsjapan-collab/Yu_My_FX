#pragma once

#include <JuceHeader.h>

//==============================================================================
/** YM Echo - a deliberately small stereo delay effect.

    It exists as a known-good test plugin for YM ORPHEUS's VST3 hosting:
    if YM Echo echoes, the host works. Original code.

    Parameters (AudioProcessorValueTreeState):
      time      10..1500 ms   delay time (smoothed, zipper-noise free)
      feedback  0.0..0.85     repeats amount, damped by a one-pole lowpass
      mix       0.0..1.0      wet level added to the dry signal
      pingpong  on/off        feedback crosses L <-> R
*/
class YMEchoProcessor : public juce::AudioProcessor
{
public:
    YMEchoProcessor();
    ~YMEchoProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    //==========================================================================
    const juce::String getName() const override            { return JucePlugin_Name; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 2.0; }

    //==========================================================================
    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    std::atomic<float>* timeParam     = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* mixParam      = nullptr;
    std::atomic<float>* pingPongParam = nullptr;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDelaySamples;

    float lowpassState[2] = { 0.0f, 0.0f };   // feedback-loop damping, per channel
    double currentSampleRate = 44100.0;

    float dampOnePole (int channel, float input)
    {
        // y += c * (x - y) with c = 0.35: gentle darkening of the repeats.
        lowpassState[channel] += 0.35f * (input - lowpassState[channel]);
        return lowpassState[channel];
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YMEchoProcessor)
};
