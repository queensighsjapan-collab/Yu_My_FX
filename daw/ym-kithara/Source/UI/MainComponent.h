#pragma once

#include <JuceHeader.h>
#include "TimelineEngine.h"
#include "TimelineComponent.h"
#include "TrackHeaderComponent.h"
#include "PianoRollComponent.h"
#include "VideoPreviewComponent.h"

//==============================================================================
/**
    MasterMeterComponent (M3.2) — compact horizontal stereo master meter
    for the transport row: two bars (L over R) plus a clip LED at the
    right end that latches at full scale and clears on a click. A 30 Hz
    timer reads + clears the engine's master peak slot and applies
    instant attack / exponential decay, mirroring the per-track header
    meters in TimelineComponent.
*/
class MasterMeterComponent final : public juce::Component,
                                   private juce::Timer
{
public:
    explicit MasterMeterComponent (TimelineEngine& engineToUse);
    ~MasterMeterComponent() override;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    juce::Rectangle<int> getLedRect() const;

    TimelineEngine& engine;
    float displayLeft = 0.0f;
    float displayRight = 0.0f;
    bool clipLatched = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterMeterComponent)
};

//==============================================================================
/**
    TimelineViewport (M3.3) — the lanes+ruler Viewport, with a hook so
    MainComponent can keep the pinned TrackHeaderComponent's vertical
    offset glued to the lanes' scroll position (the header column is
    outside this viewport and never scrolls horizontally).
*/
class TimelineViewport final : public juce::Viewport
{
public:
    std::function<void()> onViewPositionChanged;

private:
    void visibleAreaChanged (const juce::Rectangle<int>&) override
    {
        if (onViewPositionChanged != nullptr)
            onViewPositionChanged();
    }
};

//==============================================================================
/**
    MainComponent — the M6 main window of YM-Kithara.

    Layout (top to bottom):
        header row    "YM-Kithara" + M6 tag | New / Open Project... /
                      Save Project... / Import File... / Add Video... /
                      Add MIDI Track / Export WAV... / Export MP4... (M6.2) /
                      Audio Settings...
        transport     Play/Pause | Stop | Return | Video preview
                      toggle (M6) | position readout | TIMECODE
                      readout + fps selector (M6) | Tempo editor |
                      MIDI IN status (M3.3) | MASTER meter (M3.2) |
                      Volume (master gain)
        status line   last action / project messages
        timeline      pinned track-header column (M3.3, outside the
                      Viewport) + ruler and lanes in a scrolling
                      Viewport, so horizontal scrolling moves only
                      the waveforms
        bottom bar    FX row: the selected track's VST3 insert chain
                      (Add FX... / Scan for Plugins... + slot rows); for
                      a MIDI track an instrument block ("Inst: ...",
                      Open Inst / Add Instrument... / Remove Inst) sits
                      at the start of the row
                      inspector row: selected-clip fades, gain, Split,
                      Delete, horizontal Zoom and vertical V Zoom sliders

    The FX panel targets the track of the currently selected clip —
    audio or MIDI (the target stays until another clip is selected).
    Plugin editors open in floating non-modal windows, one per slot,
    and are closed when their slot, track or project goes away. The
    piano roll (double-click a MIDI clip) is a single floating window,
    closed when its clip goes away.

    Space toggles play/pause (handled in keyPressed below); it is not
    stolen from text editors or focused buttons, and modal dialogs keep
    the keyboard for themselves.

    Audio files can also be dropped straight onto the window; each file
    becomes a new track with one clip at position 0 (the stem workflow).
    Standard MIDI Files (.mid) dropped or picked the same way are routed
    to the engine's MIDI importer instead (M3.1).
*/
class MainComponent final : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer
{
public:
    explicit MainComponent (TimelineEngine& engineToUse);
    ~MainComponent() override;

    //==========================================================================
    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    // juce::FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // M6.3: frame-feed state for the headless UI smoke test (the
    // M6.2 docked monitor this used to observe was removed — the
    // feed now only runs while the preview window is open).
    bool videoFeedHasImage() const { return videoFeed.getImage().isValid(); }

    /** Headless smoke access: registers a no-op feed listener,
        which starts the feed exactly as opening the preview window
        does (a real window cannot be created without a display, so
        the smoke drives the feed through this instead). Returns the
        listener id. */
    int addVideoFeedTestListener()
    {
        return videoFeed.addListener ([] {});
    }

    /** Headless smoke access: the feed's currently published frame. */
    const juce::Image& videoFeedImageForTest() const { return videoFeed.getImage(); }

private:
    //==========================================================================
    struct PluginEditorWindow;
    struct PianoRollWindow;
    struct VideoPreviewWindow;
    struct TextClipEditorWindow;   // M6.3
    struct ExportThread;
    struct Mp4ExportThread;
    struct ExtractAudioThread;

    void timerCallback() override;

    void openAudioSettings();
    void importFiles();
    void importFileList (const juce::StringArray& fileNames);
    void newProject();
    void saveProject();
    void openProject();

    // M3
    void exportMixdown();
    void exportMp4();   // M6.2
    bool getVideoExportInfo (int& clipWidth, int& clipHeight,
                             juce::String& rateText);   // M6.2
    void showAddInstrumentMenu();
    void openPianoRoll (juce::int64 trackId, juce::int64 clipId);
    void closePianoRoll();

    // M6
    void importVideo();
    void openVideoPreview();
    void closeVideoPreview();
    void refreshTimecodeFpsBox();

    // M6.2 additions
    void extractVideoAudio();
    void refreshExtractAudioButton();

    // M6.3 additions: SRT import (chooser + shared import/report) and
    // the telop text editor window.
    void importSrt();
    void importSrtFile (const juce::File& file);
    void openTextClipEditor (juce::int64 trackId, juce::int64 clipId);
    void closeTextClipEditor();

    void refreshTransportState();
    void refreshMidiInLabel();   // M3.3
    void refreshInspector();
    void updateTimelineSize();
    void setStatus (const juce::String& text);

    // M2 FX panel
    void refreshFxPanel();
    void layoutFxControls();
    void showAddFxMenu();
    void scanForPlugins();
    void pollScanProcess();
    void openPluginEditor (juce::int64 slotId);
    void closePluginEditor (juce::int64 slotId);
    void prunePluginEditors();

    static juce::String formatTime (double seconds);

    TimelineEngine& engine;

    juce::Label titleLabel;
    juce::Label subtitleLabel;      // M6.7: "AV Workstation" under the name
    juce::Label tagLabel;
    juce::Label statusLabel;
    juce::Label positionLabel;
    juce::Label timecodeLabel;      // M6: HH:MM:SS;FF at the session rate
    juce::Label tempoLabel;
    juce::Label volumeLabel;
    juce::Label masterMeterLabel;   // M3.2
    juce::Label midiInLabel;        // M3.3: live MIDI input status
    juce::Label inspectorTitleLabel;
    juce::Label fadeInLabel;
    juce::Label fadeOutLabel;
    juce::Label clipGainLabel;
    juce::Label zoomLabel;
    juce::Label vZoomLabel;
    juce::Label fxTitleLabel;

    juce::TextButton newProjectButton  { "New" };
    juce::TextButton openProjectButton { "Open Project..." };
    juce::TextButton saveProjectButton { "Save Project..." };
    juce::TextButton importButton      { "Import File..." };
    juce::TextButton addVideoButton    { "Add Video..." };   // M6
    juce::TextButton importSrtButton   { "Import SRT..." };   // M6.3
    juce::TextButton extractAudioButton { "Extract Audio" };   // M6.2
    juce::TextButton addMidiTrackButton { "Add MIDI Track" };
    juce::TextButton exportButton      { "Export WAV..." };
    juce::TextButton exportMp4Button   { "Export MP4..." };   // M6.2
    juce::TextButton settingsButton    { "Audio Settings..." };

    juce::TextButton playButton   { "Play" };
    juce::TextButton recordButton { "REC" };   // M4 (label fixed in M6.9:
                                               // the original ● glyph was a
                                               // narrow char* literal, which
                                               // juce::String reads as ASCII —
                                               // it mojibaked on Windows)
    juce::TextButton stopButton   { "Stop" };
    juce::TextButton returnButton { "|<" };
    juce::TextButton videoButton  { "Video" };   // M6: preview window toggle

    juce::ComboBox timecodeFpsBox;   // M6: session timecode rate override

    juce::TextButton splitButton      { "Split at Playhead" };
    juce::TextButton deleteClipButton { "Delete Clip" };

    juce::TextButton addFxButton       { "Add FX..." };
    juce::TextButton scanPluginsButton { "Scan for Plugins..." };

    juce::TextEditor tempoEditor;

    juce::Slider volumeSlider;
    juce::Slider fadeInSlider;
    juce::Slider fadeOutSlider;
    juce::Slider clipGainSlider;
    juce::Slider zoomSlider;
    juce::Slider vZoomSlider;

    TimelineViewport timelineViewport;   // M3.3: lanes + ruler only
    TrackHeaderComponent trackHeaders;   // M3.3: pinned header column
    TimelineComponent timeline;
    MasterMeterComponent masterMeter;   // M3.2 (constructed with engine)

    juce::String lastMidiInText;   // M3.3: change guard for midiInLabel

    std::unique_ptr<juce::FileChooser> fileChooser;

    int lastModelVersion = -1;
    juce::int64 inspectorClipId = -1;
    bool refreshingInspector = false;

    //==========================================================================
    // M2 FX panel state.
    struct FxSlotControls
    {
        juce::int64 slotId = 0;
        std::unique_ptr<juce::Label> nameLabel;
        std::unique_ptr<juce::TextButton> openButton;
        std::unique_ptr<juce::TextButton> bypassButton;
        std::unique_ptr<juce::TextButton> removeButton;
    };

    juce::int64 fxTargetTrackId = -1;   // track of the last selected clip
    std::vector<FxSlotControls> fxControls;
    juce::String lastFxSignature;
    juce::Rectangle<int> fxSlotsArea;

    std::map<juce::int64, std::unique_ptr<PluginEditorWindow>> pluginEditors;

    // Out-of-process plugin scan in flight (see scanForPlugins()).
    std::unique_ptr<juce::ChildProcess> scanProcess;
    std::unique_ptr<juce::AlertWindow> scanProgressWindow;
    juce::Time scanStartTime;

    // M6.10: stall watchdog + progress display. The scanner rewrites
    // its results file after every plugin, so that file's modification
    // time is the progress signal; scanLastProgressTime is the last
    // moment it was seen to advance (initialised to the scan start).
    // scanShownPlugin caches the pedal file's current entry so the
    // progress window is only repainted when it actually changes.
    juce::Time scanLastProgressTime;
    juce::String scanShownPlugin;

    //==========================================================================
    // M3: instrument block (shown at the start of the FX row when the
    // target track is a MIDI track) and the piano-roll window.
    juce::Label instLabel;
    juce::TextButton instOpenButton   { "Open Inst" };
    juce::TextButton instAddButton    { "Add Instrument..." };
    juce::TextButton instRemoveButton { "Remove Inst" };

    bool fxTargetIsMidi = false;
    bool fxTargetHasInstrument = false;
    FxSlotData fxTargetInstrument;

    std::unique_ptr<PianoRollWindow> pianoRollWindow;
    PianoRollComponent* pianoRoll = nullptr;   // owned by pianoRollWindow

    //==========================================================================
    // M6: the video preview window and the timecode-rate selector state.
    std::unique_ptr<VideoPreviewWindow> videoWindow;
    VideoPreviewComponent* videoPreview = nullptr;   // owned by videoWindow

    // M6.3: the telop text editor window (at most one at a time).
    std::unique_ptr<TextClipEditorWindow> textEditorWindow;

    int lastTimecodeNum = 0, lastTimecodeDen = 0;   // fps-box sync guard
    bool refreshingTimecodeBox = false;

    std::atomic<float> exportProgress { 0.0f };
    std::atomic<float> mp4ExportProgress { 0.0f };   // M6.2
    std::atomic<bool>  mp4ExportCancel { false };    // M6.2

    //==========================================================================
    // M6.2: ONE frame feed for the pop-out preview window, owned here
    // so the window can close and reopen without losing the decoder's
    // place. M6.3: the M6.2 docked PiP monitor that also listened to
    // it was removed (かいさん's call), so the feed has no listener —
    // and runs no timer and decodes nothing — until the preview
    // window opens. That is also the Phase 1 import behaviour: the
    // M6.2 build started decoding the moment a clip existed, which
    // is the prime suspect in かいさん's import-freeze report (see
    // the README's M6.3 notes).
    VideoFrameFeed videoFeed;

    // M6.2: video-audio extraction run state.
    std::atomic<float> extractProgress { 0.0f };
    std::atomic<bool>  extractCancel { false };
    TimelineEngine::VideoAudioExtractResult extractResult;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
