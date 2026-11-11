#include <JuceHeader.h>
#include "TimelineEngine.h"
#include "MainComponent.h"

//==============================================================================
/** YM-Kithara — application class. (Renamed from YM ORPHEUS in M6.9;
    the lyre logo stays — a kithara IS the Greek lyre. Persisted data
    kept the old identifiers on purpose: project files and the
    plugin-cache folder are untouched by the rename.)

    Owns the engine (created first, destroyed last) and the main window.
    The window's content component talks only to the engine's public API.
*/
class YMKitharaApplication final : public juce::JUCEApplication
{
public:
    YMKitharaApplication() = default;

    //==========================================================================
    const juce::String getApplicationName() override    { return "YM-Kithara"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    //==========================================================================
    void initialise (const juce::String& /*commandLine*/) override
    {
        engine = std::make_unique<TimelineEngine>();
        engine->initialiseDevices();

        // M6.7 branding: the window (and taskbar) title carries the
        // "AV Workstation" subtitle; the application name itself
        // stays "YM-Kithara" (M6.9 rename).
        mainWindow = std::make_unique<MainWindow> (
            juce::String::fromUTF8 ("YM-Kithara \xe2\x80\x93 AV Workstation"),
            *engine);
    }

    void shutdown() override
    {
        mainWindow.reset();
        engine.reset();
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String& /*commandLine*/) override {}

private:
    //==========================================================================
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, TimelineEngine& engine)
            : DocumentWindow (name,
                              juce::Colour (0xff1c1e24),
                              DocumentWindow::minimiseButton | DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (engine), true);
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);

            // So transport keys (Space = play/pause) work from launch,
            // before the user has clicked anywhere.
            if (auto* content = getContentComponent())
                content->grabKeyboardFocus();
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<TimelineEngine> engine;
    std::unique_ptr<MainWindow> mainWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YMKitharaApplication)
};

//==============================================================================
START_JUCE_APPLICATION (YMKitharaApplication)
