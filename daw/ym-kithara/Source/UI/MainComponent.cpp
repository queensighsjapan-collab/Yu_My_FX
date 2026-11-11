#include "MainComponent.h"
#include "Mp4Exporter.h"
#include "ScanWatchdog.h"

namespace
{
    const juce::Colour backgroundColour { 0xff1c1e24 };
    const juce::Colour panelColour      { 0xff262932 };
    const juce::Colour textColour       { 0xffe8e9ec };
    const juce::Colour dimTextColour    { 0xff9aa0ad };
    const juce::Colour accentColour     { 0xffd8a24a };

    // M3.2 master meter colours (same scheme as the track header meters).
    const juce::Colour meterSlotColour  { 0xff101216 };
    const juce::Colour meterGreen       { 0xff3fbf5f };
    const juce::Colour meterAmber       { 0xffe8a33a };
    const juce::Colour meterRed         { 0xffe53b30 };
    const juce::Colour meterLedOff      { 0xff452222 };

    juce::Colour meterColourForLevel (float level)
    {
        if (level >= 0.99f) return meterRed;
        if (level >= 0.7f)  return meterAmber;
        return meterGreen;
    }

    void styleSlider (juce::Slider& slider, double rangeMin, double rangeMax, double value)
    {
        slider.setRange (rangeMin, rangeMax, 0.0);
        slider.setValue (value, juce::dontSendNotification);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setColour (juce::Slider::thumbColourId, accentColour);
    }

    void styleLabel (juce::Label& label, const juce::String& text,
                     juce::Justification justification = juce::Justification::centredLeft)
    {
        label.setText (text, juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, textColour);
        label.setJustificationType (justification);
    }
}

//==============================================================================
/** Floating, non-modal window hosting one plugin slot's editor. The
    close callback is delivered asynchronously by MainComponent (see
    openPluginEditor), so the window never destroys itself from inside
    its own close handler. */
class MainComponent::PluginEditorWindow final : public juce::DocumentWindow
{
public:
    PluginEditorWindow (const juce::String& name, std::function<void()> closeCallback)
        : juce::DocumentWindow (name, panelColour, juce::DocumentWindow::closeButton),
          onCloseRequested (std::move (closeCallback))
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
    }

    void closeButtonPressed() override
    {
        if (onCloseRequested != nullptr)
            onCloseRequested();
    }

private:
    std::function<void()> onCloseRequested;
};

//==============================================================================
/** Floating window hosting the piano roll for one MIDI clip; same
    close-callback pattern as PluginEditorWindow. */
class MainComponent::PianoRollWindow final : public juce::DocumentWindow
{
public:
    PianoRollWindow (const juce::String& name, std::function<void()> closeCallback)
        : juce::DocumentWindow (name, panelColour, juce::DocumentWindow::closeButton),
          onCloseRequested (std::move (closeCallback))
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
    }

    void closeButtonPressed() override
    {
        if (onCloseRequested != nullptr)
            onCloseRequested();
    }

private:
    std::function<void()> onCloseRequested;
};

//==============================================================================
/** Floating window hosting the video preview (M6); same close-callback
    pattern as the piano-roll window. */
class MainComponent::VideoPreviewWindow final : public juce::DocumentWindow
{
public:
    VideoPreviewWindow (const juce::String& name, std::function<void()> closeCallback)
        : juce::DocumentWindow (name, panelColour, juce::DocumentWindow::closeButton),
          onCloseRequested (std::move (closeCallback))
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
    }

    void closeButtonPressed() override
    {
        if (onCloseRequested != nullptr)
            onCloseRequested();
    }

private:
    std::function<void()> onCloseRequested;
};

//==============================================================================
/** Floating window hosting the telop text editor (M6.3); same
    close-callback pattern as the piano-roll window. */
class MainComponent::TextClipEditorWindow final : public juce::DocumentWindow
{
public:
    TextClipEditorWindow (const juce::String& name, std::function<void()> closeCallback)
        : juce::DocumentWindow (name, panelColour, juce::DocumentWindow::closeButton),
          onCloseRequested (std::move (closeCallback))
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
    }

    void closeButtonPressed() override
    {
        if (onCloseRequested != nullptr)
            onCloseRequested();
    }

private:
    std::function<void()> onCloseRequested;
};

//==============================================================================
/** Content of the telop text editor window (M6.3): a multi-line text
    box (the clip's text, line breaks preserved) with OK / Cancel.
    OK hands the edited text to onOk; either button ends with onDone
    (the owner closes the window — deferred by the owner, like the
    piano roll, so nothing is destroyed inside its own handler). */
class TextClipEditorComponent final : public juce::Component
{
public:
    TextClipEditorComponent (const juce::String& initialText,
                             std::function<void (const juce::String&)> onOkToUse,
                             std::function<void()> onDoneToUse)
        : onOk (std::move (onOkToUse)), onDone (std::move (onDoneToUse))
    {
        hint.setText ("Telop text - line breaks are kept, lines are drawn as-is.",
                      juce::dontSendNotification);
        hint.setFont (juce::FontOptions (11.5f));
        hint.setColour (juce::Label::textColourId, juce::Colour (0xff9aa0ad));
        addAndMakeVisible (hint);

        editor.setMultiLine (true, true);
        editor.setReturnKeyStartsNewLine (true);
        editor.setScrollbarsShown (true);
        editor.setColour (juce::TextEditor::backgroundColourId, panelColour);
        editor.setColour (juce::TextEditor::textColourId, juce::Colour (0xffe8e9ec));
        editor.setText (initialText, false);
        addAndMakeVisible (editor);

        okButton.onClick = [this]
        {
            if (onOk != nullptr)
                onOk (editor.getText());

            if (onDone != nullptr)
                onDone();
        };
        addAndMakeVisible (okButton);

        cancelButton.onClick = [this]
        {
            if (onDone != nullptr)
                onDone();
        };
        addAndMakeVisible (cancelButton);

        setSize (430, 190);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10, 8);
        hint.setBounds (area.removeFromTop (16));
        area.removeFromTop (4);

        auto buttons = area.removeFromBottom (26);
        okButton.setBounds (buttons.removeFromRight (76));
        buttons.removeFromRight (8);
        cancelButton.setBounds (buttons.removeFromRight (76));

        area.removeFromBottom (8);
        editor.setBounds (area);
    }

private:
    juce::Label hint;
    juce::TextEditor editor;
    juce::TextButton okButton { "OK" };
    juce::TextButton cancelButton { "Cancel" };
    std::function<void (const juce::String&)> onOk;
    std::function<void()> onDone;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TextClipEditorComponent)
};

namespace
{
    // M6: the timecode-rate selector's fixed choices (combo item ids
    // 1..8). Anything else a clip brings in stays unlisted — the box
    // then just shows the adopted rate as text until the user picks.
    void timecodeRateForItem (int itemId, int& num, int& den)
    {
        switch (itemId)
        {
            case 1:  num = 24000; den = 1001; break;   // 23.976
            case 2:  num = 24;    den = 1;    break;
            case 3:  num = 25;    den = 1;    break;
            case 4:  num = 30000; den = 1001; break;   // 29.97 DF
            case 5:  num = 30;    den = 1;    break;
            case 6:  num = 50;    den = 1;    break;
            case 7:  num = 60000; den = 1001; break;   // 59.94 DF
            case 8:  num = 60;    den = 1;    break;
            default: num = 30;    den = 1;    break;
        }
    }

    int timecodeItemForRate (int num, int den)
    {
        for (int itemId = 1; itemId <= 8; ++itemId)
        {
            int itemNum = 0, itemDen = 1;
            timecodeRateForItem (itemId, itemNum, itemDen);

            if (itemNum == num && itemDen == den)
                return itemId;
        }

        return 0;
    }
}

//==============================================================================
/** Mixdown export driver: ThreadWithProgressWindow whose run() pumps
    the progress bar while a worker thread does the actual (blocking,
    model-locked) render through TimelineEngine::exportMixdownWav.
    Cancelling the dialog does not abort the render — run() still waits
    for the worker, so the file is always complete; the engine call
    itself is not interruptible in v1. */
class MainComponent::ExportThread final : public juce::ThreadWithProgressWindow
{
public:
    ExportThread (TimelineEngine& engineToUse, juce::File targetFile,
                  std::atomic<float>& progressToUse)
        : juce::ThreadWithProgressWindow ("Exporting WAV...", true, true, 400),
          engine (engineToUse), target (std::move (targetFile)),
          progress (progressToUse)
    {
        setStatusMessage ("Rendering the mixdown...");
    }

    void run() override
    {
        std::thread worker ([this]
        {
            juce::String error;
            const bool ok = engine.exportMixdownWav (target, error, &progress);
            resultError = error;
            resultOk.store (ok);
            finished.store (true);
        });

        while (! finished.load())
        {
            if (threadShouldExit())
                cancelled = true;

            setProgress ((double) progress.load());

            if (wait (30))
                cancelled = true;
        }

        worker.join();
        setProgress ((double) progress.load());
    }

    TimelineEngine& engine;
    juce::File target;
    std::atomic<float>& progress;
    std::atomic<bool> finished { false };
    std::atomic<bool> resultOk { false };
    juce::String resultError;
    bool cancelled = false;
};

//==============================================================================
/** MP4 export driver (M6.2): same pumping pattern as the WAV driver,
    but cancelling the dialog DOES abort the export — it sets the
    exporter's cancel flag, the worker closes everything and deletes
    the partial file, and run() returns once it has finished doing so. */
class MainComponent::Mp4ExportThread final : public juce::ThreadWithProgressWindow
{
public:
    Mp4ExportThread (TimelineEngine& engineToUse, juce::File targetFile,
                     Mp4ExportOptions exportOptions,
                     std::atomic<float>& progressToUse,
                     std::atomic<bool>& cancelFlagToUse)
        : juce::ThreadWithProgressWindow ("Exporting MP4...", true, true, 800),
          engine (engineToUse), target (std::move (targetFile)),
          options (exportOptions), progress (progressToUse),
          cancelFlag (cancelFlagToUse)
    {
        setStatusMessage ("Rendering the video and mixdown...");
    }

    void run() override
    {
        std::thread worker ([this]
        {
            Mp4Exporter exporter;
            juce::String error;
            const bool ok = exporter.exportMp4 (engine, target, options, error,
                                                &progress, &cancelFlag);
            resultError = error;
            resultOk.store (ok);
            finished.store (true);
        });

        while (! finished.load())
        {
            if (threadShouldExit())
                cancelFlag.store (true);

            setProgress ((double) progress.load());

            if (wait (30))
                cancelFlag.store (true);
        }

        worker.join();
        setProgress ((double) progress.load());
    }

    TimelineEngine& engine;
    juce::File target;
    Mp4ExportOptions options;
    std::atomic<float>& progress;
    std::atomic<bool>& cancelFlag;
    std::atomic<bool> finished { false };
    std::atomic<bool> resultOk { false };
    juce::String resultError;
};

//==============================================================================
/** Progress wrapper for the video-audio extraction (M6.2): the same
    worker-thread pattern as the exports, so Cancel sets the engine's
    cancel flag and the decode aborts between packets. */
class MainComponent::ExtractAudioThread final : public juce::ThreadWithProgressWindow
{
public:
    ExtractAudioThread (TimelineEngine& engineToUse,
                        std::atomic<float>& progressToUse,
                        std::atomic<bool>& cancelFlagToUse,
                        TimelineEngine::VideoAudioExtractResult& resultStorage)
        : juce::ThreadWithProgressWindow ("Extracting video audio...", true, true, 800),
          engine (engineToUse), progress (progressToUse),
          cancelFlag (cancelFlagToUse), result (resultStorage)
    {
        setStatusMessage ("Decoding the video files' audio...");
    }

    void run() override
    {
        std::thread worker ([this]
        {
            result = engine.extractVideoAudio (&progress, &cancelFlag);
            finished.store (true);
        });

        while (! finished.load())
        {
            if (threadShouldExit())
                cancelFlag.store (true);

            setProgress ((double) progress.load());

            if (wait (30))
                cancelFlag.store (true);
        }

        worker.join();
        setProgress ((double) progress.load());
    }

    TimelineEngine& engine;
    std::atomic<float>& progress;
    std::atomic<bool>& cancelFlag;
    TimelineEngine::VideoAudioExtractResult& result;
    std::atomic<bool> finished { false };
};

//==============================================================================
// MasterMeterComponent (M3.2).
//==============================================================================
MasterMeterComponent::MasterMeterComponent (TimelineEngine& engineToUse)
    : engine (engineToUse)
{
    startTimerHz (30);
}

MasterMeterComponent::~MasterMeterComponent()
{
    stopTimer();
}

juce::Rectangle<int> MasterMeterComponent::getLedRect() const
{
    // 10 px square at the right end, vertically centred.
    return { getWidth() - 10, (getHeight() - 10) / 2, 10, 10 };
}

void MasterMeterComponent::timerCallback()
{
    float rawL = 0.0f, rawR = 0.0f;
    engine.getMasterPeak (rawL, rawR);
    engine.clearMasterPeak();

    bool changed = false;

    if ((rawL >= 0.999f || rawR >= 0.999f) && ! clipLatched)
    {
        clipLatched = true;
        changed = true;
    }

    // Instant attack, exponential decay — same feel as the track meters.
    const float newL = juce::jmax (rawL, displayLeft * 0.88f);
    const float newR = juce::jmax (rawR, displayRight * 0.88f);

    if (std::abs (newL - displayLeft) > 0.002f
         || std::abs (newR - displayRight) > 0.002f)
        changed = true;

    displayLeft = newL;
    displayRight = newR;

    if (changed)
        repaint();
}

void MasterMeterComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Two horizontal bars (L over R) filling all but the LED's space.
    auto barsArea = bounds.withTrimmedRight (14);
    const int barHeight = 7;
    const int gap = 3;
    const int totalHeight = barHeight * 2 + gap;
    const int top = (bounds.getHeight() - totalHeight) / 2;

    auto drawBar = [&] (int y, float level)
    {
        g.setColour (meterSlotColour);
        g.fillRect (barsArea.getX(), y, barsArea.getWidth(), barHeight);

        const int fill = (int) std::round (juce::jlimit (0.0f, 1.0f, level)
                                           * (float) barsArea.getWidth());

        if (fill > 0)
        {
            g.setColour (meterColourForLevel (level));
            g.fillRect (barsArea.getX(), y, fill, barHeight);
        }
    };

    drawBar (top, displayLeft);
    drawBar (top + barHeight + gap, displayRight);

    // Clip LED.
    const auto led = getLedRect();
    g.setColour (clipLatched ? meterRed : meterLedOff);
    g.fillRect (led);
    g.setColour (meterSlotColour);
    g.drawRect (led);
}

void MasterMeterComponent::mouseDown (const juce::MouseEvent& e)
{
    // Clicking the LED clears the latch (hit area widened a little).
    if (getLedRect().expanded (3, 6).contains (e.position.toInt()))
    {
        clipLatched = false;
        repaint();
    }
}

//==============================================================================
MainComponent::MainComponent (TimelineEngine& engineToUse)
    : engine (engineToUse), trackHeaders (engineToUse),
      timeline (engineToUse), masterMeter (engineToUse),
      videoFeed (engineToUse)
{
    // Space toggles play/pause from here. Buttons must not take keyboard
    // focus, or Space would click the focused button as well and toggle
    // twice; they are clicked with the mouse only.
    setWantsKeyboardFocus (true);

    for (auto* button : { &newProjectButton, &openProjectButton, &saveProjectButton,
                          &importButton, &addVideoButton, &importSrtButton, &extractAudioButton, &addMidiTrackButton, &exportButton, &exportMp4Button,
                          &settingsButton, &playButton, &recordButton, &stopButton,
                          &returnButton, &videoButton, &splitButton, &deleteClipButton,
                          &addFxButton, &scanPluginsButton,
                          &instOpenButton, &instAddButton, &instRemoveButton })
        button->setWantsKeyboardFocus (false);

    //--- header ---------------------------------------------------------------
    titleLabel.setText ("YM-Kithara", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, textColour);
    addAndMakeVisible (titleLabel);

    tagLabel.setText ("M6.10", juce::dontSendNotification);
    tagLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    tagLabel.setColour (juce::Label::textColourId, accentColour);
    tagLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (tagLabel);

    // M6.7 branding (かいさん): with the video feature set complete
    // this is a workstation, not just a DAW — the name carries the
    // subtitle to say so. Small letterspaced caps in the dim shade,
    // directly under the title.
    subtitleLabel.setText ("AV WORKSTATION", juce::dontSendNotification);
    subtitleLabel.setFont (juce::FontOptions (10.5f, juce::Font::bold)
                               .withKerningFactor (0.20f));
    subtitleLabel.setColour (juce::Label::textColourId, dimTextColour);
    subtitleLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (subtitleLabel);

    newProjectButton.onClick = [this] { newProject(); };
    addAndMakeVisible (newProjectButton);

    openProjectButton.onClick = [this] { openProject(); };
    addAndMakeVisible (openProjectButton);

    saveProjectButton.onClick = [this] { saveProject(); };
    addAndMakeVisible (saveProjectButton);

    importButton.onClick = [this] { importFiles(); };
    addAndMakeVisible (importButton);

    addVideoButton.onClick = [this] { importVideo(); };
    addAndMakeVisible (addVideoButton);

    // M6.3: SRT subtitles -> telop clips on the text track.
    importSrtButton.setTooltip ("Import an .srt subtitle file as timed "
                                "telop (text overlay) clips");
    importSrtButton.onClick = [this] { importSrt(); };
    addAndMakeVisible (importSrtButton);

    // M6.2: pull the video files' own audio into the session.
    extractAudioButton.setTooltip ("Extract the video track's own audio "
                                   "into a normal audio track");
    extractAudioButton.onClick = [this] { extractVideoAudio(); };
    extractAudioButton.setEnabled (false);
    addAndMakeVisible (extractAudioButton);

    // M6.3: the M6.2 docked PiP monitor is gone (かいさん's call — the
    // VIDEO button's pop-out window is enough). Nothing in the main
    // window listens to the frame feed anymore, so the feed's timer
    // (and therefore all preview decoding) runs only while the
    // preview window is open — exactly the Phase 1 behaviour. This
    // also keeps import free of any decode work (see the M6.3 freeze
    // investigation in the README).

    addMidiTrackButton.onClick = [this]
    {
        engine.addMidiTrack();
        setStatus ("MIDI track added. Select one of its clips (double-click the "
                   "lane to create one), then set an instrument in the FX row.");
    };
    addAndMakeVisible (addMidiTrackButton);

    exportButton.onClick = [this] { exportMixdown(); };
    addAndMakeVisible (exportButton);

    exportMp4Button.onClick = [this] { exportMp4(); };
    addAndMakeVisible (exportMp4Button);

    settingsButton.onClick = [this] { openAudioSettings(); };
    addAndMakeVisible (settingsButton);

    //--- transport --------------------------------------------------------------
    playButton.onClick = [this]
    {
        if (engine.isPlaying())
            engine.pause();
        else
            engine.play();

        refreshTransportState();
    };
    addAndMakeVisible (playButton);

    // M4: Record — starts playback from the current position exactly
    // like Play; with a MIDI track armed the engine records the
    // performance into a take clip (Play with tracks armed records
    // too — standard DAW behaviour). The button lights red while
    // recording is actually capturing; refreshTransportState drives
    // the toggle from the engine, never from the click.
    recordButton.setClickingTogglesState (true);
    recordButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a2326));
    recordButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffc22e24));
    recordButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe5534b));
    recordButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    recordButton.setTooltip ("Record: play from here, capturing armed MIDI tracks");
    recordButton.onClick = [this]
    {
        if (engine.isPlaying())
            engine.pause();
        else
            engine.play();

        refreshTransportState();
    };
    addAndMakeVisible (recordButton);

    stopButton.onClick = [this]
    {
        engine.pause();
        refreshTransportState();
    };
    addAndMakeVisible (stopButton);

    returnButton.onClick = [this]
    {
        engine.returnToZero();
        timeline.ensurePlayheadVisible();
        timeline.repaint();
    };
    addAndMakeVisible (returnButton);

    styleLabel (positionLabel, "0:00.0 / 0:00.0");
    positionLabel.setFont (juce::FontOptions (14.0f));
    addAndMakeVisible (positionLabel);

    // M6: video preview toggle + timecode readout + rate selector.
    videoButton.setClickingTogglesState (true);
    videoButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f6f8f));
    videoButton.setTooltip ("Show / hide the video preview window");
    videoButton.onClick = [this]
    {
        if (videoButton.getToggleState())
            openVideoPreview();
        else
            closeVideoPreview();
    };
    addAndMakeVisible (videoButton);

    timecodeLabel.setText ("00:00:00:00", juce::dontSendNotification);
    timecodeLabel.setFont (juce::Font (juce::FontOptions (
        juce::Font::getDefaultMonospacedFontName(), 16.0f, juce::Font::bold)));
    timecodeLabel.setColour (juce::Label::textColourId, accentColour);
    timecodeLabel.setJustificationType (juce::Justification::centredLeft);
    timecodeLabel.setTooltip ("Timecode at the session rate (right)");
    addAndMakeVisible (timecodeLabel);

    timecodeFpsBox.addItem ("23.976", 1);
    timecodeFpsBox.addItem ("24", 2);
    timecodeFpsBox.addItem ("25", 3);
    timecodeFpsBox.addItem ("29.97 DF", 4);
    timecodeFpsBox.addItem ("30", 5);
    timecodeFpsBox.addItem ("50", 6);
    timecodeFpsBox.addItem ("59.94 DF", 7);
    timecodeFpsBox.addItem ("60", 8);
    timecodeFpsBox.setTooltip ("Timecode rate. Choosing one fixes the rate; "
                               "otherwise the first imported video sets it.");
    timecodeFpsBox.onChange = [this]
    {
        if (refreshingTimecodeBox)
            return;

        int num = 30, den = 1;
        timecodeRateForItem (timecodeFpsBox.getSelectedId(), num, den);
        engine.setTimecodeFps (num, den);
        lastTimecodeNum = num;
        lastTimecodeDen = den;
    };
    addAndMakeVisible (timecodeFpsBox);
    refreshTimecodeFpsBox();

    styleLabel (tempoLabel, "Tempo", juce::Justification::centredRight);
    addAndMakeVisible (tempoLabel);

    tempoEditor.setText (juce::String (engine.getTempo(), 0), juce::dontSendNotification);
    tempoEditor.setInputRestrictions (6, "0123456789.");
    tempoEditor.setJustification (juce::Justification::centred);
    tempoEditor.setColour (juce::TextEditor::backgroundColourId, panelColour);
    tempoEditor.setColour (juce::TextEditor::textColourId, textColour);
    auto applyTempo = [this]
    {
        const double bpm = tempoEditor.getText().getDoubleValue();

        if (bpm > 0.0)
            engine.setTempo (bpm);

        tempoEditor.setText (juce::String (engine.getTempo(), 0), juce::dontSendNotification);
        timeline.repaint();
    };
    tempoEditor.onReturnKey = applyTempo;
    tempoEditor.onFocusLost = applyTempo;
    addAndMakeVisible (tempoEditor);

    styleLabel (volumeLabel, "Volume", juce::Justification::centredRight);
    addAndMakeVisible (volumeLabel);

    styleSlider (volumeSlider, 0.0, 1.0, engine.getMasterGain());
    volumeSlider.onValueChange = [this] { engine.setMasterGain ((float) volumeSlider.getValue()); };
    addAndMakeVisible (volumeSlider);

    // M3.2: master meter next to the Volume slider.
    styleLabel (masterMeterLabel, "MASTER", juce::Justification::centredRight);
    masterMeterLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (masterMeterLabel);
    addAndMakeVisible (masterMeter);

    // M3.3: live MIDI input status (opened devices), refreshed by the
    // timer; the full device list is in the tooltip.
    styleLabel (midiInLabel, "MIDI IN: none");
    midiInLabel.setFont (juce::FontOptions (11.5f));
    midiInLabel.setColour (juce::Label::textColourId, dimTextColour);
    addAndMakeVisible (midiInLabel);
    refreshMidiInLabel();

    styleLabel (statusLabel, "Import audio files to create tracks.");
    statusLabel.setColour (juce::Label::textColourId, dimTextColour);
    statusLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (statusLabel);

    //--- timeline ------------------------------------------------------------
    timelineViewport.setViewedComponent (&timeline, false);
    timelineViewport.setScrollBarsShown (true, true);
    timelineViewport.setScrollBarThickness (12);
    addAndMakeVisible (timelineViewport);

    // M3.3: the header column is pinned outside the viewport; its rows
    // follow the lanes' vertical scroll through this hook (fired on
    // scrollbar drags, mouse wheel, and programmatic follows alike).
    addAndMakeVisible (trackHeaders);

    timelineViewport.onViewPositionChanged = [this]
    {
        trackHeaders.setViewOffsetY (timelineViewport.getViewPositionY());
    };

    // A track deleted from its header must not leave a dangling clip
    // selection behind (the headers used to do this themselves).
    trackHeaders.onTrackRemoved = [this] (juce::int64 removedTrackId)
    {
        const auto clipId = timeline.getSelectedClipId();

        if (clipId > 0)
        {
            ClipData clip;
            juce::int64 ownerTrack = -1;

            if (engine.getClipData (clipId, clip, ownerTrack) && ownerTrack == removedTrackId)
            {
                timeline.clearSelection();
                return;
            }
        }

        const auto midiClipId = timeline.getSelectedMidiClipId();

        if (midiClipId > 0)
        {
            MidiClipData clip;
            juce::int64 ownerTrack = -1;

            if (engine.getMidiClipData (midiClipId, clip, ownerTrack)
                 && ownerTrack == removedTrackId)
                timeline.clearSelection();
        }

        const auto videoClipId = timeline.getSelectedVideoClipId();

        if (videoClipId > 0)
        {
            VideoClipData clip;
            juce::int64 ownerTrack = -1;

            if (engine.getVideoClipData (videoClipId, clip, ownerTrack)
                 && ownerTrack == removedTrackId)
                timeline.clearSelection();
        }
    };

    timeline.onSelectionChanged = [this] { refreshInspector(); };

    timeline.onMidiClipDoubleClicked = [this] (juce::int64 trackId, juce::int64 clipId)
    {
        openPianoRoll (trackId, clipId);
    };

    // M6.3: double-clicking a telop clip opens its text editor.
    timeline.onTextClipDoubleClicked = [this] (juce::int64 trackId, juce::int64 clipId)
    {
        openTextClipEditor (trackId, clipId);
    };

    timeline.onTrackHeightChanged = [this]
    {
        vZoomSlider.setValue (timeline.getTrackHeight(), juce::dontSendNotification);
        trackHeaders.setTrackHeight (timeline.getTrackHeight());
        updateTimelineSize();
    };

    //--- inspector -------------------------------------------------------------
    styleLabel (inspectorTitleLabel, "No clip selected");
    inspectorTitleLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    addAndMakeVisible (inspectorTitleLabel);

    styleLabel (fadeInLabel, "Fade In", juce::Justification::centredRight);
    addAndMakeVisible (fadeInLabel);

    styleSlider (fadeInSlider, 0.0, 5.0, 0.0);
    fadeInSlider.onValueChange = [this]
    {
        if (refreshingInspector || inspectorClipId <= 0)
            return;

        ClipData clip;
        juce::int64 trackId = -1;

        if (engine.getClipData (inspectorClipId, clip, trackId))
        {
            clip.fadeIn = engine.secondsToSamples (fadeInSlider.getValue());
            engine.updateClip (inspectorClipId, trackId, clip);
        }
    };
    addAndMakeVisible (fadeInSlider);

    styleLabel (fadeOutLabel, "Fade Out", juce::Justification::centredRight);
    addAndMakeVisible (fadeOutLabel);

    styleSlider (fadeOutSlider, 0.0, 5.0, 0.0);
    fadeOutSlider.onValueChange = [this]
    {
        if (refreshingInspector || inspectorClipId <= 0)
            return;

        ClipData clip;
        juce::int64 trackId = -1;

        if (engine.getClipData (inspectorClipId, clip, trackId))
        {
            clip.fadeOut = engine.secondsToSamples (fadeOutSlider.getValue());
            engine.updateClip (inspectorClipId, trackId, clip);
        }
    };
    addAndMakeVisible (fadeOutSlider);

    styleLabel (clipGainLabel, "Gain", juce::Justification::centredRight);
    addAndMakeVisible (clipGainLabel);

    styleSlider (clipGainSlider, 0.0, 1.25, 1.0);
    clipGainSlider.onValueChange = [this]
    {
        if (refreshingInspector || inspectorClipId <= 0)
            return;

        ClipData clip;
        juce::int64 trackId = -1;

        if (engine.getClipData (inspectorClipId, clip, trackId))
        {
            clip.gain = (float) clipGainSlider.getValue();
            engine.updateClip (inspectorClipId, trackId, clip);
        }
    };
    addAndMakeVisible (clipGainSlider);

    splitButton.onClick = [this]
    {
        // M6: a selected video clip splits in seconds, like its edits.
        const auto videoClipId = timeline.getSelectedVideoClipId();

        if (videoClipId > 0)
        {
            if (engine.splitVideoClip (videoClipId, engine.getPositionSeconds()) > 0)
                setStatus ("Video clip split at the playhead.");
            else
                setStatus ("Move the playhead inside the selected video clip to split it.");

            return;
        }

        // M6.3: a selected telop clip splits at the playhead.
        const auto textClipId = timeline.getSelectedTextClipId();

        if (textClipId > 0)
        {
            if (engine.splitTextClip (textClipId, engine.getPositionSeconds()) > 0)
                setStatus ("Telop clip split at the playhead.");
            else
                setStatus ("Move the playhead inside the selected telop clip to split it.");

            return;
        }

        const auto clipId = timeline.getSelectedClipId();

        if (clipId <= 0)
            return;

        if (engine.splitClip (clipId, engine.getPositionSamples()) > 0)
            setStatus ("Clip split at the playhead.");
        else
            setStatus ("Move the playhead inside the selected clip to split it.");
    };
    addAndMakeVisible (splitButton);

    deleteClipButton.onClick = [this]
    {
        const auto clipId = timeline.getSelectedClipId();

        if (clipId > 0)
        {
            timeline.clearSelection();
            engine.removeClip (clipId);
            setStatus ("Clip deleted.");
            return;
        }

        const auto midiClipId = timeline.getSelectedMidiClipId();

        if (midiClipId > 0)
        {
            timeline.clearSelection();
            engine.removeMidiClip (midiClipId);
            setStatus ("MIDI clip deleted.");
            return;
        }

        const auto videoClipId = timeline.getSelectedVideoClipId();

        if (videoClipId > 0)
        {
            timeline.clearSelection();
            engine.removeVideoClip (videoClipId);
            setStatus ("Video clip deleted.");
            return;
        }

        // M6.3: a selected telop clip deletes by id.
        const auto textClipId = timeline.getSelectedTextClipId();

        if (textClipId > 0)
        {
            timeline.clearSelection();
            engine.removeTextClip (textClipId);
            setStatus ("Telop clip deleted.");
        }
    };
    addAndMakeVisible (deleteClipButton);

    styleLabel (zoomLabel, "Zoom", juce::Justification::centredRight);
    addAndMakeVisible (zoomLabel);

    styleSlider (zoomSlider, 4.0, 300.0, timeline.getPixelsPerSecond());
    zoomSlider.setSkewFactor (0.5);
    zoomSlider.onValueChange = [this]
    {
        timeline.setPixelsPerSecond (zoomSlider.getValue());
        updateTimelineSize();
    };
    addAndMakeVisible (zoomSlider);

    styleLabel (vZoomLabel, "V Zoom", juce::Justification::centredRight);
    addAndMakeVisible (vZoomLabel);

    styleSlider (vZoomSlider,
                 (double) TimelineComponent::minTrackHeightPx,
                 (double) TimelineComponent::maxTrackHeightPx,
                 (double) timeline.getTrackHeight());
    vZoomSlider.onValueChange = [this]
    {
        timeline.setTrackHeight ((int) std::lround (vZoomSlider.getValue()));
        trackHeaders.setTrackHeight (timeline.getTrackHeight());
        updateTimelineSize();
    };
    addAndMakeVisible (vZoomSlider);

    //--- FX panel (M2) ---------------------------------------------------------
    styleLabel (fxTitleLabel, "FX: (select a clip to choose its track)");
    fxTitleLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    fxTitleLabel.setColour (juce::Label::textColourId, accentColour);
    addAndMakeVisible (fxTitleLabel);

    addFxButton.onClick = [this] { showAddFxMenu(); };
    addAndMakeVisible (addFxButton);

    scanPluginsButton.onClick = [this] { scanForPlugins(); };
    addAndMakeVisible (scanPluginsButton);

    //--- instrument block (M3; visible only for MIDI target tracks) ------------
    styleLabel (instLabel, "Inst: (none)");
    instLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (instLabel);

    instOpenButton.onClick = [this]
    {
        if (fxTargetHasInstrument && ! fxTargetInstrument.missing)
            openPluginEditor (fxTargetInstrument.id);
    };
    addAndMakeVisible (instOpenButton);

    instAddButton.onClick = [this] { showAddInstrumentMenu(); };
    addAndMakeVisible (instAddButton);

    instRemoveButton.onClick = [this]
    {
        if (! fxTargetHasInstrument)
            return;

        closePluginEditor (fxTargetInstrument.id);
        engine.removeTrackInstrument (fxTargetTrackId);
        setStatus ("Instrument removed.");
        refreshFxPanel();
    };
    addAndMakeVisible (instRemoveButton);

    refreshTransportState();
    refreshInspector();
    refreshFxPanel();
    startTimerHz (10);

    setSize (1100, 640);
}

MainComponent::~MainComponent()
{
    stopTimer();

    if (scanProcess != nullptr)
        scanProcess->kill();

    closePianoRoll();
    closeVideoPreview();   // M6: its decoder thread must stop with us
    closeTextClipEditor();   // M6.3

    // Close plugin editors while the engine (and its instances) is
    // still alive; editors must never outlive their plugin instance.
    pluginEditors.clear();
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (12, 10);

    // Header band. M6.7: 46 px tall so the "AV Workstation"
    // subtitle fits under the name (title on the top line, tag
    // beside it as always); the buttons keep their 32 px height on
    // a strip centred in the band.
    auto header = area.removeFromTop (46);
    auto titleBlock = header.removeFromLeft (165);
    titleLabel.setBounds (titleBlock.removeFromTop (30));
    subtitleLabel.setBounds (titleBlock);
    tagLabel.setBounds (header.removeFromLeft (40).removeFromTop (30));

    auto buttons = header.withSizeKeepingCentre (header.getWidth(), 32);
    settingsButton.setBounds (buttons.removeFromRight (104));
    buttons.removeFromRight (6);
    exportMp4Button.setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (6);
    exportButton.setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (6);
    addMidiTrackButton.setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (6);
    extractAudioButton.setBounds (buttons.removeFromRight (104));
    buttons.removeFromRight (6);
    importSrtButton.setBounds (buttons.removeFromRight (98));   // M6.3
    buttons.removeFromRight (6);
    addVideoButton.setBounds (buttons.removeFromRight (92));
    buttons.removeFromRight (6);
    importButton.setBounds (buttons.removeFromRight (94));
    buttons.removeFromRight (6);
    saveProjectButton.setBounds (buttons.removeFromRight (108));
    buttons.removeFromRight (6);
    openProjectButton.setBounds (buttons.removeFromRight (108));
    buttons.removeFromRight (6);
    newProjectButton.setBounds (buttons.removeFromRight (56));

    area.removeFromTop (6);

    // Transport row.
    auto transport = area.removeFromTop (32);
    playButton.setBounds (transport.removeFromLeft (72));
    transport.removeFromLeft (6);
    recordButton.setBounds (transport.removeFromLeft (44));   // M4 (36 px in
                                    // M6.8; widened for the "REC" label in M6.9)
    transport.removeFromLeft (6);
    stopButton.setBounds (transport.removeFromLeft (60));
    transport.removeFromLeft (6);
    returnButton.setBounds (transport.removeFromLeft (44));
    transport.removeFromLeft (8);
    videoButton.setBounds (transport.removeFromLeft (64));
    transport.removeFromLeft (10);
    positionLabel.setBounds (transport.removeFromLeft (125));
    transport.removeFromLeft (4);
    timecodeLabel.setBounds (transport.removeFromLeft (118));
    timecodeFpsBox.setBounds (transport.removeFromLeft (84));
    transport.removeFromLeft (8);
    tempoLabel.setBounds (transport.removeFromLeft (40));
    transport.removeFromLeft (4);
    tempoEditor.setBounds (transport.removeFromLeft (52));
    transport.removeFromLeft (8);
    midiInLabel.setBounds (transport.removeFromLeft (150));
    volumeSlider.setBounds (transport.removeFromRight (140));
    volumeLabel.setBounds (transport.removeFromRight (50));
    transport.removeFromRight (8);
    masterMeter.setBounds (transport.removeFromRight (110));
    masterMeterLabel.setBounds (transport.removeFromRight (48));

    area.removeFromTop (4);
    statusLabel.setBounds (area.removeFromTop (18));
    area.removeFromTop (6);

    // Bottom area: FX row above the inspector row.
    auto bottom = area.removeFromBottom (92);

    auto fxRow = bottom.removeFromTop (40);
    fxTitleLabel.setBounds (fxRow.removeFromLeft (190));
    fxRow.removeFromLeft (6);
    addFxButton.setBounds (fxRow.removeFromLeft (84));
    fxRow.removeFromLeft (6);
    scanPluginsButton.setBounds (fxRow.removeFromLeft (148));
    fxRow.removeFromLeft (10);
    fxSlotsArea = fxRow;
    layoutFxControls();

    bottom.removeFromTop (4);

    auto inspectorRow = bottom;
    vZoomSlider.setBounds (inspectorRow.removeFromRight (110));
    vZoomLabel.setBounds (inspectorRow.removeFromRight (52));
    inspectorRow.removeFromRight (10);

    zoomSlider.setBounds (inspectorRow.removeFromRight (150));
    zoomLabel.setBounds (inspectorRow.removeFromRight (42));
    inspectorRow.removeFromRight (10);
    deleteClipButton.setBounds (inspectorRow.removeFromRight (92));
    inspectorRow.removeFromRight (6);
    splitButton.setBounds (inspectorRow.removeFromRight (130));

    auto inspector = inspectorRow;
    inspectorTitleLabel.setBounds (inspector.removeFromLeft (150));
    fadeInLabel.setBounds (inspector.removeFromLeft (52));
    inspector.removeFromLeft (4);
    fadeInSlider.setBounds (inspector.removeFromLeft (110));
    inspector.removeFromLeft (8);
    fadeOutLabel.setBounds (inspector.removeFromLeft (56));
    inspector.removeFromLeft (4);
    fadeOutSlider.setBounds (inspector.removeFromLeft (110));
    inspector.removeFromLeft (8);
    clipGainLabel.setBounds (inspector.removeFromLeft (36));
    inspector.removeFromLeft (4);
    clipGainSlider.setBounds (inspector.removeFromLeft (100));

    // Timeline fills the rest: the pinned header column (M3.3) takes
    // the left headerWidthPx OUTSIDE the viewport, so horizontal
    // scrolling moves only the ruler + waveforms.
    trackHeaders.setBounds (area.removeFromLeft (TimelineComponent::headerWidthPx));
    timelineViewport.setBounds (area);
    updateTimelineSize();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        // A modal dialog (Audio Settings, an alert box) owns the keyboard.
        if (juce::Component::getCurrentlyModalComponent() != nullptr)
            return false;

        // Never steal Space from text entry (the tempo field, a track
        // rename editor — those consume Space themselves before the event
        // would reach us, this is a second line of defence) or from a
        // focused button, which would click itself and toggle twice.
        if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
        {
            if (dynamic_cast<juce::TextEditor*> (focused) != nullptr
                 || dynamic_cast<juce::Button*> (focused) != nullptr)
                return false;
        }

        if (engine.isPlaying())
            engine.pause();
        else
            engine.play();

        refreshTransportState();
        return true;
    }

    return false;
}

void MainComponent::updateTimelineSize()
{
    const int width = juce::jmax (timelineViewport.getWidth(), timeline.getDesiredWidth());
    const int height = juce::jmax (timelineViewport.getHeight(), timeline.getDesiredHeight());

    if (timeline.getWidth() != width || timeline.getHeight() != height)
        timeline.setSize (width, height);
}

//==============================================================================
void MainComponent::timerCallback()
{
    positionLabel.setText (formatTime (engine.getPositionSeconds())
                               + " / " + formatTime (engine.getTotalLengthSeconds()),
                           juce::dontSendNotification);

    // M6: the timecode readout tracks the playhead at 10 Hz like the
    // seconds readout it sits next to (the preview window has its own
    // 30 Hz view of the same position).
    {
        int num = 30, den = 1;
        bool userSet = false;
        engine.getTimecodeFps (num, den, userSet);
        timecodeLabel.setText (Timecode::format (engine.getPositionSeconds(), { num, den }),
                               juce::dontSendNotification);
    }

    refreshTransportState();
    refreshMidiInLabel();   // M3.3: device set is static, but stay honest
    refreshExtractAudioButton();   // M6.2; the M6.2 docked monitor it
                                   // used to also drive is gone (M6.3)
    pollScanProcess();

    // M3: the piano roll follows its clip; close it when the clip (or
    // its track/project) goes away.
    if (pianoRollWindow != nullptr && (pianoRoll == nullptr || ! pianoRoll->clipStillExists()))
        closePianoRoll();

    if (engine.getModelVersion() != lastModelVersion)
    {
        lastModelVersion = engine.getModelVersion();
        updateTimelineSize();
        refreshFxPanel();

        // A clip edit (trim/split/…) may have changed the selected clip's
        // values; keep the inspector honest without fighting the user.
        if (! fadeInSlider.isMouseButtonDown() && ! fadeOutSlider.isMouseButtonDown()
             && ! clipGainSlider.isMouseButtonDown())
            refreshInspector();

        // Keep the tempo field in sync with project loads.
        if (! tempoEditor.isTextInputActive())
            tempoEditor.setText (juce::String (engine.getTempo(), 0), juce::dontSendNotification);

        // M6: keep the timecode-rate selector in sync with imports
        // (first video adopts its rate) and project loads.
        refreshTimecodeFpsBox();
    }
}

void MainComponent::refreshTransportState()
{
    const bool hasContent = engine.getTotalLengthSamples() > 0;
    // M4: an armed MIDI track makes Play/Record meaningful even in an
    // empty session (the take creates the length as it records).
    const bool canPlay = hasContent || engine.hasArmedMidiTracks();
    playButton.setEnabled (canPlay);
    playButton.setButtonText (engine.isPlaying() ? "Pause" : "Play");
    recordButton.setEnabled (canPlay);
    recordButton.setToggleState (engine.isRecording(), juce::dontSendNotification);
}

void MainComponent::refreshMidiInLabel()
{
    // M3.3: one short line — "MIDI IN: none", or the device count plus
    // names (the label clips long lists; the tooltip carries them all).
    const auto names = engine.getMidiInputDeviceNames();

    const auto text = names.isEmpty()
                          ? juce::String ("MIDI IN: none")
                          : "MIDI IN: " + juce::String (names.size())
                                + (names.size() == 1 ? " device: " : " devices: ")
                                + names.joinIntoString (", ");

    if (text == lastMidiInText)
        return;

    lastMidiInText = text;
    midiInLabel.setText (text, juce::dontSendNotification);
    midiInLabel.setTooltip (text);
}

void MainComponent::refreshInspector()
{
    refreshingInspector = true;

    const auto clipId = timeline.getSelectedClipId();
    inspectorClipId = clipId;

    ClipData clip;
    juce::int64 trackId = -1;
    const bool hasClip = clipId > 0 && engine.getClipData (clipId, clip, trackId);

    // The FX panel targets the track of the selected clip.
    if (hasClip && trackId != fxTargetTrackId)
    {
        fxTargetTrackId = trackId;
        refreshFxPanel();
    }

    // M3: a selected MIDI clip targets its track exactly the same way.
    // It has no fades/gain/split in v1, but Delete works.
    if (! hasClip && timeline.getSelectedMidiClipId() > 0)
    {
        MidiClipData midiClip;
        juce::int64 midiTrackId = -1;

        if (engine.getMidiClipData (timeline.getSelectedMidiClipId(), midiClip, midiTrackId))
        {
            inspectorClipId = -1;

            if (midiTrackId != fxTargetTrackId)
            {
                fxTargetTrackId = midiTrackId;
                refreshFxPanel();
            }

            inspectorTitleLabel.setText ("MIDI clip", juce::dontSendNotification);
            fadeInSlider.setEnabled (false);
            fadeOutSlider.setEnabled (false);
            clipGainSlider.setEnabled (false);
            splitButton.setEnabled (false);
            deleteClipButton.setEnabled (true);
            refreshingInspector = false;
            return;
        }
    }

    // M6: a selected video clip gets the inspector title, Split and
    // Delete (no fades/gain — those are audio concepts), and does NOT
    // retarget the FX panel: the video track has no FX to show.
    if (! hasClip && timeline.getSelectedVideoClipId() > 0)
    {
        VideoClipData videoClip;
        juce::int64 videoTrackId = -1;

        if (engine.getVideoClipData (timeline.getSelectedVideoClipId(), videoClip, videoTrackId))
        {
            inspectorClipId = -1;

            inspectorTitleLabel.setText ("Video: " + videoClip.getDisplayName(),
                                         juce::dontSendNotification);
            fadeInSlider.setEnabled (false);
            fadeOutSlider.setEnabled (false);
            clipGainSlider.setEnabled (false);
            splitButton.setEnabled (true);
            deleteClipButton.setEnabled (true);
            refreshingInspector = false;
            return;
        }
    }

    // M6.3: a selected telop clip — its first line as the title;
    // Split and Delete work, fades/gain don't apply, and the FX
    // panel is not retargeted (same as the video branch above).
    if (! hasClip && timeline.getSelectedTextClipId() > 0)
    {
        TextClipData textClip;
        juce::int64 textTrackId = -1;

        if (engine.getTextClipData (timeline.getSelectedTextClipId(),
                                    textClip, textTrackId))
        {
            inspectorClipId = -1;

            const auto firstLine = textClip.getFirstLine();
            inspectorTitleLabel.setText (
                "Telop: " + (firstLine.isNotEmpty() ? firstLine
                                                    : juce::String ("(empty)")),
                juce::dontSendNotification);
            fadeInSlider.setEnabled (false);
            fadeOutSlider.setEnabled (false);
            clipGainSlider.setEnabled (false);
            splitButton.setEnabled (true);
            deleteClipButton.setEnabled (true);
            refreshingInspector = false;
            return;
        }
    }

    if (hasClip)
    {
        juce::String name = "Clip";

        {
            juce::ScopedLock lock (engine.getModelLock());

            if (auto* source = engine.findSource (clip.sourceId))
                name = source->name;
        }

        inspectorTitleLabel.setText (name, juce::dontSendNotification);
        fadeInSlider.setValue (engine.samplesToSeconds (clip.fadeIn), juce::dontSendNotification);
        fadeOutSlider.setValue (engine.samplesToSeconds (clip.fadeOut), juce::dontSendNotification);
        clipGainSlider.setValue (clip.gain, juce::dontSendNotification);
    }
    else
    {
        inspectorTitleLabel.setText ("No clip selected", juce::dontSendNotification);
    }

    fadeInSlider.setEnabled (hasClip);
    fadeOutSlider.setEnabled (hasClip);
    clipGainSlider.setEnabled (hasClip);
    splitButton.setEnabled (hasClip);
    deleteClipButton.setEnabled (hasClip);

    refreshingInspector = false;
}

void MainComponent::setStatus (const juce::String& text)
{
    statusLabel.setText (text, juce::dontSendNotification);
}

//==============================================================================
// FX panel (M2)
//==============================================================================
void MainComponent::refreshFxPanel()
{
    prunePluginEditors();

    juce::String trackName;
    std::vector<FxSlotData> chain;
    const bool hasTrack = fxTargetTrackId > 0
                            && engine.getTrackFx (fxTargetTrackId, trackName, chain);

    if (! hasTrack && fxTargetTrackId > 0)
        fxTargetTrackId = -1;

    fxTitleLabel.setText (hasTrack ? "FX: " + trackName
                                   : "FX: (select a clip to choose its track)",
                          juce::dontSendNotification);
    addFxButton.setEnabled (hasTrack);

    // M3: resolve the target track's instrument state (MIDI tracks only).
    fxTargetIsMidi = false;
    fxTargetHasInstrument = false;
    fxTargetInstrument = FxSlotData {};

    if (hasTrack)
    {
        juce::ScopedLock lock (engine.getModelLock());

        if (auto* track = engine.findTrack (fxTargetTrackId))
            fxTargetIsMidi = (track->kind == TrackKind::midi);
    }

    if (fxTargetIsMidi)
    {
        bool hasInst = false;
        FxSlotData inst;

        if (engine.getTrackInstrument (fxTargetTrackId, hasInst, inst))
        {
            fxTargetHasInstrument = hasInst;
            fxTargetInstrument = inst;
        }
    }

    instLabel.setText (! fxTargetIsMidi      ? juce::String ("Inst: (none)")
                       : ! fxTargetHasInstrument ? juce::String ("Inst: (none)")
                       : fxTargetInstrument.missing
                             ? "Inst: " + fxTargetInstrument.name + " (missing)"
                             : "Inst: " + fxTargetInstrument.name,
                       juce::dontSendNotification);
    instLabel.setVisible (fxTargetIsMidi);
    instOpenButton.setVisible (fxTargetIsMidi && fxTargetHasInstrument);
    instOpenButton.setEnabled (fxTargetHasInstrument && ! fxTargetInstrument.missing);
    instAddButton.setVisible (fxTargetIsMidi);
    instAddButton.setButtonText (fxTargetHasInstrument ? "Change Instrument..."
                                                       : "Add Instrument...");
    instRemoveButton.setVisible (fxTargetIsMidi && fxTargetHasInstrument);
    layoutFxControls();

    // Rebuild the slot controls only when the chain actually changed.
    juce::String signature;

    if (hasTrack)
    {
        signature << fxTargetTrackId;

        for (auto& slot : chain)
            signature << '|' << slot.id << ':' << (slot.bypass ? 1 : 0)
                      << ':' << (slot.missing ? 1 : 0) << ':' << slot.name;

        signature << "|I:" << (fxTargetIsMidi ? 1 : 0)
                  << ':' << (fxTargetHasInstrument ? fxTargetInstrument.id : (juce::int64) -1)
                  << ':' << (fxTargetHasInstrument && fxTargetInstrument.missing ? 1 : 0);
    }

    if (signature == lastFxSignature)
        return;

    lastFxSignature = signature;
    fxControls.clear();

    if (! hasTrack)
        return;

    const auto trackId = fxTargetTrackId;

    for (auto& slot : chain)
    {
        FxSlotControls controls;
        controls.slotId = slot.id;
        const auto slotId = slot.id;
        const bool bypassed = slot.bypass;

        controls.nameLabel = std::make_unique<juce::Label>();
        styleLabel (*controls.nameLabel,
                    slot.missing ? slot.name + " (missing)" : slot.name);
        controls.nameLabel->setFont (juce::FontOptions (12.0f));
        controls.nameLabel->setTooltip (slot.missing
                                            ? slot.name + " (missing)"
                                            : slot.name + " - " + slot.manufacturerName);
        addAndMakeVisible (*controls.nameLabel);

        controls.openButton = std::make_unique<juce::TextButton> ("Open");
        controls.openButton->setWantsKeyboardFocus (false);
        controls.openButton->setEnabled (! slot.missing);
        controls.openButton->onClick = [this, slotId] { openPluginEditor (slotId); };
        addAndMakeVisible (*controls.openButton);

        controls.bypassButton = std::make_unique<juce::TextButton> ("Bypass");
        controls.bypassButton->setWantsKeyboardFocus (false);
        controls.bypassButton->setClickingTogglesState (true);
        controls.bypassButton->setToggleState (bypassed, juce::dontSendNotification);
        controls.bypassButton->setEnabled (! slot.missing);
        auto* bypassButton = controls.bypassButton.get();
        controls.bypassButton->onClick = [this, trackId, slotId, bypassButton]
        {
            engine.setFxBypass (trackId, slotId, bypassButton->getToggleState());
        };
        addAndMakeVisible (*controls.bypassButton);

        controls.removeButton = std::make_unique<juce::TextButton> ("Remove");
        controls.removeButton->setWantsKeyboardFocus (false);
        controls.removeButton->onClick = [this, trackId, slotId]
        {
            closePluginEditor (slotId);
            engine.removeFx (trackId, slotId);
            refreshFxPanel();
        };
        addAndMakeVisible (*controls.removeButton);

        fxControls.push_back (std::move (controls));
    }

    layoutFxControls();
}

void MainComponent::layoutFxControls()
{
    auto area = fxSlotsArea.reduced (0, 6);

    // M3: for a MIDI target track the instrument block comes first;
    // the FX slot rows (post-instrument effects) follow it.
    if (fxTargetIsMidi)
    {
        auto block = area.removeFromLeft (470);
        instLabel.setBounds (block.removeFromLeft (170));
        block.removeFromLeft (4);

        if (fxTargetHasInstrument)
        {
            instOpenButton.setBounds (block.removeFromLeft (84));
            block.removeFromLeft (4);
            instRemoveButton.setBounds (block.removeFromLeft (96));
            block.removeFromLeft (4);
            instAddButton.setBounds (block.removeFromLeft (128));
        }
        else
        {
            instAddButton.setBounds (block.removeFromLeft (128));
        }

        area.removeFromLeft (10);
    }

    for (auto& controls : fxControls)
    {
        if (area.getWidth() < 120)
            break;   // out of room: remaining slots stay unlaid-out (M2 scope)

        auto row = area.removeFromLeft (330);
        controls.nameLabel->setBounds (row.removeFromLeft (140));
        row.removeFromLeft (4);
        controls.openButton->setBounds (row.removeFromLeft (54));
        row.removeFromLeft (4);
        controls.bypassButton->setBounds (row.removeFromLeft (62));
        row.removeFromLeft (4);
        controls.removeButton->setBounds (row.removeFromLeft (58));
        area.removeFromLeft (10);
    }
}

void MainComponent::showAddFxMenu()
{
    if (fxTargetTrackId <= 0)
        return;

    auto& knownList = engine.getPluginManager().getKnownList();

    // All known plugins, sorted by manufacturer then name.
    std::vector<juce::PluginDescription> descriptions;

    for (auto& type : knownList.getTypes())
        descriptions.push_back (type);

    std::sort (descriptions.begin(), descriptions.end(),
               [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
               {
                   const int byMaker = a.manufacturerName.compareIgnoreCase (b.manufacturerName);
                   return byMaker != 0 ? byMaker < 0
                                       : a.name.compareIgnoreCase (b.name) < 0;
               });

    juce::PopupMenu menu;
    std::map<int, juce::PluginDescription> byItemId;
    int itemId = 100;
    juce::String lastManufacturer;

    if (descriptions.empty())
    {
        menu.addItem (1, "No plugins found - run Scan for Plugins... first", false);
    }
    else
    {
        for (auto& description : descriptions)
        {
            if (description.manufacturerName != lastManufacturer)
            {
                if (itemId > 100)
                    menu.addSeparator();

                lastManufacturer = description.manufacturerName;
            }

            byItemId[itemId] = description;
            menu.addItem (itemId++, description.name + " (" + description.manufacturerName + ")");
        }
    }

    const auto trackId = fxTargetTrackId;

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addFxButton),
                        [this, trackId, byItemId] (int result)
    {
        const auto it = byItemId.find (result);

        if (it == byItemId.end())
            return;

        juce::String error;

        if (engine.addFxToTrack (trackId, it->second, error))
        {
            setStatus ("Added " + it->second.name + ".");
            refreshFxPanel();
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::AlertWindow::WarningIcon, "YM-Kithara", error);
        }
    });
}

//==============================================================================
// Instruments, piano roll and WAV export (M3)
//==============================================================================
void MainComponent::showAddInstrumentMenu()
{
    if (fxTargetTrackId <= 0 || ! fxTargetIsMidi)
        return;

    auto& knownList = engine.getPluginManager().getKnownList();

    // Known instruments only (effects are added via Add FX...).
    std::vector<juce::PluginDescription> descriptions;

    for (auto& type : knownList.getTypes())
        if (type.isInstrument)
            descriptions.push_back (type);

    std::sort (descriptions.begin(), descriptions.end(),
               [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
               {
                   const int byMaker = a.manufacturerName.compareIgnoreCase (b.manufacturerName);
                   return byMaker != 0 ? byMaker < 0
                                       : a.name.compareIgnoreCase (b.name) < 0;
               });

    juce::PopupMenu menu;
    std::map<int, juce::PluginDescription> byItemId;
    int itemId = 100;
    juce::String lastManufacturer;

    if (descriptions.empty())
    {
        menu.addItem (1, "No instruments found - run Scan for Plugins... first", false);
    }
    else
    {
        for (auto& description : descriptions)
        {
            if (description.manufacturerName != lastManufacturer)
            {
                if (itemId > 100)
                    menu.addSeparator();

                lastManufacturer = description.manufacturerName;
            }

            byItemId[itemId] = description;
            menu.addItem (itemId++, description.name + " (" + description.manufacturerName + ")");
        }
    }

    const auto trackId = fxTargetTrackId;

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (instAddButton),
                        [this, trackId, byItemId] (int result)
    {
        const auto it = byItemId.find (result);

        if (it == byItemId.end())
            return;

        juce::String error;

        if (engine.setTrackInstrument (trackId, it->second, error))
        {
            setStatus ("Instrument set: " + it->second.name + ".");
            refreshFxPanel();
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::AlertWindow::WarningIcon, "YM-Kithara", error);
        }
    });
}

void MainComponent::openPianoRoll (juce::int64 trackId, juce::int64 clipId)
{
    closePianoRoll();

    juce::String trackName = "MIDI";

    {
        juce::ScopedLock lock (engine.getModelLock());

        if (auto* track = engine.findTrack (trackId))
            trackName = track->name;
    }

    auto* content = new PianoRollComponent (engine, trackId, clipId);
    pianoRoll = content;

    pianoRollWindow = std::make_unique<PianoRollWindow> (
        trackName + " - Piano Roll",
        [this]
        {
            // Defer: a window must not be destroyed from inside its own
            // close-button handler.
            juce::MessageManager::callAsync ([this] { closePianoRoll(); });
        });

    pianoRollWindow->setContentOwned (content, true);
    pianoRollWindow->centreWithSize (content->getWidth(), content->getHeight());
    pianoRollWindow->setVisible (true);
}

void MainComponent::closePianoRoll()
{
    pianoRollWindow.reset();
    pianoRoll = nullptr;
}

//==============================================================================
// Video import and preview (M6)
//==============================================================================
void MainComponent::importVideo()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Add video or image to the video track...",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.mp4;*.mov;*.mkv;*.m4v;*.jpg;*.jpeg;*.png;*.bmp");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& chooser)
    {
        juce::StringArray names;

        for (auto& file : chooser.getResults())
            names.add (file.getFullPathName());

        importFileList (names);
        fileChooser.reset();
    });
}

void MainComponent::refreshExtractAudioButton()
{
    // The Extract Audio button is enabled exactly while the video
    // track holds clips. (In M6.2 this lived in refreshDockedMonitor;
    // the monitor is gone in M6.3, the button logic stays.)
    bool hasClips = false;

    {
        juce::ScopedLock lock (engine.getModelLock());

        for (auto& track : engine.getSession().tracks)
            if (track.kind == TrackKind::video && ! track.videoClips.empty())
                hasClips = true;
    }

    extractAudioButton.setEnabled (hasClips);
}

void MainComponent::extractVideoAudio()
{
    bool hasClips = false;

    {
        juce::ScopedLock lock (engine.getModelLock());

        for (auto& track : engine.getSession().tracks)
            if (track.kind == TrackKind::video && ! track.videoClips.empty())
                hasClips = true;
    }

    if (! hasClips)
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::InfoIcon, "Extract Audio",
            "There is no video track with clips yet.\n\n"
            "Use Add Video... first; Extract Audio then pulls each video "
            "file's own sound into the session as a normal audio track.");
        return;
    }

    extractProgress.store (0.0f);
    extractCancel.store (false);
    extractResult = {};

    ExtractAudioThread thread (engine, extractProgress, extractCancel, extractResult);
    thread.runThread();

    const auto& result = extractResult;

    if (result.cancelled)
    {
        setStatus ("Video audio extraction cancelled.");
        return;
    }

    juce::StringArray report;

    for (auto& name : result.createdTracks)
        report.add ("Created track: " + name);

    for (auto& name : result.existingTracks)
        report.add ("Already extracted (skipped): " + name);

    for (auto& file : result.filesWithoutAudio)
        report.add ("No audio stream, skipped: " + file);

    report.addArray (result.fileErrors);

    if (result.createdTracks.isEmpty() && report.isEmpty())
    {
        setStatus ("Extract Audio: nothing to do.");
        return;
    }

    if (! result.createdTracks.isEmpty())
        setStatus ("Extracted video audio: "
                   + juce::StringArray (result.createdTracks).joinIntoString (", "));

    // Anything besides a clean creation deserves a visible report —
    // in particular WHICH file had no audio (never a silent skip).
    if (! result.existingTracks.isEmpty() || ! result.filesWithoutAudio.isEmpty()
         || ! result.fileErrors.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::InfoIcon, "Extract Audio",
            juce::StringArray (report).joinIntoString ("\n"));
    }
}

void MainComponent::openVideoPreview()
{
    if (videoWindow != nullptr)
    {
        videoWindow->toFront (true);
        return;
    }

    auto* content = new VideoPreviewComponent (videoFeed);
    videoPreview = content;

    videoWindow = std::make_unique<VideoPreviewWindow> (
        "YM-Kithara - Video",
        [this]
        {
            // Defer: a window must not be destroyed from inside its
            // own close-button handler.
            juce::MessageManager::callAsync ([this] { closeVideoPreview(); });
        });

    videoWindow->setContentOwned (content, true);
    videoWindow->centreWithSize (content->getWidth(), content->getHeight());
    videoWindow->setVisible (true);
    videoButton.setToggleState (true, juce::dontSendNotification);
}

void MainComponent::closeVideoPreview()
{
    videoWindow.reset();
    videoPreview = nullptr;
    videoButton.setToggleState (false, juce::dontSendNotification);
}

//==============================================================================
// SRT import and the telop text editor (M6.3)
//==============================================================================
void MainComponent::importSrt()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Import an SRT subtitle file as telop clips...",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.srt");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        fileChooser.reset();

        if (file == juce::File())
            return;

        importSrtFile (file);
    });
}

void MainComponent::importSrtFile (const juce::File& file)
{
    TimelineEngine::SrtImportResult result;
    juce::String error;

    if (! engine.importSrtFile (file, result, error))
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon, "Import SRT", error);
        return;
    }

    juce::String message = "SRT import: " + juce::String (result.imported)
                           + " telop clip(s) added";

    if (result.skipped > 0)
        message += ", " + juce::String (result.skipped)
                   + " entr" + (result.skipped == 1 ? "y" : "ies") + " skipped";

    message += ".";
    setStatus (message);
    updateTimelineSize();
    timeline.repaint();

    // The counts are the point of the report: always confirm them
    // (an alert also when nothing was skipped, so a silent 0-import
    // can never masquerade as success).
    juce::AlertWindow::showMessageBoxAsync (
        juce::AlertWindow::InfoIcon, "Import SRT",
        message + (result.skipped > 0
                       ? "\n\nSkipped entries had a bad timing line, an end at "
                         "or before the start, or no text."
                       : juce::String()));
}

void MainComponent::openTextClipEditor (juce::int64 /*trackId*/, juce::int64 clipId)
{
    TextClipData clip;
    juce::int64 ownerTrack = -1;

    if (! engine.getTextClipData (clipId, clip, ownerTrack))
        return;

    closeTextClipEditor();

    auto* content = new TextClipEditorComponent (
        clip.text,
        [this, clipId] (const juce::String& newText)
        {
            engine.setTextClipText (clipId, newText);
            setStatus ("Telop text updated.");
        },
        [this]
        {
            // Defer: the window must not be destroyed from inside its
            // own button handler (same pattern as the piano roll).
            juce::MessageManager::callAsync ([this] { closeTextClipEditor(); });
        });

    textEditorWindow = std::make_unique<TextClipEditorWindow> (
        "Edit Telop Text",
        [this]
        {
            juce::MessageManager::callAsync ([this] { closeTextClipEditor(); });
        });

    textEditorWindow->setContentOwned (content, true);
    textEditorWindow->centreWithSize (content->getWidth(), content->getHeight());
    textEditorWindow->setVisible (true);
}

void MainComponent::closeTextClipEditor()
{
    textEditorWindow.reset();
}

void MainComponent::refreshTimecodeFpsBox()
{
    int num = 30, den = 1;
    bool userSet = false;
    engine.getTimecodeFps (num, den, userSet);

    if (num == lastTimecodeNum && den == lastTimecodeDen)
        return;

    lastTimecodeNum = num;
    lastTimecodeDen = den;

    refreshingTimecodeBox = true;
    const int itemId = timecodeItemForRate (num, den);

    if (itemId > 0)
    {
        timecodeFpsBox.setSelectedId (itemId, juce::dontSendNotification);
    }
    else
    {
        // A rate outside the fixed list (adopted from an unusual
        // clip): show it as text until the user picks a listed rate.
        timecodeFpsBox.setSelectedId (0, juce::dontSendNotification);
        timecodeFpsBox.setTextWhenNothingSelected (
            juce::String ((double) num / (double) juce::jmax (1, den), 3));
    }

    refreshingTimecodeBox = false;
}

void MainComponent::exportMixdown()
{
    if (engine.getTotalLengthSamples() <= 0)
    {
        setStatus ("Nothing to export yet.");
        return;
    }

    fileChooser = std::make_unique<juce::FileChooser> (
        "Export WAV mixdown...",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory)
            .getChildFile ("mixdown.wav"),
        "*.wav");

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& chooser)
    {
        auto target = chooser.getResult();
        fileChooser.reset();

        if (target == juce::File())
            return;

        if (target.getFileExtension().isEmpty())
            target = target.withFileExtension ("wav");

        // The export takes over the plugin instances; stop playback
        // first (the engine also keeps the audio callback out for the
        // duration of the render).
        engine.pause();
        refreshTransportState();

        exportProgress.store (0.0f);
        ExportThread thread (engine, target, exportProgress);
        thread.runThread();

        if (thread.resultOk.load())
        {
            setStatus ("Exported: " + target.getFullPathName());
        }
        else if (thread.cancelled && thread.resultError.isEmpty())
        {
            setStatus ("Export cancelled.");
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::AlertWindow::WarningIcon, "YM-Kithara",
                thread.resultError.isNotEmpty() ? thread.resultError
                                                : juce::String ("The export failed."));
        }
    });
}

bool MainComponent::getVideoExportInfo (int& clipWidth, int& clipHeight,
                                        juce::String& rateText)
{
    juce::ScopedLock lock (engine.getModelLock());
    const VideoClipData* earliest = nullptr;

    for (auto& track : engine.getSession().tracks)
    {
        if (track.kind != TrackKind::video)
            continue;

        for (auto& clip : track.videoClips)
            if (earliest == nullptr || clip.startSeconds < earliest->startSeconds)
                earliest = &clip;
    }

    if (earliest == nullptr)
        return false;

    clipWidth = earliest->width;
    clipHeight = earliest->height;

    int num = 30, den = 1;
    bool userSet = false;
    engine.getTimecodeFps (num, den, userSet);
    const double rate = (double) num / (double) juce::jmax (1, den);
    rateText = juce::String (rate, (den == 1) ? 0 : 2) + " fps";
    return true;
}

void MainComponent::exportMp4()
{
    if (engine.getTotalLengthSamples() <= 0)
    {
        setStatus ("Nothing to export yet.");
        return;
    }

    int clipWidth = 0, clipHeight = 0;
    juce::String rateText;

    if (! getVideoExportInfo (clipWidth, clipHeight, rateText))
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::InfoIcon, "YM-Kithara",
            "Export MP4 needs a video track.\n"
            "Use \"Add Video...\" to import a video clip first, then export.");
        setStatus ("Export MP4 needs a video track.");
        return;
    }

    //--- options dialog (size + quality; the frame rate is always the
    //    session timecode rate)
    juce::AlertWindow dialog ("Export MP4", "Choose the export settings.",
                              juce::AlertWindow::NoIcon);

    const int originalWidth  = juce::jmax (2, (clipWidth + 1) & ~1);
    const int originalHeight = juce::jmax (2, (clipHeight + 1) & ~1);

    dialog.addComboBox ("size",
                        { "Original (" + juce::String (originalWidth) + " x "
                              + juce::String (originalHeight) + ")",
                          "1920 x 1080 (Full HD)",
                          "1280 x 720 (HD)",
                          "1080 x 1920 (vertical 9:16)" },
                        "Picture size");
    dialog.addComboBox ("quality",
                        { "High (CRF 18)", "Standard (CRF 23)", "Light (CRF 28)" },
                        "Quality");
    dialog.addTextBlock ("Frame rate: session timecode rate (" + rateText + "). "
                         "Sources are fitted with black bars; nothing is stretched or cropped.");
    dialog.addButton ("Export...", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog.addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    if (auto* sizeBox = dialog.getComboBoxComponent ("size"))
        sizeBox->setSelectedItemIndex (0);

    if (auto* qualityBox = dialog.getComboBoxComponent ("quality"))
        qualityBox->setSelectedItemIndex (1);

    if (dialog.runModalLoop() != 1)
        return;

    Mp4ExportOptions options;

    if (auto* sizeBox = dialog.getComboBoxComponent ("size"))
    {
        switch (sizeBox->getSelectedItemIndex())
        {
            case 1:  options.width = 1920; options.height = 1080; break;
            case 2:  options.width = 1280; options.height = 720;  break;
            case 3:  options.width = 1080; options.height = 1920; break;
            default: options.width = 0;    options.height = 0;    break;
        }
    }

    if (auto* qualityBox = dialog.getComboBoxComponent ("quality"))
    {
        switch (qualityBox->getSelectedItemIndex())
        {
            case 0:  options.crf = 18; break;
            case 2:  options.crf = 28; break;
            default: options.crf = 23; break;
        }
    }

    fileChooser = std::make_unique<juce::FileChooser> (
        "Export MP4...",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory)
            .getChildFile ("movie.mp4"),
        "*.mp4");

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, options] (const juce::FileChooser& chooser)
    {
        auto target = chooser.getResult();
        fileChooser.reset();

        if (target == juce::File())
            return;

        if (target.getFileExtension().isEmpty())
            target = target.withFileExtension ("mp4");

        engine.pause();
        refreshTransportState();

        mp4ExportProgress.store (0.0f);
        mp4ExportCancel.store (false);
        Mp4ExportThread thread (engine, target, options,
                                mp4ExportProgress, mp4ExportCancel);
        thread.runThread();

        if (thread.resultOk.load())
        {
            setStatus ("Exported: " + target.getFullPathName());
        }
        else if (mp4ExportCancel.load())
        {
            setStatus ("MP4 export cancelled.");
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::AlertWindow::WarningIcon, "YM-Kithara",
                thread.resultError.isNotEmpty() ? thread.resultError
                                                : juce::String ("The MP4 export failed."));
        }
    });
}

void MainComponent::scanForPlugins()
{
    if (scanProcess != nullptr)
        return;   // a scan is already running

    auto& pluginManager = engine.getPluginManager();
    const auto scanner = pluginManager.findScannerExecutable();

    if (! scanner.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon, "YM-Kithara",
            "The plugin scanner (KitharaVstScanner) was not found.\n"
            "It should be in the same folder as the YM-Kithara application.\n"
            "Rebuild the project so both programs are produced.");
        return;
    }

    // M6.10: do NOT delete a previous results file here. The scanner
    // loads it at startup and skips up-to-date entries, so a scan
    // after a crash or a watchdog kill RESUMES instead of restarting
    // from zero — and a completed scan makes the next one cheap.
    const auto resultsFile = pluginManager.getScanResultsFile();

    scanProcess = std::make_unique<juce::ChildProcess>();

    const juce::StringArray scanArgs { scanner.getFullPathName(),
                                       resultsFile.getFullPathName() };

    if (! scanProcess->start (scanArgs))
    {
        scanProcess.reset();
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon, "YM-Kithara",
            "Could not start the plugin scanner:\n" + scanner.getFullPathName());
        return;
    }

    scanStartTime = juce::Time::getCurrentTime();
    scanLastProgressTime = scanStartTime;   // M6.10: progress baseline
    scanShownPlugin.clear();
    scanPluginsButton.setEnabled (false);
    setStatus ("Scanning for VST3 plugins...");

    scanProgressWindow = std::make_unique<juce::AlertWindow> (
        "YM-Kithara", "Scanning for VST3 plugins...\n"
                      "A large collection can take a while; progress is "
                      "saved as the scan goes, so stopping and scanning "
                      "again resumes where it left off.",
        juce::AlertWindow::NoIcon);
    scanProgressWindow->setAlwaysOnTop (true);
    scanProgressWindow->setVisible (true);
}

void MainComponent::pollScanProcess()
{
    if (scanProcess == nullptr)
        return;

    auto& pluginManager = engine.getPluginManager();
    const auto resultsFile = pluginManager.getScanResultsFile();

    if (scanProcess->isRunning())
    {
        const auto now = juce::Time::getCurrentTime();

        // M6.10: progress = the results file's modification time
        // advancing (the scanner rewrites it after every plugin).
        // Seed the baseline with the file's pre-scan state so the
        // first advance can be told apart from a leftover file.
        if (resultsFile.existsAsFile())
        {
            const auto modified = resultsFile.getLastModificationTime();

            if (modified > scanLastProgressTime)
                scanLastProgressTime = modified;
        }

        // M6.10: show the file currently being scanned. The pedal
        // file records it while the scanner works on it (and doubles
        // as the dead-man's switch after a crash).
        if (scanProgressWindow != nullptr)
        {
            auto current = juce::File (
                pluginManager.getDeadMansPedalFile().loadFileAsString().trim())
                               .getFileName();

            if (current.isEmpty())
                current = scanShownPlugin;

            if (current.isNotEmpty() && current != scanShownPlugin)
            {
                scanShownPlugin = current;
                scanProgressWindow->setMessage (
                    "Scanning for VST3 plugins...\n\nNow scanning:\n" + current);
            }
        }

        const double sinceProgress = (now - scanLastProgressTime).inSeconds();
        const double sinceStart = (now - scanStartTime).inSeconds();

        if (! ScanWatchdog::shouldKill (sinceProgress, sinceStart))
            return;

        // A plugin has hung the scanner (or the absolute backstop
        // fired). Kill it, but keep everything found so far: import
        // the partial results instead of discarding the whole scan.
        const bool stalled = sinceProgress >= ScanWatchdog::stallSeconds;

        scanProcess->kill();
        scanProcess.reset();
        scanProgressWindow.reset();
        scanPluginsButton.setEnabled (true);

        juce::String error;
        const int count = pluginManager.reloadFromScanResults (error);

        if (count >= 0)
            refreshFxPanel();

        setStatus ("Plugin scan stopped; "
                   + (count >= 0 ? juce::String (count) + " plugin(s) kept."
                                 : juce::String ("no results could be kept.")));

        juce::String message;

        if (stalled)
        {
            message = "The plugin scan stalled and was stopped.\n\n";

            if (scanShownPlugin.isNotEmpty())
                message += "It stopped while scanning:\n" + scanShownPlugin + "\n\n";

            message += count >= 0
                ? "Everything found up to that point has been kept: "
                    + juce::String (count) + " plugin(s) are now in the list.\n\n"
                : "No usable results could be kept from this scan.\n\n";

            message += "The stalled plugin will be moved to the end of "
                       "the scan order (and blacklisted) on the next scan, "
                       "and scanning again RESUMES from the results already "
                       "saved — it does not start over.";
        }
        else
        {
            message = "The plugin scan reached the one-hour limit and was "
                      "stopped.\n\n"
                    + (count >= 0
                        ? "Everything found so far has been kept: "
                            + juce::String (count) + " plugin(s) are now in the list.\n\n"
                        : "No usable results could be kept from this scan.\n\n")
                    + "Press Scan for Plugins again to RESUME from the "
                      "results already saved — it does not start over.";
        }

        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon, "YM-Kithara", message);

        return;
    }

    const auto exitCode = (juce::uint32) scanProcess->getExitCode();
    scanProcess.reset();
    scanProgressWindow.reset();
    scanPluginsButton.setEnabled (true);

    if (exitCode != 0)
    {
        setStatus ("Plugin scan failed.");

        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon, "YM-Kithara",
            "The plugin scanner failed (exit code " + juce::String ((int) exitCode) + ").");
        return;
    }

    juce::String error;
    const int count = engine.getPluginManager().reloadFromScanResults (error);

    if (count < 0)
    {
        setStatus ("Plugin scan produced no results.");

        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon, "YM-Kithara", error);
        return;
    }

    setStatus ("Plugin scan complete: " + juce::String (count) + " plugin(s) found.");
    refreshFxPanel();
}

//==============================================================================
// Plugin editor windows
//==============================================================================
void MainComponent::openPluginEditor (juce::int64 slotId)
{
    if (auto existing = pluginEditors.find (slotId); existing != pluginEditors.end())
    {
        existing->second->toFront (true);
        return;
    }

    auto* instance = engine.getFxInstance (slotId);

    if (instance == nullptr)
        return;

    auto* editor = instance->createEditorAndMakeActive();

    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor (*instance);

    auto window = std::make_unique<PluginEditorWindow> (
        instance->getName(),
        [this, slotId]
        {
            // Defer: a window must not be destroyed from inside its own
            // close-button handler.
            juce::MessageManager::callAsync ([this, slotId] { closePluginEditor (slotId); });
        });

    window->setContentOwned (editor, true);
    window->centreWithSize (window->getWidth(), window->getHeight());
    window->setVisible (true);

    pluginEditors[slotId] = std::move (window);
}

void MainComponent::closePluginEditor (juce::int64 slotId)
{
    pluginEditors.erase (slotId);

    // A plugin's reported latency can change with its settings, so
    // re-resolve the chains (and PDC) after an editor closes.
    engine.refreshFxState();
}

void MainComponent::prunePluginEditors()
{
    for (auto it = pluginEditors.begin(); it != pluginEditors.end();)
    {
        if (engine.hasFxSlot (it->first))
            ++it;
        else
            it = pluginEditors.erase (it);
    }
}

//==============================================================================
// File import / drag & drop
//==============================================================================
bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    return ! files.isEmpty();
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    importFileList (files);
}

void MainComponent::importFileList (const juce::StringArray& fileNames)
{
    int audioImported = 0;
    int midiImported = 0;
    juce::String midiStatus;   // result message of the last MIDI import
    juce::StringArray failed;

    for (auto& name : fileNames)
    {
        const juce::File file (name);

        if (! file.existsAsFile())
        {
            failed.add (file.getFileName());
            continue;
        }

        // M6: video files append to the (single) video track.
        // M6.5: still images (jpg/png/bmp) take the same path and
        // land as free-duration clips on that track.
        if (file.hasFileExtension ("mp4;mov;mkv;m4v;jpg;jpeg;png;bmp"))
        {
            juce::String error;
            const auto clipId = engine.importVideoFile (file, error);

            if (clipId > 0)
            {
                VideoClipData clip;
                juce::int64 trackId = -1;

                if (engine.getVideoClipData (clipId, clip, trackId)
                     && clip.stillImage)
                    setStatus ("Image added: " + file.getFileName()
                               + " (still image, placed for 5 seconds"
                               + " - stretch it as long as you like).");
                else
                    setStatus ("Video added: " + file.getFileName()
                               + " (its own audio is not used in Phase 1).");
            }
            else
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon, "YM-Kithara", error);

            continue;
        }

        // M6.3: SRT files become telop clips on the text track (the
        // shared import shows the imported/skipped report itself).
        if (file.hasFileExtension ("srt"))
        {
            importSrtFile (file);
            continue;
        }

        // M3.1: Standard MIDI Files route to the MIDI importer; the
        // engine's message goes to the status line on success, and a
        // failure is shown as an alert so the reason stays visible.
        if (file.hasFileExtension ("mid;midi"))
        {
            juce::String message;

            if (engine.importMidiFile (file, message))
            {
                ++midiImported;
                midiStatus = message;
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon, "YM-Kithara", message);
            }

            continue;
        }

        if (engine.importFileAsNewTrack (file) > 0)
            ++audioImported;
        else
            failed.add (file.getFileName());
    }

    if (midiImported > 0 && audioImported > 0)
        setStatus (midiStatus + " Also imported " + juce::String (audioImported)
                   + " audio file(s) as new tracks.");
    else if (midiImported > 0)
        setStatus (midiStatus);
    else if (audioImported > 0)
        setStatus ("Imported " + juce::String (audioImported) + " file(s) as new tracks.");

    if (! failed.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon, "YM-Kithara",
            "Could not import:\n" + failed.joinIntoString ("\n"));
    }

    updateTimelineSize();
    timeline.repaint();
}

void MainComponent::importFiles()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Import audio / MIDI files (each becomes a new track)...",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav;*.mp3;*.flac;*.aif;*.aiff;*.ogg;*.mid;*.midi;*.mp4;*.mov;*.mkv;*.m4v;*.jpg;*.jpeg;*.png;*.bmp");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& chooser)
    {
        juce::StringArray names;

        for (auto& file : chooser.getResults())
            names.add (file.getFullPathName());

        importFileList (names);
        fileChooser.reset();
    });
}

//==============================================================================
// Projects
//==============================================================================
void MainComponent::newProject()
{
    closePianoRoll();
    engine.newProject();
    timeline.clearSelection();
    setStatus ("New project. Import audio files to create tracks.");
    updateTimelineSize();
    timeline.repaint();
}

void MainComponent::saveProject()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose a folder for the project...",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory));

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& chooser)
    {
        auto folder = chooser.getResult();

        if (folder != juce::File())
        {
            juce::String error;

            if (engine.saveProject (folder, error))
                setStatus ("Project saved to " + folder.getFullPathName());
            else
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon, "YM-Kithara", error);
        }

        fileChooser.reset();
    });
}

void MainComponent::openProject()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Open a project folder (containing project.xml)...",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory));

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& chooser)
    {
        auto folder = chooser.getResult();

        if (folder != juce::File())
        {
            closePianoRoll();
            juce::StringArray warnings;

            if (engine.loadProject (folder, warnings))
            {
                timeline.clearSelection();
                setStatus ("Project loaded: " + folder.getFileName());

                if (! warnings.isEmpty())
                {
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::AlertWindow::WarningIcon, "YM-Kithara",
                        "These source files were missing, so their clips were dropped:\n"
                            + warnings.joinIntoString ("\n"));
                }
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon, "YM-Kithara",
                    "That folder does not contain a valid project.xml.");
            }

            updateTimelineSize();
            timeline.repaint();
        }

        fileChooser.reset();
    });
}

//==============================================================================
void MainComponent::openAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (
        engine.getDeviceManager(),
        0, 0,                       // min / max input channels
        0, 2,                       // min / max output channels
        false,                      // show MIDI input options
        false,                      // show MIDI output options
        true,                       // stereo output only
        false);                     // hide advanced options

    selector->setSize (520, 420);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector);
    options.dialogTitle = "Audio Settings";
    options.dialogBackgroundColour = panelColour;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

juce::String MainComponent::formatTime (double seconds)
{
    if (seconds < 0.0 || ! std::isfinite (seconds))
        seconds = 0.0;

    const int minutes = (int) std::floor (seconds / 60.0);
    const double secs = seconds - minutes * 60.0;
    return juce::String::formatted ("%d:%04.1f", minutes, secs);
}
