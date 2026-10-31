#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include "Bank.h"
#include "Engine.h"
#include "Sequencer.h"

class YM303Processor : public juce::AudioProcessor,
                       private juce::AudioProcessorValueTreeState::Listener,
                       private juce::AsyncUpdater
{
public:
    YM303Processor();
    ~YM303Processor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "YM303"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    ym303::Bank bank;

    // read by a future GUI: position inside the flat sequence of the step that played last (-1 = stopped)
    int currentStep() const { return seq.currentStep(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& id, float newValue) override;
    void handleAsyncUpdate() override;
    void refreshEditView();                 // copies the selected pattern section into the step parameters
    int  selectedPattern() const;
    int  editSection() const;

    std::unique_ptr<ym303::Engine> engine;
    ym303::Sequencer seq { bank };
    ym303::EventList events;
    double sampleRate = 48000;

    std::atomic<float> *pTuning, *pCutoff, *pReso, *pEnvmod, *pDecay, *pAccent, *pVolume, *pWave, *pTempo, *pRun, *pHostSync,
                       *pPlayMode, *pGroup, *pPattern, *pSection;
    std::atomic<bool> refreshing { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YM303Processor)
};
