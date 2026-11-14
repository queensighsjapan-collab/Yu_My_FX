#pragma once

#include <JuceHeader.h>

//==============================================================================
// Minimal read-only SF2 loader for the MeronTronIII font.
//
// The font is simple by construction (mono 16-bit samples, one zone per key
// with keyRange / overridingRootKey / sampleID generators, no modulators),
// so this parser reads exactly the chunks it needs: sdta/smpl + shdr and
// the phdr -> pgen -> inst -> igen chain. For keys covered by several
// zones (the 55-89 singles overlap the (0,57) / (89,127) edge zones) the
// zone with the narrowest key range wins, mirroring how the original
// instrument behaves.
class Sf2Font
{
public:
    Sf2Font() = default;

    struct SampleRef
    {
        int start = 0;        // offset into sampleData, in samples
        int length = 0;       // in samples
        int rootKey = 60;
        int sampleRate = 44100;
    };

    bool loadFromFile (const juce::File& file);
    bool isLoaded() const noexcept { return loaded; }

    int getNumPresets() const noexcept { return (int) presetNames.size(); }
    juce::String getPresetName (int preset) const;
    int getPresetProgram (int preset) const;

    // Index of the preset with the given SF2 program number, or -1.
    int findPresetByProgram (int program) const;

    // Sample for (preset, midi key); nullptr when the key is not covered.
    const SampleRef* getSample (int preset, int midiKey) const;

    const juce::int16* getSampleData() const noexcept { return sampleData.data(); }
    int getTotalSamples() const noexcept { return (int) sampleData.size(); }

    juce::String getError() const { return errorMessage; }

private:
    bool loaded = false;
    juce::String errorMessage;
    std::vector<juce::int16> sampleData;
    juce::StringArray presetNames;
    std::vector<int> presetPrograms;
    // per preset: midi key -> sample (only covered keys present)
    std::vector<std::map<int, SampleRef>> presetMaps;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sf2Font)
};
