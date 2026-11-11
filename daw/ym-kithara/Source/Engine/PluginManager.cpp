#include "PluginManager.h"

//==============================================================================
PluginManager::PluginManager()
{
    // VST3 only for M2. JUCE's VST3 hosting is self-contained — it does
    // not need the Steinberg VST3 SDK at build or run time.
    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());
    loadPersistedList();
}

//==============================================================================
juce::File PluginManager::getAppDataFolder() const
{
    // Deliberately NOT renamed in M6.9: this folder holds the user's
    // existing plugin-list.xml / scan results on disk, and keeping the
    // pre-rename name ("YM ORPHEUS") means YM-Kithara keeps reading
    // the cache YM ORPHEUS wrote. An internal identifier, never shown.
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("YM ORPHEUS");
}

juce::File PluginManager::getPluginListFile() const
{
    return getAppDataFolder().getChildFile ("plugin-list.xml");
}

juce::File PluginManager::getScanResultsFile() const
{
    return getAppDataFolder().getChildFile ("scan-results.xml");
}

juce::File PluginManager::getDeadMansPedalFile() const
{
    return getAppDataFolder().getChildFile ("vst-scan-pedal.txt");
}

void PluginManager::loadPersistedList()
{
    auto file = getPluginListFile();

    if (! file.existsAsFile())
        return;

    if (auto xml = juce::XmlDocument::parse (file))
        knownList.recreateFromXml (*xml);
}

void PluginManager::savePersistedList() const
{
    auto folder = getAppDataFolder();

    if (! folder.createDirectory())
        return;

    if (auto xml = knownList.createXml())
        xml->writeTo (getPluginListFile());
}

int PluginManager::reloadFromScanResults (juce::String& errorOut)
{
    auto file = getScanResultsFile();

    if (! file.existsAsFile())
    {
        errorOut = "The scanner did not produce a results file:\n" + file.getFullPathName();
        return -1;
    }

    auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr)
    {
        errorOut = "The scanner results file could not be read:\n" + file.getFullPathName();
        return -1;
    }

    knownList.recreateFromXml (*xml);
    savePersistedList();
    return knownList.getNumTypes();
}

juce::File PluginManager::findScannerExecutable() const
{
   #if JUCE_WINDOWS
    const juce::String scannerName = "KitharaVstScanner.exe";   // M6.9 rename
   #else
    const juce::String scannerName = "KitharaVstScanner";
   #endif

    const auto exeDir = juce::File::getSpecialLocation (
        juce::File::currentExecutableFile).getParentDirectory();

    // 1) Next to the app executable (the CMake build copies it there).
    auto direct = exeDir.getChildFile (scannerName);

    if (direct.existsAsFile())
        return direct;

    // 2) The scanner's own artefacts folder, sibling of this app's
    //    artefacts folder: <build>/<App>_artefacts/<Config>/ ->
    //    <build>/KitharaVstScanner_artefacts/<Config>/.
    const auto configName = exeDir.getFileName();            // e.g. "Release"
    const auto buildRoot = exeDir.getParentDirectory()       // *_artefacts
                                 .getParentDirectory();      // build root

    for (auto& config : { configName, juce::String ("Release"), juce::String ("Debug") })
    {
        auto sibling = buildRoot.getChildFile ("KitharaVstScanner_artefacts")
                                .getChildFile (config)
                                .getChildFile (scannerName);

        if (sibling.existsAsFile())
            return sibling;
    }

    return {};
}

std::unique_ptr<juce::AudioPluginInstance> PluginManager::createInstance (
    const juce::PluginDescription& description,
    double initialSampleRate, int initialBufferSize,
    juce::String& errorOut) const
{
    // Prefer the known list's copy of the description (freshest file
    // timestamps), matched by identifier string.
    const auto wantedId = description.createIdentifierString();

    for (auto& type : knownList.getTypes())
    {
        if (type.createIdentifierString() == wantedId)
        {
            auto instance = formatManager.createPluginInstance (
                type, initialSampleRate, initialBufferSize, errorOut);

            if (instance != nullptr)
                return instance;

            break;   // the list's copy failed; fall through to the raw description
        }
    }

    // Repair path for descriptions that lost their class uids — e.g.
    // project files saved by the first M2 build, whose FX slots carry
    // uniqueId/deprecatedUid 0 (but the correct name and file). JUCE's
    // VST3 hosting matches module classes by uid, so a uid-less
    // description can never instantiate directly; find the same plugin
    // in the known list by format + name + file and create from that
    // (uid-carrying) type instead.
    if (description.uniqueId == 0 && description.deprecatedUid == 0)
    {
        for (auto& type : knownList.getTypes())
        {
            if (type.pluginFormatName == description.pluginFormatName
                 && type.name == description.name
                 && type.fileOrIdentifier == description.fileOrIdentifier)
            {
                auto instance = formatManager.createPluginInstance (
                    type, initialSampleRate, initialBufferSize, errorOut);

                if (instance != nullptr)
                    return instance;

                break;   // matched but failed to create; raw fallback below
            }
        }
    }

    return formatManager.createPluginInstance (
        description, initialSampleRate, initialBufferSize, errorOut);
}
