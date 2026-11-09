#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include <vector>
#include "Engine.h"

class YumyBacteriaProcessor : public juce::AudioProcessor
{
public:
    YumyBacteriaProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "YuMy Bacteria"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return YB_NUM_FACTORY; }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void readParams();
    void handleMidi (const juce::MidiMessage& m);

    std::unique_ptr<yb::Core> core;
    std::atomic<float>* pRaw[yb::NUM_P] = {};
    std::atomic<float>* oRaw[yb::NUM_O] = {};
    std::atomic<float>* ldRaw[3] = {};
    std::atomic<float>* mxSrc[6] = {};
    std::atomic<float>* mxDst[6][3] = {};
    std::atomic<float>* mxAmt[6][3] = {};
    std::atomic<float>* hostSyncRaw = nullptr;

    double sampleRate = 48000;
    int blockCap = 0;
    std::vector<float> engL, engR, modBuf;
    const float* modPtr = nullptr;
    bool haveMod = false;
    double bendV = 0, modW = 0, aftV = 0;
    bool wasPlaying = false;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YumyBacteriaProcessor)
};
