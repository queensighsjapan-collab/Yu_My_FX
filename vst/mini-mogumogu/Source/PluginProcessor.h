#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "Engine.h"
#include "Spec.h"
#include "Presets.h"

class MoguProcessor : public juce::AudioProcessor,
                      private juce::AudioProcessorValueTreeState::Listener,
                      private juce::Timer
{
public:
    MoguProcessor();
    ~MoguProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "mini mogumogu"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return mm::kNumPresets; }
    int getCurrentProgram() override { return program; }
    void setCurrentProgram (int i) override;
    const juce::String getProgramName (int i) override { return (i >= 0 && i < mm::kNumPresets) ? mm::kPresets[i].name : ""; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyState;

    // ---- hooks for the editor
    int  currentStep() const   { return curStep.load(); }        // -1 when stopped
    bool seqRunning() const    { return running.load(); }
    bool hostDriving() const   { return hostDriven.load(); }
    int  transpose() const     { return trans.load(); }
    float bendShown() const    { return bendNow.load(); }
    float wheelShown() const   { return wheelNow.load(); }
    void setBendUI (float x)   { uiBend.store (x); uiBendDirty.store (true); }
    void setWheelUI (float x)  { uiWheel.store (x); uiWheelDirty.store (true); }
    // last output samples for the scope (written by the audio thread, read by the editor)
    static constexpr int kScope = 2048;
    std::array<float, kScope> scopeBuf {};
    std::atomic<int> scopeW { 0 };
    void panic()               { panicReq.store (true); }
    void randomizeSteps();
    void resetSteps();
    float value (const char* id) const;
    void setValue (const char* id, float real);       // real units, notifies the host
    int  presetIndex() const   { return program; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& id, float newValue) override;
    void timerCallback() override;
    void buildIR();
    void fillParams (mm::Params& p, double bpm) const;

    mm::Engine engine;
    double sampleRate = 48000;
    int program = 0;

    // reverb
    juce::dsp::Convolution conv;
    juce::AudioBuffer<float> wet, scratch;
    float rvGain = 0.f; int rvTail = 0, rvLen = 0; bool rvWasOn = false;
    std::atomic<bool> irDirty { false };
    float irBuiltSize = -1.f;

    // parameter access
    std::array<std::atomic<float>*, mm::kNumSpecs> raw {};
    std::atomic<float>* stOn[16]; std::atomic<float>* stNote[16]; std::atomic<float>* stTie[16];
    std::atomic<float>& R (const char* id) const { return *raw[(size_t) mm::specIndex (id)]; }

    // live notes / sequencer
    std::vector<int> held;
    int curNote = -1;
    bool seqWasOn = false, prevTie = false;
    int seqIdx = 0, seqPP = 1; long long seqCount = 0;
    double seqNext = 0;                 // internal clock: next step start in absolute samples
    long long absPos = 0;
    long long pendingOff = -1;
    int curBlockN = 0;
    std::atomic<int> curStep { -1 }, trans { 0 };
    std::atomic<bool> running { false }, hostDriven { false };
    std::atomic<float> uiBend { 0.f }, uiWheel { 0.f }, bendNow { 0.f }, wheelNow { 0.f };
    std::atomic<bool> uiBendDirty { false }, uiWheelDirty { false }, panicReq { false };
    uint32_t rndState = 12345u;

    struct Ev { int pos; int type; int a; float b; };   // 0 key on, 1 key off, 2 bend, 3 wheel, 4 all off, 5 seq on, 6 seq off
    std::vector<Ev> evs;
    int seqPick (long long n, int len);
    void seqFire (int pos, int idx, long long startAbs, double durSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MoguProcessor)
};
