#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    PluginManager — VST3 hosting support for YM-Kithara (M2), engine side.

    Owns the juce::AudioPluginFormatManager (with VST3PluginFormat
    registered — JUCE hosts VST3 itself, no external Steinberg SDK is
    needed) and the juce::KnownPluginList.

    The known list is persisted as XML in the per-user app data folder
    (<userApplicationDataDirectory>/YM ORPHEUS/plugin-list.xml). The
    folder keeps its pre-rename name on purpose (M6.9): it is an
    internal identifier holding the user's existing cache, never a
    displayed product name.

    Scanning NEVER happens in this process: unknown or crashing plugins
    must not be able to take the DAW down. The separate console app
    KitharaVstScanner does the scan and writes scan-results.xml next to
    the persisted list; this class then reloads the list from that file
    (see MainComponent's scan flow).

    All methods are message-thread only.
*/
class PluginManager final
{
public:
    PluginManager();

    juce::AudioPluginFormatManager& getFormatManager() noexcept { return formatManager; }
    juce::KnownPluginList& getKnownList() noexcept { return knownList; }

    //==========================================================================
    // Locations (all inside <userApplicationDataDirectory>/YM ORPHEUS).
    juce::File getAppDataFolder() const;
    juce::File getPluginListFile() const;     // plugin-list.xml  — persisted list
    juce::File getScanResultsFile() const;    // scan-results.xml — scanner output
    juce::File getDeadMansPedalFile() const;  // vst-scan-pedal.txt

    /** Loads plugin-list.xml into the known list, if it exists. */
    void loadPersistedList();

    /** Writes the current known list to plugin-list.xml. */
    void savePersistedList() const;

    /** Replaces the known list with the scanner's output file, then
        persists it. Returns the number of plugin types now known, or -1
        (with errorOut set) if the results file could not be read. */
    int reloadFromScanResults (juce::String& errorOut);

    /** Finds KitharaVstScanner(.exe): next to this app's executable first
        (the build copies it there), then the sibling
        KitharaVstScanner_artefacts/<config>/ folder. Returns an invalid
        File when not found. */
    juce::File findScannerExecutable() const;

    /** Creates (but does not prepare) a plugin instance for a description,
        trying the known list first and the description itself second.
        Returns nullptr with errorOut set on failure. */
    std::unique_ptr<juce::AudioPluginInstance> createInstance (
        const juce::PluginDescription& description,
        double initialSampleRate, int initialBufferSize,
        juce::String& errorOut) const;

private:
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownList;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginManager)
};
