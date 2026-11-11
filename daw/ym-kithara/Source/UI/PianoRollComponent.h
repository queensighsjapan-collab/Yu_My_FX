#pragma once

#include <JuceHeader.h>
#include "TimelineEngine.h"

//==============================================================================
/**
    PianoRollComponent — the M3 piano-roll editor for one MIDI clip.

    A DocumentWindow (owned by MainComponent) holds one of these. The
    component edits a COPY of the clip's note list and writes the whole
    list back through TimelineEngine::setMidiClipNotes whenever it
    changes (note added / moved / resized / deleted), which keeps the
    engine's model the single source of truth without any fine-grained
    note-level edit API.

    Layout: a Viewport scrolls a grid whose left strip is the piano
    keyboard (C rows labelled), X is beats within the clip (bar lines
    every 4 beats), Y is pitch at 12 px per semitone.

    Interactions (v1 scope — no playhead, no velocity lane, no
    multi-select):
      - click empty grid      add a note (0.25-beat snap; length = the
                              last used length, initially 0.5 beats;
                              velocity fixed at 100)
      - click a note          select it
      - drag a note           move it in pitch/time (0.25-beat snap)
      - drag a note's right
        edge (within 4 px)    resize its length (min 0.1 beats)
      - Delete / Backspace    delete the selected note
      - right-click a note    delete it

    M6.9: the roll edits the clip's WINDOW (see MidiClipData::
    offsetBeats) — the grid's beat 0 is the window start and its width
    is the trimmed length, so extending a clip on the timeline widens
    the roll with it. Notes are translated between content beats and
    window beats at load/commit. Notes whose start lies outside the
    window are NOT part of the edit: they are carried aside verbatim
    (hiddenNotes) and written back untouched, so trimming a clip can
    never make an edit here delete a hidden note.
*/
class PianoRollComponent final : public juce::Component
{
public:
    PianoRollComponent (TimelineEngine& engineToUse, juce::int64 trackIdToEdit,
                        juce::int64 clipIdToEdit);
    ~PianoRollComponent() override;

    juce::int64 getTrackId() const noexcept { return trackId; }
    juce::int64 getClipId() const noexcept { return clipId; }

    /** False once the clip (or its track) has been deleted — the owner
        closes the window then. */
    bool clipStillExists();

    void resized() override;

private:
    class GridComponent;
    friend class GridComponent;

    void loadNotes();
    void commitNotes();
    double getClipBeats() const noexcept { return clipBeats; }

    TimelineEngine& engine;
    const juce::int64 trackId;
    const juce::int64 clipId;

    juce::Viewport viewport;
    std::unique_ptr<GridComponent> grid;

    std::vector<MidiNoteData> notes;      // the working copy being edited (window beats)
    std::vector<MidiNoteData> hiddenNotes;   // M6.9: notes outside the clip's
                                             // window (content beats, verbatim)
    double clipBeats = 4.0;               // window length in beats at the session tempo
    double clipOffsetBeats = 0.0;         // M6.9: window start in content beats
    double lastNoteLengthBeats = 0.5;     // length given to newly placed notes
    int selectedNote = -1;                // index into notes, or -1

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollComponent)
};
