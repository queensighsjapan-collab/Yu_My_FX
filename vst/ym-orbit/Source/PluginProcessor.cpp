#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
YMOrbitProcessor::YMOrbitProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    azimuthParam   = apvts.getRawParameterValue ("azimuth");
    elevationParam = apvts.getRawParameterValue ("elevation");
    distanceParam  = apvts.getRawParameterValue ("distance");
    widthParam     = apvts.getRawParameterValue ("width");
    roomParam      = apvts.getRawParameterValue ("room");
    outputParam    = apvts.getRawParameterValue ("output");

    motionModeParam  = apvts.getRawParameterValue ("motionMode");
    motionRateParam  = apvts.getRawParameterValue ("motionRate");
    motionDepthParam = apvts.getRawParameterValue ("motionDepth");
}

YMOrbitProcessor::~YMOrbitProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout YMOrbitProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "azimuth", "Azimuth",
        juce::NormalisableRange<float> (-180.0f, 180.0f, 0.1f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "elevation", "Elevation",
        juce::NormalisableRange<float> (-40.0f, 60.0f, 0.1f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "distance", "Distance",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.25f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "width", "Width",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "room", "Room",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
        0.25f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "output", "Output",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f),
        0.0f, "dB"));

    //--- v1.1.0: MOTION (auto-movement) --------------------------------------
    params.push_back (std::make_unique<juce::AudioParameterChoice>(
        "motionMode", "Motion Mode",
        juce::StringArray { "Off", "Circle", "Wander" },
        0));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "motionRate", "Motion Rate",
        juce::NormalisableRange<float> (0.05f, 3.0f, 0.0f, 0.5f),   // v3: was 2.0 max
        0.20f, "Hz"));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "motionDepth", "Motion Depth",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
        0.7f));

    return { params.begin(), params.end() };
}

//==============================================================================
void YMOrbitProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec monoSpec { sampleRate,
                                      (juce::uint32) juce::jmax (1, samplesPerBlock),
                                      1 };

    // ITD ceiling is 1.1 ms in v3 (exaggerated on purpose); the line
    // still allows up to 1.5 ms.
    const int maxDelay = (int) std::ceil (sampleRate * 0.0015) + 8;
    itdDelayL.setMaximumDelayInSamples (maxDelay);
    itdDelayR.setMaximumDelayInSamples (maxDelay);
    itdDelayL.prepare (monoSpec);
    itdDelayR.prepare (monoSpec);
    itdDelayL.reset();
    itdDelayR.reset();

    elevationPeak.prepare (monoSpec);
    shelfL.prepare (monoSpec);
    shelfR.prepare (monoSpec);

    // Neutral starting coefficients (processBlock refreshes them anyway).
    elevationPeak.coefficients = Coeffs::makePeakFilter (sampleRate, 8000.0, 1.2, 1.0f);
    shelfL.coefficients = Coeffs::makeHighShelf (sampleRate, 3000.0, 0.7, 1.0f);
    shelfR.coefficients = Coeffs::makeHighShelf (sampleRate, 3000.0, 0.7, 1.0f);

    elevationPeak.reset();
    shelfL.reset();
    shelfR.reset();

    airStateMid = airStateSide = 0.0f;
    airCoeff = 1.0f;

    reverb.prepare (sampleRate);
    reverb.reset();

    // Smoothers: ~45 ms ramps keep parameter moves zipper-free.
    const double rampSeconds = 0.045;
    smoothAzSin.reset (sampleRate, rampSeconds);
    smoothAzCos.reset (sampleRate, rampSeconds);
    smoothElevation.reset (sampleRate, rampSeconds);
    smoothDistance.reset (sampleRate, rampSeconds);
    smoothWidth.reset (sampleRate, rampSeconds);
    smoothRoom.reset (sampleRate, rampSeconds);
    smoothOutGain.reset (sampleRate, rampSeconds);

    // MOTION restarts deterministically at every prepare.
    motionSampleCount = 0;
    circlePhaseDeg = 0.0f;
    wanderRandom.setSeed (0x594d4f52);   // fixed: repeatable per session
    wanderAz = wanderDist = wanderElev = 0.0f;
    wanderTargetAz = wanderTargetDist = wanderTargetElev = 0.0f;
    effectiveAzimuthDeg.store (azimuthParam->load());
    effectiveDistance01.store (distanceParam->load());

    const double azRad = juce::degreesToRadians ((double) azimuthParam->load());
    smoothAzSin.setCurrentAndTargetValue ((float) std::sin (azRad));
    smoothAzCos.setCurrentAndTargetValue ((float) std::cos (azRad));
    smoothElevation.setCurrentAndTargetValue (elevationParam->load());
    smoothDistance.setCurrentAndTargetValue (distanceParam->load());
    smoothWidth.setCurrentAndTargetValue (widthParam->load());
    smoothRoom.setCurrentAndTargetValue (roomParam->load());
    smoothOutGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));
}

void YMOrbitProcessor::releaseResources()
{
}

bool YMOrbitProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();

    if (out == juce::AudioChannelSet::stereo())
        return in == juce::AudioChannelSet::stereo()
            || in == juce::AudioChannelSet::mono();   // mono feed = centre source

    if (out == juce::AudioChannelSet::mono())
        return in == juce::AudioChannelSet::mono();

    return false;
}

//==============================================================================
void YMOrbitProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();

    if (numSamples <= 0 || numIn <= 0 || numOut <= 0)
        return;

    //--- MOTION: effective (base + motion) values for this block -------------
    // The offsets are computed once per block from the free-running sample
    // count (transport-independent); the spatialiser's ~45 ms smoothers
    // then carry them per-sample, so block-rate steps are invisible.
    const int   motionMode  = (int) motionModeParam->load();
    const float motionRate  = motionRateParam->load();
    const float motionDepth = motionDepthParam->load();

    float effAzDeg    = azimuthParam->load();
    float effElevDeg  = elevationParam->load();
    float effDist01   = distanceParam->load();

    if (motionMode == 1 && motionDepth > 0.0f)          // Circle
    {
        // Continuous revolutions; speed scales with depth (depth 0 = still).
        const double blockSeconds = (double) numSamples / currentSampleRate;
        circlePhaseDeg = (float) std::fmod (
            (double) circlePhaseDeg + 360.0 * (double) motionRate
                                        * (double) motionDepth * blockSeconds,
            360.0);

        const double phaseRad = juce::degreesToRadians ((double) circlePhaseDeg);
        effAzDeg   = circlePhaseDeg > 180.0f ? circlePhaseDeg - 360.0f
                                             : circlePhaseDeg;

        // v3: the distance wave is locked to the azimuth the source
        // currently sits at (not to the raw phase): cos(azimuth) is +1
        // in front and -1 behind, so the source swings CLOSE in front
        // (offset -0.38 at depth 1) and FAR behind (+0.38), passing
        // neutral at the sides — one dramatic near/far cycle per
        // revolution. (v2 pulsed distance at twice the rotation rate.)
        const double effAzRad = juce::degreesToRadians ((double) effAzDeg);
        effDist01  = juce::jlimit (0.0f, 1.0f,
                                   effDist01 - motionDepth * 0.38f
                                       * (float) std::cos (effAzRad));

        // Elevation bobs at the rotation rate (v3: +/-22 deg, was
        // +/-10 deg in v2) and additionally sinks as the source passes
        // behind the head — up to another -8 deg at azimuth +/-180 —
        // so the rear arc dips instead of riding flat.
        const float azAbs = std::abs (effAzDeg);
        const float behindDip = azAbs > 90.0f
            ? motionDepth * 8.0f * (azAbs - 90.0f) / 90.0f : 0.0f;
        effElevDeg = juce::jlimit (-40.0f, 60.0f,
                                   effElevDeg + motionDepth * 22.0f
                                       * (float) std::sin (phaseRad)
                                       - behindDip);
    }
    else if (motionMode == 2 && motionDepth > 0.0f)     // Wander
    {
        // Three independent random target points, each re-picked once per
        // block from ranges scaled by depth (azimuth the full +/-180 so
        // the source can drift behind, distance +/-0.45, elevation
        // +/-25 deg). Each axis then glides toward its target at its own
        // rate (azimuth ~0.7 s, distance ~1.8 s, elevation ~1.1 s time
        // constants at 48 kHz), so the motion sounds like smooth drifting
        // between random spots rather than noise. The targets make the
        // full depth range actually reachable, which plain lowpassed
        // white noise never does (its RMS stays tiny near the origin).
        const double blockSeconds = (double) numSamples / currentSampleRate;
        wanderTargetAz   = (wanderRandom.nextFloat() * 2.0f - 1.0f) * 180.0f * motionDepth;
        wanderTargetDist = (wanderRandom.nextFloat() * 2.0f - 1.0f) * 0.45f * motionDepth;
        wanderTargetElev = (wanderRandom.nextFloat() * 2.0f - 1.0f) * 25.0f * motionDepth;

        const float kAz = (float) (1.0 - std::exp (-blockSeconds / 0.7));
        const float kDi = (float) (1.0 - std::exp (-blockSeconds / 1.8));
        const float kEl = (float) (1.0 - std::exp (-blockSeconds / 1.1));

        wanderAz   += kAz * (wanderTargetAz   - wanderAz);
        wanderDist += kDi * (wanderTargetDist - wanderDist);
        wanderElev += kEl * (wanderTargetElev - wanderElev);

        effAzDeg   = juce::jlimit (-180.0f, 180.0f, effAzDeg + wanderAz);
        effDist01  = juce::jlimit (0.0f, 1.0f,      effDist01 + wanderDist);
        effElevDeg = juce::jlimit (-40.0f, 60.0f,   effElevDeg + wanderElev);
    }

    motionSampleCount += numSamples;
    effectiveAzimuthDeg.store (effAzDeg);
    effectiveDistance01.store (effDist01);

    //--- parameter targets ---------------------------------------------------
    const double azRad = juce::degreesToRadians ((double) effAzDeg);
    smoothAzSin.setTargetValue ((float) std::sin (azRad));
    smoothAzCos.setTargetValue ((float) std::cos (azRad));
    smoothElevation.setTargetValue (effElevDeg);
    smoothDistance.setTargetValue (effDist01);
    smoothWidth.setTargetValue (widthParam->load());
    smoothRoom.setTargetValue (roomParam->load());
    smoothOutGain.setTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));

    //--- coefficients, refreshed once per block from the smoothed state ------
    {
        float sin0 = smoothAzSin.getCurrentValue();
        float cos0 = smoothAzCos.getCurrentValue();
        const float norm = std::sqrt (sin0 * sin0 + cos0 * cos0);

        if (norm > 1.0e-6f) { sin0 /= norm; cos0 /= norm; }

        const float sinAbs0 = std::abs (sin0);
        const float elev0   = smoothElevation.getCurrentValue();
        const float dist0   = smoothDistance.getCurrentValue();

        // Air absorption: 18 kHz close up, ~2.2 kHz at maximum distance
        // (v2: far end darkens much more than v1's ~4 kHz).
        const double airHz = 18000.0 - 15800.0 * (double) dist0;
        airCoeff = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi
                                            * airHz / currentSampleRate));

        // Simplified pinna cue: a peak that climbs 6 -> 10 kHz with
        // elevation, boosting above the horizon, cutting below
        // (v3: +6 / -4.5 dB; v2 was +5 / -3.5 dB, v1 was +3 / -2 dB).
        const double peakHz = 6000.0 + ((double) elev0 + 40.0) * 40.0;
        const float peakDb = elev0 >= 0.0f ? 6.0f * (elev0 / 60.0f)
                                           : -4.5f * (-elev0 / 40.0f);
        elevationPeak.coefficients = Coeffs::makePeakFilter (
            currentSampleRate, peakHz, 1.2,
            juce::Decibels::decibelsToGain (peakDb));

        // Head shadow (far ear) vs presence lift (near ear), high shelf.
        // Azimuth > 0 puts the source on the right, so the LEFT ear is far.
        const bool farIsLeft = sin0 > 0.0f;
        const float farDb  = -16.0f * sinAbs0;   // v3: v2 was -13 dB, v1 -8 dB
        const float nearDb =   3.0f * sinAbs0;   // v3: v2 was +2.5 dB, v1 +1.5 dB
        shelfL.coefficients = Coeffs::makeHighShelf (
            currentSampleRate, 3000.0, 0.7,
            juce::Decibels::decibelsToGain (farIsLeft ? farDb : nearDb));
        shelfR.coefficients = Coeffs::makeHighShelf (
            currentSampleRate, 3000.0, 0.7,
            juce::Decibels::decibelsToGain (farIsLeft ? nearDb : farDb));
    }

    //--- per-sample render -----------------------------------------------------
    const auto* inL = buffer.getReadPointer (0);
    const auto* inR = numIn > 1 ? buffer.getReadPointer (1) : inL;
    auto* outL = buffer.getWritePointer (0);
    auto* outR = numOut > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        // Advance the smoothers; renormalise the azimuth direction.
        float sAz = smoothAzSin.getNextValue();
        float cAz = smoothAzCos.getNextValue();
        const float norm = std::sqrt (sAz * sAz + cAz * cAz);

        if (norm > 1.0e-6f) { sAz /= norm; cAz /= norm; }

        const float sinAbs  = std::abs (sAz);
        const bool  farLeft = sAz > 0.0f;
        const float elev    = smoothElevation.getNextValue();
        const float dist    = smoothDistance.getNextValue();
        const float width   = smoothWidth.getNextValue();
        const float room    = smoothRoom.getNextValue();
        const float outGain = smoothOutGain.getNextValue();

        // Point source (mid) + original stereo difference (side).
        const float xL = inL[i], xR = inR[i];
        float mid  = 0.5f * (xL + xR);
        float side = 0.5f * (xL - xR);

        // Air absorption on both (the distance EQ the width path shares).
        airStateMid  += airCoeff * (mid  - airStateMid);
        airStateSide += airCoeff * (side - airStateSide);
        mid  = airStateMid;
        side = airStateSide;

        // Elevation (pinna) cue on the source.
        mid = elevationPeak.processSample (mid);

        // Distance gain: (1 / (1 + metres past closest))^1.3 — the extra
        // exponent over v1's plain curve puts 8 m at roughly -24 dB.
        // Floored as before so the far end never fully disappears.
        const float metres = distanceToMetres (dist);
        const float distGain = juce::jmax (
            0.05f, std::pow (1.0f / (1.0f + (metres - 0.3f)), 1.3f));
        mid *= distGain;

        // ITD: the far ear is delayed (max 1.1 ms at +/-90 deg in v3,
        // was 1.0 ms in v2 — beyond the physical ~0.7 ms, that
        // exaggeration is the point — slightly less as the source
        // rises). cos(elev / 2) factor.
        const double elevRad = juce::degreesToRadians ((double) elev);
        const float itdSamples = (float) (0.0011 * currentSampleRate
                                          * (double) sinAbs * std::cos (elevRad * 0.5));
        itdDelayL.pushSample (0, mid);
        itdDelayR.pushSample (0, mid);
        float earL = itdDelayL.popSample (0, farLeft ? itdSamples : 0.0f);
        float earR = itdDelayR.popSample (0, farLeft ? 0.0f : itdSamples);

        // ILD level part: the far ear also loses up to -16 dB overall
        // (v3: raised from the planned -13 dB — with the shelf corner
        // at 3 kHz, the broadband balance at +/-90 deg is governed by
        // this term, and -13 dB measured only 14.8 dB of L/R spread on
        // the standard test mix, short of v3's >= 16 dB target.
        // v2 was -10 dB, v1 -6 dB.) -16 dB = 10^(-16/20) = 10^-0.80.
        const float farGain = std::pow (10.0f, -0.80f * sinAbs);

        if (farLeft) earL *= farGain;
        else         earR *= farGain;

        // Head-shadow / presence shelves.
        earL = shelfL.processSample (earL);
        earR = shelfR.processSample (earR);

        // Room: fed from the spatialised dry signal; the wet level rises
        // a little with distance, then the ROOM knob scales it.
        float wetL = 0.0f, wetR = 0.0f;
        reverb.processSample (earL, earR, wetL, wetR);
        const float wetGain = room * (0.35f + 0.65f * dist);   // v2 curve

        // Width: re-inject the (distance-EQ'd) stereo difference, up to a
        // half/half blend at WIDTH = 1.
        const float sideAdd = side * distGain * 0.5f * width;

        const float yL = earL + sideAdd + wetL * wetGain;
        const float yR = earR - sideAdd + wetR * wetGain;

        if (outR != nullptr)
        {
            outL[i] = yL * outGain;
            outR[i] = yR * outGain;
        }
        else
        {
            outL[i] = 0.5f * (yL + yR) * outGain;
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* YMOrbitProcessor::createEditor()
{
    return new YMOrbitEditor (*this);
}

//==============================================================================
void YMOrbitProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void YMOrbitProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new YMOrbitProcessor();
}
