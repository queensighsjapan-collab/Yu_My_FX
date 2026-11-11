#pragma once

#include <JuceHeader.h>

//==============================================================================
/** YM Mirage - a granular cloud effect.

    The stereo input is written continuously into a 4-second circular
    buffer (two channels). A scheduler spawns short grains from a
    preallocated pool (no allocation or locking in processBlock). Each
    grain re-reads the buffer's MID signal ((L + R) / 2) at a rate set by
    the PITCH parameter (+-12 semitones) with a little random detune,
    through a Hann (sin^2) window, and is panned out at a random
    equal-power position. The wet path therefore collapses the input to
    mono before re-spreading it - the classic cloud sound - while the
    DRY path stays true stereo.

    A grain's read anchor sits behind the write head by at least the
    input span the grain will consume (length x rate), so a grain can
    never overtake the write head; SMEAR adds a random extra offset of
    up to 2 seconds of history on top of that anchor. The anchor offset
    is also the latency reported to the host (it follows SIZE and
    PITCH), so a host with delay compensation (e.g. YM ORPHEUS's PDC)
    keeps the dry and wet paths aligned.

    FREEZE stops the write head: the buffer content is preserved and
    grains keep spawning and reading from the frozen material forever,
    sustaining the captured moment as an endless pad. The dry path is
    unaffected (it still follows MIX). Grains are windowed, so toggling
    FREEZE does not click.

    Parameters (AudioProcessorValueTreeState):
      sizeMs   10..200 ms   grain length
      density  0..1         mapped to ~2..80 grains/sec (exponential)
      pitch    -12..+12 st  grain playback-rate shift
      spread   0..1         random pan width + detune amount
      smear    0..1         random read-position offset, up to +2 s
      freeze   bool         hold the buffer / sustain forever
      mix      0..1         dry/wet crossfade
      output   -24..+12 dB
      pitchCc  0..127       MIDI CC number driving PITCH (default 1,
                            the modulation wheel; 0 = Off)
      freezeCc 0..127       MIDI CC number driving FREEZE (default 64,
                            the sustain pedal; 0 = Off)
      bypass   bool         true = effect bypassed (default false)
      bypassCc 0..127       MIDI CC number driving the bypass switch
                            (default 65; 0 = Off)

    MIDI input (v1.1, any channel): a controller matching pitchCc maps
    its value linearly onto -12..+12 st (values 62..65 are a centre
    dead-zone that lands on exactly 0 st); a controller matching
    freezeCc switches FREEZE on at value >= 64 and off below that
    (momentary pedal semantics); a controller matching bypassCc turns
    the effect ON at value >= 64 and bypasses it below that. All of
    them write through to the parameters themselves, so the GUI and the
    host follow along and the usual smoothing glides the change. A MIDI
    Learn mode (armed from the editor) captures the next controller
    number into pitchCc / freezeCc / bypassCc. The bypass parameter is
    also the host bypass parameter (getBypassParameter), and engaging
    it crossfades to the dry input over ~15 ms while the write head
    keeps running, so un-bypassing never resurrects ancient audio.
*/
class YMMirageProcessor : public juce::AudioProcessor
{
public:
    YMMirageProcessor();
    ~YMMirageProcessor() override;

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
    bool acceptsMidi() const override                        { return true; }
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

    //==========================================================================
    juce::AudioProcessorParameter* getBypassParameter() const override
    {
        return bypassBoolParam;
    }

    //==========================================================================
    /** MIDI Learn target. The editor arms one of these; the next
        controller event the processor receives (CC 1..127) is written
        into the matching assignment parameter and the mode disarms.
        Written from the message thread, read on the audio thread. */
    enum class LearnTarget { none = 0, pitch = 1, freeze = 2, bypass = 3 };

    LearnTarget getLearnTarget() const noexcept
    {
        return static_cast<LearnTarget> (learnTarget.load());
    }

    void setLearnTarget (LearnTarget target) noexcept
    {
        learnTarget.store (static_cast<int> (target));
    }

    /** Assignment setters (also used by the editor's right-click "Off").
        They write through the parameters, so the choice is stored in
        the plugin state and the host is notified. */
    void setPitchCcNumber (int ccNumber);
    void setFreezeCcNumber (int ccNumber);
    void setBypassCcNumber (int ccNumber);

    /** Drives the bypass parameter (the editor's ON button). */
    void setBypassed (bool shouldBeBypassed);

    static constexpr int maxGrains = 96;
    static constexpr double bufferSeconds = 4.0;

    /** Density knob (0..1) -> grains per second, ~2..80, exponential. */
    static float grainsPerSecond (float density01)
    {
        return 2.0f * std::pow (40.0f, juce::jlimit (0.0f, 1.0f, density01));
    }

private:
    //==========================================================================
    struct Grain
    {
        bool active = false;
        double readPos = 0.0;        // fractional position in the ring
        double rate = 1.0;           // read advance per output sample
        int length = 0;              // lifetime in output samples
        int age = 0;
        float windowPhaseInc = 0.0f; // pi / length (sin^2 window)
        float gainL = 0.0f, gainR = 0.0f;   // equal-power pan gains
    };

    void spawnGrain (float sizeMs, float pitchSt, float spread, float smear);
    float readMid (double pos) const noexcept;
    int computeLatencySamples (float sizeMs, float pitchSt) const noexcept;
    void refreshLatency();
    void handleMidiMessages (const juce::MidiBuffer& midiMessages);

    std::atomic<float>* sizeParam    = nullptr;
    std::atomic<float>* densityParam = nullptr;
    std::atomic<float>* pitchParam   = nullptr;
    std::atomic<float>* spreadParam  = nullptr;
    std::atomic<float>* smearParam   = nullptr;
    std::atomic<float>* freezeParam  = nullptr;
    std::atomic<float>* mixParam     = nullptr;
    std::atomic<float>* outputParam  = nullptr;
    std::atomic<float>* pitchCcParam  = nullptr;
    std::atomic<float>* freezeCcParam = nullptr;
    std::atomic<float>* bypassParam   = nullptr;
    std::atomic<float>* bypassCcParam = nullptr;

    // Typed parameter objects for setValueNotifyingHost-style writes
    // (MIDI CC input and MIDI Learn run on the audio thread).
    juce::AudioParameterFloat* pitchFloatParam  = nullptr;
    juce::AudioParameterBool*  freezeBoolParam  = nullptr;
    juce::AudioParameterInt*   pitchCcIntParam  = nullptr;
    juce::AudioParameterInt*   freezeCcIntParam = nullptr;
    juce::AudioParameterBool*  bypassBoolParam  = nullptr;
    juce::AudioParameterInt*   bypassCcIntParam = nullptr;

    std::atomic<int> learnTarget { 0 };   // LearnTarget, as int

    // Circular input buffer (stereo) and grain pool - all preallocated.
    juce::AudioBuffer<float> ring;
    int ringSize = 0;
    int writeHead = 0;
    float* ringL = nullptr;
    float* ringR = nullptr;

    std::array<Grain, maxGrains> grains;
    double spawnAccumulator = 0.0;
    juce::Random random;

    // Smoothed control values.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothSize;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothDensity;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothPitch;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothSpread;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothSmear;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothOutGain;
    // 1 = effect fully engaged, 0 = fully bypassed; ~15 ms ramp drives
    // an equal-power crossfade between the effect output and the dry
    // input, so engaging/disengaging bypass never clicks.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothBypass;

    int lastLatencySamples = -1;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YMMirageProcessor)
};
