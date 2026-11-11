#pragma once

#include <JuceHeader.h>
#include "Session.h"
#include "PluginManager.h"
#include "VideoDecoder.h"
#include "Timecode.h"

//==============================================================================
/**
    TimelineEngine — the UI-independent heart of YM-Kithara (M1).

    Owns the audio device stack and the session (sources / tracks / clips):

        AudioDeviceManager
            -> AudioSourcePlayer
                -> TimelineEngine (an AudioSource that mixes all clips)

    Threading / model access (M1.1 — lock-free render snapshots):
      - The session model is guarded by one juce::CriticalSection, used
        only by the message/edit side: UI and editing code either holds
        that lock while reading the model (getModelLock() / getSession())
        or calls the edit methods below, which lock internally.
      - The audio thread NEVER touches the model or its lock. Whenever the
        model changes, the editing side builds an immutable RenderSnapshot
        (everything the renderer needs, with mute/solo already resolved to
        a per-track audible flag and shared ownership of the decoded source
        buffers) and publishes it with an atomic shared_ptr swap.
        getNextAudioBlock just loads the current snapshot and renders from
        it: no locking, no allocation, no file I/O on the audio thread.
        (The previous design try-locked the model in the callback and
        rendered silence on contention; with the UI repainting at 30 Hz
        while holding the lock, that dropped whole 10 ms blocks and the
        playback stuttered — hence this redesign.)
      - Every model change bumps getModelVersion() and sends a change
        message; the UI polls the version to refresh itself.
      - Source buffers are immutable once decoded, except when the device
        sample rate changes — then everything is resampled under the lock,
        clip positions are rescaled to the new rate, and a fresh snapshot
        is published before callbacks resume.

    M1 memory trade-off: imported files are decoded fully into RAM and kept
    resampled at the engine rate. That keeps the render path trivial and
    sample-accurate; streaming long files from disk (Ardour's "Butler"
    thread) is a later milestone's job.

    M2 additions: rendering is per track. Each audible track is mixed
    into a preallocated scratch buffer (TrackRenderContext, allocated
    during snapshot builds — never in the callback), its VST3 insert
    chain runs there, a PDC delay aligns it with the slowest track, and
    only then is it summed into the output with the track gain. The
    snapshot carries the resolved plugin pointers and delay amounts;
    plugin instances themselves are engine-owned (see the FX section
    below for the lifetime/retirement scheme).

    M3 additions: MIDI tracks. A MIDI track's snapshot entry carries its
    hosted instrument (same instance map and retirement scheme as FX
    slots) plus a flattened, start-sorted list of note events (clip
    seconds + note beats converted with the session tempo). Rendering
    feeds per-block note on/off events to the instrument, then runs the
    track's FX chain and PDC exactly like an audio track. Stop, seek and
    end-of-session set a panic flag; the next idle callback sends an
    all-notes-off block through every instrument so notes never hang.
    The same renderBlock() drives the offline WAV mixdown export.

    M3.1 additions: Standard MIDI File import (importMidiFile) — parsed
    into ordinary MIDI tracks/clips/notes, so everything downstream
    (snapshots, XML save/load, piano roll) works on imported data
    unchanged.

    M3.2 additions: level metering. The audio-thread render path taps
    each track's post-fader contribution and the post-master-gain
    output into fixed atomic peak slots (store-max, no locks, no
    allocation). The getters below are plain loads; the UI owns the
    hold/decay dynamics and clears the slots once per tick after
    reading, so a read reflects only audio rendered since the UI's
    previous tick. Meters are pure display state — nothing here is
    persisted and the snapshot format is unchanged.

    M3.3 additions: MIDI reaches effect slots. Two sources:
      (1) Live external input — every available MIDI input device is
          opened (message thread, in initialiseDevices) into a
          juce::MidiMessageCollector; the audio callback drains one
          block's worth per block (the collector's own lock is the only
          synchronisation) and renderBlock hands those events to the FX
          chain of every track whose persisted fxMidiIn flag is set.
          Offline export has no live input, by design.
      (2) A MIDI track's own clip-generated MIDI (the same per-block
          buffer its instrument receives) is also passed to that
          track's post-instrument FX slots.
    fxMidiEventsDelivered counts every event handed to any FX slot —
    a test hook the headless smoke tests assert against.

    M4 additions (header tag M6.8): MIDI keyboard recording. A MIDI
    track can be record-armed (runtime-only state, never persisted).
    Armed tracks MONITOR live input: the drained collector events are
    merged into the track's instrument buffer both during playback
    and — via a monitor-only render pass — while the transport is
    stopped, so the keyboard sounds even with the transport parked.
    While the transport runs with any track armed, recording is
    active: the audio callback copies note on/off events (timed by
    their collector block sample offsets, not by message-thread
    polling) into a lock-free queue, and a 50 ms message-thread pump
    folds them into one take clip per armed track — created at the
    first note, positioned at the record-start position, notes stored
    as beats relative to the clip at the session tempo, exactly the
    piano roll's representation, so takes are fully editable there.
    Stop finalises the takes: held notes close at the stop position
    and an empty take leaves no clip. Note events only in v1 — CCs
    are not recorded (they still reach FX via M3.3). When nothing
    lies ahead of the record-start position (an empty session, say),
    the transport runs open-ended while recording instead of stopping
    at the session end.

    M6 additions: the video track. A session holds at most one video
    track (TrackKind::video) whose clips are windows into video files,
    decoded elsewhere (VideoDecoder / the UI's preview and filmstrip
    services) — the engine only stores the model. Video tracks are
    published in the render snapshot as permanently inaudible,
    context-less entries purely so snapshot order keeps matching
    session order (the M3.2 meter indexing depends on it); they take
    no part in audio rendering or PDC. Since M6.4 their clips' ends DO
    count toward the session length (see computeTotalLengthLocked), so
    a video-only session plays and both export spans cover the video.
    The session also carries a display-only timecode rate (see Session.h).

    M6.3 additions: video clips gain fadeIn/fadeOut (overlaps between
    video clips are legal and render as true cross-dissolves — the
    shared mapping lives in VideoComposite.h), and a session may hold
    one text track (TrackKind::text) of telop clips, imported from
    .srt files (SrtParser) and edited like other clips. Text tracks
    get exactly the video track's snapshot treatment. The session
    also carries the telop font-size index (SessionData::telopSize).

    M6.6 additions: the session carries a full telop STYLE
    (SessionData::telopStyle — font name, size %, position,
    alignment, colour); telopSize remains as its S/M/L mirror.
*/
class TimelineEngine final : public juce::AudioSource,
                             public juce::ChangeBroadcaster,
                             private juce::Timer
{
public:
    TimelineEngine();
    ~TimelineEngine() override;

    //==========================================================================
    /** Must be called once (on the message thread) before anything else. */
    void initialiseDevices();

    /** Direct access for the device selector UI. */
    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

    /** The rate all session positions are expressed in (the device rate). */
    double getSampleRate() const noexcept { return engineRate.load(); }

    /** VST3 plugin list / scanning support (M2). Message thread only. */
    PluginManager& getPluginManager() noexcept { return pluginManager; }

    juce::int64 secondsToSamples (double seconds) const noexcept
    {
        return (juce::int64) std::llround (seconds * engineRate.load());
    }

    double samplesToSeconds (juce::int64 samples) const noexcept
    {
        const double rate = engineRate.load();
        return rate > 0.0 ? (double) samples / rate : 0.0;
    }

    //==========================================================================
    // Model access. Callers reading the session directly MUST hold the lock.
    juce::CriticalSection& getModelLock() noexcept { return modelLock; }
    SessionData&       getSession()       noexcept { return session; }
    const SessionData& getSession() const noexcept { return session; }
    int getModelVersion() const noexcept { return modelVersion.load(); }

    const SourceData* findSource (juce::int64 sourceId) const;   // lock must be held
    TrackData*        findTrack  (juce::int64 trackId);          // lock must be held
    const TrackData*  findTrack  (juce::int64 trackId) const;    // lock must be held

    /** Copies out one clip (and its owning track id). Locks internally. */
    bool getClipData (juce::int64 clipId, ClipData& clipOut, juce::int64& trackIdOut) const;

    //==========================================================================
    // Editing. All of these lock internally, bump the model version and
    // broadcast a change message. IDs are engine-assigned, starting at 1;
    // -1 / false means failure.
    juce::int64 importFileAsNewTrack (const juce::File& file);

    /** Imports a Standard MIDI File (.mid) as new MIDI tracks (M3.1):
        one MIDI track per source track that contains notes (all channels
        merged, instruments left empty for the user to host). If the file
        carries a tempo event, the FIRST one sets the session tempo, so
        beat/seconds conversion matches the file; later tempo changes in
        the file are not followed in v1. Returns false (and creates
        nothing) for SMPTE-division files or files without notes;
        resultMessage describes the outcome either way. */
    bool importMidiFile (const juce::File& file, juce::String& resultMessage);

    juce::int64 addSourceFromFile (const juce::File& file);
    juce::int64 addTrack (const juce::String& name);
    juce::int64 addClip (juce::int64 trackId, const ClipData& clipWithoutId);

    /** Replaces a clip's editable fields. If targetTrackId is a different
        track, the clip is moved to that track. Values are clamped to the
        source bounds. */
    bool updateClip (juce::int64 clipId, juce::int64 targetTrackId, const ClipData& newValues);

    bool removeClip (juce::int64 clipId);

    /** Splits a clip at an absolute timeline position. Returns the id of
        the new (right-hand) clip, or -1 if the position is not strictly
        inside the clip. */
    juce::int64 splitClip (juce::int64 clipId, juce::int64 atSamples);

    bool removeTrack (juce::int64 trackId);
    void renameTrack (juce::int64 trackId, const juce::String& newName);
    void setTrackGain (juce::int64 trackId, float gain);
    void setTrackMute (juce::int64 trackId, bool mute);
    void setTrackSolo (juce::int64 trackId, bool solo);
    /** M3.3: opt a track into receiving live MIDI input in its FX
        chain (see the class notes). Persisted in project.xml. */
    void setTrackFxMidiIn (juce::int64 trackId, bool enabled);
    void setTempo (double bpm);
    double getTempo();

    //==========================================================================
    // Per-track VST3 insert FX (M2). Message thread only; all of these
    // lock internally, bump the model version and broadcast a change.
    //
    // Lifetime scheme: live instances are owned by the engine in
    // fxInstances (keyed by slot id). The render snapshot holds raw
    // AudioProcessor* pointers into that map. When a slot is removed its
    // instance is NOT deleted right away — it moves to a retirement list
    // (see retireFxInstanceLocked) so a snapshot still in flight on the
    // audio thread can never point at freed memory.
    /** Creates an instance for the description and appends it to the
        track's FX chain. Returns false with errorOut set on failure. */
    bool addFxToTrack (juce::int64 trackId, const juce::PluginDescription& description,
                       juce::String& errorOut);

    bool removeFx (juce::int64 trackId, juce::int64 slotId);
    void setFxBypass (juce::int64 trackId, juce::int64 slotId, bool bypass);

    /** The live instance for a slot, or nullptr (missing / unknown slot).
        Message thread only; the pointer stays valid while the slot
        exists because removal retires rather than deletes. */
    juce::AudioPluginInstance* getFxInstance (juce::int64 slotId);

    /** True if some track's chain still contains this slot id. */
    bool hasFxSlot (juce::int64 slotId);

    /** Rebuilds the render snapshot. Call after hosting changes that do
        not go through the edit API — e.g. when a plugin editor closes,
        because a plugin's reported latency can change with its settings. */
    void refreshFxState();

    /** Message-thread copy of one track's FX chain, for the UI panel.
        Returns false if the track does not exist. */
    bool getTrackFx (juce::int64 trackId, juce::String& trackNameOut,
                     std::vector<FxSlotData>& chainOut);

    //==========================================================================
    // MIDI tracks, instruments and piano-roll data (M3). Message thread
    // only; all of these lock internally, bump the model version and
    // broadcast a change — same contract as the editing API above.
    /** Adds an empty MIDI track (no instrument, no clips). Returns its id. */
    juce::int64 addMidiTrack();

    /** Hosts the plugin as the track's instrument, replacing any current
        one (whose instance is retired, not deleted — see the FX notes).
        Returns false with errorOut set if the track is not a MIDI track
        or the plugin cannot be loaded. */
    bool setTrackInstrument (juce::int64 trackId,
                             const juce::PluginDescription& description,
                             juce::String& errorOut);

    /** Removes the track's instrument (instance retired). */
    bool removeTrackInstrument (juce::int64 trackId);

    /** Copy-out of a MIDI track's instrument slot. Returns false when the
        track does not exist or is not a MIDI track; hasInstrumentOut then
        says whether instrumentOut is meaningful. */
    bool getTrackInstrument (juce::int64 trackId, bool& hasInstrumentOut,
                             FxSlotData& instrumentOut) const;

    /** Creates a MIDI clip on the track at startSeconds, one bar long at
        the current tempo (4 beats). Returns the new clip id or -1. */
    juce::int64 addMidiClip (juce::int64 trackId, double startSeconds);

    /** Deletes a MIDI clip by id (whichever track holds it). */
    bool removeMidiClip (juce::int64 clipId);

    /** Copies out one MIDI clip and its owning track id. Locks internally. */
    bool getMidiClipData (juce::int64 clipId, MidiClipData& clipOut,
                          juce::int64& trackIdOut) const;

    /** Moves a MIDI clip to a new start time (seconds, clamped >= 0). */
    bool moveMidiClip (juce::int64 clipId, double startSeconds);

    /** M6.9: placement write-back for the timeline edge-trim drags —
        copies startSeconds / lengthSeconds / offsetBeats from
        newValues (the notes themselves are never touched: trimming
        slides the beat window, see MidiClipData). Clamps: start >= 0,
        offsetBeats >= 0, length >= 0.25 beats at the session tempo. */
    bool updateMidiClip (juce::int64 clipId, const MidiClipData& newValues);

    bool addMidiNote (juce::int64 trackId, juce::int64 clipId, const MidiNoteData& note);

    /** Whole-list write-back of a clip's notes (the piano roll edits a
        copy and writes it back — simple and race-free). Values are
        clamped to legal ranges and the list is sorted by start beat. */
    bool setMidiClipNotes (juce::int64 trackId, juce::int64 clipId,
                           const std::vector<MidiNoteData>& notes);

    /** Copy-out of a clip's notes for the piano roll. */
    bool getMidiClipNotes (juce::int64 trackId, juce::int64 clipId,
                           std::vector<MidiNoteData>& notesOut) const;

    //==========================================================================
    // Video track and clips (M6). Message thread only; all of these
    // lock internally, bump the model version and broadcast a change —
    // same contract as the editing APIs above.
    /** Returns the id of the session's (single) video track, creating
        it (named "Video", appended last) when none exists yet. */
    juce::int64 ensureVideoTrack();

    /** Probes a video file with FFmpeg and appends it to the video
        track (created first if needed) as a clip starting where the
        previous video clip ends (0 when the track was empty). The
        clip's cached stream info comes from the probe. The first
        import into an empty track also adopts the file's frame rate
        as the session timecode rate, unless the user picked one.
        M6.5: still images (jpg/png/bmp) come through this same
        path — the probe flags them (VideoFileInfo::stillImage) and
        they land as free-duration 5 s clips with stillImage set.
        Returns the new clip id, or -1 with errorOut set. */
    juce::int64 importVideoFile (const juce::File& file, juce::String& errorOut);

    /** Copies out one video clip and its owning track id. */
    bool getVideoClipData (juce::int64 clipId, VideoClipData& clipOut,
                           juce::int64& trackIdOut) const;

    /** Replaces a video clip's placement (start / offset / duration,
        clamped to the source bounds). The file identity and its
        cached stream info always stay with the stored clip. */
    bool updateVideoClip (juce::int64 clipId, const VideoClipData& newValues);

    /** Deletes a video clip by id. */
    bool removeVideoClip (juce::int64 clipId);

    /** Splits a video clip at an absolute timeline position in
        seconds. Returns the new (right-hand) clip's id, or -1.
        M6.3: the left part keeps the clip's fade-in, the right part
        keeps its fade-out, and the two new edges get no fade. */
    juce::int64 splitVideoClip (juce::int64 clipId, double atSeconds);

    /** The session timecode rate (M6, display only — see Session.h). */
    void getTimecodeFps (int& numOut, int& denOut, bool& userSetOut);
    void setTimecodeFps (int num, int den);

    //==========================================================================
    // Text track and telop clips (M6.3). Message thread only; same
    // locking/change contract as the video APIs above. Like the video
    // track, the text track is model-only: it never renders audio,
    // never extends the session length, and its clips are composited
    // over the picture by VideoCompositing/Telop (preview + exporter).
    struct SrtImportResult
    {
        int imported = 0;   // clips added to the text track
        int skipped  = 0;   // SRT entries that could not be used
    };

    /** Returns the id of the session's (single) text track, creating
        it (named "Text", appended last) when none exists yet. */
    juce::int64 ensureTextTrack();

    /** Parses an .srt file (SrtParser) and APPENDS its entries to the
        text track (created first if needed). Returns false only when
        the file cannot be read at all (errorOut set); a readable file
        with some bad entries still imports the good ones — the
        counts land in resultOut either way so the UI can report
        "N imported, M skipped". */
    bool importSrtFile (const juce::File& file, SrtImportResult& resultOut,
                        juce::String& errorOut);

    /** Copies out one text clip and its owning track id. */
    bool getTextClipData (juce::int64 clipId, TextClipData& clipOut,
                          juce::int64& trackIdOut) const;

    /** Replaces a text clip's placement (start / duration, clamped:
        start >= 0, duration >= 0.05). The text itself is edited via
        setTextClipText so drag updates can't clobber it. */
    bool updateTextClip (juce::int64 clipId, const TextClipData& newValues);

    /** Replaces a text clip's text (multi-line allowed). */
    bool setTextClipText (juce::int64 clipId, const juce::String& newText);

    /** Deletes a text clip by id. */
    bool removeTextClip (juce::int64 clipId);

    /** Splits a text clip at an absolute timeline position in
        seconds (both halves keep the text). Returns the new
        (right-hand) clip's id, or -1. */
    juce::int64 splitTextClip (juce::int64 clipId, double atSeconds);

    /** The session telop font-size index (0 = S, 1 = M, 2 = L) —
        the S/M/L mirror of the telop style's scalePercent
        (72 / 100 / 138%). */
    int  getTelopSize();
    void setTelopSize (int sizeIndex);

    /** The session-wide telop style (M6.6): font name, size scale,
        vertical position, alignment and colour. setTelopStyle
        clamps the scale to 40..250% and keeps the telopSize mirror
        in sync; getTelopStyle returns a plain copy. */
    TelopStyleData getTelopStyle();
    void setTelopStyle (const TelopStyleData& style);

    //==========================================================================
    // WAV mixdown export (M3). Renders the whole session offline through
    // the same per-track processing as live playback (instruments, FX,
    // PDC, master gain) and writes 16-bit PCM WAV at the engine rate.
    // Runs on the CALLING thread (the UI drives it from a progress
    // thread) and blocks edits meanwhile by holding the model lock;
    // the transport must be stopped by the caller first. progress01
    // (optional) is updated 0..1 as blocks are rendered.
    bool exportMixdownWav (const juce::File& target, juce::String& errorOut,
                           std::atomic<float>* progress01);

    //==========================================================================
    // Chunked offline mixdown (M6.2) — the same render as
    // exportMixdownWav, but driven in pieces by the mp4 exporter so
    // audio and video frames can be interleaved. The CALLER must hold
    // the model lock (getModelLock()) for the whole begin..end span and
    // have stopped the transport. beginOfflineMixdown resets the
    // plugin instances and PDC delay lines, keeps the audio callback
    // out via offlineRendering, and returns the total number of samples
    // in the export span (session length + 2 s tail), or -1 with
    // errorOut set when the session is empty.
    // renderOfflineMixdownBlock renders numSamples starting at
    // `position` into buffer (channels 0/1, starting at sample 0) with
    // the master gain applied, exactly as the WAV export writes them.
    juce::int64 beginOfflineMixdown (juce::String& errorOut);
    void renderOfflineMixdownBlock (juce::AudioBuffer<float>& buffer,
                                    juce::int64 position, int numSamples);
    void endOfflineMixdown();

    //==========================================================================
    // Video-audio extraction (M6.2): pull each distinct video source
    // file's own audio into the session as a normal audio track.
    //
    // For every distinct file used by the video track's clips, the
    // audio stream is decoded (VideoAudioExtractor) to
    // "<videofile>_audio.wav" next to the video file, then a track
    // named "<videofile> Audio" is created holding one clip per video
    // clip of that file, aligned 1:1 (clip start / source offset /
    // duration copied from the video clip). A file whose extracted
    // track already exists is skipped (re-running refreshes nothing
    // and piles up no duplicates); a file without an audio stream is
    // reported in filesWithoutAudio. The model lock is NOT held while
    // decoding, so the UI stays usable; run this from a worker thread.
    struct VideoAudioExtractResult
    {
        juce::StringArray createdTracks;     // "<name> Audio"
        juce::StringArray existingTracks;    // skipped, already extracted
        juce::StringArray filesWithoutAudio; // skipped, no audio stream
        juce::StringArray fileErrors;        // "<file>: <reason>"
        bool cancelled = false;
    };

    VideoAudioExtractResult extractVideoAudio (std::atomic<float>* progress01,
                                               std::atomic<bool>* shouldCancel);

    //==========================================================================
    // Transport.
    void play();
    void pause();
    bool isPlaying() const noexcept { return playing.load(); }

    void returnToZero();
    void setPositionSamples (juce::int64 newPosition);
    void setPositionSeconds (double newPositionSeconds);
    juce::int64 getPositionSamples() const noexcept { return playPosition.load(); }
    double getPositionSeconds() const { return samplesToSeconds (getPositionSamples()); }

    juce::int64 getTotalLengthSamples();
    double getTotalLengthSeconds() { return samplesToSeconds (getTotalLengthSamples()); }

    void  setMasterGain (float newGain);
    float getMasterGain() const noexcept { return masterGain.load(); }

    //==========================================================================
    // Level metering (M3.2). Peaks are captured in the audio thread; see
    // the class notes. Getters are plain atomic loads and never reset
    // anything — the UI calls the clear methods once per tick after it
    // has read, as part of its own hold/decay. Track indices are session
    // order (the same order snapshots render in); an out-of-range index
    // reads as 0.
    static constexpr int maxMeterTracks = 256;

    void getTrackPeak (int trackIndex, float& left, float& right) const;
    void getMasterPeak (float& left, float& right) const;
    void clearTrackPeak (int trackIndex);
    void clearMasterPeak();

    //==========================================================================
    // Live MIDI input -> FX chains (M3.3). Devices are opened in
    // initialiseDevices (message thread); the audio callback drains the
    // collector itself. The methods below are the testable seams of that
    // path: addLiveMidiMessage is exactly what a device callback feeds
    // the collector, and drainLiveMidi is exactly what the audio
    // callback drains. Message thread / test thread only — never call
    // them from the audio thread.
    /** Names of the MIDI input devices opened at startup, in open
        order (empty when the machine has none). Message thread only. */
    juce::StringArray getMidiInputDeviceNames() const { return midiInputNames; }

    /** Queues a message with the live input collector, stamped "now"
        exactly as a hardware callback would deliver it. */
    void addLiveMidiMessage (const juce::MidiMessage& message);

    /** Drains the events due for one block of numSamples into
        destBuffer (which is cleared first; event offsets land inside
        the block) and returns how many events were drained. */
    int drainLiveMidi (juce::MidiBuffer& destBuffer, int numSamples);

    /** Renders numSamples at blockPosition through the current
        snapshot with the same code path as realtime playback,
        treating liveMidi as the block's live-input buffer (the buffer
        getNextAudioBlock built by draining the collector). Public so
        headless tests can drive the drain+distribute step directly;
        the app itself never calls this. Not concurrent with playback
        or export. */
    void renderBlockWithLiveMidi (juce::AudioBuffer<float>& output,
                                  juce::int64 blockPosition, int numSamples,
                                  const juce::MidiBuffer& liveMidi);

    /** Test hook (M3.3): total MIDI events handed to any FX slot so
        far (clip-generated and live input alike). */
    int getFxMidiEventsDelivered() const noexcept
    {
        return fxMidiEventsDelivered.load (std::memory_order_relaxed);
    }

    void resetFxMidiEventsDelivered() noexcept
    {
        fxMidiEventsDelivered.store (0, std::memory_order_relaxed);
    }

    //==========================================================================
    // MIDI keyboard recording (M4). Message thread only. Arm state is
    // runtime-only and never reaches project.xml.
    /** Arms/disarms a MIDI track for recording. Arming is also what
        makes the track monitor live input (see the class notes), and
        multiple tracks may be armed at once. Disarming mid-record
        finalises that track's take on the spot (held notes close at
        the current position). No-op for non-MIDI tracks. */
    void setTrackRecordArmed (juce::int64 trackId, bool armed);
    bool isTrackRecordArmed (juce::int64 trackId);

    /** True while the transport is capturing armed tracks (playback
        engaged with at least one armed MIDI track). */
    bool isRecording() const noexcept { return recordingActive.load(); }

    /** True when at least one MIDI track is armed — the transport can
        then run (and record) even in an otherwise empty session. */
    bool hasArmedMidiTracks();

    /** Injection point into the exact path device MIDI uses: stamps
        the message "now" and queues it with the collector, precisely
        like addLiveMidiMessage — so the app and the headless tests
        can feed the record/monitor path exactly as hardware does. */
    void injectMidiMessage (const juce::MidiMessage& message);

    /** One flattened note event of a MIDI track in absolute session
        seconds — the public mirror of the render snapshot's private
        RenderMidiEvent, for tests and diagnostics. */
    struct MidiEventInfo
    {
        double startSec = 0.0;
        double endSec = 0.0;
        int noteNumber = 60;
        int velocity = 100;
    };

    /** Test hook (M6.9): the current render snapshot's flattened note
        events for one MIDI track — exactly what playback would hand
        the track's instrument (windowing already applied). Empty when
        the track does not exist or has no audible notes. */
    std::vector<MidiEventInfo> getGatheredMidiEvents (juce::int64 trackId) const;

    /** Test hook (M4): total live-input events handed to armed tracks'
        instruments so far (monitoring and recording alike). */
    int getLiveMidiEventsToInstruments() const noexcept
    {
        return liveMidiEventsToInstruments.load (std::memory_order_relaxed);
    }

    void resetLiveMidiEventsToInstruments() noexcept
    {
        liveMidiEventsToInstruments.store (0, std::memory_order_relaxed);
    }

    //==========================================================================
    // Project persistence (project folder + project.xml + audio/ copies).
    /** Writes <folder>/project.xml and copies every source file into
        <folder>/audio/. Returns false (with errorOut set) on failure. */
    bool saveProject (const juce::File& folder, juce::String& errorOut);

    /** Loads a project folder previously written by saveProject. Sources
        whose files are missing are skipped and named in warningsOut; clips
        that referenced them are dropped. */
    bool loadProject (const juce::File& folder, juce::StringArray& warningsOut);

    /** Resets to an empty session (tempo 120, no tracks). */
    void newProject();

    //==========================================================================
    // juce::AudioSource
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override {}
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;

private:
    //==========================================================================
    // Helpers below assume the model lock is already held (suffix "Locked"),
    // except where noted.
    SourceData* findSourceLocked (juce::int64 sourceId);
    const SourceData* findSourceLocked (juce::int64 sourceId) const;
    TrackData* findTrackLocked (juce::int64 trackId);
    const TrackData* findTrackLocked (juce::int64 trackId) const;
    bool findClipLocked (juce::int64 clipId, TrackData*& trackOut, ClipData*& clipOut);

    juce::int64 addSourceFromFileLocked (const juce::File& file);
    juce::int64 addTrackLocked (const juce::String& name);
    juce::int64 addClipLocked (juce::int64 trackId, const ClipData& clipWithoutId);
    juce::int64 importFileLocked (const juce::File& file);   // returns new track id

    void clampClipToSourceLocked (ClipData& clip) const;
    juce::int64 computeTotalLengthLocked() const;
    void touchModelLocked();

    void resampleSourceLocked (SourceData& source);
    void rebuildAllSourcesLocked (double oldRate, double newRate);
    void computePeaksLocked (SourceData& source);

    //==========================================================================
    // Per-track render state owned by the audio path. Allocated and sized
    // on the message thread (during snapshot builds), referenced from
    // snapshots via shared_ptr, and touched by the audio thread only —
    // except that a context is replaced wholesale (never resized in
    // place) when a bigger scratch/delay capacity is needed, so an
    // in-flight callback always sees a consistent object.
    struct TrackRenderContext
    {
        juce::AudioBuffer<float> scratch;   // clips are mixed here, then the
                                            // FX chain and PDC delay run on it
        std::vector<float> delayLine[2];    // PDC ring buffers (stereo)
        int delayCapacity = 0;              // allocated ring length
        int delayWritePos = 0;              // audio thread only
        juce::MidiBuffer midiBuffer;        // M3: per-block note events for
                                            // the track's instrument, reused
                                            // (clear() keeps the capacity)
        juce::MidiBuffer fxMidiBuffer;      // M3.3: per-block MIDI handed to
                                            // the track's FX chain (clip
                                            // events and/or live input)
    };

    //==========================================================================
    // Immutable render snapshot: the ONLY thing the audio thread reads.
    struct RenderClip
    {
        std::shared_ptr<const juce::AudioBuffer<float>> buffer;  // decoded, engine rate
        juce::int64 start = 0, offset = 0, length = 0;
        juce::int64 fadeIn = 0, fadeOut = 0;
        float gain = 1.0f;
    };

    // M3: one flattened note event for a MIDI track, in absolute session
    // seconds (clip position + note beats converted with the tempo at
    // snapshot-build time), start-sorted within the track.
    struct RenderMidiEvent
    {
        double startSec = 0.0;
        double endSec = 0.0;
        int noteNumber = 60;
        int velocity = 100;
    };

    struct RenderTrack
    {
        juce::int64 trackId = 0;
        float gain = 1.0f;
        bool audible = false;                  // mute/solo already resolved
        TrackKind kind = TrackKind::audio;
        std::vector<RenderClip> clips;

        // M3 (MIDI tracks): the hosted instrument (engine-owned, same
        // lifetime scheme as fxProcessors below; nullptr when the track
        // has none or it failed to load — the track then renders silence)
        // and its flattened note events.
        juce::AudioProcessor* instrument = nullptr;
        std::vector<RenderMidiEvent> midiEvents;

        // M3.3: the track opted into live MIDI input for its FX chain
        // (TrackData::fxMidiIn, copied at snapshot-build time).
        bool fxMidiIn = false;

        // M4: the track is record-armed (TrackData::recordArmed,
        // copied at snapshot-build time). Armed MIDI tracks receive
        // the live-input merge in renderBlock — monitoring during
        // playback, and the monitor-only stopped pass.
        bool recordArmed = false;

        // M2: raw pointers to engine-owned plugin instances, in chain
        // order, bypassed/missing slots already excluded. The instances
        // outlive any snapshot that references them (retirement scheme,
        // see addFxToTrack notes).
        std::vector<juce::AudioProcessor*> fxProcessors;
        int pdcDelaySamples = 0;               // delay applied so this track
                                               // lines up with the slowest one
        std::shared_ptr<TrackRenderContext> context;
    };

    struct RenderSnapshot
    {
        std::vector<RenderTrack> tracks;
        juce::int64 totalLength = 0;

        // M3: prebuilt all-notes-off / all-sound-off buffer (channel 1),
        // sent once through every MIDI instrument after stop/seek/end so
        // no note can hang. Built here so the audio callback allocates
        // nothing when it panics.
        juce::MidiBuffer midiPanicBuffer;
    };

    /** Builds a snapshot from the current model and publishes it with an
        atomic swap. The model lock must be held. Called from
        touchModelLocked() and after device-rate rebuilds. */
    void rebuildSnapshotLocked();

    /** Renders one block from a snapshot. liveMidi (M3.3) is the live
        input drained for this block — its event offsets are relative
        to the block start and span the whole block — or nullptr when
        there is none (offline export, or a block with no live events).
        Tracks flagged fxMidiIn receive the slice of it that falls in
        each rendered chunk. monitorOnly (M4) renders ONLY armed MIDI
        tracks, fed from liveMidi alone (no clip events): the stopped-
        transport monitoring pass. */
    void renderBlock (const RenderSnapshot& snapshot,
                      juce::AudioBuffer<float>& output, int startSample,
                      juce::int64 blockPosition, int numSamples,
                      const juce::MidiBuffer* liveMidi,
                      bool monitorOnly = false);

    /** Mixes one track's clips into its context scratch buffer (clip
        gains only; track gain is applied when summing). Audio thread. */
    void renderClipsInto (const RenderTrack& track,
                          juce::AudioBuffer<float>& dest,
                          juce::int64 blockPosition, int numSamples);

    //==========================================================================
    // FX helpers (model lock must be held).
    /** Creates + prepares the live instance for a slot. On failure the
        slot is flagged missing, false is returned and errorOut receives
        JUCE's own error text (so callers can show why, not just that). */
    bool instantiateFxSlotLocked (FxSlotData& slot, juce::String& errorOut);

    void prepareFxInstanceLocked (juce::AudioPluginInstance& instance);
    juce::AudioPluginInstance* getFxInstanceLocked (juce::int64 slotId);

    /** Moves a slot's instance to the retirement list (audio-thread
        safety: see the class notes by addFxToTrack). */
    void retireFxInstanceLocked (juce::int64 slotId);
    void retireAllFxInstancesLocked();

    void timerCallback() override;   // frees retired plugin instances

    //==========================================================================
    // M3 helpers (model lock must be held).
    bool findMidiClipLocked (juce::int64 clipId, TrackData*& trackOut,
                             MidiClipData*& clipOut);
    bool findMidiClipLocked (juce::int64 clipId, const TrackData*& trackOut,
                             const MidiClipData*& clipOut) const;

    // M6 helpers (model lock must be held).
    bool findVideoClipLocked (juce::int64 clipId, TrackData*& trackOut,
                              VideoClipData*& clipOut);
    bool findVideoClipLocked (juce::int64 clipId, const TrackData*& trackOut,
                              const VideoClipData*& clipOut) const;
    void clampVideoClipLocked (VideoClipData& clip) const;
    TrackData* findVideoTrackLocked();

    // M6.3 helpers (model lock must be held).
    bool findTextClipLocked (juce::int64 clipId, TrackData*& trackOut,
                             TextClipData*& clipOut);
    bool findTextClipLocked (juce::int64 clipId, const TrackData*& trackOut,
                             const TextClipData*& clipOut) const;
    void clampTextClipLocked (TextClipData& clip) const;
    TrackData* findTextTrackLocked();

    /** The state to write for a slot on save: the live instance's current
        state when there is one (also remembered back into the slot),
        otherwise the slot's stored state verbatim. */
    juce::String captureSlotStateLocked (FxSlotData& slot);

    /** Audio thread: runs one silent block carrying the panic buffer
        through every MIDI track's instrument in the snapshot. */
    void processMidiPanic (const RenderSnapshot& snapshot);

    //==========================================================================
    // M3.3 live MIDI input. Message thread only.
    /** Opens every available MIDI input device into midiCollector and
        refreshes midiInputNames. Called from initialiseDevices. */
    void openMidiInputs();

    /** Stops and closes all opened MIDI input devices. */
    void closeMidiInputs();

    //==========================================================================
    // M4 recording internals. All "Locked" methods run on the message
    // thread with the model lock held.
    /** One captured note event, stamped by the audio callback with
        its absolute session position (block position + the event's
        offset in the drained collector block). */
    struct CapturedMidiEvent
    {
        juce::int64 samplePos = 0;
        int noteNumber = 60;
        int velocity = 0;
        bool isNoteOn = false;
    };

    struct HeldNote
    {
        juce::int64 startSamples = 0;
        int velocity = 100;
    };

    /** The in-progress take of one armed track. The clip itself is
        created lazily at the first note (clipId -1 until then), so an
        empty take leaves nothing behind; heldNotes tracks the notes
        currently down, by note number. */
    struct TakeBuilder
    {
        juce::int64 trackId = 0;
        juce::int64 clipId = -1;
        juce::int64 clipStartSamples = 0;
        std::map<int, HeldNote> heldNotes;
        double lastNoteEndSeconds = 0.0;
    };

    bool hasArmedMidiTrackLocked() const;

    /** Engages recording when the transport is playing with at least
        one armed track; tracks armed mid-playback join with a take
        starting at the current position. */
    void syncRecordingLocked();

    void beginRecordingLocked();

    /** Folds every queued capture event into the take clips and grows
        the clips to the playhead. */
    void pumpRecordQueueLocked();

    /** Closes a take's held notes at stopSamples, sorts its notes and
        sets its final length. Returns true when a clip was finalised. */
    bool finaliseTakeLocked (TakeBuilder& builder, juce::int64 stopSamples);

    /** Stop path: drain the queue, finalise every take, end recording. */
    void finishRecordingLocked();

    /** Discards any in-progress takes WITHOUT finalising (project
        new/load teardown — those paths pause() first, which already
        finalised; this is belt-and-braces). */
    void cancelRecordingLocked();

    /** After recording ends, the timer goes back to serving the FX
        retirement rotation at its usual 3 s cadence (or stops). */
    void restoreTimerAfterRecordingLocked();

    MidiClipData* findTakeClipLocked (TakeBuilder& builder);

    /** Converts one held-note span into a stored note on the take
        clip (seconds -> beats at the session tempo, relative to the
        clip start) and tracks the take's latest note end. */
    void appendRecordedNoteLocked (TakeBuilder& builder, MidiClipData& clip,
                                   int noteNumber, const HeldNote& held,
                                   juce::int64 endSamples);

    /** Audio thread: copies the note on/off events of one drained
        live block into the capture queue. Lock-free; a full queue
        drops events rather than ever blocking the callback. */
    void captureLiveMidi (const juce::MidiBuffer& liveMidi,
                          juce::int64 blockPosition, int numSamples) noexcept;

    //==========================================================================
    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;
    juce::AudioSourcePlayer  sourcePlayer;

    SessionData session;
    juce::CriticalSection modelLock;
    std::atomic<int> modelVersion { 0 };
    juce::int64 nextId = 1;                       // guarded by modelLock

    std::atomic<double> engineRate { 44100.0 };   // rate `buffer`s are resampled to;
                                                  // updated in prepareToPlay (device thread
                                                  // at device start) — read via getSampleRate()

    std::atomic<juce::int64> playPosition { 0 };
    std::atomic<bool> playing { false };
    std::atomic<bool> midiPanicPending { false };  // M3: set on stop/seek/end;
                                                   // the next idle callback
                                                   // silences all instruments
    std::shared_ptr<const RenderSnapshot> offlineSnapshot;  // M6.2: held
                                                  // between begin/endOffline
                                                  // Mixdown (model lock held
                                                  // by the caller throughout)
    std::atomic<bool> offlineRendering { false };  // M3: a mixdown export is
                                                   // rendering; the audio
                                                   // callback stays out of the way
    std::atomic<float> masterGain { 1.0f };
    std::atomic<juce::int64> cachedTotalLength { 0 };

    // M3.2 metering slots: per-track post-fader peaks in session order
    // plus the post-master-gain output peak. The audio thread store-
    // maxes block peaks into these; the UI thread loads and clears them
    // (relaxed ordering throughout — a benign race only ever costs one
    // block's peak, which is invisible on a meter).
    std::atomic<float> trackPeakLeft[maxMeterTracks] {};
    std::atomic<float> trackPeakRight[maxMeterTracks] {};
    std::atomic<float> masterPeakLeft { 0.0f };
    std::atomic<float> masterPeakRight { 0.0f };

    // M3.3 live MIDI input state. The collector is the only piece the
    // device callbacks and the audio thread both touch, and it is
    // internally locked (the JUCE-standard pattern). midiInputs /
    // midiInputNames are message-thread only. liveMidiDrainBuffer is
    // audio-thread only (plus the headless tests, which never run a
    // device callback). Declaration order matters at destruction: the
    // inputs are declared AFTER the collector so they are destroyed
    // (and stop calling it) first.
    juce::MidiMessageCollector midiCollector;
    std::vector<std::unique_ptr<juce::MidiInput>> midiInputs;
    juce::StringArray midiInputNames;
    juce::MidiBuffer liveMidiDrainBuffer;
    std::atomic<int> fxMidiEventsDelivered { 0 };   // M3.3 test hook

    // M4 recording state. The audio callback produces into
    // recordFifo/recordEventQueue and the message thread consumes
    // (a 50 ms timer pump while recording) — a single-producer /
    // single-consumer pair via juce::AbstractFifo, so the audio
    // thread never takes the model lock. takeBuilders live under the
    // model lock and exist only while recording.
    static constexpr int recordQueueCapacity = 4096;
    juce::AbstractFifo recordFifo { recordQueueCapacity };
    std::vector<CapturedMidiEvent> recordEventQueue;
    std::vector<TakeBuilder> takeBuilders;
    std::atomic<bool> recordingActive { false };
    std::atomic<bool> recordOpenEnded { false };   // latched at record
                                                   // start: nothing lay
                                                   // ahead, so no
                                                   // session-end stop
    std::atomic<int> liveMidiEventsToInstruments { 0 };   // M4 test hook

    // M4: the FX retirement rotation is time-gated (see timerCallback)
    // so the 50 ms recording pump cadence cannot shorten a retired
    // instance's survival guarantee.
    juce::int64 lastRetireRotationMs = 0;

    // Currently published render snapshot (see class notes). Written only
    // under the model lock, read lock-free from the audio thread via
    // std::atomic_load / std::atomic_store (the C++17 atomic shared_ptr
    // API; std::atomic<std::shared_ptr> itself needs C++20). The swap
    // keeps the old snapshot alive until any in-flight callback has
    // finished with it.
    std::shared_ptr<const RenderSnapshot> renderSnapshot;

    //==========================================================================
    // M2 FX state. All guarded by modelLock and only mutated on the
    // message thread (plus re-preparation inside prepareToPlay, which
    // JUCE guarantees is never concurrent with the audio callback).
    PluginManager pluginManager;

    // Live plugin instances by slot id, and the two-generation
    // retirement queue for removed ones: timerCallback() rotates
    // retiredFx into retiredFxPrevious and only frees the previous
    // generation, so an instance survives at least one full 3 s timer
    // period after removal — far longer than any audio callback that
    // could still be rendering a snapshot which references it.
    std::map<juce::int64, std::unique_ptr<juce::AudioPluginInstance>> fxInstances;
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> retiredFx;
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> retiredFxPrevious;

    // Render contexts by track id, reused across snapshot rebuilds so
    // PDC delay-line state persists while capacities are unchanged.
    std::map<juce::int64, std::shared_ptr<TrackRenderContext>> renderContexts;

    std::atomic<int> maxBlockSize { 8192 };   // largest device block seen;
                                              // drives scratch sizing and
                                              // plugin preparation

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelineEngine)
};
