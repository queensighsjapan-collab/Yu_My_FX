#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    Session data model for YM-Kithara (M1).

    Plain data only — no audio or UI logic lives here. TimelineEngine owns
    one SessionData and guards it with a critical section on the editing
    side; the audio thread never touches this model directly — it renders
    from an immutable snapshot built from it (see TimelineEngine.h).

    All positions and lengths are in samples at the engine sample rate
    (TimelineEngine::getSampleRate()). Source audio is never modified by
    editing: a clip is just a window (offset + length) into a source plus a
    timeline position, fades and a gain — Ardour-style non-destructive edits.
*/

//==============================================================================
/** One imported audio file, decoded once and shared by any number of clips. */
struct SourceData
{
    juce::int64 id = 0;
    juce::String name;                    // display name (file name without extension)
    juce::File   originalFile;            // where the audio came from (used when saving a project)
    double       fileSampleRate = 0.0;    // sample rate of the file itself

    juce::AudioBuffer<float> fileBuffer;  // decoded audio at the file's own rate

    // The same audio resampled to the engine rate. Held via shared_ptr so
    // render snapshots (see TimelineEngine) can keep a buffer alive and
    // unchanged while the model moves on to a newer resample.
    std::shared_ptr<juce::AudioBuffer<float>> buffer;

    // Min/max peaks of `buffer` (extremes across all channels), one pair per
    // peakWindowSamples samples. Used only for waveform drawing.
    static constexpr int peakWindowSamples = 512;
    std::vector<float> peakMin;
    std::vector<float> peakMax;

    juce::int64 getLengthSamples() const noexcept
    {
        return buffer != nullptr ? buffer->getNumSamples() : 0;
    }
};

//==============================================================================
/** A non-destructive clip: a window into a source, placed on the timeline. */
struct ClipData
{
    juce::int64 id       = 0;
    juce::int64 sourceId = 0;
    juce::int64 start    = 0;   // timeline position (samples, engine rate)
    juce::int64 offset   = 0;   // start position inside the source (samples)
    juce::int64 length   = 0;   // audible length (samples)
    juce::int64 fadeIn   = 0;   // linear fade-in length (samples)
    juce::int64 fadeOut  = 0;   // linear fade-out length (samples)
    float       gain     = 1.0f;

    juce::int64 getEnd() const noexcept { return start + length; }
};

//==============================================================================
/** One insert slot in a track's FX chain (M2).

    The persistent, serialisable identity of a hosted VST3 plugin:
    everything needed to recreate the instance later. The live
    juce::AudioPluginInstance is owned by TimelineEngine (keyed by slot
    id) and is never part of the model, so the model stays plain data.

    If the plugin cannot be instantiated when a project is loaded, the
    slot survives as a "missing" placeholder that keeps its name and its
    saved state, so re-saving the project does not lose it.
*/
struct FxSlotData
{
    juce::int64 id = 0;

    juce::String identifier;        // PluginDescription::createIdentifierString()
    juce::String name;
    juce::String descriptiveName;
    juce::String pluginFormatName;  // "VST3"
    juce::String category;
    juce::String manufacturerName;
    juce::String version;
    juce::String fileOrIdentifier;
    int uniqueId = 0;               // PluginDescription::uniqueId — for VST3
                                    // this is the hash of the class FUID and
                                    // JUCE's host matches classes by it, so
                                    // dropping it makes every plugin unloadable
    int deprecatedUid = 0;          // PluginDescription::deprecatedUid
    bool isInstrument = false;
    int numInputChannels = 0;
    int numOutputChannels = 0;

    bool bypass = false;
    bool missing = false;           // no live instance (not installed / failed to load)
    juce::String stateBase64;       // last saved plugin state; for a missing
                                    // slot this is the only copy and is
                                    // written back verbatim on save

    void fillFromDescription (const juce::PluginDescription& desc)
    {
        identifier       = desc.createIdentifierString();
        name             = desc.name;
        descriptiveName  = desc.descriptiveName;
        pluginFormatName = desc.pluginFormatName;
        category         = desc.category;
        manufacturerName = desc.manufacturerName;
        version          = desc.version;
        fileOrIdentifier = desc.fileOrIdentifier;
        uniqueId         = desc.uniqueId;
        deprecatedUid    = desc.deprecatedUid;
        isInstrument     = desc.isInstrument;
        numInputChannels = desc.numInputChannels;
        numOutputChannels = desc.numOutputChannels;
    }

    juce::PluginDescription toDescription() const
    {
        juce::PluginDescription desc;
        desc.name             = name;
        desc.descriptiveName  = descriptiveName.isEmpty() ? name : descriptiveName;
        desc.pluginFormatName = pluginFormatName.isEmpty() ? "VST3" : pluginFormatName;
        desc.category         = category;
        desc.manufacturerName = manufacturerName;
        desc.version          = version;
        desc.fileOrIdentifier = fileOrIdentifier;
        desc.uniqueId         = uniqueId;
        desc.deprecatedUid    = deprecatedUid;
        desc.isInstrument     = isInstrument;
        desc.numInputChannels = numInputChannels;
        desc.numOutputChannels = numOutputChannels;
        return desc;
    }
};

//==============================================================================
/** What a track holds (M3). Audio tracks hold ClipData windows into
    sources; MIDI tracks hold MidiClipData and one hosted instrument.
    M6 adds the video track: it holds VideoClipData windows into video
    files and plays no part in audio rendering at all. M6.3 adds the
    text track: it holds TextClipData telops (timed text overlays,
    usually imported from an .srt subtitle file) and, like the video
    track, never renders audio. A session holds at most one video
    track and at most one text track. */
enum class TrackKind { audio, midi, video, text };

//==============================================================================
/** One note inside a MIDI clip (M3). Times are in BEATS relative to the
    clip start and are converted to seconds with the session tempo when
    the render snapshot is built — so changing the tempo after entering
    notes shifts their timing inside the clip (set the song's BPM first).
    The channel is fixed to 1 in v1. */
struct MidiNoteData
{
    int    noteNumber  = 60;
    double startBeat   = 0.0;
    double lengthBeats = 0.5;
    int    velocity    = 100;
};

//==============================================================================
/** A MIDI clip on the timeline (M3). Unlike audio clips, the position
    and length are in SECONDS (tempo-independent placement, like audio
    clips); only the notes inside are beat-based (see MidiNoteData).

    M6.9: notes live in a beat-based CONTENT space whose origin is the
    clip's untrimmed start; the clip shows the window that begins
    `offsetBeats` into that content. A note's timeline position is
    startSeconds + (startBeat - offsetBeats) * secondsPerBeat, and a
    note sounds (and shows in the piano roll) only while its start
    lies inside the window [offsetBeats, offsetBeats + windowBeats),
    where windowBeats = lengthSeconds / secondsPerBeat. Trimming the
    edges therefore never moves or deletes note data — it only slides
    the window — so re-extending a clip restores hidden notes exactly.
    This mirrors the audio clips' sample offset, in beats. Persisted
    in project.xml ("offsetBeats", absent in pre-M6.9 files = 0). */
struct MidiClipData
{
    juce::int64 id = 0;
    double startSeconds  = 0.0;
    double lengthSeconds = 0.0;
    double offsetBeats   = 0.0;   // M6.9: window start inside the content
    std::vector<MidiNoteData> notes;

    double getEndSeconds() const noexcept { return startSeconds + lengthSeconds; }

    /** Last content beat visible in the window (exclusive). */
    double getWindowEndBeat (double secondsPerBeat) const noexcept
    {
        return offsetBeats + lengthSeconds / juce::jmax (1.0e-9, secondsPerBeat);
    }
};

//==============================================================================
/** A video clip on the video track (M6). Like a MIDI clip, placement is
    in SECONDS: a window (offset + duration) into the source file placed
    at startSeconds on the timeline. The source file itself is never
    copied or modified — the clip stores its path plus the stream info
    probed at import time (frame rate as a rational, picture size,
    source duration), so the timeline, filmstrip and timecode logic
    never need to reopen the file just to draw. The file's own audio is
    ignored in Phase 1. */
struct VideoClipData
{
    juce::int64 id = 0;
    juce::String filePath;              // absolute path of the source video
    double startSeconds  = 0.0;         // timeline position
    double offsetSeconds = 0.0;         // window start inside the source
    double durationSeconds = 0.0;       // window length
    int    fpsNum = 0, fpsDen = 1;      // source frame rate (rational)
    int    width = 0, height = 0;       // source picture size
    double sourceDurationSeconds = 0.0; // full length of the source file

    // M6.3: fade-in / fade-out durations in seconds (0 = hard edge).
    // When another clip overlaps this one, the overlap additionally
    // becomes a true cross-dissolve (see VideoComposite.h); on its
    // own a fade goes from/to black. Persisted in project.xml
    // ("fadeIn"/"fadeOut", absent in pre-M6.3 files = 0).
    double fadeInSeconds  = 0.0;
    double fadeOutSeconds = 0.0;

    // M6.5: a still image (jpg/png/bmp) placed on the video track —
    // title cards, cover art. The "source" is a single frame, so the
    // clip is NOT clamped by sourceDurationSeconds: its duration is
    // free (5 s at import, stretched/trimmed to anything) and the
    // offset stays 0. Detected at import from the probed stream
    // (see VideoDecoder::probe). Persisted in project.xml ("still",
    // absent in pre-M6.5 files = false).
    bool stillImage = false;

    double getEndSeconds() const noexcept { return startSeconds + durationSeconds; }
    double getSourceEndSeconds() const noexcept { return offsetSeconds + durationSeconds; }

    juce::String getDisplayName() const
    {
        return juce::File (filePath).getFileName();
    }
};

//==============================================================================
/** A telop (text overlay) clip on the text track (M6.3). Placement is
    in seconds, like MIDI/video clips. `text` is UTF-8 and may contain
    line breaks — the renderer draws at most the text's own lines,
    bottom-centred over the picture (see VideoComposite.h's Telop
    namespace). Imported from .srt files by SrtParser, but editable
    like any other clip. Like video clips, text clips do not extend
    the session's audio length. */
struct TextClipData
{
    juce::int64 id = 0;
    double startSeconds = 0.0;      // timeline position
    double durationSeconds = 0.0;   // how long the text stays up
    juce::String text;              // may contain '\n' line breaks

    double getEndSeconds() const noexcept { return startSeconds + durationSeconds; }

    /** First line of the text, for lane/inspector display. */
    juce::String getFirstLine() const
    {
        const int newline = text.indexOfChar ('\n');
        return newline >= 0 ? text.substring (0, newline) : text;
    }
};

//==============================================================================
struct TrackData
{
    juce::int64 id = 0;
    juce::String name;
    float gain = 1.0f;          // 0 .. 1.25
    bool  mute = false;
    bool  solo = false;
    TrackKind kind = TrackKind::audio;
    std::vector<ClipData> clips;
    std::vector<FxSlotData> fxChain;   // insert FX, processed in order (M2)

    // MIDI tracks only (M3): one hosted instrument (its live instance
    // lives in TimelineEngine's instance map, keyed by the slot id,
    // exactly like an FX slot) plus the track's MIDI clips. The fxChain
    // above still applies — on a MIDI track it runs AFTER the instrument.
    FxSlotData instrument;
    bool hasInstrument = false;
    std::vector<MidiClipData> midiClips;

    // M3.3: when true, live external MIDI input (controllers etc.) is
    // delivered to every slot of this track's FX chain during realtime
    // playback, so MIDI-aware effects (YM Mirage's CC pitch/freeze/
    // bypass, …) can be played from a controller inside YM-Kithara.
    // Persisted in project.xml ("fxMidiIn", absent = false).
    bool fxMidiIn = false;

    // M4: record-arm for MIDI keyboard recording. RUNTIME-ONLY by
    // design: saveProject serialises track fields explicitly and never
    // writes this one, and loadProject builds fresh TrackData (false),
    // so a reopened project never comes back armed.
    bool recordArmed = false;

    // Video tracks only (M6): the track's video clips. A session holds
    // at most ONE video track in Phase 1 (TimelineEngine::ensureVideoTrack
    // enforces it); audio/MIDI tracks never have entries here.
    std::vector<VideoClipData> videoClips;

    // Text tracks only (M6.3): the track's telop clips. A session
    // holds at most ONE text track (TimelineEngine::ensureTextTrack
    // enforces it); other track kinds never have entries here.
    std::vector<TextClipData> textClips;
};

//==============================================================================
/** Where the telop text block sits on the frame (M6.6). The integer
    values are persisted in project.xml ("telopVPos"), so do not
    renumber. */
enum class TelopVerticalPosition { bottom = 0, centre = 1, top = 2 };

/** How each telop line is justified (M6.6). Persisted in
    project.xml ("telopAlign"), so do not renumber. */
enum class TelopHorizontalAlign { left = 0, centre = 1, right = 2 };

/** Session-wide telop styling (M6.6): font, size, position,
    alignment and colour for every telop clip. The DEFAULTS reproduce
    the M6.3 fixed style exactly (default sans bold, 100%, bottom,
    centred, white). Persisted as root attributes in project.xml
    ("telopFont" / "telopScale" / "telopVPos" / "telopAlign" /
    "telopColour"); all absent in pre-M6.6 files. The font is named
    only — it is resolved per machine at draw time and falls back to
    the default sans when the name is not installed there (a project
    therefore never breaks on another PC, it just changes clothes). */
struct TelopStyleData
{
    juce::String fontName;              // empty = default sans
    float scalePercent = 100.0f;        // font-size scale, 40..250
    TelopVerticalPosition vertical = TelopVerticalPosition::bottom;
    TelopHorizontalAlign  align    = TelopHorizontalAlign::centre;
    juce::uint32 colourArgb = 0xffffffff;   // the text colour (white)

    bool operator== (const TelopStyleData& other) const noexcept
    {
        return fontName == other.fontName
            && scalePercent == other.scalePercent
            && vertical == other.vertical
            && align == other.align
            && colourArgb == other.colourArgb;
    }

    bool operator!= (const TelopStyleData& other) const noexcept
    {
        return ! operator== (other);
    }
};

//==============================================================================
struct SessionData
{
    double tempoBpm = 120.0;
    std::vector<std::unique_ptr<SourceData>> sources;   // owned, shared by clips
    std::vector<TrackData> tracks;

    // M6: the session timecode rate, as a rational (e.g. 30000/1001 for
    // 29.97 drop-frame). Purely a DISPLAY rate — the timeline itself is
    // seconds throughout, so clips of any frame rate still line up by
    // time. Adopted from the first imported video clip unless the user
    // picks a rate themselves (timecodeFpsUserSet); persisted in
    // project.xml (absent in pre-M6 files = the 30 fps default below).
    int  timecodeFpsNum = 30;
    int  timecodeFpsDen = 1;
    bool timecodeFpsUserSet = false;

    // M6.3: telop (text overlay) font-size scale index — 0 = S,
    // 1 = M, 2 = L — chosen in the text track's header. Persisted in
    // project.xml ("telopSize", absent in pre-M6.3 files = M).
    // M6.6: this is now the S/M/L MIRROR of telopStyle.scalePercent
    // (nearest index: 72/100/138%), kept in sync by every write path
    // (TimelineEngine's setters, project load, newProject) so the
    // M6.3 header selector and any pre-M6.6 file keep working.
    int telopSize = 1;

    // M6.6: the session-wide telop style (font / size / position /
    // alignment / colour). telopStyle.scalePercent is authoritative;
    // telopSize above is its derived S/M/L index.
    TelopStyleData telopStyle;
};
