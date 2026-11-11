#include "PianoRollComponent.h"

namespace
{
    const juce::Colour backgroundColour { 0xff1c1e24 };
    const juce::Colour gridColour       { 0xff2c303a };
    const juce::Colour barColour        { 0xff454b59 };
    const juce::Colour whiteKeyColour   { 0xffd7d9de };
    const juce::Colour blackKeyColour   { 0xff33363f };
    const juce::Colour noteColour       { 0xff1fa8a3 };
    const juce::Colour textColour       { 0xffe8e9ec };

    constexpr int    keysWidthPx  = 52;
    constexpr int    rowHeightPx  = 12;
    constexpr double pxPerBeat    = 32.0;
    constexpr double snapBeats    = 0.25;
    constexpr int    resizeZonePx = 4;

    bool isWhiteKey (int noteNumber)
    {
        switch (noteNumber % 12)
        {
            case 0: case 2: case 4: case 5: case 7: case 9: case 11: return true;
            default: return false;
        }
    }

    int noteToY (int noteNumber) noexcept
    {
        return (127 - noteNumber) * rowHeightPx;
    }

    double snapToGrid (double beats)
    {
        return std::floor (beats / snapBeats + 0.5) * snapBeats;
    }
}

//==============================================================================
class PianoRollComponent::GridComponent final : public juce::Component
{
public:
    explicit GridComponent (PianoRollComponent& ownerToUse) : owner (ownerToUse)
    {
        setWantsKeyboardFocus (true);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (backgroundColour);

        const int width = getWidth();
        const int height = getHeight();

        //--- pitch rows + keyboard strip --------------------------------
        for (int note = 0; note <= 127; ++note)
        {
            const int y = noteToY (note);
            const bool cNote = (note % 12 == 0);

            g.setColour (isWhiteKey (note) ? whiteKeyColour : blackKeyColour);
            g.fillRect (0, y, keysWidthPx, rowHeightPx);

            g.setColour (cNote ? barColour : gridColour);
            g.drawHorizontalLine (y + rowHeightPx - 1, (float) keysWidthPx, (float) width);

            if (cNote)
            {
                g.setColour (juce::Colours::black);
                g.setFont (juce::FontOptions (9.0f));
                g.drawText (juce::MidiMessage::getMidiNoteName (note, true, true, 3),
                            3, y, keysWidthPx - 5, rowHeightPx,
                            juce::Justification::centredLeft);
            }
        }

        //--- beat / bar lines --------------------------------------------
        const double beats = owner.getClipBeats();

        for (int beat = 0; beat <= (int) std::ceil (beats); ++beat)
        {
            const int x = keysWidthPx + (int) std::floor (beat * pxPerBeat);
            g.setColour (beat % 4 == 0 ? barColour : gridColour);
            g.drawVerticalLine (x, 0.0f, (float) height);
        }

        //--- notes ---------------------------------------------------------
        for (int i = 0; i < (int) owner.notes.size(); ++i)
        {
            const auto& note = owner.notes[(size_t) i];
            auto rect = noteRect (note);
            const bool selected = (i == owner.selectedNote);

            g.setColour (noteColour.withAlpha (0.9f));
            g.fillRoundedRectangle (rect.toFloat(), 2.0f);
            g.setColour (selected ? textColour : noteColour.brighter (0.6f));
            g.drawRoundedRectangle (rect.toFloat(), 2.0f, selected ? 1.6f : 1.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        const int x = (int) e.position.x;
        const int y = (int) e.position.y;

        if (x < keysWidthPx)
            return;   // keyboard strip is display-only in v1

        const int hit = hitTestNote (x, y);

        if (e.mods.isRightButtonDown())
        {
            if (hit >= 0)
            {
                owner.notes.erase (owner.notes.begin() + hit);
                owner.selectedNote = -1;
                owner.commitNotes();
                repaint();
            }

            return;
        }

        if (hit >= 0)
        {
            owner.selectedNote = hit;
            dragNoteIndex = hit;
            dragOriginal = owner.notes[(size_t) hit];
            dragStartX = x;
            dragStartY = y;
            const auto rect = noteRect (dragOriginal);
            dragIsResize = (rect.getRight() - x) <= resizeZonePx;
            repaint();
            return;
        }

        // Empty grid: place a note.
        const double beat = snapToGrid (xToBeat (x));
        const int pitch = 127 - juce::jlimit (0, heightToRows() - 1, y / rowHeightPx);

        if (beat < 0.0 || beat >= owner.getClipBeats())
            return;

        MidiNoteData note;
        note.noteNumber = juce::jlimit (0, 127, pitch);
        note.startBeat = beat;
        note.lengthBeats = owner.lastNoteLengthBeats;
        note.velocity = 100;
        owner.notes.push_back (note);
        owner.selectedNote = (int) owner.notes.size() - 1;
        owner.commitNotes();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragNoteIndex < 0 || dragNoteIndex >= (int) owner.notes.size())
            return;

        auto& note = owner.notes[(size_t) dragNoteIndex];
        const double deltaBeats = ((double) e.position.x - (double) dragStartX) / pxPerBeat;

        if (dragIsResize)
        {
            note.lengthBeats = juce::jmax (0.1, snapToGrid (dragOriginal.lengthBeats + deltaBeats));
            owner.lastNoteLengthBeats = note.lengthBeats;
        }
        else
        {
            const int deltaRows = (int) std::floor (
                ((double) dragStartY - (double) e.position.y) / (double) rowHeightPx + 0.5);
            note.noteNumber = juce::jlimit (0, 127, dragOriginal.noteNumber + deltaRows);
            note.startBeat = juce::jlimit (0.0, juce::jmax (0.0, owner.getClipBeats() - 0.1),
                                           snapToGrid (dragOriginal.startBeat + deltaBeats));
        }

        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragNoteIndex >= 0)
        {
            dragNoteIndex = -1;
            owner.commitNotes();
        }
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
             && owner.selectedNote >= 0
             && owner.selectedNote < (int) owner.notes.size())
        {
            owner.notes.erase (owner.notes.begin() + owner.selectedNote);
            owner.selectedNote = -1;
            owner.commitNotes();
            repaint();
            return true;
        }

        return false;
    }

private:
    static int heightToRows() noexcept { return 128; }

    static double xToBeat (int x)
    {
        return ((double) x - (double) keysWidthPx) / pxPerBeat;
    }

    juce::Rectangle<int> noteRect (const MidiNoteData& note) const
    {
        const int x = keysWidthPx + (int) std::floor (note.startBeat * pxPerBeat);
        const int w = juce::jmax (3, (int) std::floor (note.lengthBeats * pxPerBeat));
        return { x, noteToY (note.noteNumber) + 1, w, rowHeightPx - 2 };
    }

    int hitTestNote (int x, int y) const
    {
        // Last-drawn notes are on top.
        for (int i = (int) owner.notes.size() - 1; i >= 0; --i)
            if (noteRect (owner.notes[(size_t) i]).contains (x, y))
                return i;

        return -1;
    }

    PianoRollComponent& owner;
    int dragNoteIndex = -1;
    bool dragIsResize = false;
    MidiNoteData dragOriginal;
    int dragStartX = 0;
    int dragStartY = 0;
};

//==============================================================================
PianoRollComponent::PianoRollComponent (TimelineEngine& engineToUse,
                                        juce::int64 trackIdToEdit,
                                        juce::int64 clipIdToEdit)
    : engine (engineToUse), trackId (trackIdToEdit), clipId (clipIdToEdit)
{
    loadNotes();

    grid = std::make_unique<GridComponent> (*this);
    const int contentWidth = keysWidthPx
        + (int) std::ceil (clipBeats * pxPerBeat) + 64;
    grid->setSize (contentWidth, 128 * rowHeightPx);

    viewport.setViewedComponent (grid.get(), false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);

    // Open looking at roughly C3–C5.
    viewport.setViewPosition (0, juce::jmax (0, noteToY (79)));

    setSize (720, 420);
}

PianoRollComponent::~PianoRollComponent() = default;

bool PianoRollComponent::clipStillExists()
{
    MidiClipData clip;
    juce::int64 ownerTrack = -1;
    return engine.getMidiClipData (clipId, clip, ownerTrack) && ownerTrack == trackId;
}

void PianoRollComponent::loadNotes()
{
    MidiClipData clip;
    juce::int64 ownerTrack = -1;
    double windowBeats = 0.0;

    if (engine.getMidiClipData (clipId, clip, ownerTrack))
    {
        const double secondsPerBeat = 60.0 / juce::jmax (1.0, engine.getTempo());
        windowBeats = clip.lengthSeconds / secondsPerBeat;
        clipBeats = juce::jmax (0.25, windowBeats);
        clipOffsetBeats = clip.offsetBeats;
    }

    // M6.9: the roll shows the clip's WINDOW. Notes whose start lies
    // inside it are translated into window beats for editing; the
    // rest are set aside verbatim (hiddenNotes) so no edit here can
    // ever delete a note the user trimmed out of view — re-extending
    // the clip on the timeline brings them back untouched. The split
    // uses the same rule as playback (start inside the window).
    std::vector<MidiNoteData> all;
    engine.getMidiClipNotes (trackId, clipId, all);

    notes.clear();
    hiddenNotes.clear();

    const double windowEndBeat = clipOffsetBeats + windowBeats;

    for (auto& note : all)
    {
        if (note.startBeat < clipOffsetBeats || note.startBeat >= windowEndBeat)
        {
            hiddenNotes.push_back (note);
            continue;
        }

        auto visible = note;
        visible.startBeat -= clipOffsetBeats;
        notes.push_back (visible);
    }
}

void PianoRollComponent::commitNotes()
{
    // M6.9: write back the edited window notes (translated back into
    // content beats) PLUS the hidden notes, verbatim. The engine
    // re-sorts the combined list by start beat.
    std::vector<MidiNoteData> all = hiddenNotes;

    for (auto note : notes)
    {
        note.startBeat += clipOffsetBeats;
        all.push_back (note);
    }

    engine.setMidiClipNotes (trackId, clipId, all);
}

void PianoRollComponent::resized()
{
    viewport.setBounds (getLocalBounds());
}
