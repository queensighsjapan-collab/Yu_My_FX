#pragma once

#include <JuceHeader.h>

//==============================================================================
/** YM Choir - a voice-multiplier (choir-izer) effect.

    One voice goes in, a choir comes out. The stereo input's MID signal
    is written continuously into a 1-second ring buffer. Up to 8 choir
    voices read that buffer back through classic dual-tap crossfading
    delay-line pitch shifters: two read taps half a window apart, Hann
    (sin^2) crossfade weights that always sum to 1, and a sawtooth
    phase advancing at (ratio - 1) / windowLength per sample. The
    window is 4096 samples at 48 kHz (scaled by the host rate,
    ~85 ms - a good vocal grain), and the taps read behind the write
    head by up to one window plus the voice's timing offset, so the
    latency reported to the host is exactly the window length.

    Each voice has a FIXED character derived from its index (no runtime
    randomness): a detune fraction of the DETUNE knob spread across
    +-DETUNE cents, a timing fraction of the TIMING knob added to its
    base delay (human looseness), its own vibrato LFO in the
    3.5-6.5 Hz range with depth from the VIBRATO knob (up to
    +-15 cents), a pan position spread across the stereo field by
    SPREAD (centre voices slightly louder, edge voices slightly
    darker through per-voice one-pole lowpasses under the global TONE
    tilt), and a gain normalised by 1/sqrt(active voices).

    This is unison thickening, NOT harmony generation: every copy stays
    at the original pitch (plus a few cents of detune), so no thirds
    or fifths appear - the source simply sounds sung by several people
    at once. MIX blends the dry signal against the choir bus; the dry
    path stays true stereo while the choir bus is built from the mid
    signal and re-spread by the voice pans.

    Parameters (AudioProcessorValueTreeState):
      voices   1..8         number of added choir voices (default 4)
      detune   0..60 cents  per-voice detune spread (default 18)
      timing   0..30 ms     per-voice timing looseness (default 12)
      vibrato  0..1         per-voice vibrato depth (default 0.4)
      spread   0..1         stereo spread of the voices (default 0.7)
      tone     -1..+1       global lowpass tilt (default 0)
      mix      0..1         dry / choir-bus blend (default 0.45)
      output   -24..+12 dB
      bypass   bool         true = effect bypassed (default false)
      bypassCc 0..127       MIDI CC number driving the bypass switch
                            (default 65; 0 = Off)

    MIDI bypass is the YM house standard (かいさん, 2026-11-10): a
    controller matching bypassCc turns the effect ON at value >= 64
    and bypasses it below that, writing through to the bypass
    parameter itself (also returned from getBypassParameter, so host
    bypass buttons drive the same switch). Engaging bypass crossfades
    to the dry input over ~15 ms; while bypassed the ring buffer keeps
    being written, so un-bypassing never resurrects ancient audio. A
    MIDI Learn mode (armed from the editor) captures the next
    controller number into bypassCc.

    No allocation or locking happens in processBlock; voice levels ramp
    in and out over ~30 ms when VOICES changes, so adding a singer
    never clicks.
*/
class YMChoirProcessor : public juce::AudioProcessor
{
public:
    YMChoirProcessor();
    ~YMChoirProcessor() override;

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
    double getTailLengthSeconds() const override             { return 0.3; }

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
    /** MIDI Learn target. The editor arms it; the next controller
        event the processor receives (CC 1..127) is written into the
        bypassCc assignment parameter and the mode disarms. Written
        from the message thread, read on the audio thread. */
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

    /** Drives the bypass parameter (the editor's ON button). */
    void setBypassed (bool shouldBeBypassed);

    static constexpr int maxVoices = 8;
    static constexpr double bufferSeconds = 1.0;

    /** Pitch-shifter window: 4096 samples at 48 kHz (~85 ms), scaled
        by the host sample rate in prepareToPlay. */
    static constexpr double referenceWindowSamples = 4096.0;
    static constexpr double referenceRate = 48000.0;

private:
    //==========================================================================
    struct Voice
    {
        double phase = 0.0;        // sawtooth shifter phase, 0..1
        double vibPhase = 0.0;     // vibrato LFO phase, radians
        float lpState = 0.0f;      // per-voice one-pole lowpass state
        float level = 0.0f;        // fade in/out when VOICES changes

        // Recomputed per block from the smoothed parameters:
        float baseCents = 0.0f;    // fixed detune (vibrato added live)
        double timingSamples = 0.0;
        float panL = 0.7071f, panR = 0.7071f;
        float gain = 0.0f;
        float lpCoeff = 1.0f;
        double vibInc = 0.0;       // vibrato phase step per sample
    };

    float readRing (double delaySamples) const noexcept;
    void updateVoiceParameters (int activeVoices, float detuneCents, float timingMs,
                                float spread, float tone);
    void handleMidiMessages (const juce::MidiBuffer& midiMessages);

    std::atomic<float>* voicesParam  = nullptr;
    std::atomic<float>* detuneParam  = nullptr;
    std::atomic<float>* timingParam  = nullptr;
    std::atomic<float>* vibratoParam = nullptr;
    std::atomic<float>* spreadParam  = nullptr;
    std::atomic<float>* toneParam    = nullptr;
    std::atomic<float>* mixParam     = nullptr;
    std::atomic<float>* outputParam  = nullptr;
    std::atomic<float>* bypassParam   = nullptr;
    std::atomic<float>* bypassCcParam = nullptr;

    // Typed parameter objects for setValueNotifyingHost-style writes
    // (MIDI CC input and MIDI Learn run on the audio thread).
    juce::AudioParameterBool* bypassBoolParam  = nullptr;
    juce::AudioParameterInt*  bypassCcIntParam = nullptr;

    std::atomic<int> learnTarget { 0 };   // LearnTarget, as int

    // Ring buffer holding the input's mid signal - preallocated.
    std::vector<float> ring;
    int ringSize = 0;
    int writeHead = 0;
    int windowSamples = 4096;   // latency reported to the host

    std::array<Voice, maxVoices> voices;

    // Fixed per-voice character patterns, indexed by voice slot.
    static constexpr std::array<float, maxVoices> detunePattern
        { -0.8f, 0.6f, -0.3f, 1.0f, -1.0f, 0.35f, -0.6f, 0.85f };
    static constexpr std::array<float, maxVoices> timingPattern
        { 0.9f, 0.2f, 0.65f, 0.4f, 1.0f, 0.1f, 0.8f, 0.5f };
    static constexpr std::array<double, maxVoices> phasePattern
        { 0.0, 0.41, 0.73, 0.18, 0.58, 0.88, 0.33, 0.64 };

    // Smoothed control values.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothDetune;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothTiming;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothVibrato;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothSpread;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothTone;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothOutGain;
    // 1 = effect fully engaged, 0 = fully bypassed; ~15 ms ramp drives
    // an equal-power crossfade to the dry input.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothBypass;

    double currentSampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YMChoirProcessor)
};
