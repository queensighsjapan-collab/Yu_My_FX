#pragma once

#include <JuceHeader.h>

//==============================================================================
/** YM Vortex - a multi-stage phaser.

    Each channel runs its own cascade of first-order allpass stages
    (STAGES = 4 / 6 / 8 / 12, giving stages/2 moving notches). The stage
    centre frequencies are spaced geometrically over a 3-octave span
    around CENTRE and are all swept together: every centre is multiplied
    by 2^(R x m), where m is the combined modulation below and R is the
    sweep half-range in octaves: R = 1.5 x DEPTH up to DEPTH = 0.66
    (exactly the v1.0 curve), then a smooth quadratic knee stretches R
    to 2.0 at DEPTH = 1, so the maximum notch travel grows from 8x to
    16x (v1.1).

    The modulation is the point of the design. LFO1 (RATE, sine or
    triangle) is joined by LFO2, which runs at RATE x RATIO and is mixed
    in with DEPTH2. With an irrational-ish RATIO (1.618 and friends) the
    sum of the two sines never repeats, so the notch pattern keeps
    evolving - a complex swirl instead of a plain back-and-forth whoosh.
    m is normalised by (1 + DEPTH2) so it stays inside +-1.

    WIDTH phase-offsets the right channel's copy of BOTH LFOs
    (0 deg = mono-linked sweep, 90 = quadrature stereo phaser,
    180 = the two channels sweep in opposite directions).

    FEEDBACK returns the classic dry+chain sum (previous sample) to the
    chain input. The return path is soft-clipped with a gain-staged
    tanh, 2*tanh(x/2) (headroom to +-2; a plain tanh(x) saturated on
    the loop signal and capped the resonance - v1.1 fix), and then
    highpassed by a gentle one-pole highpass (~25 Hz) so low-end rumble
    cannot accumulate at high feedback (v1.1). The amount is signed
    (+-0.97 since v1.1, was +-0.85), so negative values give the other
    classic phaser flavour. Because the loop's peak gain is |fb| x max
    |0.5 (1 + H_ap)| = |fb| < 1 by construction, it rings and growls
    through the tanh but can never run away.

    The audible output is the classic dry + phase-shifted sum, and MIX
    crossfades between the pure dry signal and that sum.

    MIDI bypass (YM house standard):
      bypass   bool - true = bypassed (effect off). Also returned from
                      getBypassParameter() so host bypass buttons work.
      bypassCc int  - CC number that drives it (default 65; 0 = Off).
                      A matching CC with value >= 64 turns the effect
                      ON (bypass off); below 64 turns it OFF. A Learn
                      mode armed from the editor captures the next
                      controller number into bypassCc.
    Bypass crossfades over ~15 ms; while fully bypassed the chain is
    skipped entirely and the input passes through untouched (the LFOs
    keep running, like an analogue unit left switched in).

    Parameters (AudioProcessorValueTreeState):
      rate     0.02..8 Hz   LFO1 rate (log)
      depth    0..1         LFO1 sweep amount (+-1.5 octaves at 0.66,
                            kneeing up to +-2 octaves at 1)
      ratio    0.25..4      LFO2 rate = LFO1 rate x ratio
      depth2   0..1         LFO2 mix amount
      centre   100..4000 Hz geometric centre of the stage span (log)
      feedback -0.97..+0.97 signed feedback (was +-0.85 in v1.0)
      stages   choice       4 / 6 / 8 / 12 allpass stages
      width    0..180 deg   L/R modulation phase offset
      wave     choice       LFO1 waveform: Sine / Triangle
      mix      0..1         dry vs classic phaser sum
      output   -24..+12 dB
      bypass   bool         house-standard MIDI-controllable bypass
      bypassCc 0..127       CC number for bypass (default 65, 0 = Off)
*/
class YMVortexProcessor : public juce::AudioProcessor
{
public:
    YMVortexProcessor();
    ~YMVortexProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    //==========================================================================
    const juce::String getName() const override            { return JucePlugin_Name; }
    bool acceptsMidi() const override                        { return true; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 0.5; }

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

    //==========================================================================
    /** MIDI Learn target (only the bypass assignment is learnable).
        Written from the message thread, read on the audio thread. */
    enum class LearnTarget { none = 0, bypass = 1 };

    LearnTarget getLearnTarget() const noexcept
    {
        return static_cast<LearnTarget> (learnTarget.load());
    }

    void setLearnTarget (LearnTarget target) noexcept
    {
        learnTarget.store (static_cast<int> (target));
    }

    /** Assignment setter (also used by the editor's right-click "Off").
        Writes through the parameter, so the choice is stored in the
        plugin state and the host is notified. */
    void setBypassCcNumber (int ccNumber);

    /** Current combined modulation value per channel, -1..+1, for the
        editor's sweep meter. Written once per block on the audio thread. */
    float getModValueL() const noexcept { return modValueL.load(); }
    float getModValueR() const noexcept { return modValueR.load(); }

    static constexpr int maxStages = 12;
    static int stageCountForIndex (int index) noexcept
    {
        static constexpr int counts[4] = { 4, 6, 8, 12 };
        return counts[juce::jlimit (0, 3, index)];
    }

private:
    //==========================================================================
    void handleMidiMessages (const juce::MidiBuffer& midiMessages);
    float lfoWave (double phaseCycles, bool triangle) const noexcept;

    std::atomic<float>* rateParam     = nullptr;
    std::atomic<float>* depthParam    = nullptr;
    std::atomic<float>* ratioParam    = nullptr;
    std::atomic<float>* depth2Param   = nullptr;
    std::atomic<float>* centreParam   = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* stagesParam   = nullptr;
    std::atomic<float>* widthParam    = nullptr;
    std::atomic<float>* waveParam     = nullptr;
    std::atomic<float>* mixParam      = nullptr;
    std::atomic<float>* outputParam   = nullptr;
    std::atomic<float>* bypassParam   = nullptr;
    std::atomic<float>* bypassCcParam = nullptr;

    // Typed parameter objects for setValueNotifyingHost-style writes
    // (MIDI CC input and MIDI Learn run on the audio thread).
    juce::AudioParameterBool* bypassBoolParam  = nullptr;
    juce::AudioParameterInt*  bypassCcIntParam = nullptr;

    std::atomic<int> learnTarget { 0 };   // LearnTarget, as int
    std::atomic<float> modValueL { 0.0f };
    std::atomic<float> modValueR { 0.0f };

    // Phaser state (per channel, per stage): previous input / output of
    // each one-pole allpass cell, plus the feedback return sample.
    float apX1[2][maxStages] = {};
    float apY1[2][maxStages] = {};
    float fbState[2] = {};
    // Lowpass state of the one-pole highpass in the feedback return
    // (return = clipped - lp). Alpha is set in prepareToPlay (~25 Hz).
    float fbLpState[2] = {};
    double fbHpAlpha = 0.0;
    int lastStageCount = -1;

    // Free-running LFO phases, in cycles.
    double phase1 = 0.0;
    double phase2 = 0.0;

    // Bypass crossfade: 1 = effect active, 0 = bypassed.
    float bypassLevel = 1.0f;
    float bypassStep = 0.0f;   // per-sample step for a ~15 ms ramp

    // Smoothed control values.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothRate;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothDepth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothRatio;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothDepth2;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothCentre;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothFeedback;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothWidth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothOutGain;

    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YMVortexProcessor)
};
