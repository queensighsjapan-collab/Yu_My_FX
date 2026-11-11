#include "TimelineEngine.h"
#include "VideoAudioExtractor.h"
#include "SrtParser.h"

#include <deque>
#include <map>

namespace
{
    // One-shot resampling with a 4-point Lagrange interpolator. Not the
    // fanciest SRC in the world, but deterministic, dependency-free and
    // plenty for stem playback; sources are resampled once at load time.
    void resampleBuffer (const juce::AudioBuffer<float>& input, double inputRate,
                         juce::AudioBuffer<float>& output, double outputRate)
    {
        const int numChannels = input.getNumChannels();
        const int numInput = input.getNumSamples();

        if (numInput <= 0 || numChannels <= 0)
        {
            output.setSize (juce::jmax (1, numChannels), 0);
            return;
        }

        if (std::abs (inputRate - outputRate) < 0.01)
        {
            output.makeCopyOf (input);
            return;
        }

        const double ratio = inputRate / outputRate;   // input samples per output sample
        const int numOutput = juce::jmax (1, (int) std::floor ((numInput - 1) / ratio));

        output.setSize (numChannels, numOutput);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* in = input.getReadPointer (ch);
            float* out = output.getWritePointer (ch);

            auto sampleAt = [in, numInput] (int index) noexcept -> float
            {
                return in[juce::jlimit (0, numInput - 1, index)];
            };

            for (int n = 0; n < numOutput; ++n)
            {
                const double pos = n * ratio;
                const int i = (int) std::floor (pos);
                const float f = (float) (pos - i);

                const float s0 = sampleAt (i - 1);
                const float s1 = sampleAt (i);
                const float s2 = sampleAt (i + 1);
                const float s3 = sampleAt (i + 2);

                // Lagrange basis polynomials for nodes at -1, 0, 1, 2.
                const float l0 = -f * (f - 1.0f) * (f - 2.0f) / 6.0f;
                const float l1 = (f + 1.0f) * (f - 1.0f) * (f - 2.0f) / 2.0f;
                const float l2 = -(f + 1.0f) * f * (f - 2.0f) / 2.0f;
                const float l3 = (f + 1.0f) * f * (f - 1.0f) / 6.0f;

                out[n] = s0 * l0 + s1 * l1 + s2 * l2 + s3 * l3;
            }
        }
    }

    juce::String makeXmlSafeFileName (const juce::String& name)
    {
        auto result = juce::File::createLegalFileName (name);
        return result.isEmpty() ? juce::String ("audio") : result;
    }

    // M3.2: raise an atomic peak slot to at least `value` (audio thread).
    // A lost race against the UI's per-tick clear just drops one block's
    // peak — acceptable for a meter, and it keeps the render path free
    // of locks and retries with unbounded loops in the common case.
    void storeMaxPeak (std::atomic<float>& slot, float value) noexcept
    {
        float current = slot.load (std::memory_order_relaxed);

        while (value > current
                && ! slot.compare_exchange_weak (current, value,
                                                 std::memory_order_relaxed,
                                                 std::memory_order_relaxed))
        {
        }
    }
}

//==============================================================================
TimelineEngine::TimelineEngine()
{
    formatManager.registerBasicFormats();

    // M4: the capture queue's storage, sized once up front so neither
    // the audio-thread producer nor the message-thread consumer ever
    // allocates (see the recording notes in the header).
    recordEventQueue.resize ((size_t) recordQueueCapacity);

    // Publish an empty snapshot up front so the audio thread always has
    // something valid to load, even before the first edit.
    std::atomic_store (&renderSnapshot,
                       std::shared_ptr<const RenderSnapshot> (
                           std::make_shared<RenderSnapshot>()));

    sourcePlayer.setSource (this);
}

TimelineEngine::~TimelineEngine()
{
    stopTimer();
    closeMidiInputs();   // M3.3: stop device callbacks into the collector
    deviceManager.removeAudioCallback (&sourcePlayer);
    sourcePlayer.setSource (nullptr);
}

//==============================================================================
void TimelineEngine::initialiseDevices()
{
    deviceManager.initialiseWithDefaultDevices (0, 2);
    deviceManager.addAudioCallback (&sourcePlayer);

    // M3.3: open the machine's MIDI inputs (their events feed FX chains
    // of tracks that opted in; harmless when there are no devices).
    openMidiInputs();

    // If a device is already open, adopt its rate right away so files
    // imported before the first audio callback are resampled correctly.
    // (prepareToPlay will refine this when the device actually starts.)
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const double rate = device->getCurrentSampleRate();

        if (rate > 0.0)
        {
            juce::ScopedLock lock (modelLock);
            rebuildAllSourcesLocked (engineRate, rate);
            rebuildSnapshotLocked();
        }
    }
}

//==============================================================================
// Model lookup helpers (lock held by caller)
//==============================================================================
const SourceData* TimelineEngine::findSourceLocked (juce::int64 sourceId) const
{
    for (auto& source : session.sources)
        if (source->id == sourceId)
            return source.get();

    return nullptr;
}

SourceData* TimelineEngine::findSourceLocked (juce::int64 sourceId)
{
    return const_cast<SourceData*> (std::as_const (*this).findSourceLocked (sourceId));
}

const SourceData* TimelineEngine::findSource (juce::int64 sourceId) const
{
    return findSourceLocked (sourceId);
}

const TrackData* TimelineEngine::findTrackLocked (juce::int64 trackId) const
{
    for (auto& track : session.tracks)
        if (track.id == trackId)
            return &track;

    return nullptr;
}

TrackData* TimelineEngine::findTrackLocked (juce::int64 trackId)
{
    return const_cast<TrackData*> (std::as_const (*this).findTrackLocked (trackId));
}

TrackData* TimelineEngine::findTrack (juce::int64 trackId)
{
    return findTrackLocked (trackId);
}

const TrackData* TimelineEngine::findTrack (juce::int64 trackId) const
{
    return findTrackLocked (trackId);
}

bool TimelineEngine::findClipLocked (juce::int64 clipId, TrackData*& trackOut, ClipData*& clipOut)
{
    for (auto& track : session.tracks)
        for (auto& clip : track.clips)
            if (clip.id == clipId)
            {
                trackOut = &track;
                clipOut = &clip;
                return true;
            }

    trackOut = nullptr;
    clipOut = nullptr;
    return false;
}

bool TimelineEngine::getClipData (juce::int64 clipId, ClipData& clipOut, juce::int64& trackIdOut) const
{
    // The model is logically mutable even in const lookups; the lock is a
    // plain member, so const_cast for the scoped lock is safe here.
    juce::ScopedLock lock (const_cast<juce::CriticalSection&> (modelLock));

    for (auto& track : session.tracks)
        for (auto& clip : track.clips)
            if (clip.id == clipId)
            {
                clipOut = clip;
                trackIdOut = track.id;
                return true;
            }

    return false;
}

void TimelineEngine::touchModelLocked()
{
    cachedTotalLength.store (computeTotalLengthLocked());
    rebuildSnapshotLocked();
    modelVersion.fetch_add (1);
    sendChangeMessage();
}

void TimelineEngine::rebuildSnapshotLocked()
{
    auto snapshot = std::make_shared<RenderSnapshot>();
    snapshot->totalLength = cachedTotalLength.load();

    // M3: the stuck-note panic buffer, built once per snapshot so the
    // audio callback never allocates when it needs it.
    snapshot->midiPanicBuffer.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    snapshot->midiPanicBuffer.addEvent (juce::MidiMessage::controllerEvent (1, 120, 0), 0);

    bool anySolo = false;

    for (auto& track : session.tracks)
        anySolo = anySolo || track.solo;

    //--- M2 pass 1: resolve each track's FX chain and its latency, so ---
    //--- the session latency (max) and per-track PDC delays are known. ---
    struct FxResolution
    {
        std::vector<juce::AudioProcessor*> processors;
        int latency = 0;
        int numChannels = 2;
    };

    std::vector<FxResolution> fxResolutions;
    fxResolutions.reserve (session.tracks.size());
    int sessionLatency = 0;

    for (auto& track : session.tracks)
    {
        FxResolution resolution;

        // M3: a MIDI track's instrument latency counts toward the
        // track's total, so PDC aligns it with the audio tracks.
        if (track.kind == TrackKind::midi && track.hasInstrument
             && ! track.instrument.missing)
        {
            if (auto* instance = getFxInstanceLocked (track.instrument.id))
            {
                resolution.latency += instance->getLatencySamples();
                resolution.numChannels = juce::jmax (resolution.numChannels,
                                                     instance->getTotalNumInputChannels(),
                                                     instance->getTotalNumOutputChannels());
            }
        }

        for (auto& slot : track.fxChain)
        {
            if (slot.bypass || slot.missing)
                continue;

            auto* instance = getFxInstanceLocked (slot.id);

            if (instance == nullptr)
                continue;

            resolution.processors.push_back (instance);
            resolution.latency += instance->getLatencySamples();
            resolution.numChannels = juce::jmax (resolution.numChannels,
                                                 instance->getTotalNumInputChannels(),
                                                 instance->getTotalNumOutputChannels());
        }

        sessionLatency = juce::jmax (sessionLatency, resolution.latency);
        fxResolutions.push_back (std::move (resolution));
    }

    // Scratch blocks are chunked to this size, so a device block larger
    // than expected can never overflow a context buffer.
    const int scratchSamples = juce::jlimit (1024, 65536, maxBlockSize.load());

    snapshot->tracks.reserve (session.tracks.size());

    for (size_t trackIndex = 0; trackIndex < session.tracks.size(); ++trackIndex)
    {
        auto& track = session.tracks[trackIndex];
        auto& fx = fxResolutions[trackIndex];

        RenderTrack renderTrack;
        renderTrack.trackId = track.id;
        renderTrack.gain = track.gain;
        // M6: the video track never renders audio, whatever its
        // mute/solo flags say (its header hides those controls anyway).
        // M6.3: the text track gets exactly the same treatment.
        renderTrack.audible = track.kind != TrackKind::video
                               && track.kind != TrackKind::text
                               && ! track.mute && (! anySolo || track.solo);
        renderTrack.kind = track.kind;
        renderTrack.fxMidiIn = track.fxMidiIn;   // M3.3
        renderTrack.recordArmed = track.kind == TrackKind::midi
                                   && track.recordArmed;   // M4
        renderTrack.clips.reserve (track.clips.size());
        renderTrack.fxProcessors = fx.processors;
        renderTrack.pdcDelaySamples = sessionLatency - fx.latency;

        // M3: resolve the instrument and flatten the track's notes into
        // absolute-second events (clip seconds + note beats at the
        // session tempo), notes clipped to their clip's bounds.
        if (track.kind == TrackKind::midi)
        {
            if (track.hasInstrument && ! track.instrument.missing)
                renderTrack.instrument = getFxInstanceLocked (track.instrument.id);

            const double secondsPerBeat = 60.0 / juce::jmax (1.0, session.tempoBpm);

            for (auto& clip : track.midiClips)
            {
                const double clipEnd = clip.getEndSeconds();

                // M6.9: notes are windowed by the clip's beat window
                // (see MidiClipData::offsetBeats). A note sounds only
                // while its START lies inside the window; notes outside
                // stay in the model untouched, so re-extending the clip
                // brings them back exactly. Positions shift by the
                // offset so a left trim never moves a surviving note.
                const double windowStartBeat = clip.offsetBeats;
                const double windowEndBeat = clip.getWindowEndBeat (secondsPerBeat);

                for (auto& note : clip.notes)
                {
                    if (note.startBeat < windowStartBeat
                         || note.startBeat >= windowEndBeat)
                        continue;

                    RenderMidiEvent event;
                    event.startSec = clip.startSeconds
                                     + (note.startBeat - windowStartBeat) * secondsPerBeat;
                    event.endSec = clip.startSeconds
                                   + (note.startBeat + note.lengthBeats - windowStartBeat)
                                     * secondsPerBeat;
                    event.noteNumber = juce::jlimit (0, 127, note.noteNumber);
                    event.velocity = juce::jlimit (1, 127, note.velocity);

                    if (event.startSec >= clipEnd || event.endSec <= event.startSec)
                        continue;

                    event.endSec = juce::jmin (event.endSec, clipEnd);
                    renderTrack.midiEvents.push_back (event);
                }
            }

            std::sort (renderTrack.midiEvents.begin(), renderTrack.midiEvents.end(),
                       [] (const RenderMidiEvent& a, const RenderMidiEvent& b)
                       {
                           return a.startSec < b.startSec;
                       });
        }

        // M6: the video track gets no render context at all — the
        // entry above exists only so the snapshot's track order keeps
        // matching the session's (the M3.2 meter slots are indexed by
        // that order). renderBlock skips it: inaudible, no context.
        // M6.3: the text track is identical in this respect.
        if (track.kind == TrackKind::video || track.kind == TrackKind::text)
        {
            snapshot->tracks.push_back (std::move (renderTrack));
            continue;
        }

        // Render context: reuse the track's existing one (keeping its
        // PDC delay-line state) unless a bigger scratch or delay ring is
        // needed — then replace it wholesale. Old snapshots keep the old
        // context alive via their shared_ptr.
        const int neededChannels = juce::jlimit (2, 32, fx.numChannels);
        const int neededDelayCapacity = renderTrack.pdcDelaySamples > 0
                                            ? renderTrack.pdcDelaySamples + 1 : 0;

        auto& context = renderContexts[track.id];

        if (context == nullptr
             || context->scratch.getNumChannels() < neededChannels
             || context->scratch.getNumSamples() < scratchSamples
             || context->delayCapacity < neededDelayCapacity)
        {
            auto newContext = std::make_shared<TrackRenderContext>();
            newContext->scratch.setSize (neededChannels, scratchSamples);
            newContext->scratch.clear();

            if (neededDelayCapacity > 0)
            {
                newContext->delayLine[0].assign ((size_t) neededDelayCapacity, 0.0f);
                newContext->delayLine[1].assign ((size_t) neededDelayCapacity, 0.0f);
                newContext->delayCapacity = neededDelayCapacity;
            }

            context = std::move (newContext);
        }

        renderTrack.context = context;

        for (auto& clip : track.clips)
        {
            auto* source = findSourceLocked (clip.sourceId);

            if (source == nullptr || source->buffer == nullptr
                 || source->buffer->getNumSamples() <= 0)
                continue;

            RenderClip renderClip;
            renderClip.buffer  = source->buffer;   // shares ownership; the
                                                   // buffer stays alive and
                                                   // immutable for as long as
                                                   // this snapshot is in use
            renderClip.start   = clip.start;
            renderClip.offset  = clip.offset;
            renderClip.length  = clip.length;
            renderClip.fadeIn  = clip.fadeIn;
            renderClip.fadeOut = clip.fadeOut;
            renderClip.gain    = clip.gain;
            renderTrack.clips.push_back (std::move (renderClip));
        }

        snapshot->tracks.push_back (std::move (renderTrack));
    }

    // Contexts of deleted tracks would linger forever otherwise.
    for (auto it = renderContexts.begin(); it != renderContexts.end();)
    {
        bool alive = false;

        for (auto& track : session.tracks)
            if (track.id == it->first)
                alive = true;

        if (alive)
            ++it;
        else
            it = renderContexts.erase (it);
    }

    std::atomic_store (&renderSnapshot,
                       std::shared_ptr<const RenderSnapshot> (std::move (snapshot)));
}

juce::int64 TimelineEngine::computeTotalLengthLocked() const
{
    juce::int64 total = 0;

    for (auto& track : session.tracks)
    {
        for (auto& clip : track.clips)
            total = juce::jmax (total, clip.getEnd());

        // M3: MIDI clips (positions in seconds) count too.
        for (auto& clip : track.midiClips)
            total = juce::jmax (total, secondsToSamples (clip.getEndSeconds()));

        // M6.4: video clips count too (this reverses the M6 Phase 1
        // rule). Every consumer of the session length derives from this
        // one number — the transport stop point, the playhead clamp in
        // setPositionSamples, the Play enable in the UI, and both
        // export spans (session length + 2 s tail) — so counting video
        // ends here makes a video-only session playable end to end and
        // stops a video longer than the audio being cut off, in the
        // preview and in exports alike. The video track still renders
        // no audio and takes no part in PDC; only its clips' ends count.
        for (auto& clip : track.videoClips)
            total = juce::jmax (total, secondsToSamples (clip.getEndSeconds()));
    }

    return total;
}

//==============================================================================
// Sources
//==============================================================================
void TimelineEngine::computePeaksLocked (SourceData& source)
{
    if (source.buffer == nullptr)
    {
        source.peakMin.clear();
        source.peakMax.clear();
        return;
    }

    const auto& buffer = *source.buffer;
    const int numSamples = buffer.getNumSamples();
    const int numWindows = (numSamples + SourceData::peakWindowSamples - 1)
                             / SourceData::peakWindowSamples;

    source.peakMin.assign ((size_t) juce::jmax (0, numWindows), 0.0f);
    source.peakMax.assign ((size_t) juce::jmax (0, numWindows), 0.0f);

    for (int w = 0; w < numWindows; ++w)
    {
        const int start = w * SourceData::peakWindowSamples;
        const int count = juce::jmin (SourceData::peakWindowSamples, numSamples - start);

        float minValue = 0.0f, maxValue = 0.0f;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (
                buffer.getReadPointer (ch) + start, count);

            minValue = juce::jmin (minValue, range.getStart());
            maxValue = juce::jmax (maxValue, range.getEnd());
        }

        source.peakMin[(size_t) w] = minValue;
        source.peakMax[(size_t) w] = maxValue;
    }
}

void TimelineEngine::resampleSourceLocked (SourceData& source)
{
    // Always resample into a FRESH buffer: snapshots already published keep
    // pointing at the previous buffer, so the audio thread never sees a
    // buffer change underneath it.
    auto newBuffer = std::make_shared<juce::AudioBuffer<float>>();
    resampleBuffer (source.fileBuffer, source.fileSampleRate, *newBuffer, engineRate);
    source.buffer = std::move (newBuffer);
    computePeaksLocked (source);
}

void TimelineEngine::rebuildAllSourcesLocked (double oldRate, double newRate)
{
    if (newRate <= 0.0)
        return;

    const double factor = (oldRate > 0.0) ? newRate / oldRate : 1.0;
    engineRate = newRate;

    for (auto& source : session.sources)
        resampleSourceLocked (*source);

    // Clip positions are expressed in samples at the engine rate, so they
    // must be rescaled when that rate changes (e.g. the user switches to a
    // device running at a different sample rate).
    if (std::abs (factor - 1.0) > 1.0e-9)
    {
        auto scale = [factor] (juce::int64 value)
        {
            return (juce::int64) std::llround ((double) value * factor);
        };

        for (auto& track : session.tracks)
            for (auto& clip : track.clips)
            {
                clip.start   = scale (clip.start);
                clip.offset  = scale (clip.offset);
                clip.length  = scale (clip.length);
                clip.fadeIn  = scale (clip.fadeIn);
                clip.fadeOut = scale (clip.fadeOut);
            }

        playPosition.store (scale (playPosition.load()));
    }

    cachedTotalLength.store (computeTotalLengthLocked());
}

juce::int64 TimelineEngine::addSourceFromFileLocked (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0)
        return -1;

    auto source = std::make_unique<SourceData>();
    source->id = nextId++;
    source->name = file.getFileNameWithoutExtension();
    source->originalFile = file;
    source->fileSampleRate = reader->sampleRate;

    source->fileBuffer.setSize ((int) juce::jmin<juce::int64> (reader->numChannels, 2),
                                (int) reader->lengthInSamples);

    if (! reader->read (&source->fileBuffer, 0, (int) reader->lengthInSamples, 0, true, true))
        return -1;

    const juce::int64 newId = source->id;
    session.sources.push_back (std::move (source));
    resampleSourceLocked (*session.sources.back());
    return newId;
}

juce::int64 TimelineEngine::addSourceFromFile (const juce::File& file)
{
    juce::ScopedLock lock (modelLock);
    const auto id = addSourceFromFileLocked (file);

    if (id > 0)
        touchModelLocked();

    return id;
}

//==============================================================================
// Tracks and clips
//==============================================================================
juce::int64 TimelineEngine::addTrackLocked (const juce::String& name)
{
    TrackData track;
    track.id = nextId++;
    track.name = name.isEmpty() ? "Track " + juce::String (session.tracks.size() + 1) : name;
    session.tracks.push_back (std::move (track));
    return session.tracks.back().id;
}

juce::int64 TimelineEngine::addTrack (const juce::String& name)
{
    juce::ScopedLock lock (modelLock);
    const auto id = addTrackLocked (name);
    touchModelLocked();
    return id;
}

void TimelineEngine::clampClipToSourceLocked (ClipData& clip) const
{
    clip.start = juce::jmax<juce::int64> (0, clip.start);
    clip.offset = juce::jmax<juce::int64> (0, clip.offset);
    clip.length = juce::jmax<juce::int64> (0, clip.length);
    clip.gain = juce::jlimit (0.0f, 1.25f, clip.gain);

    if (auto* source = findSourceLocked (clip.sourceId))
    {
        const auto sourceLength = source->getLengthSamples();
        clip.offset = juce::jmin (clip.offset, sourceLength);
        clip.length = juce::jmin (clip.length, sourceLength - clip.offset);
    }

    clip.fadeIn  = juce::jlimit<juce::int64> (0, clip.length, clip.fadeIn);
    clip.fadeOut = juce::jlimit<juce::int64> (0, clip.length, clip.fadeOut);
}

juce::int64 TimelineEngine::addClipLocked (juce::int64 trackId, const ClipData& clipWithoutId)
{
    auto* track = findTrackLocked (trackId);

    if (track == nullptr || findSourceLocked (clipWithoutId.sourceId) == nullptr)
        return -1;

    ClipData clip = clipWithoutId;
    clip.id = nextId++;
    clampClipToSourceLocked (clip);
    track->clips.push_back (clip);
    return clip.id;
}

juce::int64 TimelineEngine::addClip (juce::int64 trackId, const ClipData& clipWithoutId)
{
    juce::ScopedLock lock (modelLock);
    const auto id = addClipLocked (trackId, clipWithoutId);

    if (id > 0)
        touchModelLocked();

    return id;
}

juce::int64 TimelineEngine::importFileLocked (const juce::File& file)
{
    const auto sourceId = addSourceFromFileLocked (file);

    if (sourceId <= 0)
        return -1;

    const auto trackId = addTrackLocked (file.getFileNameWithoutExtension());

    ClipData clip;
    clip.sourceId = sourceId;
    clip.start = 0;
    clip.offset = 0;

    if (auto* source = findSourceLocked (sourceId))
        clip.length = source->getLengthSamples();

    return addClipLocked (trackId, clip) > 0 ? trackId : -1;
}

juce::int64 TimelineEngine::importFileAsNewTrack (const juce::File& file)
{
    juce::ScopedLock lock (modelLock);
    const auto trackId = importFileLocked (file);

    if (trackId > 0)
        touchModelLocked();

    return trackId;
}

//==============================================================================
// Standard MIDI File import (M3.1). Everything is parsed into plain
// locals first; the model is only touched once the file is known to be
// usable, so a failed import leaves the session exactly as it was.
//==============================================================================
bool TimelineEngine::importMidiFile (const juce::File& file, juce::String& resultMessage)
{
    juce::ScopedLock lock (modelLock);

    juce::MidiFile midiFile;

    {
        juce::FileInputStream stream (file);

        if (! stream.openedOk() || ! midiFile.readFrom (stream))
        {
            resultMessage = "Could not read the MIDI file: " + file.getFileName();
            return false;
        }
    }

    // A positive time format is ticks per quarter note; negative means
    // SMPTE division, which the beat-based model cannot represent.
    const auto timeFormat = midiFile.getTimeFormat();

    if (timeFormat < 0)
    {
        resultMessage = "SMPTE-format MIDI files are not supported";
        return false;
    }

    if (timeFormat == 0)
    {
        resultMessage = "Invalid MIDI file (zero ticks per quarter note): " + file.getFileName();
        return false;
    }

    const double ticksPerQuarterNote = (double) timeFormat;

    //--- tempo: the FIRST tempo meta event anywhere in the file wins. ---
    // Mid-file tempo changes are not followed in v1: all beats are
    // converted with this initial tempo (see README).
    bool   foundTempo = false;
    double fileBpm = 120.0;

    for (int t = 0; t < midiFile.getNumTracks() && ! foundTempo; ++t)
    {
        const auto* sequence = midiFile.getTrack (t);

        if (sequence == nullptr)
            continue;

        for (int i = 0; i < sequence->getNumEvents(); ++i)
        {
            const auto& message = sequence->getEventPointer (i)->message;

            if (message.isTempoMetaEvent())
            {
                const double secondsPerQuarter = message.getTempoSecondsPerQuarterNote();

                if (secondsPerQuarter > 0.0)
                {
                    fileBpm = 60.0 / secondsPerQuarter;
                    foundTempo = true;
                }

                break;   // only the first tempo event of this track counts;
                         // the outer loop stops at the first track that has one
            }
        }
    }

    //--- notes: pair note-ons with note-offs per (channel, note), FIFO. --
    struct ParsedTrack
    {
        juce::String name;
        std::vector<MidiNoteData> notes;
        double lastEndBeats = 0.0;
    };

    std::vector<ParsedTrack> parsedTracks;

    for (int t = 0; t < midiFile.getNumTracks(); ++t)
    {
        const auto* sequence = midiFile.getTrack (t);

        if (sequence == nullptr)
            continue;

        juce::String trackName;
        std::vector<MidiNoteData> notes;
        double lastEventTicks = 0.0;

        // Open note-ons waiting for their note-off, per (channel, note).
        std::map<std::pair<int, int>, std::deque<std::pair<double, int>>> openNotes;

        auto closeNote = [&notes, ticksPerQuarterNote] (int noteNumber, double startTicks,
                                                        double endTicks, int velocity)
        {
            MidiNoteData note;
            note.noteNumber  = juce::jlimit (0, 127, noteNumber);
            note.startBeat   = startTicks / ticksPerQuarterNote;
            note.lengthBeats = juce::jmax (0.0, (endTicks - startTicks) / ticksPerQuarterNote);
            note.velocity    = juce::jlimit (1, 127, velocity);
            notes.push_back (note);
        };

        for (int i = 0; i < sequence->getNumEvents(); ++i)
        {
            const auto& message = sequence->getEventPointer (i)->message;
            const double ticks = message.getTimeStamp();
            lastEventTicks = juce::jmax (lastEventTicks, ticks);

            if (message.isTrackNameEvent())
            {
                if (trackName.isEmpty())
                    trackName = message.getTextFromTextMetaEvent().trim();

                continue;
            }

            // JUCE reports a velocity-0 note-on via isNoteOff(), so this
            // pair of checks covers both note-off spellings.
            if (message.isNoteOn())
            {
                openNotes[{ message.getChannel(), message.getNoteNumber() }]
                    .push_back ({ ticks, (int) message.getVelocity() });
            }
            else if (message.isNoteOff())
            {
                auto it = openNotes.find ({ message.getChannel(), message.getNoteNumber() });

                if (it != openNotes.end() && ! it->second.empty())
                {
                    const auto start = it->second.front();
                    it->second.pop_front();
                    closeNote (message.getNoteNumber(), start.first, ticks, start.second);
                }
                // Unmatched note-offs are ignored.
            }
        }

        // Notes still held at the end of the track are closed at the
        // last event's time rather than dropped.
        for (auto& entry : openNotes)
            while (! entry.second.empty())
            {
                const auto start = entry.second.front();
                entry.second.pop_front();
                closeNote (entry.first.second, start.first, lastEventTicks, start.second);
            }

        if (notes.empty())
            continue;   // conductor / meta-only track: nothing to import

        std::sort (notes.begin(), notes.end(),
                   [] (const MidiNoteData& a, const MidiNoteData& b)
                   {
                       return a.startBeat < b.startBeat;
                   });

        ParsedTrack parsed;
        parsed.name = trackName;
        parsed.notes = std::move (notes);

        for (auto& note : parsed.notes)
            parsed.lastEndBeats = juce::jmax (parsed.lastEndBeats,
                                              note.startBeat + note.lengthBeats);

        parsedTracks.push_back (std::move (parsed));
    }

    if (parsedTracks.empty())
    {
        resultMessage = "No notes found in the MIDI file: " + file.getFileName();
        return false;
    }

    // The tempo is applied BEFORE the clips are built so their lengths
    // (seconds) and all later beat<->second conversion use the file's
    // tempo. With no tempo event the session keeps its current BPM.
    if (foundTempo)
        session.tempoBpm = juce::jlimit (20.0, 300.0, fileBpm);

    const double secondsPerBeat = 60.0 / juce::jmax (1.0, session.tempoBpm);

    int createdCount = 0;

    for (auto& parsed : parsedTracks)
    {
        ++createdCount;

        TrackData track;
        track.id = nextId++;
        track.kind = TrackKind::midi;
        track.name = parsed.name.isNotEmpty()
                         ? parsed.name
                         : file.getFileNameWithoutExtension() + " " + juce::String (createdCount);

        MidiClipData clip;
        clip.id = nextId++;
        clip.startSeconds = 0.0;
        clip.notes = std::move (parsed.notes);
        clip.lengthSeconds = parsed.lastEndBeats * secondsPerBeat;

        // A clip must cover its last note end (the snapshot clips notes
        // to the clip bounds); guard the degenerate all-zero case so the
        // notes are not swallowed.
        if (clip.lengthSeconds <= 0.0)
            clip.lengthSeconds = secondsPerBeat;

        track.midiClips.push_back (std::move (clip));
        session.tracks.push_back (std::move (track));
    }

    touchModelLocked();

    resultMessage = "Imported MIDI: " + juce::String (createdCount) + " tracks";

    if (foundTempo)
    {
        const double bpm = session.tempoBpm;
        const auto bpmText = std::abs (bpm - std::round (bpm)) < 0.005
                                 ? juce::String ((int) std::llround (bpm))
                                 : juce::String (bpm, 2);
        resultMessage += ", tempo " + bpmText + " BPM";
    }
    else
    {
        resultMessage += ", no tempo in file (kept session BPM)";
    }

    return true;
}

bool TimelineEngine::updateClip (juce::int64 clipId, juce::int64 targetTrackId,
                                 const ClipData& newValues)
{
    juce::ScopedLock lock (modelLock);

    TrackData* track = nullptr;
    ClipData* clip = nullptr;

    if (! findClipLocked (clipId, track, clip))
        return false;

    ClipData updated = newValues;
    updated.id = clipId;
    updated.sourceId = clip->sourceId;
    clampClipToSourceLocked (updated);

    if (targetTrackId > 0 && targetTrackId != track->id)
    {
        auto* target = findTrackLocked (targetTrackId);

        if (target == nullptr)
            return false;

        // Move between tracks: erase from the old track, insert into the new.
        auto& clips = track->clips;
        clips.erase (std::remove_if (clips.begin(), clips.end(),
                                     [clipId] (const ClipData& c) { return c.id == clipId; }),
                     clips.end());
        target->clips.push_back (updated);
    }
    else
    {
        *clip = updated;
    }

    touchModelLocked();
    return true;
}

bool TimelineEngine::removeClip (juce::int64 clipId)
{
    juce::ScopedLock lock (modelLock);

    for (auto& track : session.tracks)
    {
        auto& clips = track.clips;
        const auto oldSize = clips.size();
        clips.erase (std::remove_if (clips.begin(), clips.end(),
                                     [clipId] (const ClipData& c) { return c.id == clipId; }),
                     clips.end());

        if (clips.size() != oldSize)
        {
            touchModelLocked();
            return true;
        }
    }

    return false;
}

juce::int64 TimelineEngine::splitClip (juce::int64 clipId, juce::int64 atSamples)
{
    juce::ScopedLock lock (modelLock);

    TrackData* track = nullptr;
    ClipData* clip = nullptr;

    if (! findClipLocked (clipId, track, clip))
        return -1;

    if (atSamples <= clip->start || atSamples >= clip->getEnd())
        return -1;

    ClipData right = *clip;
    right.id = nextId++;
    right.offset += (atSamples - clip->start);
    right.length = clip->getEnd() - atSamples;
    right.start = atSamples;
    right.fadeIn = 0;                       // the cut point starts clean

    clip->length = atSamples - clip->start;
    clip->fadeOut = juce::jmin (clip->fadeOut, clip->length);
    right.fadeOut = juce::jmin (right.fadeOut, right.length);

    track->clips.push_back (right);
    touchModelLocked();
    return right.id;
}

bool TimelineEngine::removeTrack (juce::int64 trackId)
{
    juce::ScopedLock lock (modelLock);

    // M4: finalise this track's take (if one is being recorded) before
    // the track goes away — held notes close at the current position
    // and the clip dies with the track, exactly like its other clips.
    // The capture queue is drained first, as in the disarm path.
    if (recordingActive.load())
        pumpRecordQueueLocked();

    for (auto it = takeBuilders.begin(); it != takeBuilders.end(); ++it)
    {
        if (it->trackId == trackId)
        {
            finaliseTakeLocked (*it, playPosition.load());
            takeBuilders.erase (it);
            break;
        }
    }

    if (auto* track = findTrackLocked (trackId))
    {
        for (auto& slot : track->fxChain)
            retireFxInstanceLocked (slot.id);

        if (track->hasInstrument)
            retireFxInstanceLocked (track->instrument.id);
    }

    auto& tracks = session.tracks;
    const auto oldSize = tracks.size();
    tracks.erase (std::remove_if (tracks.begin(), tracks.end(),
                                  [trackId] (const TrackData& t) { return t.id == trackId; }),
                  tracks.end());

    if (tracks.size() == oldSize)
        return false;

    touchModelLocked();
    return true;
}

void TimelineEngine::renameTrack (juce::int64 trackId, const juce::String& newName)
{
    juce::ScopedLock lock (modelLock);

    if (auto* track = findTrackLocked (trackId))
    {
        track->name = newName;
        touchModelLocked();
    }
}

void TimelineEngine::setTrackGain (juce::int64 trackId, float gain)
{
    juce::ScopedLock lock (modelLock);

    if (auto* track = findTrackLocked (trackId))
    {
        track->gain = juce::jlimit (0.0f, 1.25f, gain);
        touchModelLocked();
    }
}

void TimelineEngine::setTrackMute (juce::int64 trackId, bool mute)
{
    juce::ScopedLock lock (modelLock);

    if (auto* track = findTrackLocked (trackId))
    {
        track->mute = mute;
        touchModelLocked();
    }
}

void TimelineEngine::setTrackSolo (juce::int64 trackId, bool solo)
{
    juce::ScopedLock lock (modelLock);

    if (auto* track = findTrackLocked (trackId))
    {
        track->solo = solo;
        touchModelLocked();
    }
}

void TimelineEngine::setTrackFxMidiIn (juce::int64 trackId, bool enabled)
{
    juce::ScopedLock lock (modelLock);

    if (auto* track = findTrackLocked (trackId))
    {
        track->fxMidiIn = enabled;
        touchModelLocked();
    }
}

void TimelineEngine::setTempo (double bpm)
{
    juce::ScopedLock lock (modelLock);
    session.tempoBpm = juce::jlimit (20.0, 300.0, bpm);
    touchModelLocked();
}

double TimelineEngine::getTempo()
{
    juce::ScopedLock lock (modelLock);
    return session.tempoBpm;
}

//==============================================================================
// Transport
//==============================================================================
void TimelineEngine::play()
{
    {
        juce::ScopedLock lock (modelLock);

        // M4: with a MIDI track armed, Play/Record runs even in an
        // empty session — the take itself creates the length.
        if (cachedTotalLength.load() <= 0 && ! hasArmedMidiTrackLocked())
            return;

        if (cachedTotalLength.load() > 0
             && playPosition.load() >= cachedTotalLength.load())
            playPosition.store (0);

        playing.store (true);
        syncRecordingLocked();   // M4: armed tracks -> recording engages
    }

    sendChangeMessage();
}

void TimelineEngine::pause()
{
    playing.store (false);
    midiPanicPending.store (true);   // M3: silence any sounding notes

    // M4: stopping finalises any in-progress takes (held notes close
    // at the stop position; empty takes leave no clip).
    if (recordingActive.load())
    {
        juce::ScopedLock lock (modelLock);
        finishRecordingLocked();
    }

    sendChangeMessage();
}

void TimelineEngine::returnToZero()
{
    // M4: a seek mid-record finalises the current takes at the
    // pre-seek position; if the transport keeps playing with tracks
    // armed, fresh takes start at the new position.
    if (recordingActive.load())
    {
        juce::ScopedLock lock (modelLock);
        finishRecordingLocked();
    }

    playPosition.store (0);
    midiPanicPending.store (true);

    if (playing.load())
    {
        juce::ScopedLock lock (modelLock);
        syncRecordingLocked();
    }

    sendChangeMessage();
}

void TimelineEngine::setPositionSamples (juce::int64 newPosition)
{
    // M4: same seek rule as returnToZero (see above).
    if (recordingActive.load())
    {
        juce::ScopedLock lock (modelLock);
        finishRecordingLocked();
    }

    const auto total = cachedTotalLength.load();
    playPosition.store (juce::jlimit<juce::int64> (0, juce::jmax<juce::int64> (0, total), newPosition));
    midiPanicPending.store (true);

    if (playing.load())
    {
        juce::ScopedLock lock (modelLock);
        syncRecordingLocked();
    }

    sendChangeMessage();
}

void TimelineEngine::setPositionSeconds (double newPositionSeconds)
{
    setPositionSamples (secondsToSamples (newPositionSeconds));
}

juce::int64 TimelineEngine::getTotalLengthSamples()
{
    // Kept up to date by touchModelLocked() / rebuildAllSourcesLocked(),
    // so the UI timers that poll this never need the model lock.
    return cachedTotalLength.load();
}

void TimelineEngine::setMasterGain (float newGain)
{
    masterGain.store (juce::jlimit (0.0f, 1.0f, newGain));
}

//==============================================================================
// Level metering (M3.2): plain loads / explicit clears for the UI tick.
//==============================================================================
void TimelineEngine::getTrackPeak (int trackIndex, float& left, float& right) const
{
    if (trackIndex < 0 || trackIndex >= maxMeterTracks)
    {
        left = 0.0f;
        right = 0.0f;
        return;
    }

    left  = trackPeakLeft[(size_t) trackIndex].load (std::memory_order_relaxed);
    right = trackPeakRight[(size_t) trackIndex].load (std::memory_order_relaxed);
}

void TimelineEngine::getMasterPeak (float& left, float& right) const
{
    left  = masterPeakLeft.load (std::memory_order_relaxed);
    right = masterPeakRight.load (std::memory_order_relaxed);
}

void TimelineEngine::clearTrackPeak (int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= maxMeterTracks)
        return;

    trackPeakLeft[(size_t) trackIndex].store (0.0f, std::memory_order_relaxed);
    trackPeakRight[(size_t) trackIndex].store (0.0f, std::memory_order_relaxed);
}

void TimelineEngine::clearMasterPeak()
{
    masterPeakLeft.store (0.0f, std::memory_order_relaxed);
    masterPeakRight.store (0.0f, std::memory_order_relaxed);
}

//==============================================================================
// Per-track VST3 insert FX (M2)
//==============================================================================
void TimelineEngine::prepareFxInstanceLocked (juce::AudioPluginInstance& instance)
{
    const double rate = engineRate.load();
    const int block = juce::jlimit (64, 65536, maxBlockSize.load());
    instance.setRateAndBufferSizeDetails (rate, block);
    instance.prepareToPlay (rate, block);
}

juce::AudioPluginInstance* TimelineEngine::getFxInstanceLocked (juce::int64 slotId)
{
    auto it = fxInstances.find (slotId);
    return it != fxInstances.end() ? it->second.get() : nullptr;
}

bool TimelineEngine::instantiateFxSlotLocked (FxSlotData& slot,
                                              juce::String& errorOut)
{
    errorOut.clear();
    auto instance = pluginManager.createInstance (slot.toDescription(),
                                                  engineRate.load(),
                                                  maxBlockSize.load(),
                                                  errorOut);

    if (instance == nullptr)
    {
        slot.missing = true;
        return false;
    }

    prepareFxInstanceLocked (*instance);
    fxInstances[slot.id] = std::move (instance);
    slot.missing = false;
    return true;
}

void TimelineEngine::retireFxInstanceLocked (juce::int64 slotId)
{
    auto it = fxInstances.find (slotId);

    if (it == fxInstances.end())
        return;

    // Never delete here: a render snapshot already in flight on the
    // audio thread may still hold a raw pointer to this instance. The
    // retirement queue (freed by timerCallback, two generations deep)
    // keeps it alive far beyond any callback's lifetime.
    retiredFx.push_back (std::move (it->second));
    fxInstances.erase (it);

    if (! isTimerRunning())
        startTimer (3000);
}

void TimelineEngine::retireAllFxInstancesLocked()
{
    for (auto& pair : fxInstances)
        retiredFx.push_back (std::move (pair.second));

    fxInstances.clear();

    if (! retiredFx.empty() && ! isTimerRunning())
        startTimer (3000);
}

void TimelineEngine::timerCallback()
{
    // M4: while recording this timer runs at a 50 ms cadence. Pump the
    // capture queue into the take clips, and notice when the transport
    // stopped on its own at the session end — the audio thread only
    // flips `playing`; finalising takes is message-thread work.
    if (recordingActive.load())
    {
        juce::ScopedLock lock (modelLock);
        pumpRecordQueueLocked();

        if (! playing.load())
            finishRecordingLocked();
    }

    // Rotate the retirement generations: only the older generation is
    // destroyed, so every retired instance survives at least one full
    // timer period (3 s) after its removal. M4: the rotation is
    // TIME-gated (>= 2.9 s since the last one) rather than per-tick,
    // because the recording pump above runs this same callback at
    // 50 ms — an ungated rotation would free retirees after ~100 ms.
    const auto nowMs = (juce::int64) (juce::Time::getMillisecondCounterHiRes() * 1000.0);

    if (nowMs - lastRetireRotationMs >= 2900)
    {
        lastRetireRotationMs = nowMs;
        retiredFxPrevious.clear();
        retiredFxPrevious.swap (retiredFx);

        if (retiredFx.empty() && retiredFxPrevious.empty()
             && ! recordingActive.load())
            stopTimer();
    }
}

bool TimelineEngine::addFxToTrack (juce::int64 trackId,
                                   const juce::PluginDescription& description,
                                   juce::String& errorOut)
{
    juce::ScopedLock lock (modelLock);

    auto* track = findTrackLocked (trackId);

    if (track == nullptr)
    {
        errorOut = "The track no longer exists.";
        return false;
    }

    FxSlotData slot;
    slot.id = nextId++;
    slot.fillFromDescription (description);

    juce::String pluginError;

    if (! instantiateFxSlotLocked (slot, pluginError))
    {
        errorOut = "Could not load the plugin \"" + slot.name + "\".\n"
                 + (pluginError.isNotEmpty()
                        ? pluginError
                        : juce::String ("It may have been moved or uninstalled "
                                        "since the last scan."));
        return false;
    }

    track->fxChain.push_back (slot);
    touchModelLocked();
    return true;
}

bool TimelineEngine::removeFx (juce::int64 trackId, juce::int64 slotId)
{
    juce::ScopedLock lock (modelLock);

    auto* track = findTrackLocked (trackId);

    if (track == nullptr)
        return false;

    auto& chain = track->fxChain;
    const auto oldSize = chain.size();
    chain.erase (std::remove_if (chain.begin(), chain.end(),
                                 [slotId] (const FxSlotData& s) { return s.id == slotId; }),
                 chain.end());

    if (chain.size() == oldSize)
        return false;

    retireFxInstanceLocked (slotId);
    touchModelLocked();
    return true;
}

void TimelineEngine::setFxBypass (juce::int64 trackId, juce::int64 slotId, bool bypass)
{
    juce::ScopedLock lock (modelLock);

    if (auto* track = findTrackLocked (trackId))
    {
        for (auto& slot : track->fxChain)
        {
            if (slot.id == slotId)
            {
                slot.bypass = bypass;
                touchModelLocked();
                return;
            }
        }
    }
}

juce::AudioPluginInstance* TimelineEngine::getFxInstance (juce::int64 slotId)
{
    juce::ScopedLock lock (modelLock);
    return getFxInstanceLocked (slotId);
}

bool TimelineEngine::hasFxSlot (juce::int64 slotId)
{
    juce::ScopedLock lock (modelLock);

    for (auto& track : session.tracks)
    {
        for (auto& slot : track.fxChain)
            if (slot.id == slotId)
                return true;

        // M3: instrument slots count too (their editor windows are
        // pruned by the same check).
        if (track.hasInstrument && track.instrument.id == slotId)
            return true;
    }

    return false;
}

void TimelineEngine::refreshFxState()
{
    juce::ScopedLock lock (modelLock);
    touchModelLocked();
}

bool TimelineEngine::getTrackFx (juce::int64 trackId, juce::String& trackNameOut,
                                 std::vector<FxSlotData>& chainOut)
{
    juce::ScopedLock lock (modelLock);

    auto* track = findTrackLocked (trackId);

    if (track == nullptr)
        return false;

    trackNameOut = track->name;
    chainOut = track->fxChain;
    return true;
}

juce::String TimelineEngine::captureSlotStateLocked (FxSlotData& slot)
{
    // The live instance's current state wins; a missing slot writes its
    // stored state back verbatim so a save/load round-trip never loses it.
    if (auto* instance = getFxInstanceLocked (slot.id))
    {
        juce::MemoryBlock block;
        instance->getStateInformation (block);
        slot.stateBase64 = block.toBase64Encoding();
    }

    return slot.stateBase64;
}

//==============================================================================
// MIDI tracks, instruments and piano-roll data (M3)
//==============================================================================
bool TimelineEngine::findMidiClipLocked (juce::int64 clipId, TrackData*& trackOut,
                                         MidiClipData*& clipOut)
{
    for (auto& track : session.tracks)
        for (auto& clip : track.midiClips)
            if (clip.id == clipId)
            {
                trackOut = &track;
                clipOut = &clip;
                return true;
            }

    trackOut = nullptr;
    clipOut = nullptr;
    return false;
}

bool TimelineEngine::findMidiClipLocked (juce::int64 clipId, const TrackData*& trackOut,
                                         const MidiClipData*& clipOut) const
{
    for (auto& track : session.tracks)
        for (auto& clip : track.midiClips)
            if (clip.id == clipId)
            {
                trackOut = &track;
                clipOut = &clip;
                return true;
            }

    trackOut = nullptr;
    clipOut = nullptr;
    return false;
}

juce::int64 TimelineEngine::addMidiTrack()
{
    juce::ScopedLock lock (modelLock);

    int midiCount = 0;

    for (auto& track : session.tracks)
        if (track.kind == TrackKind::midi)
            ++midiCount;

    TrackData track;
    track.id = nextId++;
    track.name = "MIDI " + juce::String (midiCount + 1);
    track.kind = TrackKind::midi;
    session.tracks.push_back (std::move (track));

    const auto id = session.tracks.back().id;
    touchModelLocked();
    return id;
}

bool TimelineEngine::setTrackInstrument (juce::int64 trackId,
                                         const juce::PluginDescription& description,
                                         juce::String& errorOut)
{
    juce::ScopedLock lock (modelLock);

    auto* track = findTrackLocked (trackId);

    if (track == nullptr || track->kind != TrackKind::midi)
    {
        errorOut = "Instruments can only be set on a MIDI track.";
        return false;
    }

    FxSlotData slot;
    slot.id = nextId++;
    slot.fillFromDescription (description);

    juce::String pluginError;

    if (! instantiateFxSlotLocked (slot, pluginError))
    {
        errorOut = "Could not load the instrument \"" + slot.name + "\".\n"
                 + (pluginError.isNotEmpty()
                        ? pluginError
                        : juce::String ("It may have been moved or uninstalled "
                                        "since the last scan."));
        return false;
    }

    if (track->hasInstrument)
        retireFxInstanceLocked (track->instrument.id);

    track->instrument = slot;
    track->hasInstrument = true;
    touchModelLocked();
    return true;
}

bool TimelineEngine::removeTrackInstrument (juce::int64 trackId)
{
    juce::ScopedLock lock (modelLock);

    auto* track = findTrackLocked (trackId);

    if (track == nullptr || track->kind != TrackKind::midi || ! track->hasInstrument)
        return false;

    retireFxInstanceLocked (track->instrument.id);
    track->instrument = FxSlotData {};
    track->hasInstrument = false;
    touchModelLocked();
    return true;
}

bool TimelineEngine::getTrackInstrument (juce::int64 trackId, bool& hasInstrumentOut,
                                         FxSlotData& instrumentOut) const
{
    juce::ScopedLock lock (const_cast<juce::CriticalSection&> (modelLock));

    auto* track = findTrackLocked (trackId);

    if (track == nullptr || track->kind != TrackKind::midi)
        return false;

    hasInstrumentOut = track->hasInstrument;
    instrumentOut = track->instrument;
    return true;
}

juce::int64 TimelineEngine::addMidiClip (juce::int64 trackId, double startSeconds)
{
    juce::ScopedLock lock (modelLock);

    auto* track = findTrackLocked (trackId);

    if (track == nullptr || track->kind != TrackKind::midi)
        return -1;

    MidiClipData clip;
    clip.id = nextId++;
    clip.startSeconds = juce::jmax (0.0, startSeconds);
    clip.lengthSeconds = 4.0 * 60.0 / juce::jmax (1.0, session.tempoBpm);   // one bar
    track->midiClips.push_back (clip);

    touchModelLocked();
    return clip.id;
}

bool TimelineEngine::removeMidiClip (juce::int64 clipId)
{
    juce::ScopedLock lock (modelLock);

    for (auto& track : session.tracks)
    {
        auto& clips = track.midiClips;
        const auto oldSize = clips.size();
        clips.erase (std::remove_if (clips.begin(), clips.end(),
                                     [clipId] (const MidiClipData& c) { return c.id == clipId; }),
                     clips.end());

        if (clips.size() != oldSize)
        {
            touchModelLocked();
            return true;
        }
    }

    return false;
}

bool TimelineEngine::getMidiClipData (juce::int64 clipId, MidiClipData& clipOut,
                                      juce::int64& trackIdOut) const
{
    juce::ScopedLock lock (const_cast<juce::CriticalSection&> (modelLock));

    const TrackData* track = nullptr;
    const MidiClipData* clip = nullptr;

    if (! findMidiClipLocked (clipId, track, clip))
        return false;

    clipOut = *clip;
    trackIdOut = track->id;
    return true;
}

bool TimelineEngine::moveMidiClip (juce::int64 clipId, double startSeconds)
{
    juce::ScopedLock lock (modelLock);

    TrackData* track = nullptr;
    MidiClipData* clip = nullptr;

    if (! findMidiClipLocked (clipId, track, clip))
        return false;

    clip->startSeconds = juce::jmax (0.0, startSeconds);
    touchModelLocked();
    return true;
}

bool TimelineEngine::updateMidiClip (juce::int64 clipId, const MidiClipData& newValues)
{
    juce::ScopedLock lock (modelLock);

    TrackData* track = nullptr;
    MidiClipData* clip = nullptr;

    if (! findMidiClipLocked (clipId, track, clip))
        return false;

    // Placement only (M6.9): the note list is the clip's content and
    // never changes through a trim — only the window onto it does.
    // The minimum length is a quarter beat at the session tempo, so a
    // clip can never be trimmed out of existence by its edges.
    const double minLengthSeconds = 0.25 * 60.0 / juce::jmax (1.0, session.tempoBpm);
    clip->startSeconds  = juce::jmax (0.0, newValues.startSeconds);
    clip->lengthSeconds = juce::jmax (minLengthSeconds, newValues.lengthSeconds);
    clip->offsetBeats   = juce::jmax (0.0, newValues.offsetBeats);
    touchModelLocked();
    return true;
}

std::vector<TimelineEngine::MidiEventInfo>
TimelineEngine::getGatheredMidiEvents (juce::int64 trackId) const
{
    std::vector<MidiEventInfo> result;

    const auto snapshot = std::atomic_load (&renderSnapshot);

    if (snapshot == nullptr)
        return result;

    for (auto& track : snapshot->tracks)
    {
        if (track.trackId != trackId)
            continue;

        result.reserve (track.midiEvents.size());

        for (auto& event : track.midiEvents)
            result.push_back ({ event.startSec, event.endSec,
                                event.noteNumber, event.velocity });

        break;
    }

    return result;
}

//==============================================================================
// Video track and clips (M6). Model-only: nothing here decodes video
// (import probes the file header synchronously, which is fast) and
// nothing here touches the render path — the snapshot builder marks
// the video track inaudible and context-less (see rebuildSnapshotLocked).
//==============================================================================
TrackData* TimelineEngine::findVideoTrackLocked()
{
    for (auto& track : session.tracks)
        if (track.kind == TrackKind::video)
            return &track;

    return nullptr;
}

bool TimelineEngine::findVideoClipLocked (juce::int64 clipId, TrackData*& trackOut,
                                          VideoClipData*& clipOut)
{
    for (auto& track : session.tracks)
        for (auto& clip : track.videoClips)
            if (clip.id == clipId)
            {
                trackOut = &track;
                clipOut = &clip;
                return true;
            }

    return false;
}

bool TimelineEngine::findVideoClipLocked (juce::int64 clipId, const TrackData*& trackOut,
                                          const VideoClipData*& clipOut) const
{
    for (auto& track : session.tracks)
        for (auto& clip : track.videoClips)
            if (clip.id == clipId)
            {
                trackOut = &track;
                clipOut = &clip;
                return true;
            }

    return false;
}

void TimelineEngine::clampVideoClipLocked (VideoClipData& clip) const
{
    clip.startSeconds = juce::jmax (0.0, clip.startSeconds);
    clip.offsetSeconds = juce::jmax (0.0, clip.offsetSeconds);

    if (clip.stillImage)
    {
        // M6.5: a still's picture is constant, so its window is
        // meaningless — the offset stays 0 and the duration is FREE
        // (never clamped by the one-frame source's nominal length).
        clip.offsetSeconds = 0.0;
        clip.durationSeconds = juce::jmax (0.0, clip.durationSeconds);
    }
    else if (clip.sourceDurationSeconds > 0.0)
    {
        clip.offsetSeconds = juce::jmin (clip.offsetSeconds, clip.sourceDurationSeconds);
        clip.durationSeconds = juce::jlimit (0.0, clip.sourceDurationSeconds - clip.offsetSeconds,
                                             clip.durationSeconds);
    }
    else
    {
        clip.durationSeconds = juce::jmax (0.0, clip.durationSeconds);
    }

    // M6.3: fades live INSIDE the clip — a fade longer than the clip
    // (after a trim) shrinks to fit.
    clip.fadeInSeconds  = juce::jlimit (0.0, clip.durationSeconds, clip.fadeInSeconds);
    clip.fadeOutSeconds = juce::jlimit (0.0, clip.durationSeconds, clip.fadeOutSeconds);
}

juce::int64 TimelineEngine::ensureVideoTrack()
{
    juce::ScopedLock lock (modelLock);

    if (auto* existing = findVideoTrackLocked())
        return existing->id;

    TrackData track;
    track.id = nextId++;
    track.name = "Video";
    track.kind = TrackKind::video;
    session.tracks.push_back (std::move (track));

    const auto id = session.tracks.back().id;
    touchModelLocked();
    return id;
}

juce::int64 TimelineEngine::importVideoFile (const juce::File& file, juce::String& errorOut)
{
    // Probe BEFORE taking the model lock: opening the container and
    // reading stream info is the only file I/O, and edits must not
    // wait on it. The probe result is plain data.
    const auto info = VideoDecoder::probe (file);

    if (! info.ok)
    {
        errorOut = "Could not read a video stream from:\n" + file.getFullPathName()
                   + "\n\nThe file may be a format FFmpeg cannot decode, "
                     "or it may not contain video at all.";
        return -1;
    }

    juce::ScopedLock lock (modelLock);

    auto* track = findVideoTrackLocked();
    const bool trackWasEmpty = track == nullptr || track->videoClips.empty();

    if (track == nullptr)
    {
        TrackData newTrack;
        newTrack.id = nextId++;
        newTrack.name = "Video";
        newTrack.kind = TrackKind::video;
        session.tracks.push_back (std::move (newTrack));
        track = &session.tracks.back();
    }

    double appendAt = 0.0;

    for (auto& existing : track->videoClips)
        appendAt = juce::jmax (appendAt, existing.getEndSeconds());

    VideoClipData clip;
    clip.id = nextId++;
    clip.filePath = file.getFullPathName();
    clip.startSeconds = appendAt;
    clip.offsetSeconds = 0.0;
    // M6.5: a still image lands as a 5-second clip (its probed
    // "duration" is one frame / nothing at all); from there the
    // user stretches or trims it freely — clampVideoClipLocked
    // never holds a still to its source length.
    clip.stillImage = info.stillImage;
    clip.durationSeconds = info.stillImage ? 5.0 : info.durationSeconds;
    clip.fpsNum = info.fpsNum;
    clip.fpsDen = info.fpsDen;
    clip.width = info.width;
    clip.height = info.height;
    clip.sourceDurationSeconds = info.durationSeconds;
    track->videoClips.push_back (clip);

    // The session timecode rate follows the first video the project
    // ever sees, until the user picks a rate themselves. A still's
    // nominal rate (the image demuxer's 25 fps placeholder) is NOT
    // adopted — a picture has no frame rate.
    if (trackWasEmpty && ! session.timecodeFpsUserSet && ! info.stillImage
         && info.fpsNum > 0 && info.fpsDen > 0)
    {
        session.timecodeFpsNum = info.fpsNum;
        session.timecodeFpsDen = info.fpsDen;
    }

    touchModelLocked();
    return clip.id;
}

bool TimelineEngine::getVideoClipData (juce::int64 clipId, VideoClipData& clipOut,
                                       juce::int64& trackIdOut) const
{
    juce::ScopedLock lock (const_cast<juce::CriticalSection&> (modelLock));

    const TrackData* track = nullptr;
    const VideoClipData* clip = nullptr;

    if (! findVideoClipLocked (clipId, track, clip))
        return false;

    clipOut = *clip;
    trackIdOut = track->id;
    return true;
}

bool TimelineEngine::updateVideoClip (juce::int64 clipId, const VideoClipData& newValues)
{
    juce::ScopedLock lock (modelLock);

    TrackData* track = nullptr;
    VideoClipData* clip = nullptr;

    if (! findVideoClipLocked (clipId, track, clip))
        return false;

    // Placement only: the file identity and cached stream info are
    // part of the stored clip and never change through an edit.
    // M6.3: fades are placement too (the fade-corner drag writes them
    // here); clampVideoClipLocked keeps them inside the duration.
    clip->startSeconds = newValues.startSeconds;
    clip->offsetSeconds = newValues.offsetSeconds;
    clip->durationSeconds = newValues.durationSeconds;
    clip->fadeInSeconds = newValues.fadeInSeconds;
    clip->fadeOutSeconds = newValues.fadeOutSeconds;
    clampVideoClipLocked (*clip);
    touchModelLocked();
    return true;
}

bool TimelineEngine::removeVideoClip (juce::int64 clipId)
{
    juce::ScopedLock lock (modelLock);

    for (auto& track : session.tracks)
    {
        auto& clips = track.videoClips;
        const auto oldSize = clips.size();
        clips.erase (std::remove_if (clips.begin(), clips.end(),
                                     [clipId] (const VideoClipData& c) { return c.id == clipId; }),
                     clips.end());

        if (clips.size() != oldSize)
        {
            touchModelLocked();
            return true;
        }
    }

    return false;
}

TimelineEngine::VideoAudioExtractResult
TimelineEngine::extractVideoAudio (std::atomic<float>* progress01,
                                   std::atomic<bool>* shouldCancel)
{
    VideoAudioExtractResult result;

    struct PendingFile
    {
        juce::File file;
        std::vector<VideoClipData> clips;   // sorted by start
        juce::File wavTarget;
        bool decoded = false;
    };

    std::vector<PendingFile> pending;
    juce::StringArray existingTrackNames;

    // Phase A: snapshot the video clips grouped by distinct source
    // file (first-appearance order), plus the names of audio tracks
    // that already look like extraction products.
    {
        juce::ScopedLock lock (modelLock);

        for (auto& track : session.tracks)
            if (track.kind == TrackKind::audio)
                existingTrackNames.add (track.name);

        for (auto& track : session.tracks)
        {
            if (track.kind != TrackKind::video)
                continue;

            for (auto& clip : track.videoClips)
            {
                // M6.5: a still image has no audio stream to
                // extract — skip it silently rather than reporting
                // it as a file "without audio".
                if (clip.stillImage)
                    continue;

                const juce::File file (clip.filePath);
                auto it = std::find_if (pending.begin(), pending.end(),
                                        [&file] (const PendingFile& p)
                                        { return p.file == file; });

                if (it == pending.end())
                {
                    pending.push_back ({ file, {}, {}, false });
                    it = pending.end() - 1;
                }

                it->clips.push_back (clip);
            }
        }

        for (auto& p : pending)
            std::sort (p.clips.begin(), p.clips.end(),
                       [] (const VideoClipData& a, const VideoClipData& b)
                       { return a.startSeconds < b.startSeconds; });
    }

    const int totalFiles = (int) pending.size();

    for (int fileIndex = 0; fileIndex < totalFiles; ++fileIndex)
    {
        if (shouldCancel != nullptr && shouldCancel->load())
        {
            result.cancelled = true;
            break;
        }

        auto& p = pending[(size_t) fileIndex];
        const auto stem = p.file.getFileNameWithoutExtension();
        const auto trackName = stem + " Audio";

        if (existingTrackNames.contains (trackName))
        {
            result.existingTracks.add (trackName);
            continue;
        }

        p.wavTarget = p.file.getSiblingFile (stem + "_audio.wav");

        double durationSeconds = 0.0;
        bool noAudio = false;
        juce::String error;

        // Progress is reported per completed file (below); the files
        // this serves are music-video length, and decode is fast.
        const bool ok = VideoAudioExtraction::decodeToWav (
            p.file, p.wavTarget, getSampleRate(), durationSeconds,
            noAudio, error, nullptr, shouldCancel);

        if (progress01 != nullptr)
            progress01->store ((float) (fileIndex + 1) / (float) juce::jmax (1, totalFiles));

        if (! ok)
        {
            if (shouldCancel != nullptr && shouldCancel->load())
            {
                result.cancelled = true;
                break;
            }

            if (noAudio)
                result.filesWithoutAudio.add (p.file.getFileName());
            else
                result.fileErrors.add (p.file.getFileName() + ": " + error);

            continue;
        }

        p.decoded = true;
    }

    // Phase C: build the tracks and the aligned clips (model lock).
    bool anyCreated = false;

    {
        juce::ScopedLock lock (modelLock);
        const double rate = getSampleRate();

        for (auto& p : pending)
        {
            if (! p.decoded)
                continue;

            const auto stem = p.file.getFileNameWithoutExtension();
            const auto trackName = stem + " Audio";

            // Re-checked under the lock: another extraction (or the
            // user) may have created it while we were decoding.
            bool exists = false;

            for (auto& track : session.tracks)
                if (track.kind == TrackKind::audio && track.name == trackName)
                    exists = true;

            if (exists)
            {
                result.existingTracks.add (trackName);
                continue;
            }

            const auto sourceId = addSourceFromFileLocked (p.wavTarget);

            if (sourceId <= 0)
            {
                result.fileErrors.add (p.file.getFileName()
                                       + ": the extracted WAV could not be loaded");
                continue;
            }

            const auto* source = findSourceLocked (sourceId);
            const juce::int64 sourceLength = source != nullptr ? source->getLengthSamples() : 0;
            const auto trackId = addTrackLocked (trackName);

            for (auto& videoClip : p.clips)
            {
                ClipData clip;
                clip.sourceId = sourceId;
                clip.start  = (juce::int64) std::llround (videoClip.startSeconds * rate);
                clip.offset = (juce::int64) std::llround (videoClip.offsetSeconds * rate);
                clip.length = (juce::int64) std::llround (videoClip.durationSeconds * rate);

                if (clip.offset >= sourceLength)
                    continue;

                clip.length = juce::jmin (clip.length, sourceLength - clip.offset);

                if (clip.length > 0)
                    addClipLocked (trackId, clip);
            }

            result.createdTracks.add (trackName);
            anyCreated = true;
        }

        if (anyCreated)
            touchModelLocked();
    }

    if (progress01 != nullptr && ! result.cancelled)
        progress01->store (1.0f);

    return result;
}

juce::int64 TimelineEngine::splitVideoClip (juce::int64 clipId, double atSeconds)
{
    juce::ScopedLock lock (modelLock);

    TrackData* track = nullptr;
    VideoClipData* clip = nullptr;

    if (! findVideoClipLocked (clipId, track, clip))
        return -1;

    if (atSeconds <= clip->startSeconds || atSeconds >= clip->getEndSeconds())
        return -1;

    VideoClipData right = *clip;
    right.id = nextId++;
    right.offsetSeconds += (atSeconds - clip->startSeconds);
    right.durationSeconds = clip->getEndSeconds() - atSeconds;
    right.startSeconds = atSeconds;

    // M6.5: splitting a still yields two stills (the flag rides
    // along in the copy); a still's offset stays 0 — there is no
    // window into a one-frame source.
    if (clip->stillImage)
        right.offsetSeconds = 0.0;

    clip->durationSeconds = atSeconds - clip->startSeconds;

    // M6.3 fade rule for splits: the left part keeps the fade-in, the
    // right part keeps the fade-out, and the two NEW edges (the cut
    // itself) get no fade — the picture stays continuous across it.
    clip->fadeOutSeconds = 0.0;
    right.fadeInSeconds = 0.0;

    track->videoClips.push_back (right);
    touchModelLocked();
    return right.id;
}

//==============================================================================
// Text track and telop clips (M6.3). Model-only, exactly like the
// video track: nothing here renders or decodes anything — the
// preview feed and the mp4 exporter read the clips and composite the
// text over the picture (VideoComposite.h, namespace Telop).
//==============================================================================
TrackData* TimelineEngine::findTextTrackLocked()
{
    for (auto& track : session.tracks)
        if (track.kind == TrackKind::text)
            return &track;

    return nullptr;
}

bool TimelineEngine::findTextClipLocked (juce::int64 clipId, TrackData*& trackOut,
                                         TextClipData*& clipOut)
{
    for (auto& track : session.tracks)
        for (auto& clip : track.textClips)
            if (clip.id == clipId)
            {
                trackOut = &track;
                clipOut = &clip;
                return true;
            }

    return false;
}

bool TimelineEngine::findTextClipLocked (juce::int64 clipId, const TrackData*& trackOut,
                                         const TextClipData*& clipOut) const
{
    for (auto& track : session.tracks)
        for (auto& clip : track.textClips)
            if (clip.id == clipId)
            {
                trackOut = &track;
                clipOut = &clip;
                return true;
            }

    return false;
}

void TimelineEngine::clampTextClipLocked (TextClipData& clip) const
{
    clip.startSeconds = juce::jmax (0.0, clip.startSeconds);
    clip.durationSeconds = juce::jmax (0.05, clip.durationSeconds);
}

juce::int64 TimelineEngine::ensureTextTrack()
{
    juce::ScopedLock lock (modelLock);

    if (auto* existing = findTextTrackLocked())
        return existing->id;

    TrackData track;
    track.id = nextId++;
    track.name = "Text";
    track.kind = TrackKind::text;
    session.tracks.push_back (std::move (track));

    const auto id = session.tracks.back().id;
    touchModelLocked();
    return id;
}

bool TimelineEngine::importSrtFile (const juce::File& file, SrtImportResult& resultOut,
                                    juce::String& errorOut)
{
    resultOut = {};

    if (! file.existsAsFile())
    {
        errorOut = "The SRT file does not exist:\n" + file.getFullPathName();
        return false;
    }

    // Parse BEFORE taking the model lock (file I/O, like the video
    // import's probe): SrtParser is pure text work.
    int skipped = 0;
    auto parsed = SrtParser::parse (file.loadFileAsString(), skipped);
    resultOut.skipped = skipped;

    if (parsed.empty() && skipped == 0)
    {
        errorOut = "No subtitle entries were found in:\n" + file.getFullPathName()
                   + "\n\nThe file may be empty, or not an SRT file at all.";
        return false;
    }

    juce::ScopedLock lock (modelLock);

    auto* track = findTextTrackLocked();

    if (track == nullptr)
    {
        TrackData newTrack;
        newTrack.id = nextId++;
        newTrack.name = "Text";
        newTrack.kind = TrackKind::text;
        session.tracks.push_back (std::move (newTrack));
        track = &session.tracks.back();
    }

    // Append: existing telop clips stay where they are (on overlap,
    // the later-starting clip wins at render — see Telop).
    for (auto& clip : parsed)
    {
        clip.id = nextId++;
        clampTextClipLocked (clip);
        track->textClips.push_back (std::move (clip));
        ++resultOut.imported;
    }

    touchModelLocked();
    return true;
}

bool TimelineEngine::getTextClipData (juce::int64 clipId, TextClipData& clipOut,
                                      juce::int64& trackIdOut) const
{
    juce::ScopedLock lock (const_cast<juce::CriticalSection&> (modelLock));

    const TrackData* track = nullptr;
    const TextClipData* clip = nullptr;

    if (! findTextClipLocked (clipId, track, clip))
        return false;

    clipOut = *clip;
    trackIdOut = track->id;
    return true;
}

bool TimelineEngine::updateTextClip (juce::int64 clipId, const TextClipData& newValues)
{
    juce::ScopedLock lock (modelLock);

    TrackData* track = nullptr;
    TextClipData* clip = nullptr;

    if (! findTextClipLocked (clipId, track, clip))
        return false;

    clip->startSeconds = newValues.startSeconds;
    clip->durationSeconds = newValues.durationSeconds;
    clampTextClipLocked (*clip);
    touchModelLocked();
    return true;
}

bool TimelineEngine::setTextClipText (juce::int64 clipId, const juce::String& newText)
{
    juce::ScopedLock lock (modelLock);

    TrackData* track = nullptr;
    TextClipData* clip = nullptr;

    if (! findTextClipLocked (clipId, track, clip))
        return false;

    clip->text = newText;
    touchModelLocked();
    return true;
}

bool TimelineEngine::removeTextClip (juce::int64 clipId)
{
    juce::ScopedLock lock (modelLock);

    for (auto& track : session.tracks)
    {
        auto& clips = track.textClips;
        const auto oldSize = clips.size();
        clips.erase (std::remove_if (clips.begin(), clips.end(),
                                     [clipId] (const TextClipData& c) { return c.id == clipId; }),
                     clips.end());

        if (clips.size() != oldSize)
        {
            touchModelLocked();
            return true;
        }
    }

    return false;
}

juce::int64 TimelineEngine::splitTextClip (juce::int64 clipId, double atSeconds)
{
    juce::ScopedLock lock (modelLock);

    TrackData* track = nullptr;
    TextClipData* clip = nullptr;

    if (! findTextClipLocked (clipId, track, clip))
        return -1;

    if (atSeconds <= clip->startSeconds || atSeconds >= clip->getEndSeconds())
        return -1;

    TextClipData right = *clip;   // both halves keep the text
    right.id = nextId++;
    right.durationSeconds = clip->getEndSeconds() - atSeconds;
    right.startSeconds = atSeconds;

    clip->durationSeconds = atSeconds - clip->startSeconds;

    track->textClips.push_back (right);
    touchModelLocked();
    return right.id;
}

namespace
{
    // M6.6: the S/M/L presets as scale percents — the same values
    // Telop::scaleForSizeIndex returns as fractions (0.72/1.0/1.38).
    float telopScalePercentForSizeIndex (int sizeIndex) noexcept
    {
        switch (juce::jlimit (0, 2, sizeIndex))
        {
            case 0:  return 72.0f;
            case 2:  return 138.0f;
            default: return 100.0f;
        }
    }

    // The S/M/L index nearest a scale percent (midpoints 86 / 119).
    int telopSizeIndexForScalePercent (float percent) noexcept
    {
        if (percent <= 86.0f)  return 0;
        if (percent <= 119.0f) return 1;
        return 2;
    }
}

int TimelineEngine::getTelopSize()
{
    juce::ScopedLock lock (modelLock);
    return session.telopSize;
}

void TimelineEngine::setTelopSize (int sizeIndex)
{
    juce::ScopedLock lock (modelLock);
    session.telopSize = juce::jlimit (0, 2, sizeIndex);
    session.telopStyle.scalePercent = telopScalePercentForSizeIndex (session.telopSize);
    touchModelLocked();
}

TelopStyleData TimelineEngine::getTelopStyle()
{
    juce::ScopedLock lock (modelLock);
    return session.telopStyle;
}

void TimelineEngine::setTelopStyle (const TelopStyleData& newStyle)
{
    juce::ScopedLock lock (modelLock);

    auto style = newStyle;
    style.scalePercent = juce::jlimit (40.0f, 250.0f, style.scalePercent);
    style.vertical = (TelopVerticalPosition)
        juce::jlimit (0, 2, (int) style.vertical);
    style.align = (TelopHorizontalAlign)
        juce::jlimit (0, 2, (int) style.align);

    session.telopStyle = style;
    session.telopSize = telopSizeIndexForScalePercent (style.scalePercent);
    touchModelLocked();
}

void TimelineEngine::getTimecodeFps (int& numOut, int& denOut, bool& userSetOut)
{
    juce::ScopedLock lock (modelLock);
    numOut = session.timecodeFpsNum;
    denOut = session.timecodeFpsDen;
    userSetOut = session.timecodeFpsUserSet;
}

void TimelineEngine::setTimecodeFps (int num, int den)
{
    if (num <= 0 || den <= 0)
        return;

    juce::ScopedLock lock (modelLock);
    session.timecodeFpsNum = num;
    session.timecodeFpsDen = den;
    session.timecodeFpsUserSet = true;
    touchModelLocked();
}

namespace
{
    MidiNoteData clampedNote (const MidiNoteData& note)
    {
        MidiNoteData result = note;
        result.noteNumber  = juce::jlimit (0, 127, result.noteNumber);
        result.velocity    = juce::jlimit (1, 127, result.velocity);
        result.startBeat   = juce::jmax (0.0, result.startBeat);
        result.lengthBeats = juce::jmax (0.05, result.lengthBeats);
        return result;
    }
}

bool TimelineEngine::addMidiNote (juce::int64 trackId, juce::int64 clipId,
                                  const MidiNoteData& note)
{
    juce::ScopedLock lock (modelLock);

    auto* track = findTrackLocked (trackId);

    if (track == nullptr)
        return false;

    for (auto& clip : track->midiClips)
    {
        if (clip.id == clipId)
        {
            clip.notes.push_back (clampedNote (note));
            touchModelLocked();
            return true;
        }
    }

    return false;
}

bool TimelineEngine::setMidiClipNotes (juce::int64 trackId, juce::int64 clipId,
                                       const std::vector<MidiNoteData>& notes)
{
    juce::ScopedLock lock (modelLock);

    auto* track = findTrackLocked (trackId);

    if (track == nullptr)
        return false;

    for (auto& clip : track->midiClips)
    {
        if (clip.id == clipId)
        {
            clip.notes.clear();
            clip.notes.reserve (notes.size());

            for (auto& note : notes)
                clip.notes.push_back (clampedNote (note));

            std::sort (clip.notes.begin(), clip.notes.end(),
                       [] (const MidiNoteData& a, const MidiNoteData& b)
                       {
                           return a.startBeat < b.startBeat;
                       });

            touchModelLocked();
            return true;
        }
    }

    return false;
}

bool TimelineEngine::getMidiClipNotes (juce::int64 trackId, juce::int64 clipId,
                                       std::vector<MidiNoteData>& notesOut) const
{
    juce::ScopedLock lock (const_cast<juce::CriticalSection&> (modelLock));

    auto* track = findTrackLocked (trackId);

    if (track == nullptr)
        return false;

    for (auto& clip : track->midiClips)
    {
        if (clip.id == clipId)
        {
            notesOut = clip.notes;
            return true;
        }
    }

    return false;
}

//==============================================================================
// Live MIDI input -> FX chains (M3.3)
//==============================================================================
void TimelineEngine::openMidiInputs()
{
    // Message thread (initialiseDevices). Re-opening is harmless: any
    // previously opened devices are closed first.
    closeMidiInputs();

    // The collector needs its rate before the first callback can stamp
    // messages; prepareToPlay refreshes it whenever the device starts.
    midiCollector.reset (engineRate.load());

    for (auto& deviceInfo : juce::MidiInput::getAvailableDevices())
    {
        auto input = juce::MidiInput::openDevice (deviceInfo.identifier,
                                                  &midiCollector);

        if (input == nullptr)
            continue;   // device busy/gone — the rest still open

        input->start();
        midiInputNames.add (deviceInfo.name);
        midiInputs.push_back (std::move (input));
    }
}

void TimelineEngine::closeMidiInputs()
{
    for (auto& input : midiInputs)
        if (input != nullptr)
            input->stop();

    midiInputs.clear();
    midiInputNames.clear();
}

void TimelineEngine::addLiveMidiMessage (const juce::MidiMessage& message)
{
    // Exactly what a hardware callback delivers to the collector: the
    // message stamped with the current time in seconds (the collector
    // converts that into its internal sample timeline).
    auto stamped = message;
    stamped.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    midiCollector.addMessageToQueue (stamped);
}

int TimelineEngine::drainLiveMidi (juce::MidiBuffer& destBuffer, int numSamples)
{
    destBuffer.clear();

    if (numSamples > 0)
        midiCollector.removeNextBlockOfMessages (destBuffer, numSamples);

    return destBuffer.getNumEvents();
}

void TimelineEngine::renderBlockWithLiveMidi (juce::AudioBuffer<float>& output,
                                              juce::int64 blockPosition,
                                              int numSamples,
                                              const juce::MidiBuffer& liveMidi)
{
    // Test seam (see the header): the same renderBlock the audio
    // callback uses, with the caller's buffer standing in for the
    // collector drain. Deliberately NOT flagged as offline rendering,
    // so meter taps behave exactly as in live playback.
    const auto snapshot = std::atomic_load (&renderSnapshot);

    if (snapshot == nullptr || numSamples <= 0)
        return;

    renderBlock (*snapshot, output, 0, blockPosition, numSamples, &liveMidi);
}

//==============================================================================
// MIDI keyboard recording (M4)
//
// Capture path: device MIDI -> MidiMessageCollector (addLiveMidiMessage
// / injectMidiMessage) -> the audio callback drains one block into
// liveMidiDrainBuffer -> while recording, captureLiveMidi copies the
// note on/off events into the AbstractFifo queue, stamped with the
// ABSOLUTE session position (block position + the event's offset in the
// drained block — i.e. collector timestamp timing, never message-thread
// polling time) -> the 50 ms timer pump (or Stop) folds the queue into
// the take clips under the model lock.
//==============================================================================
void TimelineEngine::injectMidiMessage (const juce::MidiMessage& message)
{
    addLiveMidiMessage (message);
}

void TimelineEngine::setTrackRecordArmed (juce::int64 trackId, bool armed)
{
    juce::ScopedLock lock (modelLock);

    auto* track = findTrackLocked (trackId);

    if (track == nullptr || track->kind != TrackKind::midi)
        return;

    if (track->recordArmed == armed)
        return;

    track->recordArmed = armed;

    if (! armed)
    {
        // Drain the capture queue first: events captured since the
        // last timer pump belong to this take too (a fast disarm must
        // not lose the note the player is holding right now).
        pumpRecordQueueLocked();

        // Disarming mid-record finalises this track's take on the
        // spot: held notes close at the current position.
        for (auto it = takeBuilders.begin(); it != takeBuilders.end(); ++it)
        {
            if (it->trackId == trackId)
            {
                finaliseTakeLocked (*it, playPosition.load());
                takeBuilders.erase (it);
                break;
            }
        }

        // Silence a note the player may still be holding through the
        // disarm (the panic also reaches live-played notes now — see
        // getNextAudioBlock).
        midiPanicPending.store (true);

        // That was the last take: recording is over.
        if (takeBuilders.empty() && recordingActive.load())
        {
            recordingActive.store (false);
            restoreTimerAfterRecordingLocked();
        }
    }

    touchModelLocked();        // the snapshot picks up the arm flag
    syncRecordingLocked();     // arming mid-playback starts a take now
}

bool TimelineEngine::isTrackRecordArmed (juce::int64 trackId)
{
    juce::ScopedLock lock (modelLock);

    auto* track = findTrackLocked (trackId);
    return track != nullptr && track->kind == TrackKind::midi
            && track->recordArmed;
}

bool TimelineEngine::hasArmedMidiTracks()
{
    juce::ScopedLock lock (modelLock);
    return hasArmedMidiTrackLocked();
}

bool TimelineEngine::hasArmedMidiTrackLocked() const
{
    for (auto& track : session.tracks)
        if (track.kind == TrackKind::midi && track.recordArmed)
            return true;

    return false;
}

void TimelineEngine::syncRecordingLocked()
{
    if (! playing.load() || ! hasArmedMidiTrackLocked())
        return;

    if (! recordingActive.load())
        beginRecordingLocked();

    // Tracks armed after recording started join with a take that
    // begins at the current position.
    for (auto& track : session.tracks)
    {
        if (track.kind != TrackKind::midi || ! track.recordArmed)
            continue;

        bool hasBuilder = false;

        for (auto& builder : takeBuilders)
            if (builder.trackId == track.id)
                hasBuilder = true;

        if (! hasBuilder)
        {
            TakeBuilder builder;
            builder.trackId = track.id;
            builder.clipStartSamples = playPosition.load();
            takeBuilders.push_back (builder);
        }
    }
}

void TimelineEngine::beginRecordingLocked()
{
    recordFifo.reset();   // drop stragglers from any earlier take
    takeBuilders.clear();

    const auto start = playPosition.load();

    for (auto& track : session.tracks)
    {
        if (track.kind != TrackKind::midi || ! track.recordArmed)
            continue;

        TakeBuilder builder;
        builder.trackId = track.id;
        builder.clipStartSamples = start;
        takeBuilders.push_back (builder);
    }

    // Open-ended when nothing lies ahead of the record-start position
    // (an empty session, or recording from the very end): the take
    // itself creates the session length, so the transport must not
    // stop at the (stale) session end.
    recordOpenEnded.store (cachedTotalLength.load() <= start);
    recordingActive.store (true);
    startTimer (50);   // capture pump cadence; retirement is time-gated
}

void TimelineEngine::captureLiveMidi (const juce::MidiBuffer& liveMidi,
                                      juce::int64 blockPosition,
                                      int numSamples) noexcept
{
    // Audio thread. Note events only — the clip model stores notes,
    // so CCs are ignored for recording in v1 (they still reach FX
    // chains live via M3.3). Offsets outside the rendered part of the
    // block are not captured: their absolute time belongs to a later
    // block, which will drain and capture them then.
    for (const auto metadata : liveMidi)
    {
        if (metadata.samplePosition < 0 || metadata.samplePosition >= numSamples)
            continue;

        const auto message = metadata.getMessage();

        CapturedMidiEvent event;
        event.samplePos = blockPosition + metadata.samplePosition;

        if (message.isNoteOn())
        {
            event.isNoteOn = true;
            event.velocity = message.getVelocity();
        }
        else if (message.isNoteOff())
        {
            event.isNoteOn = false;
        }
        else
        {
            continue;
        }

        event.noteNumber = message.getNoteNumber();

        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        recordFifo.prepareToWrite (1, start1, size1, start2, size2);

        if (size1 > 0)
        {
            recordEventQueue[(size_t) start1] = event;
            recordFifo.finishedWrite (1);
        }
        else if (size2 > 0)
        {
            recordEventQueue[(size_t) start2] = event;
            recordFifo.finishedWrite (1);
        }
        // else: the queue is full — the message thread has stalled
        // for 4096 events; dropping beats blocking the audio thread.
    }
}

MidiClipData* TimelineEngine::findTakeClipLocked (TakeBuilder& builder)
{
    auto* track = findTrackLocked (builder.trackId);

    if (track == nullptr)
        return nullptr;

    for (auto& clip : track->midiClips)
        if (clip.id == builder.clipId)
            return &clip;

    return nullptr;
}

void TimelineEngine::appendRecordedNoteLocked (TakeBuilder& builder,
                                               MidiClipData& clip,
                                               int noteNumber,
                                               const HeldNote& held,
                                               juce::int64 endSamples)
{
    const double rate = engineRate.load();

    if (rate <= 0.0)
        return;

    // Seconds -> beats at the session tempo AS IT STANDS WHEN THE
    // NOTE IS CONSUMED (the piano roll shares this rule: set the
    // song's BPM before recording, exactly as before entering notes).
    const double secondsPerBeat = 60.0 / juce::jmax (1.0, session.tempoBpm);

    MidiNoteData note;
    note.noteNumber = noteNumber;
    note.velocity = held.velocity;
    note.startBeat = ((double) (held.startSamples - builder.clipStartSamples) / rate)
                     / secondsPerBeat;
    note.lengthBeats = ((double) juce::jmax<juce::int64> (0, endSamples - held.startSamples)
                        / rate) / secondsPerBeat;
    clip.notes.push_back (clampedNote (note));

    builder.lastNoteEndSeconds = juce::jmax (builder.lastNoteEndSeconds,
                                             (double) endSamples / rate);
}

void TimelineEngine::pumpRecordQueueLocked()
{
    const double rate = engineRate.load();

    if (rate <= 0.0)
        return;

    bool changed = false;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    recordFifo.prepareToRead (recordQueueCapacity,
                              start1, size1, start2, size2);

    auto applyEvent = [this, &changed] (TakeBuilder& builder,
                                        const CapturedMidiEvent& event)
    {
        auto* track = findTrackLocked (builder.trackId);

        if (track == nullptr)
            return;

        if (event.isNoteOn)
        {
            // Retriggering a pitch that is still held closes the
            // sounding instance first — a stored note list cannot
            // overlap itself per pitch.
            auto heldIt = builder.heldNotes.find (event.noteNumber);

            if (heldIt != builder.heldNotes.end())
            {
                if (auto* clip = findTakeClipLocked (builder))
                {
                    appendRecordedNoteLocked (builder, *clip, event.noteNumber,
                                              heldIt->second, event.samplePos);
                    changed = true;
                }

                builder.heldNotes.erase (heldIt);
            }

            // The take clip is born at the first note, positioned at
            // the record-start position. Overlapping an existing clip
            // on the track is allowed (no merging in v1).
            if (builder.clipId < 0)
            {
                MidiClipData clip;
                clip.id = nextId++;
                clip.startSeconds = (double) builder.clipStartSamples
                                    / engineRate.load();
                clip.lengthSeconds = 0.0;
                track->midiClips.push_back (clip);
                builder.clipId = clip.id;
                changed = true;
            }

            builder.heldNotes[event.noteNumber] = { event.samplePos,
                                                    event.velocity };
        }
        else
        {
            auto heldIt = builder.heldNotes.find (event.noteNumber);

            if (heldIt == builder.heldNotes.end())
                return;   // note-off for a note we never saw (or a
                          // retrigger already closed it)

            if (auto* clip = findTakeClipLocked (builder))
            {
                appendRecordedNoteLocked (builder, *clip, event.noteNumber,
                                          heldIt->second, event.samplePos);
                changed = true;
            }

            builder.heldNotes.erase (heldIt);
        }
    };

    auto consumeSpan = [this, &applyEvent] (int start, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            const auto& event = recordEventQueue[(size_t) (start + i)];

            // Every armed track's take receives the performance.
            for (auto& builder : takeBuilders)
                applyEvent (builder, event);
        }
    };

    consumeSpan (start1, size1);
    consumeSpan (start2, size2);
    recordFifo.finishedRead (size1 + size2);

    // Growth: each take clip reaches at least to the playhead (and
    // never shrinks below its last note end), so the user watches the
    // clip grow while recording.
    const double playheadSeconds = (double) playPosition.load() / rate;

    for (auto& builder : takeBuilders)
    {
        if (builder.clipId < 0)
            continue;

        if (auto* clip = findTakeClipLocked (builder))
        {
            const double wanted = juce::jmax (builder.lastNoteEndSeconds,
                                              playheadSeconds)
                                  - clip->startSeconds;

            if (wanted > clip->lengthSeconds)
            {
                clip->lengthSeconds = wanted;
                changed = true;
            }
        }
    }

    if (changed)
        touchModelLocked();
}

bool TimelineEngine::finaliseTakeLocked (TakeBuilder& builder,
                                         juce::int64 stopSamples)
{
    if (builder.clipId < 0)
        return false;   // no note ever arrived: no clip exists

    auto* clip = findTakeClipLocked (builder);

    if (clip == nullptr)
        return false;

    const double rate = engineRate.load();

    if (rate <= 0.0)
        return false;

    // Notes still held close at the stop position.
    for (auto& held : builder.heldNotes)
        appendRecordedNoteLocked (builder, *clip, held.first,
                                  held.second, stopSamples);

    builder.heldNotes.clear();

    // The piano roll keeps a clip's notes start-sorted.
    std::sort (clip->notes.begin(), clip->notes.end(),
               [] (const MidiNoteData& a, const MidiNoteData& b)
               {
                   return a.startBeat < b.startBeat;
               });

    const double clipStartSeconds = (double) builder.clipStartSamples / rate;
    const double endSeconds = juce::jmax ((double) stopSamples / rate,
                                          builder.lastNoteEndSeconds);
    clip->lengthSeconds = juce::jmax (0.0, endSeconds - clipStartSeconds);
    return true;
}

void TimelineEngine::finishRecordingLocked()
{
    if (! recordingActive.load())
        return;

    pumpRecordQueueLocked();   // drain everything captured so far

    const auto stopSamples = playPosition.load();
    bool changed = false;

    for (auto& builder : takeBuilders)
        changed = finaliseTakeLocked (builder, stopSamples) || changed;

    takeBuilders.clear();
    recordingActive.store (false);
    recordFifo.reset();
    restoreTimerAfterRecordingLocked();

    if (changed)
        touchModelLocked();
}

void TimelineEngine::cancelRecordingLocked()
{
    takeBuilders.clear();
    recordFifo.reset();

    if (recordingActive.load())
    {
        recordingActive.store (false);
        restoreTimerAfterRecordingLocked();
    }
}

void TimelineEngine::restoreTimerAfterRecordingLocked()
{
    if (! retiredFx.empty() || ! retiredFxPrevious.empty())
        startTimer (3000);
    else
        stopTimer();
}

//==============================================================================
// Chunked offline mixdown (M6.2) — the export span rendered in pieces
// for the mp4 exporter. The WAV export above keeps its own inline
// version of the same reset/render sequence; both go through
// renderBlock() so the sound is identical by construction.
//==============================================================================
juce::int64 TimelineEngine::beginOfflineMixdown (juce::String& errorOut)
{
    // The caller holds the model lock for the whole begin..end span.
    offlineSnapshot = std::atomic_load (&renderSnapshot);

    if (offlineSnapshot == nullptr || offlineSnapshot->totalLength <= 0)
    {
        offlineSnapshot = nullptr;
        errorOut = "Nothing to export - the session is empty.";
        return -1;
    }

    for (auto& pair : fxInstances)
        pair.second->reset();

    for (auto& pair : renderContexts)
    {
        for (auto& line : pair.second->delayLine)
            std::fill (line.begin(), line.end(), 0.0f);

        pair.second->delayWritePos = 0;
    }

    offlineRendering.store (true);
    return offlineSnapshot->totalLength + secondsToSamples (2.0);
}

void TimelineEngine::renderOfflineMixdownBlock (juce::AudioBuffer<float>& buffer,
                                                juce::int64 position, int numSamples)
{
    if (offlineSnapshot == nullptr || numSamples <= 0)
        return;

    buffer.clear();
    renderBlock (*offlineSnapshot, buffer, 0, position, numSamples, nullptr);

    const float gain = masterGain.load();

    if (gain != 1.0f)
        buffer.applyGain (gain);
}

void TimelineEngine::endOfflineMixdown()
{
    offlineRendering.store (false);

    for (auto& pair : fxInstances)
        pair.second->reset();

    offlineSnapshot = nullptr;
}

//==============================================================================
// WAV mixdown export (M3)
//==============================================================================
bool TimelineEngine::exportMixdownWav (const juce::File& target, juce::String& errorOut,
                                       std::atomic<float>* progress01)
{
    // The model lock is held for the whole pass: the model, the snapshot
    // and the live instance set stay frozen. The UI runs this from a
    // progress thread behind a modal progress window, so no edit can be
    // attempted meanwhile; the transport is stopped by the caller, and
    // offlineRendering keeps the audio callback out of the instances and
    // track contexts entirely.
    juce::ScopedLock lock (modelLock);

    auto snapshot = std::atomic_load (&renderSnapshot);

    if (snapshot == nullptr || snapshot->totalLength <= 0)
    {
        errorOut = "Nothing to export - the session is empty.";
        return false;
    }

    const double rate = engineRate.load();
    const juce::int64 totalToRender = snapshot->totalLength + secondsToSamples (2.0);

    // Start from a clean slate: plugin state reset and PDC delay lines
    // emptied, so the export does not inherit tails from live playback.
    for (auto& pair : fxInstances)
        pair.second->reset();

    for (auto& pair : renderContexts)
    {
        for (auto& line : pair.second->delayLine)
            std::fill (line.begin(), line.end(), 0.0f);

        pair.second->delayWritePos = 0;
    }

    target.deleteFile();

    std::unique_ptr<juce::OutputStream> stream = target.createOutputStream();

    if (stream == nullptr)
    {
        errorOut = "Could not create the file:\n" + target.getFullPathName();
        return false;
    }

    juce::WavAudioFormat wavFormat;
    auto writer = wavFormat.createWriterFor (
        stream, juce::AudioFormatWriterOptions()
                    .withSampleRate (rate)
                    .withNumChannels (2)
                    .withBitsPerSample (16));

    if (writer == nullptr)
    {
        errorOut = "Could not create the WAV writer for:\n" + target.getFullPathName();
        return false;
    }

    offlineRendering.store (true);

    juce::AudioBuffer<float> blockBuffer (2, 512);
    juce::int64 position = 0;
    bool writeOk = true;

    while (position < totalToRender)
    {
        const int chunk = (int) juce::jmin<juce::int64> (512, totalToRender - position);

        blockBuffer.clear();
        renderBlock (*snapshot, blockBuffer, 0, position, chunk, nullptr);   // offline: no live MIDI (M3.3)

        // renderBlock applies track gains; the master gain is applied
        // here, exactly as getNextAudioBlock does for live playback.
        const float gain = masterGain.load();

        if (gain != 1.0f)
            blockBuffer.applyGain (gain);

        if (! writer->writeFromAudioSampleBuffer (blockBuffer, 0, chunk))
        {
            writeOk = false;
            break;
        }

        position += chunk;

        if (progress01 != nullptr)
            progress01->store ((float) ((double) position / (double) totalToRender));
    }

    offlineRendering.store (false);
    writer.reset();   // flush + close before reporting success

    // Leave the instances in a clean state for the next live playback.
    for (auto& pair : fxInstances)
        pair.second->reset();

    if (! writeOk)
    {
        errorOut = "Writing the WAV file failed:\n" + target.getFullPathName();
        return false;
    }

    if (progress01 != nullptr)
        progress01->store (1.0f);

    return true;
}

//==============================================================================
// Audio rendering
//==============================================================================
void TimelineEngine::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    if (samplesPerBlockExpected > 0)
        maxBlockSize.store (juce::jmax (maxBlockSize.load(), samplesPerBlockExpected));

    // M3.3: the live MIDI collector runs on the audio device's rate;
    // refresh it whenever a device (re)starts so event timestamps map
    // onto blocks correctly. Internally locked — safe from here.
    if (sampleRate > 0.0)
        midiCollector.reset (sampleRate);

    if (sampleRate > 0.0 && std::abs (sampleRate - engineRate) > 0.01)
    {
        // Device (re)started at a different rate: resample every source
        // and rescale the session's sample positions, re-prepare every
        // plugin instance at the new rate, then publish a fresh snapshot.
        // This happens on the device thread before callbacks resume, so a
        // blocking lock is OK here (never in the callback).
        juce::ScopedLock lock (modelLock);
        rebuildAllSourcesLocked (engineRate, sampleRate);

        for (auto& pair : fxInstances)
        {
            pair.second->releaseResources();
            prepareFxInstanceLocked (*pair.second);
        }

        rebuildSnapshotLocked();
        modelVersion.fetch_add (1);
        sendChangeMessage();
    }
}

void TimelineEngine::renderClipsInto (const RenderTrack& track,
                                      juce::AudioBuffer<float>& dest,
                                      juce::int64 blockPosition, int numSamples)
{
    // Audio thread, snapshot-only: raw pointers, no locks, no allocation,
    // no per-sample division. Fade gains are stepped incrementally.
    // Mixes into channels 0/1 of dest (already cleared by the caller),
    // applying clip gains only — the track gain is applied later, when
    // the processed track is summed into the output.
    const int numDestChannels = juce::jmin (2, dest.getNumChannels());
    const auto blockEnd = blockPosition + numSamples;

    float* outPointers[2] = { nullptr, nullptr };

    for (int ch = 0; ch < numDestChannels; ++ch)
        outPointers[ch] = dest.getWritePointer (ch);

    for (auto& clip : track.clips)
    {
        const auto clipEnd = clip.start + clip.length;

        if (clipEnd <= blockPosition || clip.start >= blockEnd)
            continue;

        if (clip.gain <= 0.0f)
            continue;

        const auto& buffer = *clip.buffer;
        const int numSourceChannels = buffer.getNumChannels();

        if (numSourceChannels <= 0)
            continue;

        const float* srcPointers[2] = { buffer.getReadPointer (0),
                                        buffer.getReadPointer (
                                            juce::jmin (1, numSourceChannels - 1)) };

        const auto renderStart = juce::jmax (clip.start, blockPosition);
        const auto renderEnd = juce::jmin (clipEnd, blockEnd);
        const int destOffset = (int) (renderStart - blockPosition);
        const int count = (int) (renderEnd - renderStart);
        const int sourceStart = (int) (clip.offset + (renderStart - clip.start));
        const juce::int64 relStart = renderStart - clip.start;   // pos inside clip

        // Fade multipliers for the first rendered sample, plus their
        // per-sample steps. Outside the fade regions the multiplier is
        // a constant 1 with a zero step, so the steady state costs one
        // multiply per sample and nothing else.
        float fadeInGain = 1.0f, fadeInStep = 0.0f;

        if (clip.fadeIn > 0 && relStart < clip.fadeIn)
        {
            fadeInGain = (float) relStart / (float) clip.fadeIn;
            fadeInStep = 1.0f / (float) clip.fadeIn;
        }

        float fadeOutGain = 1.0f, fadeOutStep = 0.0f;
        juce::int64 fadeOutDelay = 0;   // samples until the fade-out starts

        if (clip.fadeOut > 0)
        {
            const auto remaining = clip.length - relStart;

            if (remaining <= clip.fadeOut)
            {
                fadeOutGain = (float) remaining / (float) clip.fadeOut;
                fadeOutStep = -1.0f / (float) clip.fadeOut;
            }
            else
            {
                fadeOutDelay = remaining - clip.fadeOut;
            }
        }

        for (int i = 0; i < count; ++i)
        {
            const float gain = clip.gain * fadeInGain * fadeOutGain;
            const int sourceIndex = sourceStart + i;
            const int destIndex = destOffset + i;

            if (gain != 0.0f)
                for (int ch = 0; ch < numDestChannels; ++ch)
                    outPointers[ch][destIndex] += srcPointers[ch][sourceIndex] * gain;

            // Advance the fade multipliers for the next sample.
            if (fadeInStep != 0.0f)
            {
                fadeInGain += fadeInStep;

                if (fadeInGain >= 1.0f)
                {
                    fadeInGain = 1.0f;
                    fadeInStep = 0.0f;
                }
            }

            if (fadeOutDelay > 0)
            {
                if (--fadeOutDelay == 0)
                    fadeOutStep = -1.0f / (float) clip.fadeOut;
            }
            else if (fadeOutStep != 0.0f)
            {
                fadeOutGain += fadeOutStep;

                if (fadeOutGain < 0.0f)
                    fadeOutGain = 0.0f;
            }
        }
    }
}

void TimelineEngine::renderBlock (const RenderSnapshot& snapshot,
                                  juce::AudioBuffer<float>& output, int startSample,
                                  juce::int64 blockPosition, int numSamples,
                                  const juce::MidiBuffer* liveMidi,
                                  bool monitorOnly)
{
    // Audio thread. Per track: clips -> scratch buffer -> VST3 chain ->
    // PDC delay -> track gain -> sum into the output. Everything used
    // here (scratch buffers, delay lines) was allocated when the
    // snapshot was built; this function allocates nothing, locks
    // nothing and does no I/O. Every audible track is processed for the
    // whole block even when it has no sounding clip, so plugin tails
    // ring out and the PDC delay lines stay sample-aligned.
    const int numOutputChannels = juce::jmin (2, output.getNumChannels());

    if (numOutputChannels <= 0)
        return;

    // M3.2: meter taps are live-playback only. The offline mixdown calls
    // this same function with offlineRendering set and must not move the
    // meters (its render position has nothing to do with what is heard).
    const bool metering = ! offlineRendering.load (std::memory_order_relaxed);

    for (size_t trackIndex = 0; trackIndex < snapshot.tracks.size(); ++trackIndex)
    {
        auto& track = snapshot.tracks[trackIndex];

        if (! track.audible || track.context == nullptr)
            continue;

        // M4: the stopped-transport monitor pass renders ONLY armed
        // MIDI tracks (their instruments fed from live input below);
        // everything else stays silent while parked.
        if (monitorOnly && ! (track.kind == TrackKind::midi && track.recordArmed))
            continue;

        auto& context = *track.context;
        auto& scratch = context.scratch;
        const int scratchCapacity = scratch.getNumSamples();

        if (scratchCapacity <= 0)
            continue;

        // M3.2: this track's peak contribution for the whole block,
        // accumulated chunk by chunk below (post-FX, post-PDC, post-gain
        // — i.e. exactly what the track adds to the mix; non-audible
        // tracks never reach here, so muted tracks meter as silence).
        float trackPeakL = 0.0f, trackPeakR = 0.0f;

        int done = 0;

        while (done < numSamples)
        {
            const int chunk = juce::jmin (scratchCapacity, numSamples - done);

            for (int ch = 0; ch < scratch.getNumChannels(); ++ch)
                scratch.clear (ch, 0, chunk);

            // M3.3: this chunk's clip-generated MIDI for a MIDI track.
            // Built whether or not an instrument is hosted, because the
            // track's FX chain receives the same events (below) — e.g.
            // CC data in an imported file driving a later effect.
            auto& clipMidi = context.midiBuffer;
            clipMidi.clear();

            // M3.3: the FX chain's buffer for this chunk starts as a
            // copy of the clip events, taken BEFORE the instrument runs
            // — an instrument is free to consume/clear its input
            // buffer, and the FX chain must see the events regardless.
            auto& fxMidi = context.fxMidiBuffer;
            fxMidi.clear();

            if (track.kind == TrackKind::midi)
            {
                // M4: the monitor pass has no clip events — live input
                // (merged below) is an armed track's only source.
                if (! monitorOnly)
                {
                    const double rate = engineRate.load();
                    const double t0 = (double) (blockPosition + done) / rate;
                    const double t1 = (double) (blockPosition + done + chunk) / rate;

                    // Notes starting or ending inside [t0, t1) are placed at
                    // their sample offset; a note that merely spans the
                    // chunk produces no event (it was switched on by an
                    // earlier chunk).
                    for (auto& event : track.midiEvents)
                    {
                        if (event.startSec >= t0 && event.startSec < t1)
                        {
                            const int offset = juce::jlimit (
                                0, chunk - 1,
                                (int) std::llround ((event.startSec - t0) * rate));
                            clipMidi.addEvent (juce::MidiMessage::noteOn (
                                                   1, event.noteNumber,
                                                   (juce::uint8) event.velocity),
                                               offset);
                        }

                        if (event.endSec >= t0 && event.endSec < t1)
                        {
                            const int offset = juce::jlimit (
                                0, chunk - 1,
                                (int) std::llround ((event.endSec - t0) * rate));
                            clipMidi.addEvent (juce::MidiMessage::noteOff (
                                                   1, event.noteNumber),
                                               offset);
                        }
                    }
                }

                fxMidi.addEvents (clipMidi, 0, -1, 0);

                // M4: an armed track's instrument also receives this
                // chunk's live input — monitoring while playing, and
                // the ONLY input in the stopped monitor pass. The FX
                // copy above was taken first, so the M3.3 rule stands:
                // the FX chain hears live input only via fxMidiIn.
                if (track.recordArmed && liveMidi != nullptr)
                {
                    const int eventsBefore = clipMidi.getNumEvents();
                    clipMidi.addEvents (*liveMidi, done, chunk, -done);
                    const int added = clipMidi.getNumEvents() - eventsBefore;

                    if (added > 0)
                        liveMidiEventsToInstruments.fetch_add (
                            added, std::memory_order_relaxed);
                }

                // M3: feed the events to the instrument. No instrument =
                // silence; the FX chain below still runs, exactly like
                // an empty audio track.
                if (track.instrument != nullptr)
                {
                    juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(),
                                                   scratch.getNumChannels(), 0, chunk);
                    track.instrument->processBlock (view, clipMidi);
                }
            }
            else
            {
                renderClipsInto (track, scratch, blockPosition + done, chunk);
            }

            if (! track.fxProcessors.empty())
            {
                // M3.3: when the track opted in, add the slice of the
                // block's live input that falls inside this chunk
                // (offsets rebased to the chunk start) to the clip
                // events assembled above.
                if (track.fxMidiIn && liveMidi != nullptr)
                    fxMidi.addEvents (*liveMidi, done, chunk, -done);

                juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(),
                                               scratch.getNumChannels(), 0, chunk);

                for (auto* processor : track.fxProcessors)
                {
                    // M3.3: every slot is handed its OWN copy of the
                    // chunk's events. Hosting wrappers consume the
                    // buffer they receive (the JUCE VST3 wrapper
                    // replaces it with the plugin's own output events),
                    // so sharing one buffer would starve every slot
                    // after the first — and the delivery counter below
                    // must sample the events as they are handed over,
                    // not whatever the plugin leaves behind.
                    juce::MidiBuffer slotMidi;
                    slotMidi.addEvents (fxMidi, 0, -1, 0);

                    if (! slotMidi.isEmpty())
                        fxMidiEventsDelivered.fetch_add (
                            slotMidi.getNumEvents(), std::memory_order_relaxed);

                    processor->processBlock (view, slotMidi);
                }
            }

            // PDC: delay this track so it lines up with the slowest one.
            // The ring capacity is always delay + 1 (see snapshot build),
            // so reading (writePos - delay) can never hit the slot that
            // is about to be overwritten.
            const int delay = track.pdcDelaySamples;

            if (delay > 0 && context.delayCapacity > delay)
            {
                float* channelData[2] = { scratch.getWritePointer (0),
                                          scratch.getWritePointer (1) };
                const int capacity = context.delayCapacity;
                int writePos = context.delayWritePos;

                for (int i = 0; i < chunk; ++i)
                {
                    int readPos = writePos - delay;

                    if (readPos < 0)
                        readPos += capacity;

                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const float input = channelData[ch][i];
                        channelData[ch][i] = context.delayLine[ch][(size_t) readPos];
                        context.delayLine[ch][(size_t) writePos] = input;
                    }

                    if (++writePos >= capacity)
                        writePos = 0;
                }

                context.delayWritePos = writePos;
            }

            if (track.gain > 0.0f)
            {
                for (int ch = 0; ch < numOutputChannels; ++ch)
                    output.addFrom (ch, startSample + done,
                                    scratch, ch, 0, chunk, track.gain);

                // M3.2: the scratch now holds the final post-PDC signal;
                // scaling its peak by the track gain gives the peak of
                // the contribution just summed into the output.
                if (metering)
                {
                    trackPeakL = juce::jmax (trackPeakL,
                                             scratch.getMagnitude (0, 0, chunk) * track.gain);

                    if (scratch.getNumChannels() > 1)
                        trackPeakR = juce::jmax (trackPeakR,
                                                 scratch.getMagnitude (1, 0, chunk) * track.gain);
                    else
                        trackPeakR = trackPeakL;
                }
            }

            done += chunk;
        }

        if (metering && trackIndex < (size_t) maxMeterTracks)
        {
            storeMaxPeak (trackPeakLeft[trackIndex], trackPeakL);
            storeMaxPeak (trackPeakRight[trackIndex], trackPeakR);
        }
    }
}

void TimelineEngine::processMidiPanic (const RenderSnapshot& snapshot)
{
    // Audio thread, transport stopped: one silent block carrying the
    // snapshot's all-notes-off buffer through every MIDI instrument, so
    // notes left sounding by a stop/seek/end are released. The output is
    // discarded (rendered into each track's own scratch). Everything
    // used was allocated at snapshot-build time.
    for (auto& track : snapshot.tracks)
    {
        if (track.kind != TrackKind::midi || track.instrument == nullptr
             || track.context == nullptr)
            continue;

        auto& scratch = track.context->scratch;
        const int chunk = juce::jmin (512, scratch.getNumSamples());

        if (chunk <= 0)
            continue;

        for (int ch = 0; ch < scratch.getNumChannels(); ++ch)
            scratch.clear (ch, 0, chunk);

        juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(),
                                       scratch.getNumChannels(), 0, chunk);
        auto panic = snapshot.midiPanicBuffer;   // cheap copy of a 2-event buffer
        track.instrument->processBlock (view, panic);
    }
}

void TimelineEngine::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Audio thread. Lock-free by design (see class notes): load the current
    // immutable snapshot atomically and render only from it. The shared_ptr
    // copy is a single atomic refcount bump, and it keeps the snapshot (and
    // its source buffers) alive for exactly this callback.
    auto* output = bufferToFill.buffer;
    output->clear (bufferToFill.startSample, bufferToFill.numSamples);

    // M3: while a mixdown export renders offline it owns the plugin
    // instances and track contexts; stay completely out of its way.
    if (offlineRendering.load())
        return;

    // M3.3: drain this block's live MIDI input. Done every callback —
    // playing or not — so the collector never accumulates a backlog
    // that would burst into the FX chains when playback starts. While
    // stopped the drained events feed the M4 monitor pass below.
    liveMidiDrainBuffer.clear();
    drainLiveMidi (liveMidiDrainBuffer, bufferToFill.numSamples);

    const auto snapshot = std::atomic_load (&renderSnapshot);

    if (snapshot == nullptr)
        return;

    // Master gain + master peak tap, shared by the playing path and
    // the M4 monitor pass: exactly the region this callback fills.
    auto applyMasterGainAndMeter = [this, output, &bufferToFill]()
    {
        const float gain = masterGain.load();

        if (gain != 1.0f)
            for (int ch = 0; ch < output->getNumChannels(); ++ch)
                output->applyGain (ch, bufferToFill.startSample,
                                   bufferToFill.numSamples, gain);

        // M3.2: master peak tap — the final output buffer, post
        // master gain.
        if (output->getNumChannels() > 0)
            storeMaxPeak (masterPeakLeft,
                          output->getMagnitude (0, bufferToFill.startSample,
                                                bufferToFill.numSamples));

        if (output->getNumChannels() > 1)
            storeMaxPeak (masterPeakRight,
                          output->getMagnitude (1, bufferToFill.startSample,
                                                bufferToFill.numSamples));
    };

    // M3/M4: a stop/seek/disarm asked for stuck-note cleanup — run it
    // once, whether the transport is playing or parked. (Before M4
    // this was consumed only while stopped, so a seek during playback
    // left clip notes hanging; consuming it here also releases notes
    // that were played in live through the monitor.)
    if (midiPanicPending.exchange (false))
        processMidiPanic (*snapshot);

    if (! playing.load())
    {
        // M4: monitoring — armed MIDI tracks sound their instruments
        // from live input while the transport is parked. With nothing
        // armed this pass renders silence and costs one snapshot walk.
        renderBlock (*snapshot, *output, bufferToFill.startSample,
                     playPosition.load(), bufferToFill.numSamples,
                     liveMidiDrainBuffer.isEmpty() ? nullptr : &liveMidiDrainBuffer,
                     true);
        applyMasterGainAndMeter();
        return;
    }

    const auto total = snapshot->totalLength;
    auto position = playPosition.load();

    // M4: while recording with nothing ahead of the record-start
    // position the transport runs open-ended (the take creates the
    // length); otherwise the usual session-end stop applies.
    const bool recording = recordingActive.load();
    const bool openEnded = recording && recordOpenEnded.load();

    if (! openEnded && (total <= 0 || position >= total))
    {
        playing.store (false);
        midiPanicPending.store (true);
        return;
    }

    const int rendered = openEnded
        ? bufferToFill.numSamples
        : (int) juce::jmin<juce::int64> (bufferToFill.numSamples,
                                        total - position);

    // M4: capture this block's live note events into the lock-free
    // queue (absolute positions — see captureLiveMidi).
    if (recording)
        captureLiveMidi (liveMidiDrainBuffer, position, rendered);

    renderBlock (*snapshot, *output, bufferToFill.startSample, position, rendered,
                 liveMidiDrainBuffer.isEmpty() ? nullptr : &liveMidiDrainBuffer);

    applyMasterGainAndMeter();

    position += rendered;
    playPosition.store (position);

    if (! openEnded && position >= total)
    {
        playing.store (false);
        midiPanicPending.store (true);   // release any notes still held
    }
}

//==============================================================================
// Project save / load
//==============================================================================
bool TimelineEngine::saveProject (const juce::File& folder, juce::String& errorOut)
{
    juce::ScopedLock lock (modelLock);

    if (! folder.createDirectory())
    {
        errorOut = "Could not create the project folder:\n" + folder.getFullPathName();
        return false;
    }

    auto audioFolder = folder.getChildFile ("audio");

    if (! audioFolder.createDirectory())
    {
        errorOut = "Could not create the audio folder:\n" + audioFolder.getFullPathName();
        return false;
    }

    // Copy every source file into audio/, remembering the relative path.
    std::map<juce::int64, juce::String> relativePaths;

    for (auto& source : session.sources)
    {
        if (! source->originalFile.existsAsFile())
        {
            errorOut = "A source file is missing, so the project cannot be saved:\n"
                       + source->originalFile.getFullPathName();
            return false;
        }

        auto fileName = makeXmlSafeFileName (source->originalFile.getFileName());
        auto destination = audioFolder.getChildFile (fileName);
        int suffix = 2;

        while (destination.existsAsFile() && destination != source->originalFile)
        {
            fileName = makeXmlSafeFileName (source->originalFile.getFileNameWithoutExtension()
                                            + " " + juce::String (suffix++))
                       + "." + source->originalFile.getFileExtension();
            destination = audioFolder.getChildFile (fileName);
        }

        if (destination != source->originalFile
             && ! source->originalFile.copyFileTo (destination))
        {
            errorOut = "Could not copy an audio file into the project:\n"
                       + source->originalFile.getFullPathName();
            return false;
        }

        relativePaths[source->id] = "audio/" + fileName;
    }

    // Build the XML document.
    auto xml = std::make_unique<juce::XmlElement> ("YMOrpheusProject");
    xml->setAttribute ("version", 1);
    xml->setAttribute ("tempo", session.tempoBpm);
    xml->setAttribute ("masterGain", (double) masterGain.load());
    xml->setAttribute ("sampleRate", engineRate);
    // M6: the session timecode rate (absent in pre-M6 files).
    xml->setAttribute ("timecodeFpsNum", session.timecodeFpsNum);
    xml->setAttribute ("timecodeFpsDen", session.timecodeFpsDen);
    xml->setAttribute ("timecodeFpsUserSet", session.timecodeFpsUserSet ? 1 : 0);
    // M6.3: the telop font-size index (absent in pre-M6.3 files = M).
    xml->setAttribute ("telopSize", session.telopSize);
    // M6.6: the telop style. telopSize above is still written (the
    // S/M/L mirror of telopScale) so pre-M6.6 builds keep the size.
    xml->setAttribute ("telopFont", session.telopStyle.fontName);
    xml->setAttribute ("telopScale", (double) session.telopStyle.scalePercent);
    xml->setAttribute ("telopVPos", (int) session.telopStyle.vertical);
    xml->setAttribute ("telopAlign", (int) session.telopStyle.align);
    xml->setAttribute ("telopColour",
                       juce::String::toHexString (
                           (juce::int64) session.telopStyle.colourArgb));

    auto* sourcesXml = xml->createNewChildElement ("Sources");

    for (auto& source : session.sources)
    {
        auto* element = sourcesXml->createNewChildElement ("Source");
        element->setAttribute ("id", juce::String (source->id));
        element->setAttribute ("name", source->name);
        element->setAttribute ("file", relativePaths[source->id]);
    }

    auto* tracksXml = xml->createNewChildElement ("Tracks");

    for (auto& track : session.tracks)
    {
        auto* trackXml = tracksXml->createNewChildElement ("Track");
        trackXml->setAttribute ("id", juce::String (track.id));
        trackXml->setAttribute ("name", track.name);
        trackXml->setAttribute ("gain", (double) track.gain);
        trackXml->setAttribute ("mute", track.mute ? 1 : 0);
        trackXml->setAttribute ("solo", track.solo ? 1 : 0);
        trackXml->setAttribute ("kind", track.kind == TrackKind::midi ? "midi"
                                        : track.kind == TrackKind::video ? "video"
                                        : track.kind == TrackKind::text ? "text"
                                                                        : "audio");
        trackXml->setAttribute ("fxMidiIn", track.fxMidiIn ? 1 : 0);   // M3.3

        // One <Plugin> element per hosted slot — shared by the FX chain
        // (M2) and a MIDI track's instrument (M3).
        auto writePlugin = [this] (juce::XmlElement* parent, FxSlotData& slot)
        {
            auto* pluginXml = parent->createNewChildElement ("Plugin");
            pluginXml->setAttribute ("identifier", slot.identifier);
            pluginXml->setAttribute ("name", slot.name);
            pluginXml->setAttribute ("manufacturer", slot.manufacturerName);
            pluginXml->setAttribute ("version", slot.version);
            pluginXml->setAttribute ("fileOrIdentifier", slot.fileOrIdentifier);
            pluginXml->setAttribute ("uniqueId", slot.uniqueId);
            pluginXml->setAttribute ("deprecatedUid", slot.deprecatedUid);
            pluginXml->setAttribute ("format", slot.pluginFormatName);
            pluginXml->setAttribute ("bypass", slot.bypass ? 1 : 0);
            pluginXml->setAttribute ("missing", slot.missing ? 1 : 0);
            pluginXml->setAttribute ("state", captureSlotStateLocked (slot));

            // The full plugin description, so the instance can be
            // rebuilt even if the known plugin list has changed.
            if (auto descriptionXml = slot.toDescription().createXml())
                pluginXml->addChildElement (descriptionXml.release());
        };

        for (auto& clip : track.clips)
        {
            auto* clipXml = trackXml->createNewChildElement ("Clip");
            clipXml->setAttribute ("id", juce::String (clip.id));
            clipXml->setAttribute ("source", juce::String (clip.sourceId));
            clipXml->setAttribute ("start", juce::String (clip.start));
            clipXml->setAttribute ("offset", juce::String (clip.offset));
            clipXml->setAttribute ("length", juce::String (clip.length));
            clipXml->setAttribute ("fadeIn", juce::String (clip.fadeIn));
            clipXml->setAttribute ("fadeOut", juce::String (clip.fadeOut));
            clipXml->setAttribute ("gain", (double) clip.gain);
        }

        // M3: a MIDI track's hosted instrument.
        if (track.kind == TrackKind::midi && track.hasInstrument)
        {
            auto* instrumentXml = trackXml->createNewChildElement ("Instrument");
            writePlugin (instrumentXml, track.instrument);
        }

        // M2: the track's VST3 insert chain, in processing order.
        if (! track.fxChain.empty())
        {
            auto* fxXml = trackXml->createNewChildElement ("FxChain");

            for (auto& slot : track.fxChain)
                writePlugin (fxXml, slot);
        }

        // M3: the track's MIDI clips and their notes (clip position and
        // length in seconds, note times in beats).
        if (track.kind == TrackKind::midi && ! track.midiClips.empty())
        {
            auto* midiXml = trackXml->createNewChildElement ("MidiClips");

            for (auto& clip : track.midiClips)
            {
                auto* clipXml = midiXml->createNewChildElement ("MidiClip");
                clipXml->setAttribute ("id", juce::String (clip.id));
                clipXml->setAttribute ("start", clip.startSeconds);
                clipXml->setAttribute ("length", clip.lengthSeconds);
                // M6.9: the beat-window offset (pre-M6.9 files have
                // no such attribute and load it as 0).
                clipXml->setAttribute ("offsetBeats", clip.offsetBeats);

                for (auto& note : clip.notes)
                {
                    auto* noteXml = clipXml->createNewChildElement ("Note");
                    noteXml->setAttribute ("note", note.noteNumber);
                    noteXml->setAttribute ("start", note.startBeat);
                    noteXml->setAttribute ("length", note.lengthBeats);
                    noteXml->setAttribute ("vel", note.velocity);
                }
            }
        }

        // M6: the video track's clips. Video files are referenced by
        // absolute path and are NOT copied into the project folder
        // (they are large, and the audio/ copy rule exists so stems
        // travel with the project — video stays an external reference
        // in Phase 1; a missing file at load drops the clip with a
        // warning, exactly like a missing audio source).
        if (track.kind == TrackKind::video && ! track.videoClips.empty())
        {
            auto* videoXml = trackXml->createNewChildElement ("VideoClips");

            for (auto& clip : track.videoClips)
            {
                auto* clipXml = videoXml->createNewChildElement ("VideoClip");
                clipXml->setAttribute ("id", juce::String (clip.id));
                clipXml->setAttribute ("file", clip.filePath);
                clipXml->setAttribute ("start", clip.startSeconds);
                clipXml->setAttribute ("offset", clip.offsetSeconds);
                clipXml->setAttribute ("length", clip.durationSeconds);
                clipXml->setAttribute ("fpsNum", clip.fpsNum);
                clipXml->setAttribute ("fpsDen", clip.fpsDen);
                clipXml->setAttribute ("width", clip.width);
                clipXml->setAttribute ("height", clip.height);
                clipXml->setAttribute ("sourceLength", clip.sourceDurationSeconds);
                // M6.3: fades (absent in pre-M6.3 files = 0).
                clipXml->setAttribute ("fadeIn", clip.fadeInSeconds);
                clipXml->setAttribute ("fadeOut", clip.fadeOutSeconds);
                // M6.5: still-image flag (written only when true;
                // absent in pre-M6.5 files = false).
                if (clip.stillImage)
                    clipXml->setAttribute ("still", 1);
            }
        }

        // M6.3: the text track's telop clips. The text itself (UTF-8,
        // may contain line breaks) is stored as the element's text
        // content — XML text carries newlines and markup characters
        // safely, which attributes would not.
        if (track.kind == TrackKind::text && ! track.textClips.empty())
        {
            auto* textXml = trackXml->createNewChildElement ("TextClips");

            for (auto& clip : track.textClips)
            {
                auto* clipXml = textXml->createNewChildElement ("TextClip");
                clipXml->setAttribute ("id", juce::String (clip.id));
                clipXml->setAttribute ("start", clip.startSeconds);
                clipXml->setAttribute ("length", clip.durationSeconds);
                clipXml->addTextElement (clip.text);
            }
        }
    }

    auto projectFile = folder.getChildFile ("project.xml");

    if (! projectFile.replaceWithText (xml->toString()))
    {
        errorOut = "Could not write the project file:\n" + projectFile.getFullPathName();
        return false;
    }

    return true;
}

bool TimelineEngine::loadProject (const juce::File& folder, juce::StringArray& warningsOut)
{
    auto projectFile = folder.getChildFile ("project.xml");

    if (! projectFile.existsAsFile())
        return false;

    auto xml = juce::XmlDocument::parse (projectFile);

    if (xml == nullptr || ! xml->hasTagName ("YMOrpheusProject"))
        return false;

    pause();

    juce::ScopedLock lock (modelLock);

    cancelRecordingLocked();   // M4: pause() finalised any takes; make
                               // sure no builder state survives the swap
    retireAllFxInstancesLocked();
    session.sources.clear();
    session.tracks.clear();
    session.tempoBpm = xml->getDoubleAttribute ("tempo", 120.0);
    // M6: timecode rate (absent in pre-M6 files = the 30 fps default).
    session.timecodeFpsNum = xml->getIntAttribute ("timecodeFpsNum", 30);
    session.timecodeFpsDen = xml->getIntAttribute ("timecodeFpsDen", 1);
    session.timecodeFpsUserSet = xml->getBoolAttribute ("timecodeFpsUserSet", false);

    // M6.3: telop font size (absent in pre-M6.3 files = M).
    session.telopSize = juce::jlimit (0, 2, xml->getIntAttribute ("telopSize", 1));

    // M6.6: telop style. The scale comes from "telopScale" when the
    // file has one; older files carry only the S/M/L index, which
    // maps to 72/100/138%. Everything else defaults to the M6.3 look.
    {
        TelopStyleData style;
        style.fontName = xml->getStringAttribute ("telopFont", {});

        if (xml->hasAttribute ("telopScale"))
            style.scalePercent = (float) juce::jlimit (
                40.0, 250.0, xml->getDoubleAttribute ("telopScale", 100.0));
        else
            style.scalePercent = telopScalePercentForSizeIndex (session.telopSize);

        style.vertical = (TelopVerticalPosition)
            juce::jlimit (0, 2, xml->getIntAttribute ("telopVPos", 0));
        style.align = (TelopHorizontalAlign)
            juce::jlimit (0, 2, xml->getIntAttribute ("telopAlign", 1));

        const auto colourText = xml->getStringAttribute ("telopColour", {});

        style.colourArgb = colourText.isNotEmpty()
            ? (juce::uint32) colourText.getHexValue64()
            : 0xffffffffu;

        session.telopStyle = style;
        session.telopSize = telopSizeIndexForScalePercent (style.scalePercent);
    }

    if (session.timecodeFpsNum <= 0 || session.timecodeFpsDen <= 0)
    {
        session.timecodeFpsNum = 30;
        session.timecodeFpsDen = 1;
        session.timecodeFpsUserSet = false;
    }

    masterGain.store ((float) xml->getDoubleAttribute ("masterGain", 1.0));
    playPosition.store (0);
    nextId = 1;

    // Positions in the file are samples at the rate the project was saved
    // with; rescale them to the current engine rate.
    const double savedRate = xml->getDoubleAttribute ("sampleRate", engineRate);
    const double factor = (savedRate > 0.0) ? engineRate / savedRate : 1.0;

    auto scale = [factor] (juce::int64 value)
    {
        return (juce::int64) std::llround ((double) value * factor);
    };

    std::map<juce::int64, juce::int64> sourceIdMap;   // saved id -> loaded id (or -1)

    if (auto* sourcesXml = xml->getChildByName ("Sources"))
    {
        for (auto* element : sourcesXml->getChildIterator())
        {
            const auto savedId = element->getStringAttribute ("id").getLargeIntValue();
            const auto relative = element->getStringAttribute ("file");
            auto file = juce::File::isAbsolutePath (relative) ? juce::File (relative)
                                                              : folder.getChildFile (relative);

            if (! file.existsAsFile())
            {
                warningsOut.add (element->getStringAttribute ("name", relative));
                sourceIdMap[savedId] = -1;
                continue;
            }

            const auto newId = addSourceFromFileLocked (file);
            sourceIdMap[savedId] = newId;

            if (newId <= 0)
                warningsOut.add (element->getStringAttribute ("name", relative));
        }
    }

    // Shared by the FX chain (M2) and the instrument slot (M3): rebuild
    // a slot from its <Plugin> element, preferring the embedded full
    // description. Instantiation failure leaves a "missing" placeholder
    // that keeps its name and state (see instantiateFxSlotLocked).
    auto readPluginSlot = [] (juce::XmlElement* pluginXml, FxSlotData& slot)
    {
        slot.identifier       = pluginXml->getStringAttribute ("identifier");
        slot.name             = pluginXml->getStringAttribute ("name", "Unknown plugin");
        slot.manufacturerName = pluginXml->getStringAttribute ("manufacturer");
        slot.version          = pluginXml->getStringAttribute ("version");
        slot.fileOrIdentifier = pluginXml->getStringAttribute ("fileOrIdentifier");
        slot.uniqueId         = pluginXml->getIntAttribute ("uniqueId", 0);
        slot.deprecatedUid    = pluginXml->getIntAttribute ("deprecatedUid", 0);
        slot.pluginFormatName = pluginXml->getStringAttribute ("format", "VST3");
        slot.descriptiveName  = slot.name;
        slot.bypass           = pluginXml->getBoolAttribute ("bypass", false);
        slot.stateBase64      = pluginXml->getStringAttribute ("state");

        if (auto* descriptionXml = pluginXml->getChildByName ("PLUGIN"))
        {
            juce::PluginDescription description;

            if (description.loadFromXml (*descriptionXml))
                slot.fillFromDescription (description);
        }
    };

    auto instantiateAndRestore = [this] (FxSlotData& slot)
    {
        juce::String loadError;   // per-slot detail not surfaced here

        if (instantiateFxSlotLocked (slot, loadError) && slot.stateBase64.isNotEmpty())
        {
            juce::MemoryBlock block;
            block.fromBase64Encoding (slot.stateBase64);

            if (block.getSize() > 0)
                if (auto* instance = getFxInstanceLocked (slot.id))
                    instance->setStateInformation (block.getData(), (int) block.getSize());
        }
    };

    if (auto* tracksXml = xml->getChildByName ("Tracks"))
    {
        for (auto* trackElement : tracksXml->getChildIterator())
        {
            TrackData track;
            track.id = trackElement->getStringAttribute ("id").getLargeIntValue();
            track.name = trackElement->getStringAttribute ("name", "Track");
            track.gain = juce::jlimit (0.0f, 1.25f,
                                       (float) trackElement->getDoubleAttribute ("gain", 1.0));
            track.mute = trackElement->getBoolAttribute ("mute", false);
            track.solo = trackElement->getBoolAttribute ("solo", false);
            {
                // absent in M1/M2 files (= "audio"); "video" arrives
                // with M6, "text" with M6.3
                const auto kindString = trackElement->getStringAttribute ("kind", "audio");
                track.kind = kindString == "midi"  ? TrackKind::midi
                             : kindString == "video" ? TrackKind::video
                             : kindString == "text"  ? TrackKind::text
                                                     : TrackKind::audio;
            }
            track.fxMidiIn = trackElement->getBoolAttribute ("fxMidiIn", false);   // M3.3; absent before M3.3

            if (track.id <= 0)
                track.id = nextId++;

            nextId = juce::jmax (nextId, track.id + 1);
            const auto trackIndex = session.tracks.size();
            session.tracks.push_back (std::move (track));

            for (auto* clipElement : trackElement->getChildIterator())
            {
                if (! clipElement->hasTagName ("Clip"))
                    continue;   // e.g. the FxChain element (M2)

                const auto savedSourceId = clipElement->getStringAttribute ("source").getLargeIntValue();
                const auto mapped = sourceIdMap.find (savedSourceId);

                if (mapped == sourceIdMap.end() || mapped->second <= 0)
                    continue;   // source missing — clip dropped (see warnings)

                ClipData clip;
                clip.id = clipElement->getStringAttribute ("id").getLargeIntValue();
                clip.sourceId = mapped->second;
                clip.start   = scale (clipElement->getStringAttribute ("start").getLargeIntValue());
                clip.offset  = scale (clipElement->getStringAttribute ("offset").getLargeIntValue());
                clip.length  = scale (clipElement->getStringAttribute ("length").getLargeIntValue());
                clip.fadeIn  = scale (clipElement->getStringAttribute ("fadeIn").getLargeIntValue());
                clip.fadeOut = scale (clipElement->getStringAttribute ("fadeOut").getLargeIntValue());
                clip.gain = (float) clipElement->getDoubleAttribute ("gain", 1.0);

                if (clip.id <= 0)
                    clip.id = nextId++;

                nextId = juce::jmax (nextId, clip.id + 1);
                clampClipToSourceLocked (clip);
                session.tracks[trackIndex].clips.push_back (clip);
            }

            // M3: a MIDI track's instrument (absent in M1/M2 projects).
            if (auto* instrumentXml = trackElement->getChildByName ("Instrument"))
            {
                if (auto* pluginXml = instrumentXml->getChildByName ("Plugin"))
                {
                    FxSlotData slot;
                    slot.id = nextId++;
                    readPluginSlot (pluginXml, slot);

                    auto& target = session.tracks[trackIndex];
                    target.instrument = slot;
                    target.hasInstrument = true;
                    instantiateAndRestore (target.instrument);
                }
            }

            // M2: the track's VST3 insert chain (absent in M1 projects).
            // Each slot is instantiated now; a plugin that cannot be
            // loaded becomes a placeholder that keeps its name and
            // state so the chain (and a later save) survive intact.
            if (auto* fxXml = trackElement->getChildByName ("FxChain"))
            {
                for (auto* pluginXml : fxXml->getChildIterator())
                {
                    if (! pluginXml->hasTagName ("Plugin"))
                        continue;

                    FxSlotData slot;
                    slot.id = nextId++;
                    readPluginSlot (pluginXml, slot);

                    session.tracks[trackIndex].fxChain.push_back (slot);
                    instantiateAndRestore (session.tracks[trackIndex].fxChain.back());
                }
            }

            // M3: the track's MIDI clips and notes.
            if (auto* midiXml = trackElement->getChildByName ("MidiClips"))
            {
                for (auto* clipElement : midiXml->getChildIterator())
                {
                    if (! clipElement->hasTagName ("MidiClip"))
                        continue;

                    MidiClipData clip;
                    clip.id = clipElement->getStringAttribute ("id").getLargeIntValue();
                    clip.startSeconds = juce::jmax (0.0, clipElement->getDoubleAttribute ("start", 0.0));
                    clip.lengthSeconds = juce::jmax (0.0, clipElement->getDoubleAttribute ("length", 0.0));
                    // M6.9: absent in pre-M6.9 files = 0 (no windowing).
                    clip.offsetBeats = juce::jmax (0.0, clipElement->getDoubleAttribute ("offsetBeats", 0.0));

                    if (clip.id <= 0)
                        clip.id = nextId++;

                    nextId = juce::jmax (nextId, clip.id + 1);

                    for (auto* noteElement : clipElement->getChildIterator())
                    {
                        if (! noteElement->hasTagName ("Note"))
                            continue;

                        MidiNoteData note;
                        note.noteNumber  = juce::jlimit (0, 127, noteElement->getIntAttribute ("note", 60));
                        note.startBeat   = juce::jmax (0.0, noteElement->getDoubleAttribute ("start", 0.0));
                        note.lengthBeats = juce::jmax (0.05, noteElement->getDoubleAttribute ("length", 0.5));
                        note.velocity    = juce::jlimit (1, 127, noteElement->getIntAttribute ("vel", 100));
                        clip.notes.push_back (note);
                    }

                    session.tracks[trackIndex].midiClips.push_back (std::move (clip));
                }
            }

            // M6: the video track's clips (absent in pre-M6 projects).
            // A clip whose file has gone missing is dropped and named
            // in the warnings, like a missing audio source.
            if (auto* videoXml = trackElement->getChildByName ("VideoClips"))
            {
                for (auto* clipElement : videoXml->getChildIterator())
                {
                    if (! clipElement->hasTagName ("VideoClip"))
                        continue;

                    VideoClipData clip;
                    clip.id = clipElement->getStringAttribute ("id").getLargeIntValue();
                    clip.filePath = clipElement->getStringAttribute ("file");
                    clip.startSeconds = juce::jmax (0.0, clipElement->getDoubleAttribute ("start", 0.0));
                    clip.offsetSeconds = juce::jmax (0.0, clipElement->getDoubleAttribute ("offset", 0.0));
                    clip.durationSeconds = juce::jmax (0.0, clipElement->getDoubleAttribute ("length", 0.0));
                    clip.fpsNum = clipElement->getIntAttribute ("fpsNum", 0);
                    clip.fpsDen = juce::jmax (1, clipElement->getIntAttribute ("fpsDen", 1));
                    clip.width = clipElement->getIntAttribute ("width", 0);
                    clip.height = clipElement->getIntAttribute ("height", 0);
                    clip.sourceDurationSeconds = juce::jmax (0.0, clipElement->getDoubleAttribute ("sourceLength", 0.0));
                    // M6.3: fades (absent in pre-M6.3 files = 0).
                    clip.fadeInSeconds = juce::jmax (0.0, clipElement->getDoubleAttribute ("fadeIn", 0.0));
                    clip.fadeOutSeconds = juce::jmax (0.0, clipElement->getDoubleAttribute ("fadeOut", 0.0));
                    // M6.5: still-image flag (absent in pre-M6.5
                    // files = false). Read BEFORE the clamp below:
                    // a still's duration is free, and the clamp keys
                    // off this flag.
                    clip.stillImage = clipElement->getIntAttribute ("still", 0) != 0;

                    if (clip.id <= 0)
                        clip.id = nextId++;

                    nextId = juce::jmax (nextId, clip.id + 1);

                    if (! juce::File (clip.filePath).existsAsFile())
                    {
                        warningsOut.add (clip.getDisplayName());
                        continue;
                    }

                    clampVideoClipLocked (clip);
                    session.tracks[trackIndex].videoClips.push_back (std::move (clip));
                }
            }

            // M6.3: the text track's telop clips (absent in pre-M6.3
            // projects). The text is the element's text content.
            if (auto* textXml = trackElement->getChildByName ("TextClips"))
            {
                for (auto* clipElement : textXml->getChildIterator())
                {
                    if (! clipElement->hasTagName ("TextClip"))
                        continue;

                    TextClipData clip;
                    clip.id = clipElement->getStringAttribute ("id").getLargeIntValue();
                    clip.startSeconds = juce::jmax (0.0, clipElement->getDoubleAttribute ("start", 0.0));
                    clip.durationSeconds = juce::jmax (0.0, clipElement->getDoubleAttribute ("length", 0.0));
                    clip.text = clipElement->getAllSubText().replace ("\r\n", "\n");

                    if (clip.id <= 0)
                        clip.id = nextId++;

                    nextId = juce::jmax (nextId, clip.id + 1);
                    clampTextClipLocked (clip);
                    session.tracks[trackIndex].textClips.push_back (std::move (clip));
                }
            }
        }
    }

    touchModelLocked();
    return true;
}

void TimelineEngine::newProject()
{
    pause();

    juce::ScopedLock lock (modelLock);
    cancelRecordingLocked();   // M4: see loadProject
    retireAllFxInstancesLocked();
    session.sources.clear();
    session.tracks.clear();
    session.tempoBpm = 120.0;
    session.timecodeFpsNum = 30;   // M6 defaults
    session.timecodeFpsDen = 1;
    session.timecodeFpsUserSet = false;
    session.telopSize = 1;         // M6.3 default (M)
    session.telopStyle = TelopStyleData();   // M6.6 defaults
    nextId = 1;
    playPosition.store (0);
    touchModelLocked();
}
