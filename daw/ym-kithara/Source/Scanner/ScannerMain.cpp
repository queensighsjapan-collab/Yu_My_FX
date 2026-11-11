#include <JuceHeader.h>

#include <iostream>

//==============================================================================
/**
    KitharaVstScanner — out-of-process VST3 scanner for YM-Kithara (M2;
    renamed from OrpheusVstScanner in M6.9, with the product).

    Usage:
        KitharaVstScanner <output-xml-path>

    Scans the VST3 default locations, writes the resulting
    juce::KnownPluginList as XML to <output-xml-path>, prints one
    progress line per file to stdout, and exits 0 on success.

    This runs as a separate process on purpose: a plugin that crashes
    while being probed takes this throwaway scanner down with it, never
    the DAW. The dead-man's-pedal file (placed next to the output file)
    records any file that was being scanned when a crash happened;
    juce::PluginDirectoryScanner moves those files to the end of the
    scan order on the next run and blacklists them into the list, so a
    couple of rescans still find every healthy plugin.

    M6.10: the results file is rewritten INCREMENTALLY — after every
    scanned file — not just once at the end, and an existing results
    file is loaded at startup so up-to-date entries are skipped. A
    crash or kill (including the app's stall watchdog) therefore loses
    at most the file that was in progress, and re-running the scanner
    resumes where the last run stopped instead of starting over. At
    the scale of a user VST3 folder (hundreds of files, a results XML
    well under a megabyte) the per-file rewrite cost is negligible.
*/
class ScannerApplication final : public juce::JUCEApplicationBase
{
public:
    ScannerApplication() = default;

    //==========================================================================
    const juce::String getApplicationName() override    { return "KitharaVstScanner"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return false; }
    void suspended() override {}
    void resumed() override {}
    void unhandledException (const std::exception*, const juce::String&, int) override {}

    //==========================================================================
    void initialise (const juce::String& /*commandLine*/) override
    {
        const auto args = getCommandLineParameterArray();

        if (args.isEmpty())
        {
            std::cerr << "usage: KitharaVstScanner <output-xml-path>" << std::endl;
            setApplicationReturnValue (2);
            quit();
            return;
        }

        const juce::File outputFile (args[0]);

        if (! outputFile.getParentDirectory().createDirectory())
        {
            std::cerr << "error: could not create the output folder: "
                      << outputFile.getParentDirectory().getFullPathName() << std::endl;
            setApplicationReturnValue (1);
            quit();
            return;
        }

        juce::AudioPluginFormatManager formatManager;
        formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());

        juce::KnownPluginList list;

        // Keep the results of any previous scan: files that are still
        // up to date are skipped, and if this run dies on a bad plugin,
        // the next run resumes from here instead of starting over.
        if (outputFile.existsAsFile())
        {
            if (auto xml = juce::XmlDocument::parse (outputFile))
                list.recreateFromXml (*xml);
        }

        const auto pedalFile = outputFile.getParentDirectory()
                                   .getChildFile ("vst-scan-pedal.txt");

        // M6.10: persist the list after EVERY scanned file, so a crash
        // or kill leaves a complete, parseable results file covering
        // everything scanned so far. The final write below (after the
        // loop) captures the finished state, including files that
        // failed without adding a plugin type.
        auto writeResults = [&outputFile, &list]
        {
            if (auto xml = list.createXml())
                xml->writeTo (outputFile);
        };

        for (auto* format : formatManager.getFormats())
        {
            juce::PluginDirectoryScanner scanner (
                list, *format,
                format->getDefaultLocationsToSearch(),
                true, pedalFile);

            juce::String pluginName;

            while (scanner.scanNextFile (true, pluginName))
            {
                std::cout << "Scanning: " << pluginName << std::endl;
                writeResults();
            }

            for (auto& failedFile : scanner.getFailedFiles())
                std::cout << "Failed: " << failedFile << std::endl;
        }

        if (auto xml = list.createXml())
        {
            if (! xml->writeTo (outputFile))
            {
                std::cerr << "error: could not write the results file: "
                          << outputFile.getFullPathName() << std::endl;
                setApplicationReturnValue (1);
                quit();
                return;
            }
        }

        std::cout << "Done: " << list.getNumTypes() << " plugin(s) found." << std::endl;
        setApplicationReturnValue (0);
        quit();
    }

    void shutdown() override {}

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String& /*commandLine*/) override {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScannerApplication)
};

//==============================================================================
START_JUCE_APPLICATION (ScannerApplication)
