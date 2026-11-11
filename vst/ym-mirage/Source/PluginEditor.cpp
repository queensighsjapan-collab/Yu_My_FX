#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const juce::Colour panelColour   { 0xff20202a };
    const juce::Colour headerColour  { 0xffffb84d };   // YM accent amber
    const juce::Colour textColour    { 0xffe8e8ee };
    const juce::Colour captionColour { 0xff9a9aa8 };
    const juce::Colour knobColour    { 0xff3a3a48 };
    const juce::Colour padBgColour   { 0xff17171f };
    const juce::Colour padLineColour { 0xff3d3d4d };
    const juce::Colour dotColour     { 0xffcfd6e4 };   // pale cloud dots
}

//==============================================================================
CloudCanvas::CloudCanvas (YMMirageProcessor& p)
    : processor (p)
{
    densityParam = processor.apvts.getRawParameterValue ("density");
    freezeParam  = processor.apvts.getRawParameterValue ("freeze");

    for (auto& dot : dots)
        resetDot (dot, true);

    startTimerHz (30);
}

void CloudCanvas::resetDot (Dot& dot, bool randomPhase)
{
    dot.x = rng.nextFloat();
    dot.y = 0.12f + rng.nextFloat() * 0.76f;
    dot.phase = randomPhase ? rng.nextFloat() : 0.0f;
    dot.drift = 0.0006f + rng.nextFloat() * 0.0016f;
    dot.radius = 2.0f + rng.nextFloat() * 3.5f;
}

void CloudCanvas::timerCallback()
{
    const bool frozen = freezeParam != nullptr && freezeParam->load() > 0.5f;

    for (auto& dot : dots)
    {
        dot.phase += 0.008f;

        if (! frozen)
            dot.x += dot.drift;

        if (dot.phase >= 1.0f || dot.x > 1.05f)
            resetDot (dot, false);
    }

    repaint();
}

void CloudCanvas::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (padBgColour);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (padLineColour);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    const float density = densityParam != nullptr ? densityParam->load() : 0.5f;
    const bool frozen = freezeParam != nullptr && freezeParam->load() > 0.5f;
    const float brightness = 0.25f + 0.75f * density;
    const auto& base = frozen ? headerColour : dotColour;

    const float w = bounds.getWidth();
    const float h = bounds.getHeight();

    for (const auto& dot : dots)
    {
        const float fade = std::sin (dot.phase * juce::MathConstants<float>::pi);
        const float alpha = fade * brightness * 0.85f;
        const float cx = dot.x * w;
        const float cy = dot.y * h;
        const float r = dot.radius;

        g.setColour (base.withAlpha (alpha * 0.25f));
        g.fillEllipse (cx - r * 2.2f, cy - r * 2.2f, r * 4.4f, r * 4.4f);
        g.setColour (base.withAlpha (alpha));
        g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
    }

    if (frozen)
    {
        g.setColour (headerColour);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText ("FROZEN", juce::Rectangle<float> (10.0f, 6.0f, 70.0f, 14.0f),
                    juce::Justification::topLeft, false);
    }
}

//==============================================================================
YMMirageEditor::YMMirageEditor (YMMirageProcessor& p)
    : AudioProcessorEditor (p), processorRef (p)
{
    titleLabel.setText ("YM MIRAGE", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, headerColour);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    captionLabel.setText ("GRANULAR CLOUD", juce::dontSendNotification);
    captionLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    captionLabel.setColour (juce::Label::textColourId, captionColour);
    captionLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (captionLabel);

    canvas = std::make_unique<CloudCanvas> (processorRef);
    addAndMakeVisible (canvas.get());

    hintLabel.setText ("FREEZE holds the buffer: the cloud sustains the captured moment forever. "
                       "Try it on vocals and pads.",
                       juce::dontSendNotification);
    hintLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    hintLabel.setColour (juce::Label::textColourId, captionColour);
    hintLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (hintLabel);

    setupKnob (sizeSlider, sizeLabel, "SIZE");
    setupKnob (densitySlider, densityLabel, "DENSITY");
    setupKnob (pitchSlider, pitchLabel, "PITCH");
    setupKnob (spreadSlider, spreadLabel, "SPREAD");
    setupKnob (smearSlider, smearLabel, "SMEAR");
    setupKnob (mixSlider, mixLabel, "MIX");
    setupKnob (outputSlider, outputLabel, "OUTPUT");

    sizeSlider.textFromValueFunction = [] (double v)
    {
        return juce::String ((int) std::lround (v)) + " ms";
    };
    sizeSlider.valueFromTextFunction = [] (const juce::String& t)
    {
        return (double) t.getFloatValue();
    };

    auto percentText = [] (double v)
    {
        return juce::String ((int) std::lround (v * 100.0)) + " %";
    };
    auto percentValue = [] (const juce::String& t)
    {
        return (double) juce::jlimit (0.0f, 1.0f, t.getFloatValue() / 100.0f);
    };

    for (auto* slider : { &densitySlider, &spreadSlider, &smearSlider, &mixSlider })
    {
        slider->textFromValueFunction = percentText;
        slider->valueFromTextFunction = percentValue;
    }

    pitchSlider.textFromValueFunction = [] (double v)
    {
        if (std::abs (v) < 0.05)
            return juce::String ("0 st");

        return juce::String::formatted ("%+.1f st", v);
    };
    pitchSlider.valueFromTextFunction = [] (const juce::String& t)
    {
        return (double) t.getFloatValue();
    };
    pitchSlider.setDoubleClickReturnValue (true, 0.0);   // centre detent

    outputSlider.textFromValueFunction = [] (double v)
    {
        return juce::String (v, 1) + " dB";
    };
    outputSlider.valueFromTextFunction = [] (const juce::String& t)
    {
        return (double) t.getFloatValue();
    };

    sizeAttachment    = std::make_unique<SliderAttachment> (processorRef.apvts, "sizeMs",  sizeSlider);
    densityAttachment = std::make_unique<SliderAttachment> (processorRef.apvts, "density", densitySlider);
    pitchAttachment   = std::make_unique<SliderAttachment> (processorRef.apvts, "pitch",   pitchSlider);
    spreadAttachment  = std::make_unique<SliderAttachment> (processorRef.apvts, "spread",  spreadSlider);
    smearAttachment   = std::make_unique<SliderAttachment> (processorRef.apvts, "smear",   smearSlider);
    mixAttachment     = std::make_unique<SliderAttachment> (processorRef.apvts, "mix",     mixSlider);
    outputAttachment  = std::make_unique<SliderAttachment> (processorRef.apvts, "output",  outputSlider);

    //--- FREEZE ---------------------------------------------------------------
    freezeButton.setButtonText ("FREEZE");
    freezeButton.setClickingTogglesState (true);
    freezeButton.setColour (juce::TextButton::buttonColourId, knobColour);
    freezeButton.setColour (juce::TextButton::buttonOnColourId, headerColour);
    freezeButton.setColour (juce::TextButton::textColourOffId, textColour);
    freezeButton.setColour (juce::TextButton::textColourOnId, panelColour);
    addAndMakeVisible (freezeButton);

    freezeAttachment = std::make_unique<ButtonAttachment> (processorRef.apvts, "freeze", freezeButton);

    //--- ON / BYPASS ----------------------------------------------------------
    powerButton.setColour (juce::TextButton::buttonColourId, headerColour);
    powerButton.setColour (juce::TextButton::textColourOffId, panelColour);
    powerButton.onClick = [this]
    {
        const bool bypassed = bypassRaw != nullptr && bypassRaw->load() > 0.5f;
        processorRef.setBypassed (! bypassed);
    };
    addAndMakeVisible (powerButton);

    //--- MIDI CC strip ----------------------------------------------------------
    pitchCcRaw  = processorRef.apvts.getRawParameterValue ("pitchCc");
    freezeCcRaw = processorRef.apvts.getRawParameterValue ("freezeCc");
    bypassCcRaw = processorRef.apvts.getRawParameterValue ("bypassCc");
    bypassRaw   = processorRef.apvts.getRawParameterValue ("bypass");

    auto setupStripLabel = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        label.setColour (juce::Label::textColourId, captionColour);
        label.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (label);
    };

    auto setupReadout = [this] (juce::Label& label)
    {
        label.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        label.setColour (juce::Label::textColourId, textColour);
        label.setColour (juce::Label::backgroundColourId, padBgColour);
        label.setColour (juce::Label::outlineColourId, padLineColour);
        label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (label);
    };

    setupStripLabel (pitchCcLabel, "PITCH");
    setupStripLabel (freezeCcLabel, "FREEZE");
    setupStripLabel (bypassCcLabel, "BYPASS");
    setupReadout (pitchCcReadout);
    setupReadout (freezeCcReadout);
    setupReadout (bypassCcReadout);

    using LT = YMMirageProcessor::LearnTarget;
    setupLearnButton (pitchLearnButton,  LT::pitch,
                      [this] (int cc) { processorRef.setPitchCcNumber (cc); });
    setupLearnButton (freezeLearnButton, LT::freeze,
                      [this] (int cc) { processorRef.setFreezeCcNumber (cc); });
    setupLearnButton (bypassLearnButton, LT::bypass,
                      [this] (int cc) { processorRef.setBypassCcNumber (cc); });

    refreshMidiStrip();
    startTimerHz (10);

    setSize (600, 460);
}

YMMirageEditor::~YMMirageEditor() = default;

void YMMirageEditor::setupKnob (juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
    slider.setColour (juce::Slider::rotarySliderFillColourId, headerColour);
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, knobColour);
    slider.setColour (juce::Slider::thumbColourId, textColour);
    slider.setColour (juce::Slider::textBoxTextColourId, textColour);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::Font (juce::FontOptions (13.0f)));
    label.setColour (juce::Label::textColourId, textColour);
    label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (label);
}

//==============================================================================
void YMMirageEditor::setupLearnButton (LearnButton& button,
                                       YMMirageProcessor::LearnTarget target,
                                       std::function<void (int)> clearAssignment)
{
    button.setButtonText ("LEARN");
    button.setColour (juce::TextButton::buttonColourId, knobColour);
    button.setColour (juce::TextButton::textColourOffId, textColour);

    button.onClick = [this, target]
    {
        // Clicking the armed target again cancels; clicking another
        // LEARN retargets.
        if (processorRef.getLearnTarget() == target)
            processorRef.setLearnTarget (YMMirageProcessor::LearnTarget::none);
        else
            processorRef.setLearnTarget (target);

        refreshMidiStrip();
    };

    button.onRightClick = [this, target, clearAssignment]
    {
        clearAssignment (0);   // 0 = Off: no CC assigned

        if (processorRef.getLearnTarget() == target)
            processorRef.setLearnTarget (YMMirageProcessor::LearnTarget::none);

        refreshMidiStrip();
    };

    addAndMakeVisible (button);
}

juce::String YMMirageEditor::ccReadoutText (std::atomic<float>* rawParam)
{
    if (rawParam == nullptr)
        return "Off";

    const int cc = (int) rawParam->load();
    return cc >= 1 ? "CC " + juce::String (cc) : juce::String ("Off");
}

void YMMirageEditor::refreshMidiStrip()
{
    pitchCcReadout.setText (ccReadoutText (pitchCcRaw), juce::dontSendNotification);
    freezeCcReadout.setText (ccReadoutText (freezeCcRaw), juce::dontSendNotification);
    bypassCcReadout.setText (ccReadoutText (bypassCcRaw), juce::dontSendNotification);

    using LT = YMMirageProcessor::LearnTarget;
    const auto armed = processorRef.getLearnTarget();

    auto showArmed = [] (LearnButton& button, bool isArmed)
    {
        button.setButtonText (isArmed ? "LEARN..." : "LEARN");
        button.setColour (juce::TextButton::buttonColourId,
                          isArmed ? headerColour : knobColour);
        button.setColour (juce::TextButton::textColourOffId,
                          isArmed ? panelColour : textColour);
    };

    showArmed (pitchLearnButton,  armed == LT::pitch);
    showArmed (freezeLearnButton, armed == LT::freeze);
    showArmed (bypassLearnButton, armed == LT::bypass);

    const bool bypassed = bypassRaw != nullptr && bypassRaw->load() > 0.5f;

    if (bypassed != lastBypassShown || powerButton.getButtonText().isEmpty())
    {
        lastBypassShown = bypassed;
        powerButton.setButtonText (bypassed ? "BYPASS" : "ON");
        powerButton.setColour (juce::TextButton::buttonColourId,
                               bypassed ? knobColour : headerColour);
        powerButton.setColour (juce::TextButton::textColourOffId,
                               bypassed ? captionColour : panelColour);
    }
}

void YMMirageEditor::timerCallback()
{
    refreshMidiStrip();
}

//==============================================================================
void YMMirageEditor::paint (juce::Graphics& g)
{
    g.fillAll (panelColour);

    // Thin accent line under the header, matching the YM panel style.
    g.setColour (headerColour.withAlpha (0.55f));
    g.fillRect (16, 58, getWidth() - 32, 2);
}

void YMMirageEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto header = area.removeFromTop (44);
    titleLabel.setBounds (header.removeFromLeft (210));
    captionLabel.setBounds (header.withTrimmedTop (16));

    area.removeFromTop (10);

    canvas->setBounds (area.removeFromTop (52));

    area.removeFromTop (10);

    hintLabel.setBounds (area.removeFromBottom (20));
    area.removeFromBottom (6);

    //--- MIDI CC strip ------------------------------------------------------
    auto strip = area.removeFromBottom (30);
    area.removeFromBottom (8);

    auto placeStripGroup = [] (juce::Rectangle<int> group,
                               juce::Label& nameLabel, juce::Label& readout,
                               LearnButton& learnButton)
    {
        nameLabel.setBounds (group.removeFromLeft (58));
        group.removeFromLeft (4);
        readout.setBounds (group.removeFromLeft (52).withHeight (24)
                               .withY (group.getY() + 3));
        group.removeFromLeft (4);
        learnButton.setBounds (group.removeFromLeft (62).withHeight (24)
                                   .withY (group.getY() + 3));
    };

    placeStripGroup (strip.removeFromLeft (180), pitchCcLabel, pitchCcReadout, pitchLearnButton);
    strip.removeFromLeft (14);
    placeStripGroup (strip.removeFromLeft (180), freezeCcLabel, freezeCcReadout, freezeLearnButton);
    strip.removeFromLeft (14);
    placeStripGroup (strip, bypassCcLabel, bypassCcReadout, bypassLearnButton);

    // Two knob rows of four cells: SIZE DENSITY PITCH SPREAD /
    // SMEAR MIX OUTPUT FREEZE.
    const int cellWidth = area.getWidth() / 4;
    const int rowHeight = area.getHeight() / 2;

    auto placeKnob = [] (juce::Rectangle<int> column, juce::Label& label, juce::Slider& slider)
    {
        label.setBounds (column.removeFromTop (20));
        slider.setBounds (column.reduced (6, 0));
    };

    auto row1 = area.removeFromTop (rowHeight);
    placeKnob (row1.removeFromLeft (cellWidth), sizeLabel, sizeSlider);
    placeKnob (row1.removeFromLeft (cellWidth), densityLabel, densitySlider);
    placeKnob (row1.removeFromLeft (cellWidth), pitchLabel, pitchSlider);
    placeKnob (row1, spreadLabel, spreadSlider);

    auto row2 = area;
    placeKnob (row2.removeFromLeft (cellWidth), smearLabel, smearSlider);
    placeKnob (row2.removeFromLeft (cellWidth), mixLabel, mixSlider);
    placeKnob (row2.removeFromLeft (cellWidth), outputLabel, outputSlider);

    // FREEZE with the ON/BYPASS power button stacked under it, in the
    // last cell's knob area.
    auto freezeCell = row2;
    freezeCell.removeFromTop (20);
    freezeCell = freezeCell.withSizeKeepingCentre (freezeCell.getWidth() - 24, 92);
    freezeButton.setBounds (freezeCell.removeFromTop (46));
    freezeCell.removeFromTop (8);
    powerButton.setBounds (freezeCell);
}
