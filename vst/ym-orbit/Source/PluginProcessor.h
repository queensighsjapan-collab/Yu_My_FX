#pragma once

#include <JuceHeader.h>
#include "SchroederReverb.h"

//==============================================================================
/** YM Orbit - a 3D spatial panner in the RSS / spectral-panning family.

    The stereo input is treated as a point source: its mid (mono sum) is
    rendered binaurally and its side (stereo difference) can be blended
    back with the WIDTH parameter so stereo material does not fully
    collapse. Per sample/block the renderer applies:

      - distance gain (1 / (1 + metres)) + air-absorption lowpass
      - a simplified pinna peak whose frequency/gain follow elevation
      - an interaural time difference (fractional delay on the far ear)
      - head shadow: a high-shelf cut + level drop on the far ear,
        and a small presence lift on the near ear
      - a small original Schroeder reverb whose wet level also grows
        a little with distance

    All of these are approximations of real HRTF cues — see README.md.
    v2 (1.1.0) deliberately exaggerated every cue beyond the physical
    values; v3 (1.2.0) pushes them further still (ITD 1.1 ms, head
    shadow -16 dB shelf / -16 dB level, pinna +6 / -4.5 dB): the effect
    is meant to be clearly audible, not accurate.
    Azimuth is smoothed as (sin, cos) so sweeps across the +/-180 degree
    wrap take the short way around. Original code only.

    Parameters (AudioProcessorValueTreeState):
      azimuth    -180..180 deg   (0 = front, + = right)
      elevation  -40..+60 deg
      distance   0.0..1.0        (mapped to 0.3..8.0 metres internally)
      width      0.0..1.0        (original stereo-difference blend)
      room       0.0..1.0        (reverb wet scale)
      output     -24..+12 dB
      motionMode choice          Off / Circle / Wander (auto-movement)
      motionRate 0.05..3.0 Hz    (motion speed; v3 extended the top end)
      motionDepth 0.0..1.0       (motion amount; 0 = still)

    MOTION adds offsets on top of the base azimuth/elevation/distance
    before they reach the spatialiser (sample-count based, so it runs
    free of the host transport). Circle rotates through full revolutions;
    in v3 its distance is locked to the source azimuth (close in front,
    far behind) and its elevation bobs wider with a dip behind the head.
    Wander drifts with smoothed random offsets.
    The current effective position is published in atomics so the editor
    can draw where the source actually is right now.
*/
class YMOrbitProcessor : public juce::AudioProcessor
{
public:
    YMOrbitProcessor();
    ~YMOrbitProcessor() override;

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
    double getTailLengthSeconds() const override             { return 1.5; }

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

    /** Distance parameter (0..1) -> metres (0.3..8.0), shared with the editor. */
    static float distanceToMetres (float distance01)
    {
        return 0.3f + juce::jlimit (0.0f, 1.0f, distance01) * 7.7f;
    }

    //==========================================================================
    /** Effective (base + MOTION) position currently being rendered.
        Written by the audio thread in processBlock; safe to read from
        the editor timer or a test harness. Azimuth is -180..180 degrees,
        distance is 0..1 (same scales as the parameters). */
    float getEffectiveAzimuthDeg() const noexcept { return effectiveAzimuthDeg.load(); }
    float getEffectiveDistance01() const noexcept { return effectiveDistance01.load(); }

private:
    std::atomic<float>* azimuthParam   = nullptr;
    std::atomic<float>* elevationParam = nullptr;
    std::atomic<float>* distanceParam  = nullptr;
    std::atomic<float>* widthParam     = nullptr;
    std::atomic<float>* roomParam      = nullptr;
    std::atomic<float>* outputParam    = nullptr;

    std::atomic<float>* motionModeParam  = nullptr;   // AudioParameterChoice value
    std::atomic<float>* motionRateParam  = nullptr;
    std::atomic<float>* motionDepthParam = nullptr;

    //==========================================================================
    // MOTION state (audio thread only).
    juce::int64 motionSampleCount = 0;   // free-running, transport-independent
    float circlePhaseDeg = 0.0f;         // accumulated Circle angle, 0..360

    // Wander: random target points, one per block, glided toward smoothly.
    juce::Random wanderRandom { 0x594d4f52 };   // fixed seed: "YMOR"
    float wanderAz = 0.0f, wanderDist = 0.0f, wanderElev = 0.0f;             // glided offsets
    float wanderTargetAz = 0.0f, wanderTargetDist = 0.0f, wanderTargetElev = 0.0f;

    // Effective position published for the editor / tests.
    std::atomic<float> effectiveAzimuthDeg { 0.0f };
    std::atomic<float> effectiveDistance01 { 0.25f };

    // Smoothed control values (azimuth travels as sin/cos, see above).
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothAzSin, smoothAzCos;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothElevation;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothDistance;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothWidth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothRoom;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothOutGain;

    using DelayLine = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>;
    DelayLine itdDelayL, itdDelayR;

    using Filter = juce::dsp::IIR::Filter<float>;
    using Coeffs = juce::dsp::IIR::Coefficients<float>;
    Filter elevationPeak;   // on the mono source, follows elevation
    Filter shelfL, shelfR;  // per-ear high shelf: head shadow / presence

    float airStateMid = 0.0f, airStateSide = 0.0f;   // air-absorption one-poles
    float airCoeff = 1.0f;                            // recomputed once per block

    SchroederReverb reverb;

    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YMOrbitProcessor)
};
