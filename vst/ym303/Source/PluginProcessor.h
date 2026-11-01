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

    // ---- hooks for the editor
    int  currentStep() const  { return seq.currentStep(); }
    int  playingEntry() const { return playing.load() ? seq.currentEntry() : -1; }   // pattern*32+section*16+step, -1 = not playing
    bool isPlayingNow() const { return playing.load(); }
    bool hostIsDriving() const { return hostDriven.load(); }          // true while the DAW transport is the clock
    void previewNote (int midiNote, bool accent);                     // audition (ignored while the sequencer plays)
    void refreshEditView();                                           // copies the selected pattern section into the step parameters
    int  selectedPattern() const;
    int  editSection() const;

    // edit-panel state that survives closing the editor (message thread only, not saved in the project)
    struct UiState { bool write = true; bool pitch = true; bool time = false; int cursor = 0; } ui;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& id, float newValue) override;
    void handleAsyncUpdate() override;
    std::unique_ptr<ym303::Engine> engine;
    ym303::Sequencer seq { bank };
    ym303::EventList events;
    double sampleRate = 48000;

    std::atomic<float> *pTuning, *pCutoff, *pReso, *pEnvmod, *pDecay, *pAccent, *pVolume, *pWave, *pTempo, *pRun, *pHostSync,
                       *pPlayMode, *pGroup, *pPattern, *pSection;
    std::atomic<bool> refreshing { false };
    std::atomic<bool> playing { false }, hostDriven { false };
    std::atomic<int> previewReq { -1 };       // midi note | 0x100 if accent, -1 = none
    int previewOffLeft = 0;                    // samples until the audition note is released

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YM303Processor)
};
