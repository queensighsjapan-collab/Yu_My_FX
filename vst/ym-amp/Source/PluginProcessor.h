#pragma once

#include <JuceHeader.h>

namespace nam { class DSP; }

//==============================================================================
/** YM Amp - a Neural Amp Modeler (NAM) player.

    Loads .nam captures (A1 and A2, including SlimmableContainer) with
    NeuralAmpModelerCore (MIT, vendored under ThirdParty/) and runs them
    inside a complete amp chain:

      source (LIVE input / built-in FILE stem player)
        -> INPUT gain -> noise GATE -> BOOST (treble booster, on/off)
        -> NAM model -> CAB IR convolution (on/off, user .wav)
        -> EQ (BASS / MID / TREBLE) + PRESENCE
        -> CHORUS (on/off) -> DELAY (on/off) -> REVERB (on/off)
        -> MASTER

    NAM models are trained at 48 kHz. When the host runs at another
    sample rate, the mono signal is resampled into the model's rate
    before the NAM stage and back afterwards (two stateful Lagrange
    interpolators with small staging buffers), so the model always
    runs at its expected rate. When the model carries loudness
    metadata, a normalisation gain brings it to a -18 dB reference so
    different captures sit at comparable levels.

    Two factory captures are embedded (BinaryData) and extracted to
    the user folder "YM Amp/Factory" on first run; both are CC BY
    (see CREDITS.txt). The FILE player decodes WAV/MP3/FLAC/AIFF into
    memory on a background thread and plays it back with a fractional
    playhead (click-safe, no allocation or locking in processBlock),
    so Suno stems can be re-amped without a live input - also handy in
    the Standalone build, which uses the audio interface directly.

    MIDI (house standard): "bypass" bool (also the host bypass
    parameter) driven by "bypassCc" (default CC 65, >= 64 = effect ON,
    < 64 = bypassed), ~15 ms equal-power crossfade, editor power
    button, MIDI Learn (right-click LEARN clears to Off).
*/
class YMAmpProcessor : public juce::AudioProcessor
{
public:
    YMAmpProcessor();
    ~YMAmpProcessor() override;

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
    double getTailLengthSeconds() const override             { return 3.0; }

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
    /** MIDI Learn target (only the bypass assignment is learnable,
        matching the approved GUI's single LEARN button). */
    enum class LearnTarget { none = 0, bypass = 1 };

    LearnTarget getLearnTarget() const noexcept
    {
        return static_cast<LearnTarget> (learnTarget.load());
    }

    void setLearnTarget (LearnTarget target) noexcept
    {
        learnTarget.store (static_cast<int> (target));
    }

    void setBypassCcNumber (int ccNumber);
    void setBypassed (bool shouldBeBypassed);

    //==========================================================================
    // Model / IR / audio-file loading (message-thread API; the heavy
    // work runs on a background thread pool and results are swapped in
    // lock-free-ish at the top of processBlock).
    void loadModelAsync (const juce::File& file);
    void loadModelBlocking (const juce::File& file);   // used by the smoke harness
    void loadImpulseResponse (const juce::File& file);
    void loadAudioFileAsync (const juce::File& file);
    void loadAudioFileBlocking (const juce::File& file);

    bool isModelLoaded() const noexcept   { return modelLoadedFlag.load(); }
    juce::String getModelDisplayName();   // LCD line 1 (file name or hint)
    juce::String getModelDisplaySub();    // LCD line 2 (architecture / hint)
    juce::String getIrDisplayName();
    juce::String getFileDisplayName();
    juce::String getStatusText();

    static juce::File getFactoryDir();
    static juce::File getPresetsDir();
    juce::File getFactoryModelFile (const juce::String& fileName) const;

    //==========================================================================
    // File-player transport (editor buttons).
    void setFilePlaying (bool shouldPlay);
    bool isFilePlaying() const noexcept   { return filePlaying.load(); }
    void setFileLoop (bool shouldLoop)    { fileLoop.store (shouldLoop); }
    bool isFileLoop() const noexcept      { return fileLoop.load(); }
    bool hasAudioFile() const noexcept    { return fileLoadedFlag.load(); }
    float getFilePositionNorm() const noexcept { return filePosNorm.load(); }

    //==========================================================================
    // Presets: 4 factory presets, then user presets saved as XML files
    // in the "YM Amp/Presets" user folder.
    int getNumPresets();
    juce::String getPresetName (int index);
    int getCurrentPresetIndex() const noexcept { return currentPresetIndex.load(); }
    void applyPreset (int index);
    void saveUserPreset (const juce::String& name);

    //==========================================================================
    // Meters for the editor (block peaks; the editor decays them).
    float getInputPeak() const noexcept  { return inputPeak.load(); }
    float getOutputPeak() const noexcept { return outputPeak.load(); }

private:
    //==========================================================================
    void handleMidiMessages (const juce::MidiBuffer& midiMessages);
    void processSegment (juce::AudioBuffer<float>& buffer,
                         const float* inL, const float* inR,
                         float* outL, float* outR,
                         int offset, int numSamples);
    void renderFileMono (int numSamples);
    bool srcFilePlayingNow() const noexcept;
    void ensureFactoryContent();
    void doLoadModel (const juce::File& file);
    void doLoadAudioFile (const juce::File& file);
    void publishModelInfo (const juce::String& name, const juce::String& sub);
    void setStatus (const juce::String& text);
    void setParamValue (const char* paramId, float value);
    void refreshUserPresetList();
    juce::ValueTree captureStateTree();
    void applyStateTree (const juce::ValueTree& tree);

    //--- parameters -----------------------------------------------------
    std::atomic<float>* srcSelectParam  = nullptr;
    std::atomic<float>* inputGainParam  = nullptr;
    std::atomic<float>* gateParam       = nullptr;
    std::atomic<float>* boostOnParam    = nullptr;
    std::atomic<float>* boostAmtParam   = nullptr;
    std::atomic<float>* driveParam      = nullptr;
    std::atomic<float>* bassParam       = nullptr;
    std::atomic<float>* midParam        = nullptr;
    std::atomic<float>* trebleParam     = nullptr;
    std::atomic<float>* presenceParam   = nullptr;
    std::atomic<float>* cabOnParam      = nullptr;
    std::atomic<float>* chorusOnParam   = nullptr;
    std::atomic<float>* chorusRateParam = nullptr;
    std::atomic<float>* chorusDepthParam = nullptr;
    std::atomic<float>* delayOnParam    = nullptr;
    std::atomic<float>* delayTimeParam  = nullptr;
    std::atomic<float>* delayFbParam    = nullptr;
    std::atomic<float>* delayMixParam   = nullptr;
    std::atomic<float>* reverbOnParam   = nullptr;
    std::atomic<float>* reverbMixParam  = nullptr;
    std::atomic<float>* masterParam     = nullptr;
    std::atomic<float>* bypassParam     = nullptr;
    std::atomic<float>* bypassCcParam   = nullptr;

    juce::AudioParameterBool* bypassBoolParam  = nullptr;
    juce::AudioParameterInt*  bypassCcIntParam = nullptr;

    std::atomic<int> learnTarget { 0 };

    //--- background work --------------------------------------------------
    juce::ThreadPool threadPool { 1 };
    std::atomic<bool> alive { true };

    //--- NAM model (audio thread owns activeModel) ------------------------
    std::unique_ptr<nam::DSP> activeModel;
    std::unique_ptr<nam::DSP> pendingModel;
    std::mutex modelMutex;
    bool modelPending = false;                    // guarded by modelMutex
    float pendingNormGain = 1.0f;                 // guarded by modelMutex
    double pendingExpectedSR = 48000.0;           // guarded by modelMutex
    std::atomic<float> modelNormGain { 1.0f };
    std::atomic<bool> modelLoadedFlag { false };
    double activeExpectedSR = 48000.0;            // audio thread only

    //--- display / state strings (guarded by infoLock) --------------------
    juce::CriticalSection infoLock;
    juce::String currentModelPath, currentModelName, currentModelSub;
    juce::String currentIrPath, currentIrName;
    juce::String currentFilePath, currentFileName;
    juce::String statusText { "READY" };
    juce::String modelHint;                       // set by model-less presets

    //--- resampling around the NAM stage ----------------------------------
    juce::LagrangeInterpolator rsIn, rsOut;
    std::vector<float> rsInStage, rsOutStage, modelInBuf, modelOutBuf, modelOutBuf2;
    int rsInCount = 0, rsOutCount = 0;
    double modelAcc = 0.0;
    bool resamplingActive = false;

    //--- scratch ------------------------------------------------------------
    std::vector<float> dryL, dryR, monoBuf;
    int scratchCapacity = 0;

    //--- file player ---------------------------------------------------------
    struct FileData
    {
        juce::AudioBuffer<float> buffer;
        double fileSampleRate = 48000.0;
    };
    std::shared_ptr<FileData> activeFile;         // swapped under fileMutex
    std::shared_ptr<FileData> pendingFile;
    std::mutex fileMutex;
    bool filePending = false;                     // guarded by fileMutex
    std::atomic<bool> fileLoadedFlag { false };
    std::atomic<bool> filePlaying { false };
    std::atomic<bool> fileLoop { true };
    std::atomic<bool> fileRestart { false };
    std::atomic<float> filePosNorm { 0.0f };
    double playPos = 0.0;                         // audio thread only

    //--- DSP chain -----------------------------------------------------------
    float gateEnv = 0.0f, gateGain = 1.0f;
    juce::dsp::IIR::Filter<float> boostFilter;

    juce::dsp::Convolution cabConvolution;

    using StereoFilter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                        juce::dsp::IIR::Coefficients<float>>;
    StereoFilter eqBass, eqMid, eqTreble, eqPresence;
    float lastBass = 999.0f, lastMid = 999.0f, lastTreble = 999.0f, lastPresence = 999.0f;
    float lastBoostAmt = -1.0f;
    bool lastBoostOn = false;

    juce::dsp::Chorus<float> chorus;
    juce::dsp::Reverb reverb;
    float lastReverbMix = -1.0f;

    std::vector<float> delayBufL, delayBufR;
    int delayWritePos = 0;
    float delayTimeSmooth = 0.0f;

    //--- smoothers / meters ---------------------------------------------------
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothInputGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothDrive;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothMaster;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothBypass;
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };

    //--- presets ----------------------------------------------------------------
    std::atomic<int> currentPresetIndex { 0 };
    juce::StringArray userPresetNames;            // message thread only
    std::vector<juce::File> userPresetFiles;

    double currentSampleRate = 44100.0;
    int reportedLatency = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YMAmpProcessor)
};
