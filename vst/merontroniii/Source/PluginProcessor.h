#pragma once

#include <JuceHeader.h>
#include "Sf2Font.h"

//==============================================================================
// MeronTronIII - tape replay instrument.
//
// Plays the restored MeronTronIII.sf2 (recovered from the 2013 MeronTronII
// DLL) with a polyphonic one-shot tape engine: VOLUME / TONE / ATTACK /
// RELEASE / REVERB and a HALF SPEED switch, plus five tape sets in the
// panel order of the approved mock (Strings / Flute / Choir / Brass /
// Violin). The font file is loaded at runtime from next to the binary /
// inside the VST3 bundle (see findFontFile()).
class MeronTronIIIProcessor final : public juce::AudioProcessor
{
public:
    MeronTronIIIProcessor();
    ~MeronTronIIIProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MeronTronIII"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 5; }
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- font state (for the editor / smoke test) -------------------------
    enum class FontStatus { loading, ready, notFound, error };
    FontStatus getFontStatus() const noexcept { return fontStatus.load(); }
    juce::String getFontMessage() const { return fontMessage; }
    bool isFontReady() const noexcept { return getFontStatus() == FontStatus::ready; }

    // Font preset index for a panel (UI) index 0..4, or -1.
    int fontPresetForUi (int uiIndex) const;

    // ---- performance state (for the editor) -------------------------------
    int getLastNote() const noexcept { return lastNote.load(); }
    int getHeldNoteCount() const noexcept { return heldNotes.load(); }
    bool isAnyVoiceActive() const noexcept { return anyActive.load(); }
    // Bit (note - 55) set for held notes in the 55..89 display range.
    juce::uint64 getHeldMask() const noexcept { return heldMask.load(); }

    // GUI keyboard entry points (thread-safe; consumed in processBlock).
    void noteOnFromGui (int midiNote, float velocity);
    void noteOffFromGui (int midiNote);

    juce::AudioProcessorValueTreeState apvts;

    // Panel order of the tape sets (matches the approved mock).
    static const juce::StringArray& tapeSetNames();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::File findFontFile() const;
    void loadFontAsync();
    void applyPendingProgramChange();

    std::unique_ptr<Sf2Font> font;
    std::atomic<FontStatus> fontStatus { FontStatus::loading };
    juce::String fontMessage { "LOADING TAPES..." };
    std::array<int, 5> uiToFontPreset { -1, -1, -1, -1, -1 };

    // ---- voices ------------------------------------------------------------
    struct Voice
    {
        bool active = false;
        bool releasing = false;
        int note = -1;
        int fontPreset = -1;
        const Sf2Font::SampleRef* ref = nullptr;
        double pos = 0.0;
        double step = 1.0;
        float velGain = 1.0f;
        float env = 0.0f;
        int attackPos = 0, attackLen = 1;
        int releasePos = 0, releaseLen = 1;
        float releaseFrom = 1.0f;
    };
    static constexpr int maxVoices = 24;
    std::array<Voice, maxVoices> voices;
    int voiceCursor = 0;

    void startVoice (int note, float velocity);
    void stopVoice (int note);
    void killVoice (Voice& v, int fadeSamples);
    float renderVoiceSample (Voice& v);

    // ---- GUI note queue ------------------------------------------------------
    static constexpr int guiQueueSize = 128;
    juce::AbstractFifo guiFifo { guiQueueSize };
    std::array<juce::uint32, guiQueueSize> guiQueue {};

    // ---- master chain ---------------------------------------------------------
    double currentSampleRate = 44100.0;
    float toneStateL = 0.0f, toneStateR = 0.0f;
    float toneCoeff = 1.0f;
    float lastToneValue = -1.0f;
    juce::Reverb reverb;
    float lastReverbValue = -1.0f;

    std::atomic<int> lastNote { -1 };
    std::atomic<int> heldNotes { 0 };
    std::atomic<bool> anyActive { false };
    std::atomic<juce::uint64> heldMask { 0 };
    std::atomic<int> pendingProgram { -1 };

    float rawParam (const char* id) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeronTronIIIProcessor)
};
