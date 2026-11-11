#include "TrackHeaderComponent.h"
#include "TelopStyleDialog.h"   // M6.6: the text track's Style... dialog

namespace
{
    const juce::Colour backgroundColour { 0xff1c1e24 };
    const juce::Colour headerColour     { 0xff23262f };
    const juce::Colour gridColour       { 0xff31353f };
    const juce::Colour textColour       { 0xffe8e9ec };
    const juce::Colour dimTextColour    { 0xff9aa0ad };
    const juce::Colour accentColour     { 0xffd8a24a };

    // Meter colours: dark slot, then green / amber / red by level.
    const juce::Colour meterSlotColour  { 0xff101216 };
    const juce::Colour meterGreen       { 0xff3fbf5f };
    const juce::Colour meterAmber       { 0xffe8a33a };
    const juce::Colour meterRed         { 0xffe53b30 };
    const juce::Colour meterLedOff      { 0xff452222 };

    // Per-tick display decay (timer runs at 30 Hz) — the level falls
    // to ~2% in about a second, which reads naturally.
    constexpr float meterDecayPerTick = 0.88f;

    juce::Colour meterColourForLevel (float level)
    {
        if (level >= 0.99f) return meterRed;
        if (level >= 0.7f)  return meterAmber;
        return meterGreen;
    }

    //==========================================================================
    // M6.9: the record-arm toggle paints its own dot instead of using a
    // ● text glyph. The M6.8 build labelled it with the glyph's UTF-8
    // bytes in a narrow char* literal — but juce::String builds from
    // CharPointer_ASCII, so every byte above 127 became its own bogus
    // character and the button rendered as mojibake on Windows (the
    // transport Record button had the same bug). A drawn ellipse has
    // no encoding and no font-glyph dependence at all, and renders
    // identically on any locale. Colours come from the same TextButton
    // colour ids the M4 code set on the old TextButton: red background
    // when on, white dot when on, dim-red dot when off.
    class RecordArmButton final : public juce::Button
    {
    public:
        RecordArmButton() : juce::Button ("Record-arm") {}

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            getLookAndFeel().drawButtonBackground (
                g, *this,
                findColour (getToggleState() ? juce::TextButton::buttonOnColourId
                                             : juce::TextButton::buttonColourId),
                highlighted, down);

            const auto bounds = getLocalBounds().toFloat();
            const float diameter = juce::jmin (bounds.getWidth(),
                                               bounds.getHeight()) * 0.5f;

            g.setColour (findColour (getToggleState()
                                         ? juce::TextButton::textColourOnId
                                         : juce::TextButton::textColourOffId));
            g.fillEllipse (bounds.withSizeKeepingCentre (diameter, diameter));
        }
    };

    // One-line summary of a track's hosted plugins for the header, e.g.
    // "FX: YM Echo > KV-Element-FX (off)". Empty when an audio track's
    // chain is empty. MIDI tracks (M3) are prefixed with their
    // instrument: "Inst: YM2000 | FX: YM Echo", or "Inst: -" when none.
    // The video track (M6) hosts nothing: its line just says "Video".
    // The text track (M6.3) likewise: "Text (telop)".
    juce::String buildFxSummary (const TrackData& track)
    {
        if (track.kind == TrackKind::video)
            return "Video";

        if (track.kind == TrackKind::text)
            return "Text (telop)";

        juce::String fxPart;

        if (! track.fxChain.empty())
        {
            juce::StringArray entries;

            for (auto& slot : track.fxChain)
            {
                auto entry = slot.name;

                if (slot.missing)
                    entry += " (missing)";
                else if (slot.bypass)
                    entry += " (off)";

                entries.add (entry);
            }

            fxPart = "FX: " + entries.joinIntoString (" > ");
        }

        if (track.kind == TrackKind::midi)
        {
            juce::String instPart = "Inst: -";

            if (track.hasInstrument)
            {
                instPart = "Inst: " + track.instrument.name;

                if (track.instrument.missing)
                    instPart += " (missing)";
            }

            return fxPart.isEmpty() ? instPart : instPart + " | " + fxPart;
        }

        return fxPart;
    }
}

//==============================================================================
TrackHeaderComponent::TrackHeaderComponent (TimelineEngine& engineToUse)
    : engine (engineToUse)
{
    startTimerHz (30);
}

TrackHeaderComponent::~TrackHeaderComponent()
{
    stopTimer();
}

//==============================================================================
void TrackHeaderComponent::setTrackHeight (int newHeightPx)
{
    const int clamped = juce::jlimit (TimelineComponent::minTrackHeightPx,
                                      TimelineComponent::maxTrackHeightPx,
                                      newHeightPx);

    if (clamped == trackHeight)
        return;

    trackHeight = clamped;
    layoutHeaders();
    repaint();
}

void TrackHeaderComponent::setViewOffsetY (int offsetY)
{
    if (offsetY == viewOffsetY)
        return;

    viewOffsetY = offsetY;
    layoutHeaders();
    repaint();
}

void TrackHeaderComponent::resized()
{
    layoutHeaders();
}

//==============================================================================
// Track headers
//==============================================================================
juce::int64 TrackHeaderComponent::computeTrackSignature() const
{
    juce::ScopedLock lock (const_cast<TimelineEngine&> (engine).getModelLock());
    juce::int64 signature = (juce::int64) engine.getSession().tracks.size();

    for (auto& track : engine.getSession().tracks)
        signature = signature * 1000003 + track.id;

    return signature;
}

void TrackHeaderComponent::rebuildHeaders()
{
    headers.clear();

    juce::ScopedLock lock (engine.getModelLock());
    rebuildingHeaders = true;

    for (auto& track : engine.getSession().tracks)
    {
        HeaderControls controls;
        controls.trackId = track.id;
        const auto trackId = track.id;

        controls.nameLabel = std::make_unique<juce::Label>();
        controls.nameLabel->setText (track.name, juce::dontSendNotification);
        controls.nameLabel->setFont (juce::FontOptions (13.0f));
        controls.nameLabel->setColour (juce::Label::textColourId, textColour);
        controls.nameLabel->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        controls.nameLabel->setEditable (false, true, false);
        controls.nameLabel->onTextChange = [this, trackId, label = controls.nameLabel.get()]
        {
            if (! rebuildingHeaders)
                engine.renameTrack (trackId, label->getText());
        };
        addAndMakeVisible (*controls.nameLabel);

        controls.fxLabel = std::make_unique<juce::Label>();
        const auto fxSummary = buildFxSummary (track);
        controls.fxLabel->setText (fxSummary, juce::dontSendNotification);
        controls.fxLabel->setFont (juce::FontOptions (10.5f));
        controls.fxLabel->setColour (juce::Label::textColourId, dimTextColour);
        controls.fxLabel->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        controls.fxLabel->setJustificationType (juce::Justification::centredLeft);
        controls.fxLabel->setMinimumHorizontalScale (0.7f);
        controls.fxLabel->setEditable (false, false, false);
        controls.fxLabel->setTooltip (fxSummary);   // full chain, unelided
        addAndMakeVisible (*controls.fxLabel);

        controls.muteButton = std::make_unique<juce::TextButton> ("M");
        controls.muteButton->setWantsKeyboardFocus (false);   // Space belongs to play/pause
        controls.muteButton->setClickingTogglesState (true);
        controls.muteButton->setToggleState (track.mute, juce::dontSendNotification);
        controls.muteButton->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffb03a3a));
        controls.muteButton->onClick = [this, trackId, button = controls.muteButton.get()]
        {
            engine.setTrackMute (trackId, button->getToggleState());
        };
        addAndMakeVisible (*controls.muteButton);

        controls.soloButton = std::make_unique<juce::TextButton> ("S");
        controls.soloButton->setWantsKeyboardFocus (false);   // Space belongs to play/pause
        controls.soloButton->setClickingTogglesState (true);
        controls.soloButton->setToggleState (track.solo, juce::dontSendNotification);
        controls.soloButton->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffb08830));
        controls.soloButton->onClick = [this, trackId, button = controls.soloButton.get()]
        {
            engine.setTrackSolo (trackId, button->getToggleState());
        };
        addAndMakeVisible (*controls.soloButton);

        // M3.3: live MIDI input -> this track's FX chain. The label is
        // "M→FX" (the arrow is U+2192). M6.9: the string is built with
        // fromUTF8 — a plain narrow literal would be read by
        // juce::String as ASCII, turning the arrow's bytes into
        // mojibake (the bug that hit the ● buttons).
        controls.fxMidiButton = std::make_unique<juce::TextButton> (
            juce::String::fromUTF8 ("M\xe2\x86\x92" "FX"));
        controls.fxMidiButton->setWantsKeyboardFocus (false);   // Space belongs to play/pause
        controls.fxMidiButton->setClickingTogglesState (true);
        controls.fxMidiButton->setToggleState (track.fxMidiIn, juce::dontSendNotification);
        controls.fxMidiButton->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f6f8f));
        controls.fxMidiButton->setTooltip ("Send live MIDI (CC) to this track's effects");
        controls.fxMidiButton->onClick = [this, trackId, button = controls.fxMidiButton.get()]
        {
            engine.setTrackFxMidiIn (trackId, button->getToggleState());
        };
        addAndMakeVisible (*controls.fxMidiButton);

        // M4: record-arm (MIDI tracks only). The button lights red
        // while armed. Arming also switches on live monitoring for
        // the track. M6.9: the indicator is a programmatically drawn
        // dot (RecordArmButton, above) — the M6.8 ● glyph label
        // mojibaked on Windows (see the class comment).
        if (track.kind == TrackKind::midi)
        {
            controls.isMidiTrack = true;

            controls.armButton = std::make_unique<RecordArmButton>();
            controls.armButton->setWantsKeyboardFocus (false);   // Space belongs to play/pause
            controls.armButton->setClickingTogglesState (true);
            controls.armButton->setToggleState (track.recordArmed, juce::dontSendNotification);
            controls.armButton->setColour (juce::TextButton::buttonOnColourId,
                                           juce::Colour (0xffc22e24));
            controls.armButton->setColour (juce::TextButton::textColourOffId,
                                           juce::Colour (0xff8a4a44));
            controls.armButton->setColour (juce::TextButton::textColourOnId,
                                           juce::Colours::white);
            controls.armButton->setTooltip ("Record-arm: monitor live MIDI "
                                            "and record it while playing");
            controls.armButton->onClick = [this, trackId, button = controls.armButton.get()]
            {
                engine.setTrackRecordArmed (trackId, button->getToggleState());
            };
            addAndMakeVisible (*controls.armButton);
        }

        controls.removeButton = std::make_unique<juce::TextButton> ("x");
        controls.removeButton->setWantsKeyboardFocus (false);   // Space belongs to play/pause
        controls.removeButton->onClick = [this, trackId]
        {
            if (onTrackRemoved != nullptr)
                onTrackRemoved (trackId);

            engine.removeTrack (trackId);
        };
        addAndMakeVisible (*controls.removeButton);

        controls.gainSlider = std::make_unique<juce::Slider>();
        controls.gainSlider->setRange (0.0, 1.25, 0.0);
        controls.gainSlider->setValue (track.gain, juce::dontSendNotification);
        controls.gainSlider->setSliderStyle (juce::Slider::LinearHorizontal);
        controls.gainSlider->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        controls.gainSlider->setColour (juce::Slider::thumbColourId, accentColour);
        controls.gainSlider->onValueChange = [this, trackId, slider = controls.gainSlider.get()]
        {
            engine.setTrackGain (trackId, (float) slider->getValue());
        };
        addAndMakeVisible (*controls.gainSlider);

        // M6: the video track has no audio role — no gain, no Mute /
        // Solo, no M→FX (it has no FX chain), and paint() skips its
        // meter. The name (renamable) and the remove button stay.
        if (track.kind == TrackKind::video)
        {
            controls.muteButton->setVisible (false);
            controls.soloButton->setVisible (false);
            controls.fxMidiButton->setVisible (false);
            controls.gainSlider->setVisible (false);
        }

        // M6.3: the text track is the same (no audio role), plus its
        // one control: the session telop font-size selector (S/M/L),
        // which lives in the mid row the hidden buttons vacated.
        if (track.kind == TrackKind::text)
        {
            controls.isTextTrack = true;
            controls.muteButton->setVisible (false);
            controls.soloButton->setVisible (false);
            controls.fxMidiButton->setVisible (false);
            controls.gainSlider->setVisible (false);

            controls.telopSizeBox = std::make_unique<juce::ComboBox>();
            controls.telopSizeBox->setWantsKeyboardFocus (false);
            controls.telopSizeBox->addItem ("Telop S", 1);
            controls.telopSizeBox->addItem ("Telop M", 2);
            controls.telopSizeBox->addItem ("Telop L", 3);
            controls.telopSizeBox->setSelectedId (
                juce::jlimit (0, 2, engine.getSession().telopSize) + 1,
                juce::dontSendNotification);
            controls.telopSizeBox->setTooltip ("Telop (subtitle) text size");
            controls.telopSizeBox->onChange = [this, box = controls.telopSizeBox.get()]
            {
                engine.setTelopSize (box->getSelectedId() - 1);
            };
            addAndMakeVisible (*controls.telopSizeBox);

            // M6.6: the full style dialog (font, size, position,
            // alignment, colour) next to the size selector.
            controls.telopStyleButton = std::make_unique<juce::TextButton> ("Style...");
            controls.telopStyleButton->setWantsKeyboardFocus (false);
            controls.telopStyleButton->setTooltip (
                "Telop style: font, size, position, alignment, colour");
            controls.telopStyleButton->onClick = [this]
            {
                TelopStyleUI::showDialog (engine);
            };
            addAndMakeVisible (*controls.telopStyleButton);
        }

        headers.push_back (std::move (controls));
    }

    rebuildingHeaders = false;

    juce::int64 signature = (juce::int64) engine.getSession().tracks.size();

    for (auto& track : engine.getSession().tracks)
        signature = signature * 1000003 + track.id;

    lastTrackSignature = signature;
    layoutHeaders();
}

void TrackHeaderComponent::layoutHeaders()
{
    // The FX summary line only fits once tracks are tall enough; below
    // the threshold the header keeps its compact original two-row layout
    // (name row, then M/S + volume), and the M/S row simply claims less
    // height at the minimum track size, exactly as before.
    const bool showFxLine = trackHeight >= 52;

    for (size_t i = 0; i < headers.size(); ++i)
    {
        auto& controls = headers[i];
        const int y = trackIndexToY ((int) i);
        // The meter strip at the header's right edge is excluded from
        // the controls area (name/fx/buttons/slider end before it).
        auto area = juce::Rectangle<int> (4, y + 4,
                                          TimelineComponent::headerWidthPx - 12
                                              - TimelineComponent::meterStripWidthPx,
                                          trackHeight - 8);

        auto topRow = area.removeFromTop (showFxLine ? 18 : 22);
        controls.removeButton->setBounds (topRow.removeFromRight (20));
        topRow.removeFromRight (4);
        controls.nameLabel->setBounds (topRow);

        controls.fxLabel->setVisible (showFxLine);

        if (showFxLine)
        {
            area.removeFromTop (2);
            controls.fxLabel->setBounds (area.removeFromTop (13));
            area.removeFromTop (2);
        }
        else
        {
            area.removeFromTop (4);
        }

        auto midRow = area.removeFromTop (20);

        // M6.3: a text track's mid row holds the telop size selector
        // (its M/S/M→FX/gain controls are hidden); M6.6 adds the
        // Style... button beside it. The row is exactly the controls
        // width (160 px): 96 + 4 + 60.
        if (controls.isTextTrack && controls.telopSizeBox != nullptr)
        {
            controls.telopSizeBox->setBounds (midRow.removeFromLeft (96));
            midRow.removeFromLeft (4);

            if (controls.telopStyleButton != nullptr)
                controls.telopStyleButton->setBounds (midRow.removeFromLeft (60));
        }

        controls.muteButton->setBounds (midRow.removeFromLeft (26));
        midRow.removeFromLeft (3);
        controls.soloButton->setBounds (midRow.removeFromLeft (26));
        midRow.removeFromLeft (3);

        if (controls.isMidiTrack && controls.armButton != nullptr)
        {
            // M4: MIDI rows make room for the arm toggle between S
            // and M→FX (which narrows a little); the gain slider
            // takes what is left (~38 px at the standard width).
            controls.armButton->setBounds (midRow.removeFromLeft (20));
            midRow.removeFromLeft (3);
            controls.fxMidiButton->setBounds (midRow.removeFromLeft (38));
            midRow.removeFromLeft (3);
        }
        else
        {
            controls.fxMidiButton->setBounds (midRow.removeFromLeft (44));
            midRow.removeFromLeft (4);
        }

        controls.gainSlider->setBounds (midRow);
    }
}

//==============================================================================
// Peak meters (M3.2, moved here with the headers in M3.3).
//==============================================================================
juce::Rectangle<int> TrackHeaderComponent::getMeterStripRect (int trackIndex) const
{
    return { TimelineComponent::headerWidthPx - TimelineComponent::meterStripWidthPx,
             trackIndexToY (trackIndex),
             TimelineComponent::meterStripWidthPx, trackHeight };
}

juce::Rectangle<int> TrackHeaderComponent::getMeterLedRect (int trackIndex) const
{
    const auto strip = getMeterStripRect (trackIndex);
    return { strip.getX() + 2, strip.getY() + 4, 12, 6 };
}

void TrackHeaderComponent::drawTrackMeter (juce::Graphics& g, int trackIndex,
                                           juce::int64 trackId)
{
    const auto strip = getMeterStripRect (trackIndex);

    // Clip LED (latched by updateMeters, cleared by clicking it).
    bool latched = false;

    if (auto it = meterClipLatched.find (trackId); it != meterClipLatched.end())
        latched = it->second;

    const auto led = getMeterLedRect (trackIndex);
    g.setColour (latched ? meterRed : meterLedOff);
    g.fillRect (led);
    g.setColour (meterSlotColour);
    g.drawRect (led);

    // Stereo bars: bottom-up fill, colour stepped by the current level.
    // At the minimum track height (40 px) this still leaves ~22 px of
    // bar under the LED; taller headers just get longer bars.
    const int barsTop = strip.getY() + 14;
    const int barsBottom = strip.getBottom() - 4;
    const int barsHeight = juce::jmax (4, barsBottom - barsTop);

    float levelL = 0.0f, levelR = 0.0f;

    if (auto it = meterLevelLeft.find (trackId); it != meterLevelLeft.end())
        levelL = it->second;

    if (auto it = meterLevelRight.find (trackId); it != meterLevelRight.end())
        levelR = it->second;

    auto drawBar = [&] (int x, float level)
    {
        g.setColour (meterSlotColour);
        g.fillRect (x, barsTop, 5, barsHeight);

        const int fill = (int) std::round (juce::jlimit (0.0f, 1.0f, level)
                                           * (float) barsHeight);

        if (fill > 0)
        {
            g.setColour (meterColourForLevel (level));
            g.fillRect (x, barsBottom - fill, 5, fill);
        }
    };

    drawBar (strip.getX() + 2, levelL);
    drawBar (strip.getX() + 9, levelR);
}

void TrackHeaderComponent::updateMeters()
{
    // One dynamics tick (30 Hz): read each track's engine peak slot,
    // clear it so the next tick sees only fresh audio, latch the clip
    // LED on full-scale peaks, and decay the stored display levels.
    bool changed = false;

    {
        juce::ScopedLock lock (engine.getModelLock());
        auto& tracks = engine.getSession().tracks;

        std::vector<juce::int64> liveIds;
        liveIds.reserve (tracks.size());

        for (int i = 0; i < (int) tracks.size(); ++i)
        {
            const auto trackId = tracks[(size_t) i].id;
            liveIds.push_back (trackId);

            float rawL = 0.0f, rawR = 0.0f;
            engine.getTrackPeak (i, rawL, rawR);
            engine.clearTrackPeak (i);

            if ((rawL >= 0.999f || rawR >= 0.999f) && ! meterClipLatched[trackId])
            {
                meterClipLatched[trackId] = true;
                changed = true;
            }

            const float newL = juce::jmax (rawL, meterLevelLeft[trackId] * meterDecayPerTick);
            const float newR = juce::jmax (rawR, meterLevelRight[trackId] * meterDecayPerTick);

            if (std::abs (newL - meterLevelLeft[trackId]) > 0.002f
                 || std::abs (newR - meterLevelRight[trackId]) > 0.002f)
                changed = true;

            meterLevelLeft[trackId] = newL;
            meterLevelRight[trackId] = newR;
        }

        // Prune display state for tracks that no longer exist.
        auto prune = [&liveIds, &changed] (auto& map)
        {
            for (auto it = map.begin(); it != map.end();)
            {
                if (std::find (liveIds.begin(), liveIds.end(), it->first) == liveIds.end())
                {
                    it = map.erase (it);
                    changed = true;
                }
                else
                {
                    ++it;
                }
            }
        };

        prune (meterLevelLeft);
        prune (meterLevelRight);
        prune (meterClipLatched);
    }

    if (changed)
        repaint (TimelineComponent::headerWidthPx - TimelineComponent::meterStripWidthPx,
                 0, TimelineComponent::meterStripWidthPx, getHeight());
}

//==============================================================================
void TrackHeaderComponent::timerCallback()
{
    // Refresh header controls when the track set changes (add/remove/load).
    if (computeTrackSignature() != lastTrackSignature)
        rebuildHeaders();

    // Keep header controls in sync with model changes made elsewhere
    // (project load, another control, …).
    if (engine.getModelVersion() != lastModelVersion)
    {
        juce::ScopedLock lock (engine.getModelLock());

        for (auto& controls : headers)
        {
            if (auto* track = engine.findTrack (controls.trackId))
            {
                if (! controls.nameLabel->isBeingEdited()
                     && controls.nameLabel->getText() != track->name)
                    controls.nameLabel->setText (track->name, juce::dontSendNotification);

                controls.muteButton->setToggleState (track->mute, juce::dontSendNotification);
                controls.soloButton->setToggleState (track->solo, juce::dontSendNotification);
                controls.fxMidiButton->setToggleState (track->fxMidiIn, juce::dontSendNotification);

                // M4: keep the arm toggle in sync with the model
                // (runtime-only state, but the header reads it under
                // the same lock as everything else here).
                if (controls.armButton != nullptr)
                    controls.armButton->setToggleState (track->recordArmed,
                                                        juce::dontSendNotification);

                if (! controls.gainSlider->isMouseButtonDown())
                    controls.gainSlider->setValue (track->gain, juce::dontSendNotification);

                const auto fxSummary = buildFxSummary (*track);

                if (controls.fxLabel->getText() != fxSummary)
                {
                    controls.fxLabel->setText (fxSummary, juce::dontSendNotification);
                    controls.fxLabel->setTooltip (fxSummary);
                }

                // M6.3: keep the telop size selector in sync with the
                // session (project load, another header, ...). The
                // model lock is already held in this loop, so read
                // the session directly.
                if (controls.telopSizeBox != nullptr)
                {
                    const int wantedId = juce::jlimit (0, 2, engine.getSession().telopSize) + 1;

                    if (controls.telopSizeBox->getSelectedId() != wantedId)
                        controls.telopSizeBox->setSelectedId (wantedId,
                                                              juce::dontSendNotification);
                }
            }
        }

        repaint();
    }

    lastModelVersion = engine.getModelVersion();

    updateMeters();   // advance header meter dynamics
}

//==============================================================================
void TrackHeaderComponent::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour);

    juce::ScopedLock lock (engine.getModelLock());
    auto& session = engine.getSession();
    const int numTracks = (int) session.tracks.size();

    // The top rulerHeightPx stays a blank corner block: the ruler lives
    // with the lanes (inside the Viewport), and this column's rows begin
    // at the same Y as the lanes' rows.
    for (int i = 0; i < numTracks; ++i)
    {
        const int y = trackIndexToY (i);

        g.setColour (headerColour);
        g.fillRect (0, y, getWidth(), trackHeight);

        g.setColour (gridColour);
        g.drawHorizontalLine (y + trackHeight - 1, 0.0f, (float) getWidth());

        // Peak meter painted over the header background (the child
        // controls never reach into the meter strip). The video track
        // (M6) and the text track (M6.3) render no audio, so they get
        // no meter.
        if (session.tracks[(size_t) i].kind != TrackKind::video
             && session.tracks[(size_t) i].kind != TrackKind::text)
            drawTrackMeter (g, i, session.tracks[(size_t) i].id);
    }

    // Right separator against the lanes.
    g.setColour (gridColour);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
}

void TrackHeaderComponent::mouseDown (const juce::MouseEvent& e)
{
    // Clicking a track's meter clip LED clears its latch (the LED rect
    // is expanded a little so the small target is easy to hit). Every
    // other click belongs to the child controls.
    if (e.position.x < getWidth() - TimelineComponent::meterStripWidthPx)
        return;

    juce::ScopedLock lock (engine.getModelLock());
    auto& tracks = engine.getSession().tracks;

    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        if (getMeterLedRect (i).expanded (2, 3).contains (e.position.toInt()))
        {
            meterClipLatched[tracks[(size_t) i].id] = false;
            repaint (getMeterStripRect (i));
            return;
        }
    }
}
