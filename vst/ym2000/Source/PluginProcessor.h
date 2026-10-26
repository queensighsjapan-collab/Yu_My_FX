#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <bitset>
#include <memory>
#include <vector>
#include "Engine.h"
#include "Fx.h"
#include "Params.h"

class YM2000Processor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    YM2000Processor();
    ~YM2000Processor() override { keyboardState.removeListener (&midiCollector); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "YM2000"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return ym::kNumFactory; }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- GUI hooks (message thread -> audio thread through the collector; audio -> GUI through atomics)
    juce::MidiKeyboardState keyboardState;
    juce::MidiMessageCollector midiCollector;
    std::atomic<float> uiBend { 0.0f }, uiMod { 0.0f };
    std::atomic<int> uiSeqPos { -1 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void handleAsyncUpdate() override;
    void loadFactory (int index);

    // ---- note / arpeggiator / mod-sequence state (audio thread only)
    void handleMidi (const juce::MidiMessage& m);
    void noteOn (int note, int vel);
    void noteOff (int note);
    void pedal (bool on);
    void allNotesOff();
    void arpStop();
    void arpStep (double stepSamples);
    void seqStep();
    void clockStart();
    void clockTick();
    bool clockWanted() const;
    void engOn (int note, int vel);
    void engOff (int note);
    void readParams();
    bool isHeld (int note) const { return phys.test ((size_t) note); }

    struct Binding { std::atomic<float>* raw; int engIdx; };
    std::vector<Binding> engineBindings;
    std::atomic<float>* seqRaw[16] = {};
    std::atomic<float>* vlevRaw[16] = {};
    std::atomic<float>* vpanRaw[16] = {};
    struct FxRaw
    {
        std::atomic<float> *vol, *ai1, *ai2, *vmode, *vfs, *vcl, *mft, *mfs, *mfd, *dt, *dtm, *ddp, *eqhf, *eqhg, *eqlf, *eqlg;
        std::atomic<float> *arpon, *arplt, *arpt, *arpr, *arpg, *arptm, *msq, *msk;
    } fr {};

    std::unique_ptr<ym::Engine> engine;
    ym::FxChain fx;
    ym::FxParams fxp;
    double sampleRate = 48000;
    int blockCap = 0;
    std::vector<float> engL, engR, engIn, srcMono, outL, outR;

    // controller state
    double bendVal = 0, modVal = 0;
    std::bitset<128> phys, sustained;
    bool sus = false;
    std::vector<int> arpHeld, arpCur, pool;
    int arpIdx = 0, arpVel = 100, seqIdx = 0;
    bool clockRunning = false;
    double clockCountdown = 0, arpOffCountdown = -1;
    bool lastArpOn = false, lastArpLatch = false, lastMsq = false;
    bool arpOn = false, arpLatch = false, seqOn = false, seqKeySync = false;
    int arpType = 0, arpRange = 1;
    double arpGate = 64, arpTempo = 120;
    int seqVals[16] = {};
    juce::Random rng;

    std::atomic<int> pendingProgram { -1 };
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YM2000Processor)
};
