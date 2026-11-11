#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const juce::Colour creamColour     { 0xffe9dec0 };
    const juce::Colour creamDimColour  { 0xffb8ad8d };
    const juce::Colour goldColour      { 0xffc8a24d };
    const juce::Colour goldLightColour { 0xffe8c97a };
    const juce::Colour amberColour     { 0xffffab1a };
    const juce::Colour lineColour      { 0xff454548 };
    const juce::Colour lcdDarkText     { 0xff2a1400 };
    const juce::Colour lcdDarkSubText  { 0xff5a3400 };
    const juce::Colour greenColour     { 0xff3dff6e };
    const juce::Colour yellowColour    { 0xffffcc33 };
    const juce::Colour redColour       { 0xffff2e2e };

    juce::String dbText (double v)
    {
        return (v > 0.0 ? "+" : "") + juce::String (v, 1);
    }

    juce::String plainText (double v)  { return juce::String (v, 1); }
    juce::String intText (double v)    { return juce::String ((int) std::lround (v)); }
    juce::String percentText (double v) { return juce::String ((int) std::lround (v * 100.0)); }

    // FX band geometry: left edge of each block (BOOST / CHORUS /
    // DELAY / REVERB) and the separator x positions between them.
    // DELAY is wider than the others - it carries three mini knobs
    // (TIME / FEEDBACK / MIX). Shared by resized() and paint().
    const int fxBlockX[4]     = { 14, 278, 566, 946 };
    const int fxSeparatorX[3] = { 274, 562, 942 };
}

//==============================================================================
RackLookAndFeel::RackLookAndFeel()
{
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff343437));
    setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff8a5a00));
    setColour (juce::TextButton::textColourOffId, creamColour);
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xffffe9bd));
    setColour (juce::Label::textColourId, creamColour);
}

void RackLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPosProportional, float, float, juce::Slider&)
{
    const float size = (float) juce::jmin (width, height);
    const float cx = (float) x + (float) width * 0.5f;
    const float cy = (float) y + (float) height * 0.5f;
    const float radius = size * 0.5f;

    auto bounds = juce::Rectangle<float> (cx - radius, cy - radius, size, size);

    // Bezel.
    g.setColour (juce::Colour (0xff101012));
    g.fillEllipse (bounds);
    g.setColour (juce::Colour (0xff5a5a60));
    g.drawEllipse (bounds.reduced (0.5f), 1.0f);

    // Body: diagonal metal gradient.
    auto body = bounds.reduced (size * 0.07f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5c5c62), body.getX(), body.getY(),
                                             juce::Colour (0xff222224), body.getRight(), body.getBottom(),
                                             false));
    g.fillEllipse (body);
    g.setColour (juce::Colours::black);
    g.drawEllipse (body.reduced (0.5f), 1.0f);

    // Pointer: -135 deg .. +135 deg from 12 o'clock, clockwise.
    const float angle = (-135.0f + sliderPosProportional * 270.0f)
                        * juce::MathConstants<float>::pi / 180.0f;
    const float inner = radius * 0.34f;
    const float outer = radius * 0.78f;
    const float dx = std::sin (angle);
    const float dy = -std::cos (angle);

    g.setColour (goldLightColour);
    juce::Line<float> pointer (cx + dx * inner, cy + dy * inner, cx + dx * outer, cy + dy * outer);
    g.drawLine (pointer, 3.0f);
    g.fillEllipse (cx + dx * outer - 2.0f, cy + dy * outer - 2.0f, 4.0f, 4.0f);

    // Centre cap.
    auto cap = juce::Rectangle<float> (cx - radius * 0.16f, cy - radius * 0.16f,
                                       radius * 0.32f, radius * 0.32f);
    g.setColour (juce::Colour (0xff3a3a3e));
    g.fillEllipse (cap);
    g.setColour (juce::Colour (0xff0a0a0b));
    g.drawEllipse (cap.reduced (0.5f), 1.0f);
}

void RackLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                            const juce::Colour& backgroundColour,
                                            bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto base = backgroundColour;

    if (button.getToggleState())
        base = button.findColour (juce::TextButton::buttonOnColourId);

    if (down)
        base = base.darker (0.25f);
    else if (highlighted)
        base = base.brighter (0.12f);

    g.setGradientFill (juce::ColourGradient (base.brighter (0.45f), bounds.getX(), bounds.getY(),
                                             base.darker (0.35f), bounds.getX(), bounds.getBottom(),
                                             false));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.drawLine (bounds.getX() + 3.0f, bounds.getY() + 1.0f,
                bounds.getRight() - 3.0f, bounds.getY() + 1.0f, 1.0f);
}

juce::Font RackLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return juce::Font (juce::FontOptions (11.5f, juce::Font::bold));
}

//==============================================================================
RackKnob::RackKnob (const juce::String& label, std::function<juce::String (double)> formatter,
                    bool miniLayout)
    : labelText (label), formatValue (std::move (formatter)), mini (miniLayout)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible (slider);
    refreshValue();
}

void RackKnob::resized()
{
    if (mini)
        slider.setBounds (4, 4, getHeight() - 8, getHeight() - 8);
    else
        slider.setBounds (8, 2, getWidth() - 16, getWidth() - 16);
}

void RackKnob::refreshValue()
{
    auto text = formatValue != nullptr ? formatValue (slider.getValue()) : juce::String();

    if (text != valueText)
    {
        valueText = text;
        repaint();
    }
}

void RackKnob::paint (juce::Graphics& g)
{
    const auto sliderBounds = slider.getBounds();

    // Tick marks around the dial.
    const float cx = sliderBounds.getCentreX();
    const float cy = sliderBounds.getCentreY();
    const float dialRadius = sliderBounds.getWidth() * 0.5f;

    g.setColour (creamDimColour.withAlpha (0.75f));

    for (int i = 0; i <= 10; ++i)
    {
        const float angle = (-135.0f + (float) i * 27.0f) * juce::MathConstants<float>::pi / 180.0f;
        const bool major = (i % 5 == 0);
        const float r0 = dialRadius + 2.0f;
        const float r1 = dialRadius + (major ? 7.0f : 4.5f);
        const float dx = std::sin (angle);
        const float dy = -std::cos (angle);
        g.drawLine (cx + dx * r0, cy + dy * r0, cx + dx * r1, cy + dy * r1, major ? 1.6f : 1.0f);
    }

    if (mini)
    {
        auto textArea = getLocalBounds().withTrimmedLeft (sliderBounds.getRight() + 6);
        g.setColour (amberColour);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText (valueText, textArea.removeFromTop (getHeight() / 2),
                    juce::Justification::bottomLeft, false);
        g.setColour (creamDimColour);
        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
        g.drawText (labelText, textArea, juce::Justification::topLeft, false);
    }
    else
    {
        auto textArea = getLocalBounds().withTrimmedTop (sliderBounds.getBottom() + 4);
        g.setColour (amberColour);
        g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
        g.drawText (valueText, textArea.removeFromTop (14), juce::Justification::centred, false);
        g.setColour (creamColour);
        g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
        g.drawText (labelText, textArea, juce::Justification::centred, false);
    }
}

//==============================================================================
LcdDisplay::LcdDisplay (bool amberStyle) : amber (amberStyle) {}

void LcdDisplay::setLines (const juce::String& line1, const juce::String& line2)
{
    if (line1 != text1 || line2 != text2)
    {
        text1 = line1;
        text2 = line2;
        repaint();
    }
}

void LcdDisplay::setFontSizes (float line1Size, float line2Size)
{
    size1 = line1Size;
    size2 = line2Size;
}

void LcdDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (amber)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffc44d), bounds.getX(), bounds.getY(),
                                                 juce::Colour (0xffd97e00), bounds.getX(), bounds.getBottom(),
                                                 false));
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.20f));
        g.fillRoundedRectangle (bounds.withHeight (bounds.getHeight() * 0.45f), 5.0f);
    }
    else
    {
        g.setColour (juce::Colour (0xff0d0d0e));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);
    }

    auto textArea = getLocalBounds().reduced (8, 4);
    const int line1Height = juce::jmax (12, (int) (size1 + 4.0f));

    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), size1, juce::Font::bold));
    g.setColour (amber ? lcdDarkText : amberColour);
    g.drawText (text1, textArea.removeFromTop (line1Height), juce::Justification::centredLeft, false);

    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), size2, juce::Font::plain));
    g.setColour (amber ? lcdDarkSubText : juce::Colour (0xff8a6a20));
    g.drawText (text2, textArea, juce::Justification::topLeft, false);
}

//==============================================================================
void LedMeter::setLevel (float newLevel)
{
    newLevel = juce::jlimit (0.0f, 1.0f, newLevel);

    if (std::abs (newLevel - level) > 0.01f)
    {
        level = newLevel;
        repaint();
    }
}

void LedMeter::paint (juce::Graphics& g)
{
    const int segments = 8;
    auto bounds = getLocalBounds();
    const float segHeight = (float) bounds.getHeight() / (float) segments;

    for (int i = 0; i < segments; ++i)
    {
        // i = 0 is the bottom segment.
        const float y = (float) bounds.getBottom() - (float) (i + 1) * segHeight;
        auto seg = juce::Rectangle<float> ((float) bounds.getX() + 1.0f, y + 1.0f,
                                           (float) bounds.getWidth() - 2.0f, segHeight - 2.0f);
        juce::Colour colour = i < 5 ? greenColour : (i < 7 ? yellowColour : redColour);
        const bool lit = level * (float) segments > (float) i + 0.25f;

        g.setColour (lit ? colour : colour.withAlpha (0.13f));
        g.fillRoundedRectangle (seg, 1.5f);
    }
}

//==============================================================================
void LedDot::setOn (bool shouldBeOn)
{
    if (shouldBeOn != on)
    {
        on = shouldBeOn;
        repaint();
    }
}

void LedDot::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (on ? redColour : juce::Colour (0xff571d1d));
    g.fillEllipse (bounds);

    if (on)
    {
        g.setColour (redColour.withAlpha (0.35f));
        g.fillEllipse (bounds.expanded (2.0f));
        g.setColour (juce::Colours::white.withAlpha (0.65f));
        g.fillEllipse (bounds.getX() + bounds.getWidth() * 0.22f,
                       bounds.getY() + bounds.getHeight() * 0.18f,
                       bounds.getWidth() * 0.28f, bounds.getHeight() * 0.28f);
    }
}

//==============================================================================
void RackArrowButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto& lf = getLookAndFeel();
    lf.drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId),
                             highlighted, down);

    auto bounds = getLocalBounds().toFloat();
    juce::Path triangle;
    const float cx = bounds.getCentreX();
    const float cy = bounds.getCentreY();
    const float w = 9.0f, h = 12.0f;

    if (left)
        triangle.addTriangle (cx + w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f,
                              cx - w * 0.5f, cy);
    else
        triangle.addTriangle (cx - w * 0.5f, cy - h * 0.5f, cx - w * 0.5f, cy + h * 0.5f,
                              cx + w * 0.5f, cy);

    g.setColour (creamColour);
    g.fillPath (triangle);
}

//==============================================================================
void PowerSwitch::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat();
    const bool active = getToggleState();

    // Slot plate.
    auto plate = bounds.withSizeKeepingCentre (30.0f, bounds.getHeight());
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4a4e), plate.getX(), plate.getY(),
                                             juce::Colour (0xff1c1c1e), plate.getX(), plate.getBottom(),
                                             false));
    g.fillRoundedRectangle (plate, 4.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (plate.reduced (0.5f), 4.0f, 1.0f);

    // Slot.
    auto slot = plate.withSizeKeepingCentre (8.0f, plate.getHeight() - 14.0f);
    g.setColour (juce::Colour (0xff0a0a0b));
    g.fillRoundedRectangle (slot, 4.0f);

    // Lever: up = active, down = bypassed.
    const float leverHeight = 20.0f;
    const float leverY = active ? plate.getY() + 5.0f : plate.getBottom() - 5.0f - leverHeight;
    auto lever = juce::Rectangle<float> (plate.getX() + 3.0f, leverY, plate.getWidth() - 6.0f, leverHeight);

    if (down)
        lever = lever.translated (0.0f, 1.0f);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff77777d), lever.getX(), lever.getY(),
                                             juce::Colour (0xff2c2c2f), lever.getX(), lever.getBottom(),
                                             false));
    g.fillRoundedRectangle (lever, 3.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (lever.reduced (0.5f), 3.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.25f));

    for (int i = 0; i < 3; ++i)
    {
        const float y = lever.getCentreY() - 3.0f + (float) i * 3.0f;
        g.drawLine (lever.getX() + 4.0f, y, lever.getRight() - 4.0f, y, 1.0f);
    }
}

//==============================================================================
RackKnob* YMAmpEditor::addKnob (const juce::String& paramId, const juce::String& label,
                                std::function<juce::String (double)> formatter, bool mini)
{
    auto knob = std::make_unique<RackKnob> (label, std::move (formatter), mini);
    knob->slider.setLookAndFeel (&rackLookAndFeel);
    addAndMakeVisible (*knob);
    sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.apvts, paramId, knob->slider));

    auto* raw = knob.get();
    knobs.push_back (std::move (knob));
    knobById[paramId] = raw;
    return raw;
}

juce::TextButton* YMAmpEditor::addMetalButton (const juce::String& text)
{
    auto button = std::make_unique<juce::TextButton> (text);
    addAndMakeVisible (*button);
    auto* raw = button.get();
    ownedButtons.push_back (std::move (button));
    return raw;
}

juce::Label* YMAmpEditor::addLabel (const juce::String& text, float fontSize,
                                    juce::Colour colour, juce::Justification justification)
{
    auto label = std::make_unique<juce::Label> (juce::String(), text);
    label->setFont (juce::Font (juce::FontOptions (fontSize, juce::Font::bold)));
    label->setColour (juce::Label::textColourId, colour);
    label->setJustificationType (justification);
    addAndMakeVisible (*label);
    auto* raw = label.get();
    ownedLabels.push_back (std::move (label));
    return raw;
}

juce::Label* YMAmpEditor::addSectionLabel (const juce::String& text)
{
    return addLabel (text, 10.0f, creamDimColour, juce::Justification::centredLeft);
}

//==============================================================================
YMAmpEditor::YMAmpEditor (YMAmpProcessor& p)
    : AudioProcessorEditor (p), processorRef (p)
{
    setLookAndFeel (&rackLookAndFeel);

    //--- brand -------------------------------------------------------------
    brandLabel = addLabel ("YM AMP", 30.0f, goldColour, juce::Justification::centredLeft);
    brandSubLabel = addLabel ("NEURAL AMP PLAYER", 10.0f, creamDimColour,
                              juce::Justification::centredLeft);

    powerSwitch.onClick = [this]
    {
        auto* raw = processorRef.apvts.getRawParameterValue ("bypass");
        const bool bypassed = raw != nullptr && raw->load() > 0.5f;
        processorRef.setBypassed (! bypassed);
    };
    addAndMakeVisible (powerSwitch);
    powerStateLabel = addLabel ("ON - ACTIVE", 10.0f, goldLightColour, juce::Justification::centred);

    //--- input section --------------------------------------------------------
    addSectionLabel ("INPUT - SOURCE");

    guitarButton = addMetalButton ("GUITAR");
    fileButton = addMetalButton ("FILE");
    guitarButton->setClickingTogglesState (true);
    fileButton->setClickingTogglesState (true);

    auto setSource = [this] (int index)
    {
        if (auto* param = processorRef.apvts.getParameter ("srcSelect"))
            param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
    };
    guitarButton->onClick = [setSource] { setSource (0); };
    fileButton->onClick = [setSource] { setSource (1); };

    addKnob ("inputGain", "INPUT", dbText);
    addAndMakeVisible (inputMeter);
    inputMeterCaption = addLabel ("IN", 9.0f, creamDimColour, juce::Justification::centred);

    fileLcd.setFontSizes (11.0f, 9.0f);
    addAndMakeVisible (fileLcd);

    fileLoadButton = addMetalButton ("LOAD");
    fileLoadButton->onClick = [this] { chooseAudioFile(); };

    filePlayButton = addMetalButton ("PLAY");
    filePlayButton->onClick = [this]
    {
        processorRef.setFilePlaying (! processorRef.isFilePlaying());
    };

    fileLoopButton = addMetalButton ("LOOP");
    fileLoopButton->setClickingTogglesState (true);
    fileLoopButton->onClick = [this]
    {
        processorRef.setFileLoop (fileLoopButton->getToggleState());
    };

    //--- model section ---------------------------------------------------------
    addSectionLabel ("MODEL - .NAM CAPTURE");
    modelLcd.setFontSizes (15.0f, 10.0f);
    addAndMakeVisible (modelLcd);

    loadModelButton = addMetalButton ("LOAD MODEL");
    loadModelButton->onClick = [this] { chooseModel(); };

    presetPrevButton.onClick = [this]
    {
        const int n = processorRef.getNumPresets();
        if (n > 0)
            processorRef.applyPreset ((processorRef.getCurrentPresetIndex() + n - 1) % n);
    };
    presetNextButton.onClick = [this]
    {
        const int n = processorRef.getNumPresets();
        if (n > 0)
            processorRef.applyPreset ((processorRef.getCurrentPresetIndex() + 1) % n);
    };
    addAndMakeVisible (presetPrevButton);
    addAndMakeVisible (presetNextButton);

    presetLcd.setFontSizes (11.5f, 8.0f);
    addAndMakeVisible (presetLcd);

    savePresetButton = addMetalButton ("SAVE");
    savePresetButton->onClick = [this] { askSavePreset(); };
    presetNoteLabel = addLabel ("FACTORY + USER PRESETS", 9.0f, creamDimColour,
                                juce::Justification::centredLeft);

    //--- tone section -----------------------------------------------------------
    addSectionLabel ("TONE - POST EQ");
    addKnob ("drive", "GAIN", dbText);
    addKnob ("bass", "BASS", dbText);
    addKnob ("mid", "MID", dbText);
    addKnob ("treble", "TREBLE", dbText);
    addKnob ("presence", "PRESENCE", dbText);

    //--- cab section ---------------------------------------------------------------
    addSectionLabel ("CABINET - IR");
    irLcd.setFontSizes (10.5f, 8.0f);
    addAndMakeVisible (irLcd);

    loadIrButton = addMetalButton ("LOAD IR");
    loadIrButton->onClick = [this] { chooseIr(); };

    cabToggleButton = addMetalButton ("CAB ON");
    cabToggleButton->setClickingTogglesState (true);
    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, "cabOn", *cabToggleButton));

    //--- output section ---------------------------------------------------------------
    addSectionLabel ("OUTPUT");
    addKnob ("gate", "GATE", [] (double v) { return juce::String ((int) std::lround (v)); });
    addKnob ("master", "MASTER", dbText);
    addAndMakeVisible (outputMeter);
    outputMeterCaption = addLabel ("OUT", 9.0f, creamDimColour, juce::Justification::centred);

    //--- FX band -------------------------------------------------------------------------
    static const char* fxNames[4] = { "BOOST", "CHORUS", "DELAY", "REVERB" };
    static const char* fxOnIds[4] = { "boostOn", "chorusOn", "delayOn", "reverbOn" };

    for (int i = 0; i < 4; ++i)
    {
        auto& block = fxBlocks[(size_t) i];
        block.onParamId = fxOnIds[i];
        block.toggle.setButtonText (fxNames[i]);
        block.toggle.setClickingTogglesState (true);
        block.toggle.setLookAndFeel (&rackLookAndFeel);
        block.toggle.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        block.toggle.setColour (juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
        block.toggle.setColour (juce::TextButton::textColourOffId, creamColour);
        block.toggle.setColour (juce::TextButton::textColourOnId, goldLightColour);
        addAndMakeVisible (block.toggle);
        addAndMakeVisible (block.led);
        buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processorRef.apvts, fxOnIds[i], block.toggle));
    }

    addKnob ("boostAmt", "AMOUNT", percentText, true);
    addKnob ("chorusRate", "RATE", plainText, true);
    addKnob ("chorusDepth", "DEPTH", percentText, true);
    addKnob ("delayTime", "TIME", intText, true);
    addKnob ("delayFb", "FEEDBACK", percentText, true);
    addKnob ("delayMix", "MIX", percentText, true);
    addKnob ("reverbMix", "MIX", percentText, true);

    //--- bottom strip ---------------------------------------------------------------------
    midiCaptionLabel = addLabel ("MIDI", 10.0f, creamDimColour, juce::Justification::centredLeft);

    {
        auto readout = std::make_unique<juce::Label> (juce::String(), "CC 65");
        readout->setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        readout->setColour (juce::Label::textColourId, creamColour);
        readout->setColour (juce::Label::backgroundColourId, juce::Colour (0xff0d0d0e));
        readout->setColour (juce::Label::outlineColourId, lineColour);
        readout->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (*readout);
        midiReadoutLabel = readout.get();
        ownedLabels.push_back (std::move (readout));
    }

    midiBypassLabel = addLabel ("- BYPASS", 10.0f, creamDimColour, juce::Justification::centredLeft);

    learnButton.setButtonText ("LEARN");
    learnButton.setLookAndFeel (&rackLookAndFeel);
    learnButton.onClick = [this]
    {
        using LT = YMAmpProcessor::LearnTarget;
        processorRef.setLearnTarget (processorRef.getLearnTarget() == LT::bypass ? LT::none
                                                                                  : LT::bypass);
    };
    learnButton.onRightClick = [this]
    {
        processorRef.setBypassCcNumber (0);
        processorRef.setLearnTarget (YMAmpProcessor::LearnTarget::none);
    };
    addAndMakeVisible (learnButton);

    statusLabel = addLabel ("READY", 10.0f, creamColour, juce::Justification::centredLeft);
    versionLabel = addLabel ("YMmg - v1.1.0 - 48 kHz recommended", 10.0f, creamDimColour,
                             juce::Justification::centredRight);

    refreshAll();
    startTimerHz (12);
    setSize (1240, 430);
}

YMAmpEditor::~YMAmpEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void YMAmpEditor::resized()
{
    // Brand / power
    brandLabel->setBounds (16, 24, 150, 36);
    brandSubLabel->setBounds (16, 62, 170, 14);
    powerSwitch.setBounds (57, 100, 46, 58);
    powerStateLabel->setBounds (14, 164, 132, 14);

    // Section labels (creation order: input, model, tone, cab, output)
    // They are the first five owned labels after brand/power labels.
    // (Positions are set explicitly below via the label list.)
    auto& labels = ownedLabels;
    // indices: 0 brand, 1 brandSub, 2 powerState, 3 inputMeterCaption is NOT
    // a section label... so set section labels by searching their text.
    for (auto& label : labels)
    {
        const auto text = label->getText();

        if (text == "INPUT - SOURCE")        label->setBounds (158, 16, 184, 14);
        else if (text == "MODEL - .NAM CAPTURE") label->setBounds (354, 16, 272, 14);
        else if (text == "TONE - POST EQ")   label->setBounds (638, 16, 330, 14);
        else if (text == "CABINET - IR")     label->setBounds (980, 16, 110, 14);
        else if (text == "OUTPUT")           label->setBounds (1088, 16, 100, 14);
    }

    // Input section
    guitarButton->setBounds (158, 36, 88, 24);
    fileButton->setBounds (254, 36, 88, 24);
    knobById["inputGain"]->setBounds (158, 64, 60, 92);
    inputMeter.setBounds (306, 70, 14, 78);
    inputMeterCaption->setBounds (300, 150, 26, 12);
    fileLcd.setBounds (158, 162, 184, 34);
    fileLoadButton->setBounds (158, 204, 84, 26);
    filePlayButton->setBounds (246, 204, 44, 26);
    fileLoopButton->setBounds (294, 204, 48, 26);

    // Model section
    modelLcd.setBounds (354, 38, 272, 54);
    loadModelButton->setBounds (354, 100, 272, 26);
    presetPrevButton.setBounds (354, 136, 28, 38);
    presetLcd.setBounds (386, 136, 208, 38);
    presetNextButton.setBounds (598, 136, 28, 38);
    savePresetButton->setBounds (354, 184, 84, 24);
    presetNoteLabel->setBounds (446, 189, 180, 14);

    // Tone section: five knobs across 330 px
    const char* toneIds[5] = { "drive", "bass", "mid", "treble", "presence" };

    for (int i = 0; i < 5; ++i)
        knobById[toneIds[i]]->setBounds (638 + i * 66, 52, 66, 92);

    // Cab section
    irLcd.setBounds (980, 38, 96, 42);
    loadIrButton->setBounds (980, 88, 96, 24);
    cabToggleButton->setBounds (980, 120, 96, 26);

    // Output section
    knobById["gate"]->setBounds (1088, 44, 56, 88);
    knobById["master"]->setBounds (1148, 44, 56, 88);
    outputMeter.setBounds (1212, 48, 14, 80);
    outputMeterCaption->setBounds (1206, 132, 26, 12);

    // FX band: four blocks (DELAY carries three knobs, the rest one
    // or two; block widths come from fxBlockX).
    const char* fxKnobIds[4][3] = { { "boostAmt", nullptr, nullptr },
                                    { "chorusRate", "chorusDepth", nullptr },
                                    { "delayTime", "delayFb", "delayMix" },
                                    { "reverbMix", nullptr, nullptr } };

    for (int i = 0; i < 4; ++i)
    {
        const int bx = fxBlockX[i];
        auto& block = fxBlocks[(size_t) i];
        block.led.setBounds (bx + 8, 273, 10, 10);
        block.toggle.setBounds (bx + 22, 266, 130, 22);

        for (int k = 0; k < 3; ++k)
        {
            if (fxKnobIds[i][k] != nullptr)
                knobById[fxKnobIds[i][k]]->setBounds (bx + 8 + k * 104, 296, 100, 52);
        }
    }

    // Bottom strip
    midiCaptionLabel->setBounds (16, 388, 36, 14);
    midiReadoutLabel->setBounds (54, 382, 56, 26);
    midiBypassLabel->setBounds (116, 388, 62, 14);
    learnButton.setBounds (184, 382, 64, 26);
    statusLabel->setBounds (264, 388, 420, 14);
    versionLabel->setBounds (876, 388, 348, 14);
}

//==============================================================================
void YMAmpEditor::paint (juce::Graphics& g)
{
    const auto width = getWidth();
    const auto height = getHeight();

    // Faceplate: charcoal brushed metal.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff353538), 0.0f, 0.0f,
                                             juce::Colour (0xff1a1a1c), 0.0f, (float) height,
                                             false));
    g.fillRect (getLocalBounds());

    g.setColour (juce::Colours::white.withAlpha (0.014f));

    for (int y = 0; y < height; y += 3)
        g.fillRect (0, y, width, 1);

    // Section dividers (main band).
    g.setColour (lineColour.withAlpha (0.55f));

    for (int x : { 152, 348, 632, 974, 1082 })
        g.fillRect (x, 14, 1, 230);

    // FX band inset with a group-box style legend.
    auto fxBand = juce::Rectangle<int> (10, 256, width - 20, 100);
    g.setColour (juce::Colour (0xff1b1b1e).withAlpha (0.9f));
    g.fillRoundedRectangle (fxBand.toFloat(), 6.0f);
    g.setColour (lineColour);
    g.drawRoundedRectangle (fxBand.toFloat().reduced (0.5f), 6.0f, 1.0f);

    g.setColour (juce::Colour (0xff262628));
    g.fillRect (20, 249, 96, 14);
    g.setColour (goldColour);
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText ("BUILT-IN FX", 24, 249, 110, 14, juce::Justification::centredLeft, false);
    g.setColour (juce::Colour (0xff262628));
    g.fillRect (118, 249, 330, 14);
    g.setColour (creamDimColour);
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.drawText ("BOOST PRE-AMP - CHORUS / DELAY / REVERB POST-CAB",
                122, 249, 420, 14, juce::Justification::centredLeft, false);

    // FX block separators.
    g.setColour (lineColour.withAlpha (0.4f));

    for (int sepX : fxSeparatorX)
        g.fillRect (sepX, 266, 1, 80);

    // Bottom strip.
    g.setColour (juce::Colour (0xff141416));
    g.fillRect (0, 368, width, height - 368);
    g.setColour (lineColour);
    g.fillRect (0, 368, width, 1);

    // Corner screws.
    auto drawScrew = [&g] (float cx, float cy, float slotAngle)
    {
        auto screw = juce::Rectangle<float> (cx - 7.0f, cy - 7.0f, 14.0f, 14.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a6a70), cx - 5.0f, cy - 5.0f,
                                                 juce::Colour (0xff2a2a2d), cx + 5.0f, cy + 5.0f,
                                                 false));
        g.fillEllipse (screw);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawEllipse (screw.reduced (0.5f), 1.0f);
        const float dx = std::cos (slotAngle) * 5.0f;
        const float dy = std::sin (slotAngle) * 5.0f;
        g.setColour (juce::Colour (0xff101012));
        g.drawLine (cx - dx, cy - dy, cx + dx, cy + dy, 2.0f);
    };

    drawScrew (16.0f, 16.0f, 0.6f);
    drawScrew ((float) width - 16.0f, 16.0f, 2.2f);
    drawScrew (16.0f, (float) height - 16.0f, 1.4f);
    drawScrew ((float) width - 16.0f, (float) height - 16.0f, 2.9f);
}

//==============================================================================
void YMAmpEditor::refreshAll()
{
    timerCallback();
}

void YMAmpEditor::timerCallback()
{
    // Meters with editor-side decay (the processor reports block peaks).
    inputMeterShown = juce::jmax (processorRef.getInputPeak(), inputMeterShown - 0.045f);
    outputMeterShown = juce::jmax (processorRef.getOutputPeak(), outputMeterShown - 0.045f);
    inputMeter.setLevel (inputMeterShown);
    outputMeter.setLevel (outputMeterShown);

    // LCDs.
    modelLcd.setLines (processorRef.getModelDisplayName(), processorRef.getModelDisplaySub());
    irLcd.setLines (processorRef.getIrDisplayName(), "IMPULSE RESPONSE");

    if (processorRef.hasAudioFile())
    {
        const auto pos = (int) std::lround (processorRef.getFilePositionNorm() * 100.0f);
        fileLcd.setLines (processorRef.getFileDisplayName(),
                          (processorRef.isFilePlaying() ? "PLAYING " : "STOPPED ")
                              + juce::String (pos) + "%"
                              + (processorRef.isFileLoop() ? " - LOOP" : ""));
    }
    else
    {
        fileLcd.setLines (processorRef.getFileDisplayName(), "STEM - WAV / MP3 / FLAC");
    }

    // Preset display.
    const int presetIndex = processorRef.getCurrentPresetIndex();

    if (presetIndex != lastPresetShown)
    {
        lastPresetShown = presetIndex;
        presetLcd.setLines ("PRESET " + juce::String (presetIndex + 1).paddedLeft ('0', 2)
                                + " - " + processorRef.getPresetName (presetIndex),
                            presetIndex < 4 ? "FACTORY" : "USER");
    }

    statusLabel->setText (processorRef.getStatusText(), juce::dontSendNotification);

    // Power / bypass mirror.
    auto* bypassRaw = processorRef.apvts.getRawParameterValue ("bypass");
    const bool bypassed = bypassRaw != nullptr && bypassRaw->load() > 0.5f;

    if (bypassed != lastPowerShown)
    {
        lastPowerShown = bypassed;
        powerSwitch.setToggleState (! bypassed, juce::dontSendNotification);
        powerStateLabel->setText (bypassed ? "BYPASSED" : "ON - ACTIVE", juce::dontSendNotification);
        powerStateLabel->setColour (juce::Label::textColourId,
                                    bypassed ? creamDimColour : goldLightColour);
    }

    // Source buttons mirror.
    auto* srcRaw = processorRef.apvts.getRawParameterValue ("srcSelect");
    const int srcIndex = srcRaw != nullptr ? (int) srcRaw->load() : 0;
    guitarButton->setToggleState (srcIndex == 0, juce::dontSendNotification);
    fileButton->setToggleState (srcIndex == 1, juce::dontSendNotification);

    // Transport mirror.
    filePlayButton->setButtonText (processorRef.isFilePlaying() ? "STOP" : "PLAY");
    fileLoopButton->setToggleState (processorRef.isFileLoop(), juce::dontSendNotification);

    // Cab toggle text.
    auto* cabRaw = processorRef.apvts.getRawParameterValue ("cabOn");
    cabToggleButton->setButtonText (cabRaw != nullptr && cabRaw->load() > 0.5f ? "CAB ON" : "CAB OFF");

    // FX LEDs.
    for (auto& block : fxBlocks)
    {
        auto* raw = processorRef.apvts.getRawParameterValue (block.onParamId);
        block.led.setOn (raw != nullptr && raw->load() > 0.5f);
    }

    // MIDI strip.
    auto* ccRaw = processorRef.apvts.getRawParameterValue ("bypassCc");
    const int cc = ccRaw != nullptr ? (int) ccRaw->load() : 0;
    midiReadoutLabel->setText (cc >= 1 ? "CC " + juce::String (cc) : "OFF",
                               juce::dontSendNotification);

    const bool learnArmed = processorRef.getLearnTarget() == YMAmpProcessor::LearnTarget::bypass;
    learnButton.setButtonText (learnArmed ? "LEARN..." : "LEARN");
    learnButton.setColour (juce::TextButton::buttonColourId,
                           learnArmed ? juce::Colour (0xff8a5a00) : juce::Colour (0xff343437));

    // Knob value readouts.
    for (auto& knob : knobs)
        knob->refreshValue();
}

//==============================================================================
void YMAmpEditor::chooseModel()
{
    chooser = std::make_unique<juce::FileChooser> ("Load NAM model", juce::File(), "*.nam");
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();

        if (file.existsAsFile())
            processorRef.loadModelAsync (file);
    });
}

void YMAmpEditor::chooseIr()
{
    chooser = std::make_unique<juce::FileChooser> ("Load cabinet IR (WAV)", juce::File(), "*.wav");
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();

        if (file.existsAsFile())
            processorRef.loadImpulseResponse (file);
    });
}

void YMAmpEditor::chooseAudioFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load stem / audio file", juce::File(),
                                                   "*.wav;*.mp3;*.flac;*.aif;*.aiff");
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();

        if (file.existsAsFile())
            processorRef.loadAudioFileAsync (file);
    });
}

void YMAmpEditor::askSavePreset()
{
    saveWindow = std::make_unique<juce::AlertWindow> ("Save Preset", "Enter a name for this preset:",
                                                      juce::AlertWindow::NoIcon);
    saveWindow->addTextEditor ("name", "My Preset", juce::String());
    saveWindow->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveWindow->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    saveWindow->enterModalState (true,
        juce::ModalCallbackFunction::create (
            [this] (int result)
            {
                if (result == 1 && saveWindow != nullptr)
                {
                    const auto name = saveWindow->getTextEditorContents ("name");
                    processorRef.saveUserPreset (name);
                    lastPresetShown = -1;   // force preset LCD refresh
                }

                saveWindow.reset();
            }),
        false);   // ownership stays with the unique_ptr; the callback resets it
}
