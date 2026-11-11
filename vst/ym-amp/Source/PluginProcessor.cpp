#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <BinaryData.h>

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

#include <cmath>

//==============================================================================
namespace
{
    const char* const evhModelFileName   = "EVH_5150_III_AmpOnly.nam";
    const char* const plexiFileName      = "1968_KT66_Plexi.nam";   // extracted name

    constexpr double loudnessTargetDb = -18.0;
    constexpr int namChunkSamples = 2048;   // <= Reset() max buffer size
}

//==============================================================================
YMAmpProcessor::YMAmpProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    srcSelectParam   = apvts.getRawParameterValue ("srcSelect");
    inputGainParam   = apvts.getRawParameterValue ("inputGain");
    gateParam        = apvts.getRawParameterValue ("gate");
    boostOnParam     = apvts.getRawParameterValue ("boostOn");
    boostAmtParam    = apvts.getRawParameterValue ("boostAmt");
    driveParam       = apvts.getRawParameterValue ("drive");
    bassParam        = apvts.getRawParameterValue ("bass");
    midParam         = apvts.getRawParameterValue ("mid");
    trebleParam      = apvts.getRawParameterValue ("treble");
    presenceParam    = apvts.getRawParameterValue ("presence");
    cabOnParam       = apvts.getRawParameterValue ("cabOn");
    chorusOnParam    = apvts.getRawParameterValue ("chorusOn");
    chorusRateParam  = apvts.getRawParameterValue ("chorusRate");
    chorusDepthParam = apvts.getRawParameterValue ("chorusDepth");
    delayOnParam     = apvts.getRawParameterValue ("delayOn");
    delayTimeParam   = apvts.getRawParameterValue ("delayTime");
    delayFbParam     = apvts.getRawParameterValue ("delayFb");
    delayMixParam    = apvts.getRawParameterValue ("delayMix");
    reverbOnParam    = apvts.getRawParameterValue ("reverbOn");
    reverbMixParam   = apvts.getRawParameterValue ("reverbMix");
    masterParam      = apvts.getRawParameterValue ("master");
    bypassParam      = apvts.getRawParameterValue ("bypass");
    bypassCcParam    = apvts.getRawParameterValue ("bypassCc");

    bypassBoolParam  = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
    bypassCcIntParam = dynamic_cast<juce::AudioParameterInt*>  (apvts.getParameter ("bypassCc"));

    ensureFactoryContent();
    refreshUserPresetList();

    {
        const juce::ScopedLock lock (infoLock);
        currentIrName = "YM CAB 4x12 (BUILT-IN)";
    }

    // Factory preset 01 (BRIT STACK) is the power-on default: queue its
    // model now so the plugin comes up ready to play.
    loadModelAsync (getFactoryModelFile (plexiFileName));
}

YMAmpProcessor::~YMAmpProcessor()
{
    alive.store (false);
    threadPool.removeAllJobs (true, 5000);
}

juce::AudioProcessorValueTreeState::ParameterLayout YMAmpProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "srcSelect", "Input Source", juce::StringArray { "GUITAR", "FILE" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "inputGain", "Input Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, "dB"));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "gate", "Gate",
        juce::NormalisableRange<float> (-80.0f, -10.0f, 0.5f), -50.0f, "dB"));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "boostOn", "Boost On", false));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "boostAmt", "Boost Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "drive", "Gain",
        juce::NormalisableRange<float> (-12.0f, 24.0f, 0.1f), 6.0f, "dB"));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "bass", "Bass",
        juce::NormalisableRange<float> (-15.0f, 15.0f, 0.1f), 0.0f, "dB"));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mid", "Mid",
        juce::NormalisableRange<float> (-15.0f, 15.0f, 0.1f), 0.0f, "dB"));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "treble", "Treble",
        juce::NormalisableRange<float> (-15.0f, 15.0f, 0.1f), 0.0f, "dB"));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "presence", "Presence",
        juce::NormalisableRange<float> (-15.0f, 15.0f, 0.1f), 0.0f, "dB"));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "cabOn", "Cab On", true));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "chorusOn", "Chorus On", false));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "chorusRate", "Chorus Rate",
        juce::NormalisableRange<float> (0.1f, 8.0f, 0.01f), 1.2f, "Hz"));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "chorusDepth", "Chorus Depth",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.35f));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "delayOn", "Delay On", false));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "delayTime", "Delay Time",
        juce::NormalisableRange<float> (30.0f, 1000.0f, 1.0f), 375.0f, "ms"));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "delayFb", "Delay Feedback",
        juce::NormalisableRange<float> (0.0f, 0.85f, 0.01f), 0.35f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "delayMix", "Delay Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.25f));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "reverbOn", "Reverb On", false));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "reverbMix", "Reverb Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.3f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "master", "Master",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, "dB"));

    // House standard (2026-11-10): every YM effect's ON/OFF must be
    // MIDI-CC controllable. "bypass" is also returned from
    // getBypassParameter(); bypassCc (default 65) flips it with
    // pedal-style values (>= 64 = effect ON, < 64 = bypassed).
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "bypass", "Bypass", false));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        "bypassCc", "Bypass CC", 0, 127, 65));

    return { params.begin(), params.end() };
}

//==============================================================================
juce::File YMAmpProcessor::getFactoryDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("YM Amp").getChildFile ("Factory");
}

juce::File YMAmpProcessor::getPresetsDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("YM Amp").getChildFile ("Presets");
}

juce::File YMAmpProcessor::getFactoryModelFile (const juce::String& fileName) const
{
    return getFactoryDir().getChildFile (fileName);
}

void YMAmpProcessor::ensureFactoryContent()
{
    auto dir = getFactoryDir();
    dir.createDirectory();

    auto extract = [&dir] (const char* fileName, const char* data, int dataSize)
    {
        auto target = dir.getChildFile (fileName);

        if (! target.existsAsFile() || target.getSize() != dataSize)
        {
            juce::TemporaryFile temp (target);
            {
                std::unique_ptr<juce::FileOutputStream> out (temp.getFile().createOutputStream());

                if (out != nullptr)
                {
                    out->write (data, (size_t) dataSize);
                    out->flush();
                }
            }
            temp.overwriteTargetFileWithTemporary();
        }
    };

    extract (evhModelFileName, BinaryData::EVH_5150_III_AmpOnly_nam,
             BinaryData::EVH_5150_III_AmpOnly_namSize);
    extract (plexiFileName, BinaryData::KT66_Plexi_1968_nam,
             BinaryData::KT66_Plexi_1968_namSize);
}

//==============================================================================
void YMAmpProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    const int maxBlock = juce::jmax (8192, samplesPerBlock);
    scratchCapacity = maxBlock;

    dryL.assign ((size_t) maxBlock, 0.0f);
    dryR.assign ((size_t) maxBlock, 0.0f);
    monoBuf.assign ((size_t) maxBlock, 0.0f);

    // Resampler staging: worst realistic ratio is 48000/22050 ~ 2.18.
    const int modelCap = (int) ((double) maxBlock * 2.5) + 8192;
    rsInStage.assign ((size_t) maxBlock + 8192, 0.0f);
    rsOutStage.assign ((size_t) modelCap, 0.0f);
    modelInBuf.assign ((size_t) modelCap, 0.0f);
    modelOutBuf.assign ((size_t) modelCap, 0.0f);
    modelOutBuf2.assign ((size_t) modelCap, 0.0f);
    rsInCount = 0;
    rsOutCount = 0;
    modelAcc = 0.0;
    rsIn.reset();
    rsOut.reset();

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = currentSampleRate;
    spec.maximumBlockSize = (juce::uint32) maxBlock;
    spec.numChannels = 2;

    boostFilter.prepare (juce::dsp::ProcessSpec { currentSampleRate, (juce::uint32) maxBlock, 1 });
    boostFilter.reset();
    lastBoostAmt = -1.0f;

    cabConvolution.reset();
    cabConvolution.prepare (spec);

    // (Re)load the current IR after every prepare: the bundled
    // in-house cab by default, or the user's IR when one was chosen.
    {
        juce::String irPath;

        {
            const juce::ScopedLock lock (infoLock);
            irPath = currentIrPath;
        }

        juce::File irFile (irPath);

        if (irPath.isNotEmpty() && irFile.existsAsFile())
        {
            cabConvolution.loadImpulseResponse (irFile,
                                                juce::dsp::Convolution::Stereo::yes,
                                                juce::dsp::Convolution::Trim::yes, 0,
                                                juce::dsp::Convolution::Normalise::no);
        }
        else
        {
            cabConvolution.loadImpulseResponse (BinaryData::YMCab_4x12_wav,
                                                (size_t) BinaryData::YMCab_4x12_wavSize,
                                                juce::dsp::Convolution::Stereo::yes,
                                                juce::dsp::Convolution::Trim::yes, 0,
                                                juce::dsp::Convolution::Normalise::no);
        }
    }


    eqBass.prepare (spec);
    eqMid.prepare (spec);
    eqTreble.prepare (spec);
    eqPresence.prepare (spec);
    lastBass = lastMid = lastTreble = lastPresence = 999.0f;

    chorus.prepare (spec);
    chorus.setCentreDelay (12.0f);
    chorus.setFeedback (0.0f);
    chorus.setMix (0.5f);

    reverb.prepare (spec);
    reverb.reset();
    lastReverbMix = -1.0f;

    const int delaySize = (int) (3.0 * currentSampleRate) + 1024;
    delayBufL.assign ((size_t) delaySize, 0.0f);
    delayBufR.assign ((size_t) delaySize, 0.0f);
    delayWritePos = 0;
    delayTimeSmooth = (float) (delayTimeParam->load() * 0.001 * currentSampleRate);

    gateEnv = 0.0f;
    gateGain = 1.0f;

    smoothInputGain.reset (currentSampleRate, 0.02);
    smoothDrive.reset (currentSampleRate, 0.02);
    smoothMaster.reset (currentSampleRate, 0.02);
    smoothBypass.reset (currentSampleRate, 0.015);

    smoothInputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inputGainParam->load()));
    smoothDrive.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (driveParam->load()));
    smoothMaster.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (masterParam->load()));
    smoothBypass.setCurrentAndTargetValue (bypassParam->load() > 0.5f ? 0.0f : 1.0f);

    reportedLatency = -1;
}

void YMAmpProcessor::releaseResources()
{
    activeModel.reset();
    modelLoadedFlag.store (false);
}

bool YMAmpProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::stereo())
        return false;

    return in == juce::AudioChannelSet::stereo()
        || in == juce::AudioChannelSet::mono();
}

//==============================================================================
void YMAmpProcessor::setBypassCcNumber (int ccNumber)
{
    if (bypassCcIntParam != nullptr)
        *bypassCcIntParam = juce::jlimit (0, 127, ccNumber);
}

void YMAmpProcessor::setBypassed (bool shouldBeBypassed)
{
    if (bypassBoolParam != nullptr)
        *bypassBoolParam = shouldBeBypassed;
}

void YMAmpProcessor::handleMidiMessages (const juce::MidiBuffer& midiMessages)
{
    // Omni: channel ignored. Writes go through the parameters
    // themselves, so GUI and host follow along.
    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();

        if (! message.isController())
            continue;

        const int ccNumber = message.getControllerNumber();

        // MIDI Learn has priority: the next controller is captured as
        // the bypass assignment and consumed.
        if (getLearnTarget() != LearnTarget::none && ccNumber >= 1)
        {
            setBypassCcNumber (ccNumber);
            setLearnTarget (LearnTarget::none);
            continue;
        }

        const int bypassCc = bypassCcParam != nullptr ? (int) bypassCcParam->load() : 0;

        if (bypassCc >= 1 && ccNumber == bypassCc && bypassBoolParam != nullptr)
            *bypassBoolParam = (message.getControllerValue() < 64);   // >= 64 = effect ON
    }
}

//==============================================================================
void YMAmpProcessor::setStatus (const juce::String& text)
{
    const juce::ScopedLock lock (infoLock);
    statusText = text;
}

juce::String YMAmpProcessor::getStatusText()
{
    const juce::ScopedLock lock (infoLock);
    return statusText;
}

juce::String YMAmpProcessor::getModelDisplayName()
{
    const juce::ScopedLock lock (infoLock);

    if (currentModelName.isNotEmpty())
        return currentModelName;

    return modelHint.isNotEmpty() ? modelHint : juce::String ("NO MODEL LOADED");
}

juce::String YMAmpProcessor::getModelDisplaySub()
{
    const juce::ScopedLock lock (infoLock);

    if (currentModelName.isNotEmpty())
        return currentModelSub;

    return modelHint.isNotEmpty() ? juce::String ("USE LOAD MODEL") : juce::String ("LOAD A .NAM FILE");
}

juce::String YMAmpProcessor::getIrDisplayName()
{
    const juce::ScopedLock lock (infoLock);
    return currentIrName;
}

juce::String YMAmpProcessor::getFileDisplayName()
{
    const juce::ScopedLock lock (infoLock);
    return currentFileName.isNotEmpty() ? currentFileName : juce::String ("NO FILE LOADED");
}

//==============================================================================
void YMAmpProcessor::loadModelAsync (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    setStatus ("LOADING MODEL...");
    threadPool.addJob ([this, file]
    {
        if (alive.load())
            doLoadModel (file);
    });
}

void YMAmpProcessor::loadModelBlocking (const juce::File& file)
{
    doLoadModel (file);

    // Publish immediately (the smoke harness runs off the audio
    // thread, so do the swap here too).
    std::lock_guard<std::mutex> lock (modelMutex);

    if (modelPending)
    {
        activeModel = std::move (pendingModel);
        modelNormGain.store (pendingNormGain);
        activeExpectedSR = pendingExpectedSR;
        modelPending = false;
        modelLoadedFlag.store (activeModel != nullptr);
    }
}

void YMAmpProcessor::doLoadModel (const juce::File& file)
{
    std::unique_ptr<nam::DSP> dsp;

    try
    {
        dsp = nam::get_dsp (std::filesystem::path (file.getFullPathName().toStdString()));
    }
    catch (const std::exception& e)
    {
        setStatus (juce::String ("MODEL ERROR: ") + e.what());
        return;
    }

    if (dsp == nullptr)
    {
        setStatus ("MODEL ERROR: EMPTY");
        return;
    }

    double expectedSR = dsp->GetExpectedSampleRate();

    if (expectedSR <= 0.0)
        expectedSR = 48000.0;

    // We always feed the model at its expected rate (resampling around
    // it when needed), and never feed more than namChunkSamples frames
    // per process() call.
    dsp->Reset (expectedSR, namChunkSamples);

    float normGain = 1.0f;

    if (dsp->HasLoudness())
    {
        const double gainDb = loudnessTargetDb - dsp->GetLoudness();
        normGain = (float) juce::jlimit (-26.0, 26.0, gainDb);
        normGain = juce::Decibels::decibelsToGain (normGain);
    }

    // Display info from the file's JSON header (best effort).
    juce::String archDisplay = "NAM";
    const auto jsonText = file.loadFileAsString();
    auto parsed = juce::JSON::parse (jsonText);

    if (auto* obj = parsed.getDynamicObject())
    {
        auto arch = obj->getProperty ("architecture").toString();

        if (arch == "SlimmableContainer")
        {
            // Report the inner architecture of the first submodel.
            auto cfg = obj->getProperty ("config");
            if (auto* cfgObj = cfg.getDynamicObject())
            {
                auto subs = cfgObj->getProperty ("submodels");

                if (auto* subsArr = subs.getArray())
                    if (subsArr->size() > 0)
                        if (auto* first = (*subsArr)[0].getDynamicObject())
                            if (auto* modelObj = first->getProperty ("model").getDynamicObject())
                                arch = modelObj->getProperty ("architecture").toString()
                                       + " (SLIMMABLE)";
            }
        }

        if (arch.isNotEmpty())
            archDisplay = arch.toUpperCase();
    }

    {
        std::lock_guard<std::mutex> lock (modelMutex);
        pendingModel = std::move (dsp);
        pendingNormGain = normGain;
        pendingExpectedSR = expectedSR;
        modelPending = true;
    }

    {
        const juce::ScopedLock lock (infoLock);
        currentModelPath = file.getFullPathName();
        currentModelName = file.getFileName();
        currentModelSub = archDisplay + " - 48 kHz";
        modelHint.clear();
    }

    setStatus ("MODEL READY");
}

void YMAmpProcessor::loadImpulseResponse (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    cabConvolution.loadImpulseResponse (file,
                                        juce::dsp::Convolution::Stereo::yes,
                                        juce::dsp::Convolution::Trim::yes, 0,
                                        juce::dsp::Convolution::Normalise::no);
    {
        const juce::ScopedLock lock (infoLock);
        currentIrPath = file.getFullPathName();
        currentIrName = file.getFileName();
    }

    setStatus ("IR LOADED");
}

//==============================================================================
void YMAmpProcessor::loadAudioFileAsync (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    setStatus ("LOADING FILE...");
    threadPool.addJob ([this, file]
    {
        if (alive.load())
            doLoadAudioFile (file);
    });
}

void YMAmpProcessor::loadAudioFileBlocking (const juce::File& file)
{
    doLoadAudioFile (file);

    std::lock_guard<std::mutex> lock (fileMutex);

    if (filePending)
    {
        activeFile = pendingFile;
        pendingFile.reset();
        filePending = false;
        fileLoadedFlag.store (activeFile != nullptr);
        playPos = 0.0;
    }
}

void YMAmpProcessor::doLoadAudioFile (const juce::File& file)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr)
    {
        setStatus ("FILE ERROR: UNSUPPORTED FORMAT");
        return;
    }

    // Cap at 15 minutes of decoded audio.
    const auto maxSamples = (juce::int64) (15.0 * 60.0 * reader->sampleRate);
    const auto numSamples = (int) juce::jmin (reader->lengthInSamples, maxSamples);

    auto data = std::make_shared<FileData>();
    data->fileSampleRate = reader->sampleRate;
    data->buffer.setSize ((int) reader->numChannels, numSamples);
    reader->read (&data->buffer, 0, numSamples, 0, true, reader->numChannels > 1);

    {
        std::lock_guard<std::mutex> lock (fileMutex);
        pendingFile = data;
        filePending = true;
    }

    {
        const juce::ScopedLock lock (infoLock);
        currentFilePath = file.getFullPathName();
        currentFileName = file.getFileName();
    }

    setStatus ("FILE READY");
}

void YMAmpProcessor::setFilePlaying (bool shouldPlay)
{
    if (shouldPlay && fileLoadedFlag.load())
    {
        // Restart from the top when the previous pass has ended.
        if (filePosNorm.load() >= 0.999f)
            fileRestart.store (true);

        filePlaying.store (true);
    }
    else
    {
        filePlaying.store (false);
    }
}

//==============================================================================
void YMAmpProcessor::setParamValue (const char* paramId, float value)
{
    if (auto* param = apvts.getParameter (paramId))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
}

int YMAmpProcessor::getNumPresets()
{
    return 4 + (int) userPresetFiles.size();
}

juce::String YMAmpProcessor::getPresetName (int index)
{
    static const char* factoryNames[] = { "BRIT STACK", "EVH 5150", "MAY CHIME", "CLEAN CHORUS" };

    if (index >= 0 && index < 4)
        return factoryNames[index];

    const int userIndex = index - 4;

    if (userIndex >= 0 && userIndex < (int) userPresetNames.size())
        return userPresetNames[userIndex];

    return "---";
}

void YMAmpProcessor::applyPreset (int index)
{
    if (index < 0 || index >= getNumPresets())
        return;

    currentPresetIndex.store (index);

    if (index >= 4)
    {
        auto file = userPresetFiles[(size_t) (index - 4)];

        if (file.existsAsFile())
        {
            auto xml = juce::XmlDocument::parse (file);

            if (xml != nullptr)
                applyStateTree (juce::ValueTree::fromXml (*xml));
        }

        currentPresetIndex.store (index);
        setStatus ("PRESET: " + getPresetName (index));
        return;
    }

    auto loadFactoryModel = [this] (const char* fileName)
    {
        auto target = getFactoryModelFile (fileName);
        juce::String currentPath;

        {
            const juce::ScopedLock lock (infoLock);
            currentPath = currentModelPath;
        }

        if (currentPath != target.getFullPathName())
            loadModelAsync (target);
    };

    auto setHint = [this] (const juce::String& hint)
    {
        const juce::ScopedLock lock (infoLock);
        modelHint = hint;
    };

    switch (index)
    {
        case 0:   // BRIT STACK - bundled CC BY Plexi capture
            setHint ({});
            loadFactoryModel (plexiFileName);
            setParamValue ("inputGain", 0.0f);
            setParamValue ("gate", -55.0f);
            setParamValue ("boostOn", 0.0f);
            setParamValue ("drive", 8.0f);
            setParamValue ("bass", 2.0f);
            setParamValue ("mid", 3.0f);
            setParamValue ("treble", 2.0f);
            setParamValue ("presence", 1.0f);
            setParamValue ("cabOn", 1.0f);
            setParamValue ("chorusOn", 0.0f);
            setParamValue ("delayOn", 0.0f);
            setParamValue ("reverbOn", 1.0f);
            setParamValue ("reverbMix", 0.15f);
            setParamValue ("master", 0.0f);
            break;

        case 1:   // EVH 5150 - bundled CC BY 5150 III capture
            setHint ({});
            loadFactoryModel (evhModelFileName);
            setParamValue ("inputGain", 0.0f);
            setParamValue ("gate", -38.0f);
            setParamValue ("boostOn", 0.0f);
            setParamValue ("drive", 10.0f);
            setParamValue ("bass", 1.0f);
            setParamValue ("mid", -1.0f);
            setParamValue ("treble", 3.0f);
            setParamValue ("presence", 4.0f);
            setParamValue ("cabOn", 1.0f);
            setParamValue ("chorusOn", 0.0f);
            setParamValue ("delayOn", 1.0f);
            setParamValue ("delayTime", 320.0f);
            setParamValue ("delayFb", 0.30f);
            setParamValue ("delayMix", 0.18f);
            setParamValue ("reverbOn", 1.0f);
            setParamValue ("reverbMix", 0.20f);
            setParamValue ("master", 0.0f);
            break;

        case 2:   // MAY CHIME - treble-boosted AC30 style; no bundled
                  // model (none is redistribution-licensed), so the
                  // preset keeps the current model and suggests one.
            setHint ("LOAD AN AC30-STYLE MODEL");
            setParamValue ("inputGain", 0.0f);
            setParamValue ("gate", -55.0f);
            setParamValue ("boostOn", 1.0f);
            setParamValue ("boostAmt", 0.8f);
            setParamValue ("drive", 4.0f);
            setParamValue ("bass", 0.0f);
            setParamValue ("mid", 2.0f);
            setParamValue ("treble", 4.0f);
            setParamValue ("presence", 2.0f);
            setParamValue ("cabOn", 1.0f);
            setParamValue ("chorusOn", 0.0f);
            setParamValue ("delayOn", 0.0f);
            setParamValue ("reverbOn", 1.0f);
            setParamValue ("reverbMix", 0.18f);
            setParamValue ("master", 0.0f);
            break;

        case 3:   // CLEAN CHORUS - JC-style clean + chorus; no bundled model.
            setHint ("LOAD A CLEAN JC-STYLE MODEL");
            setParamValue ("inputGain", 0.0f);
            setParamValue ("gate", -60.0f);
            setParamValue ("boostOn", 0.0f);
            setParamValue ("drive", -2.0f);
            setParamValue ("bass", 1.0f);
            setParamValue ("mid", 0.0f);
            setParamValue ("treble", 2.0f);
            setParamValue ("presence", 1.0f);
            setParamValue ("cabOn", 1.0f);
            setParamValue ("chorusOn", 1.0f);
            setParamValue ("chorusRate", 1.4f);
            setParamValue ("chorusDepth", 0.45f);
            setParamValue ("delayOn", 0.0f);
            setParamValue ("reverbOn", 1.0f);
            setParamValue ("reverbMix", 0.25f);
            setParamValue ("master", 0.0f);
            break;

        default:
            break;
    }

    setStatus ("PRESET: " + getPresetName (index));
}

void YMAmpProcessor::refreshUserPresetList()
{
    userPresetNames.clear();
    userPresetFiles.clear();

    auto dir = getPresetsDir();

    if (! dir.isDirectory())
        return;

    for (const auto& file : dir.findChildFiles (juce::File::findFiles, false, "*.xml"))
    {
        userPresetFiles.push_back (file);
        userPresetNames.add (file.getFileNameWithoutExtension());
    }
}

void YMAmpProcessor::saveUserPreset (const juce::String& name)
{
    auto cleanName = juce::File::createLegalFileName (name.trim());

    if (cleanName.isEmpty())
        return;

    auto dir = getPresetsDir();
    dir.createDirectory();

    auto state = captureStateTree();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());

    if (xml != nullptr)
        xml->writeTo (dir.getChildFile (cleanName + ".xml"));

    refreshUserPresetList();
    setStatus ("PRESET SAVED: " + cleanName);
}

//==============================================================================
juce::ValueTree YMAmpProcessor::captureStateTree()
{
    auto state = apvts.copyState();

    const juce::ScopedLock lock (infoLock);
    state.setProperty ("modelPath", currentModelPath, nullptr);
    state.setProperty ("irPath", currentIrPath, nullptr);
    state.setProperty ("filePath", currentFilePath, nullptr);
    state.setProperty ("presetIndex", currentPresetIndex.load(), nullptr);
    state.setProperty ("fileLoop", fileLoop.load(), nullptr);

    return state;
}

void YMAmpProcessor::applyStateTree (const juce::ValueTree& tree)
{
    if (! tree.isValid() || ! tree.hasType (apvts.state.getType()))
        return;

    apvts.replaceState (tree);

    const auto modelPath = tree.getProperty ("modelPath").toString();
    const auto irPath = tree.getProperty ("irPath").toString();
    const auto filePath = tree.getProperty ("filePath").toString();

    if (tree.hasProperty ("fileLoop"))
        fileLoop.store ((bool) tree.getProperty ("fileLoop"));

    if (modelPath.isNotEmpty())
    {
        juce::File modelFile (modelPath);

        if (modelFile.existsAsFile())
            loadModelAsync (modelFile);
    }

    if (irPath.isNotEmpty())
    {
        juce::File irFile (irPath);

        if (irFile.existsAsFile())
            loadImpulseResponse (irFile);
    }

    if (filePath.isNotEmpty())
    {
        juce::File audioFile (filePath);

        if (audioFile.existsAsFile())
            loadAudioFileAsync (audioFile);
    }
}

void YMAmpProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = captureStateTree();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void YMAmpProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml != nullptr)
        applyStateTree (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
void YMAmpProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                   juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    handleMidiMessages (midiMessages);

    //--- publish a finished model load (never blocks the audio thread
    //    for longer than a pointer swap; if the loader still holds the
    //    mutex we simply keep the old model for this block) -----------
    if (modelPending)
    {
        std::unique_lock<std::mutex> lock (modelMutex, std::try_to_lock);

        if (lock.owns_lock() && modelPending)
        {
            activeModel = std::move (pendingModel);
            modelNormGain.store (pendingNormGain);
            activeExpectedSR = pendingExpectedSR;
            modelPending = false;
            modelLoadedFlag.store (activeModel != nullptr);
            rsIn.reset();
            rsOut.reset();
            rsInCount = 0;
            rsOutCount = 0;
            modelAcc = 0.0;
        }
    }

    if (filePending)
    {
        std::unique_lock<std::mutex> lock (fileMutex, std::try_to_lock);

        if (lock.owns_lock() && filePending)
        {
            activeFile = pendingFile;
            pendingFile.reset();
            filePending = false;
            fileLoadedFlag.store (activeFile != nullptr);
            playPos = 0.0;
            filePosNorm.store (0.0f);
        }
    }

    const int numSamples = buffer.getNumSamples();
    const int numInChannels = getTotalNumInputChannels();

    if (numSamples == 0 || scratchCapacity == 0)
        return;

    auto* inL = buffer.getReadPointer (0);
    auto* inR = buffer.getReadPointer (numInChannels > 1 ? 1 : 0);
    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getWritePointer (1);

    for (int offset = 0; offset < numSamples; offset += scratchCapacity)
    {
        const int segN = juce::jmin (scratchCapacity, numSamples - offset);
        processSegment (buffer, inL + offset, inR + offset,
                        outL + offset, outR + offset, offset, segN);
    }

    const int wantLatency = (resamplingActive ? 5 : 0)
                          + (cabOnParam->load() > 0.5f ? cabConvolution.getLatency() : 0);

    if (wantLatency != reportedLatency)
    {
        reportedLatency = wantLatency;
        setLatencySamples (wantLatency);
    }
}

//==============================================================================
void YMAmpProcessor::processSegment (juce::AudioBuffer<float>& buffer,
                                     const float* inL, const float* inR,
                                     float* outL, float* outR,
                                     int offset, int numSamples)
{
    const double sr = currentSampleRate;

    //--- keep the pristine input for the bypass crossfade + meters ----
    std::memcpy (dryL.data(), inL, (size_t) numSamples * sizeof (float));
    std::memcpy (dryR.data(), inR, (size_t) numSamples * sizeof (float));

    float peakIn = 0.0f;

    for (int i = 0; i < numSamples; ++i)
        peakIn = juce::jmax (peakIn, std::abs (dryL[i]), std::abs (dryR[i]));

    inputPeak.store (peakIn);

    const bool srcIsFile = srcSelectParam->load() > 0.5f;

    auto advanceFilePlayheadOnly = [this, numSamples]
    {
        // Used while fully bypassed: the file keeps playing (and the
        // position display keeps moving) even though its audio is not
        // routed anywhere, mirroring how Mirage's write head keeps
        // running under bypass.
        if (! srcFilePlayingNow())
            return;

        auto file = activeFile;

        if (file == nullptr)
            return;

        const int len = file->buffer.getNumSamples();

        if (len <= 0)
            return;

        playPos += (double) numSamples * file->fileSampleRate / currentSampleRate;

        if (playPos >= (double) len)
        {
            if (fileLoop.load())
                playPos = std::fmod (playPos, (double) len);
            else
            {
                playPos = (double) len;
                filePlaying.store (false);
            }
        }

        filePosNorm.store ((float) (playPos / (double) len));
    };

    //--- bypass: settled state is a plain passthrough -------------------
    const bool bypassed = bypassParam->load() > 0.5f;
    smoothBypass.setTargetValue (bypassed ? 0.0f : 1.0f);

    if (bypassed && ! smoothBypass.isSmoothing())
    {
        std::memcpy (outL, dryL.data(), (size_t) numSamples * sizeof (float));
        std::memcpy (outR, dryR.data(), (size_t) numSamples * sizeof (float));

        if (srcIsFile)
            advanceFilePlayheadOnly();

        outputPeak.store (peakIn);
        return;
    }

    //--- mono source ------------------------------------------------------
    if (! srcIsFile)
    {
        for (int i = 0; i < numSamples; ++i)
            monoBuf[(size_t) i] = (dryL[(size_t) i] + dryR[(size_t) i]) * 0.5f;
    }
    else
    {
        renderFileMono (numSamples);
    }

    //--- input gain ---------------------------------------------------------
    smoothInputGain.setTargetValue (juce::Decibels::decibelsToGain (inputGainParam->load()));

    for (int i = 0; i < numSamples; ++i)
        monoBuf[(size_t) i] *= smoothInputGain.getNextValue();

    //--- noise gate -----------------------------------------------------------
    {
        const float thrLin = juce::Decibels::decibelsToGain (gateParam->load());
        const float envRelease = std::exp (-1.0f / (float) (0.030 * sr));
        const float gainOpen = std::exp (-1.0f / (float) (0.001 * sr));
        const float gainClose = std::exp (-1.0f / (float) (0.060 * sr));

        for (int i = 0; i < numSamples; ++i)
        {
            float& sample = monoBuf[(size_t) i];
            const float level = std::abs (sample);

            gateEnv = juce::jmax (level, gateEnv * envRelease);

            const float target = gateEnv > thrLin ? 1.0f : 0.0f;
            const float coef = target > gateGain ? gainOpen : gainClose;
            gateGain = target + (gateGain - target) * coef;
            sample *= gateGain;
        }
    }

    //--- drive + treble BOOST --------------------------------------------------
    smoothDrive.setTargetValue (juce::Decibels::decibelsToGain (driveParam->load()));
    const bool boostOn = boostOnParam->load() > 0.5f;
    const float boostAmt = boostAmtParam->load();
    const float boostMakeup = boostOn ? juce::Decibels::decibelsToGain (boostAmt * 10.0f) : 1.0f;

    for (int i = 0; i < numSamples; ++i)
        monoBuf[(size_t) i] *= smoothDrive.getNextValue() * boostMakeup;

    if (boostOn)
    {
        if (std::abs (boostAmt - lastBoostAmt) > 0.001f || ! lastBoostOn)
        {
            lastBoostAmt = boostAmt;
            auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                sr, 1800.0f, 0.7f, juce::Decibels::decibelsToGain (boostAmt * 12.0f));
            *boostFilter.coefficients = *coeffs;
        }

        for (int i = 0; i < numSamples; ++i)
            monoBuf[(size_t) i] = boostFilter.processSample (monoBuf[(size_t) i]);
    }

    lastBoostOn = boostOn;

    //--- NAM stage ---------------------------------------------------------------
    if (activeModel != nullptr)
    {
        resamplingActive = std::abs (activeExpectedSR - sr) > 0.01;
        const float norm = modelNormGain.load();
        const int outChannels = activeModel->NumOutputChannels();

        float* modelInPtrs[1] = { nullptr };
        float* modelOutPtrs[2] = { nullptr, nullptr };

        if (! resamplingActive)
        {
            for (int off = 0; off < numSamples; off += namChunkSamples)
            {
                const int cnt = juce::jmin (namChunkSamples, numSamples - off);
                modelInPtrs[0] = monoBuf.data() + off;
                modelOutPtrs[0] = modelOutBuf.data();
                modelOutPtrs[1] = modelOutBuf2.data();
                activeModel->process (modelInPtrs, modelOutPtrs, cnt);

                for (int i = 0; i < cnt; ++i)
                {
                    float v = modelOutBuf[(size_t) i];

                    if (outChannels > 1)
                        v = (v + modelOutBuf2[(size_t) i]) * 0.5f;

                    monoBuf[(size_t) (off + i)] = v * norm;
                }
            }
        }
        else
        {
            // Append this block to the input staging buffer.
            const int inRoom = (int) rsInStage.size() - rsInCount;
            const int toCopy = juce::jmin (numSamples, inRoom);
            std::memcpy (rsInStage.data() + rsInCount, monoBuf.data(),
                         (size_t) toCopy * sizeof (float));
            rsInCount += toCopy;

            modelAcc += (double) numSamples * (activeExpectedSR / sr);
            int wanted = (int) modelAcc;
            modelAcc -= (double) wanted;
            wanted = juce::jmin (wanted, (int) modelInBuf.size());

            const int consumed = rsIn.process (sr / activeExpectedSR,
                                               rsInStage.data(), modelInBuf.data(),
                                               wanted, rsInCount, 0);
            if (consumed > 0 && consumed < rsInCount)
                std::memmove (rsInStage.data(), rsInStage.data() + consumed,
                              (size_t) (rsInCount - consumed) * sizeof (float));
            rsInCount -= consumed;

            for (int off = 0; off < wanted; off += namChunkSamples)
            {
                const int cnt = juce::jmin (namChunkSamples, wanted - off);
                modelInPtrs[0] = modelInBuf.data() + off;
                modelOutPtrs[0] = modelOutBuf.data() + off;
                modelOutPtrs[1] = modelOutBuf2.data() + off;
                activeModel->process (modelInPtrs, modelOutPtrs, cnt);
            }

            if (outChannels > 1)
            {
                for (int i = 0; i < wanted; ++i)
                    modelOutBuf[(size_t) i] =
                        (modelOutBuf[(size_t) i] + modelOutBuf2[(size_t) i]) * 0.5f;
            }

            const int outRoom = (int) rsOutStage.size() - rsOutCount;
            const int outCopy = juce::jmin (wanted, outRoom);
            std::memcpy (rsOutStage.data() + rsOutCount, modelOutBuf.data(),
                         (size_t) outCopy * sizeof (float));
            rsOutCount += outCopy;

            const int consumedOut = rsOut.process (activeExpectedSR / sr,
                                                   rsOutStage.data(), monoBuf.data(),
                                                   numSamples, rsOutCount, 0);
            if (consumedOut > 0 && consumedOut < rsOutCount)
                std::memmove (rsOutStage.data(), rsOutStage.data() + consumedOut,
                              (size_t) (rsOutCount - consumedOut) * sizeof (float));
            rsOutCount -= consumedOut;

            for (int i = 0; i < numSamples; ++i)
                monoBuf[(size_t) i] *= norm;
        }
    }
    else
    {
        resamplingActive = false;
    }

    //--- mono -> stereo work area (the output buffer itself) --------------------
    for (int i = 0; i < numSamples; ++i)
    {
        outL[i] = monoBuf[(size_t) i];
        outR[i] = monoBuf[(size_t) i];
    }

    auto segmentBlock = juce::dsp::AudioBlock<float> (buffer)
                            .getSubBlock ((size_t) offset, (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> context (segmentBlock);

    //--- cab IR -------------------------------------------------------------------
    if (cabOnParam->load() > 0.5f)
        cabConvolution.process (context);

    //--- EQ + Presence ----------------------------------------------------------------
    {
        const float bass = bassParam->load();
        const float mid = midParam->load();
        const float treble = trebleParam->load();
        const float presence = presenceParam->load();

        if (std::abs (bass - lastBass) > 0.01f)
        {
            lastBass = bass;
            *eqBass.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (
                sr, 150.0f, 0.7f, juce::Decibels::decibelsToGain (bass));
        }

        if (std::abs (mid - lastMid) > 0.01f)
        {
            lastMid = mid;
            *eqMid.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (
                sr, 750.0f, 1.0f, juce::Decibels::decibelsToGain (mid));
        }

        if (std::abs (treble - lastTreble) > 0.01f)
        {
            lastTreble = treble;
            *eqTreble.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                sr, 3200.0f, 0.7f, juce::Decibels::decibelsToGain (treble));
        }

        if (std::abs (presence - lastPresence) > 0.01f)
        {
            lastPresence = presence;
            *eqPresence.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                sr, 5500.0f, 0.7f, juce::Decibels::decibelsToGain (presence));
        }

        eqBass.process (context);
        eqMid.process (context);
        eqTreble.process (context);
        eqPresence.process (context);
    }

    //--- chorus ---------------------------------------------------------------------------
    if (chorusOnParam->load() > 0.5f)
    {
        chorus.setRate (chorusRateParam->load());
        chorus.setDepth (chorusDepthParam->load());
        chorus.process (context);
    }

    //--- delay ------------------------------------------------------------------------------
    if (delayOnParam->load() > 0.5f)
    {
        const int delaySize = (int) delayBufL.size();

        if (delaySize > 0)
        {
            const float targetTime = (float) (delayTimeParam->load() * 0.001 * sr);
            const float fb = delayFbParam->load();
            const float mix = delayMixParam->load();
            const float timeCoef = 0.001f;

            for (int i = 0; i < numSamples; ++i)
            {
                delayTimeSmooth += (targetTime - delayTimeSmooth) * timeCoef;

                float readPos = (float) delayWritePos - delayTimeSmooth;

                while (readPos < 0.0f)
                    readPos += (float) delaySize;

                const int i0 = (int) readPos;
                const float frac = readPos - (float) i0;
                const int i1 = (i0 + 1) % delaySize;

                const float wetL = delayBufL[(size_t) i0] * (1.0f - frac)
                                 + delayBufL[(size_t) i1] * frac;
                const float wetR = delayBufR[(size_t) i0] * (1.0f - frac)
                                 + delayBufR[(size_t) i1] * frac;

                const float inSampleL = outL[i];
                const float inSampleR = outR[i];

                outL[i] = inSampleL + wetL * mix;
                outR[i] = inSampleR + wetR * mix;

                delayBufL[(size_t) delayWritePos] = inSampleL + wetL * fb;
                delayBufR[(size_t) delayWritePos] = inSampleR + wetR * fb;

                if (++delayWritePos >= delaySize)
                    delayWritePos = 0;
            }
        }
    }

    //--- reverb --------------------------------------------------------------------------------
    if (reverbOnParam->load() > 0.5f)
    {
        const float mix = reverbMixParam->load();

        if (std::abs (mix - lastReverbMix) > 0.001f)
        {
            lastReverbMix = mix;
            juce::dsp::Reverb::Parameters params;
            params.roomSize = 0.65f;
            params.damping = 0.45f;
            params.wetLevel = mix;
            params.dryLevel = 1.0f - mix * 0.4f;
            params.width = 1.0f;
            params.freezeMode = 0.0f;
            reverb.setParameters (params);
        }

        reverb.process (context);
    }

    //--- master -----------------------------------------------------------------------------------
    smoothMaster.setTargetValue (juce::Decibels::decibelsToGain (masterParam->load()));

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = smoothMaster.getNextValue();
        outL[i] *= g;
        outR[i] *= g;
    }

    //--- bypass crossfade (equal power, ~15 ms) ------------------------------------------------------
    if (bypassed || smoothBypass.isSmoothing())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float blend = smoothBypass.getNextValue();
            const float angle = blend * juce::MathConstants<float>::halfPi;
            const float gainEffect = std::sin (angle);
            const float gainDry = std::cos (angle);

            outL[i] = outL[i] * gainEffect + dryL[(size_t) i] * gainDry;
            outR[i] = outR[i] * gainEffect + dryR[(size_t) i] * gainDry;
        }
    }
    else
    {
        smoothBypass.skip (numSamples);
    }

    float peakOut = 0.0f;

    for (int i = 0; i < numSamples; ++i)
        peakOut = juce::jmax (peakOut, std::abs (outL[i]), std::abs (outR[i]));

    outputPeak.store (peakOut);
}

//==============================================================================
bool YMAmpProcessor::srcFilePlayingNow() const noexcept
{
    return filePlaying.load() && activeFile != nullptr;
}

void YMAmpProcessor::renderFileMono (int numSamples)
{
    if (fileRestart.exchange (false))
        playPos = 0.0;

    auto file = activeFile;

    if (file == nullptr || ! filePlaying.load())
    {
        std::fill (monoBuf.begin(), monoBuf.begin() + numSamples, 0.0f);
        return;
    }

    const auto& fileBuffer = file->buffer;
    const int len = fileBuffer.getNumSamples();

    if (len <= 0)
    {
        std::fill (monoBuf.begin(), monoBuf.begin() + numSamples, 0.0f);
        return;
    }

    const int fileChannels = fileBuffer.getNumChannels();
    const float* fp0 = fileBuffer.getReadPointer (0);
    const float* fp1 = fileChannels > 1 ? fileBuffer.getReadPointer (1) : fp0;
    const double ratio = file->fileSampleRate / currentSampleRate;

    for (int i = 0; i < numSamples; ++i)
    {
        if (playPos >= (double) len)
        {
            if (fileLoop.load())
            {
                playPos = std::fmod (playPos, (double) len);
            }
            else
            {
                filePlaying.store (false);
                monoBuf[(size_t) i] = 0.0f;
                continue;
            }
        }

        const int i0 = (int) playPos;
        const float frac = (float) (playPos - (double) i0);
        const int i1 = juce::jmin (i0 + 1, len - 1);

        const float s0 = fp0[i0] + (fp0[i1] - fp0[i0]) * frac;
        const float s1 = fp1[i0] + (fp1[i1] - fp1[i0]) * frac;
        monoBuf[(size_t) i] = (s0 + s1) * 0.5f;

        playPos += ratio;
    }

    filePosNorm.store ((float) juce::jlimit (0.0, 1.0, playPos / (double) len));
}

//==============================================================================
juce::AudioProcessorEditor* YMAmpProcessor::createEditor()
{
    return new YMAmpEditor (*this);
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new YMAmpProcessor();
}
