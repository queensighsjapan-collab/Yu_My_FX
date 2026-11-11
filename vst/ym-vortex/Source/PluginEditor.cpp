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
    const juce::Colour offColour     { 0xff6e3a3a };   // dim red: bypassed
}

//==============================================================================
SweepMeter::SweepMeter (YMVortexProcessor& p) : processor (p)
{
    startTimerHz (30);
}

void SweepMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (padBgColour);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (padLineColour);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    const float values[2] = { processor.getModValueL(), processor.getModValueR() };
    const float laneH = bounds.getHeight() / 2.0f;
    const float trackX0 = bounds.getX() + 24.0f;
    const float trackX1 = bounds.getRight() - 12.0f;

    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));

    for (int lane = 0; lane < 2; ++lane)
    {
        const float cy = bounds.getY() + laneH * ((float) lane + 0.5f);

        g.setColour (captionColour);
        g.drawText (lane == 0 ? "L" : "R",
                    juce::Rectangle<float> (bounds.getX() + 7.0f, cy - 7.0f, 12.0f, 14.0f),
                    juce::Justification::centredLeft, false);

        g.setColour (padLineColour);
        g.drawLine (trackX0, cy, trackX1, cy, 1.0f);
        g.drawLine ((trackX0 + trackX1) * 0.5f, cy - 4.0f, (trackX0 + trackX1) * 0.5f, cy + 4.0f, 1.0f);

        const float m = juce::jlimit (-1.0f, 1.0f, values[lane]);
        const float dx = trackX0 + (m + 1.0f) * 0.5f * (trackX1 - trackX0);

        g.setColour ((lane == 0 ? headerColour : textColour).withAlpha (0.28f));
        g.fillEllipse (dx - 7.0f, cy - 7.0f, 14.0f, 14.0f);
        g.setColour (lane == 0 ? headerColour : textColour);
        g.fillEllipse (dx - 3.5f, cy - 3.5f, 7.0f, 7.0f);
    }
}

//==============================================================================
YMVortexEditor::YMVortexEditor (YMVortexProcessor& p)
    : AudioProcessorEditor (p), processorRef (p)
{
    titleLabel.setText ("YM VORTEX", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, headerColour);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    captionLabel.setText ("MULTI-STAGE PHASER", juce::dontSendNotification);
    captionLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    captionLabel.setColour (juce::Label::textColourId, captionColour);
    captionLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (captionLabel);

    sweepMeter = std::make_unique<SweepMeter> (processorRef);
    addAndMakeVisible (sweepMeter.get());

    //--- knobs ------------------------------------------------------------------
    setupKnob (rateSlider, rateLabel, "RATE");
    setupKnob (depthSlider, depthLabel, "DEPTH");
    setupKnob (ratioSlider, ratioLabel, "RATIO");
    setupKnob (depth2Slider, depth2Label, "DEPTH 2");
    setupKnob (centreSlider, centreLabel, "CENTRE");
    setupKnob (feedbackSlider, feedbackLabel, "FEEDBACK");
    setupKnob (widthSlider, widthLabel, "WIDTH");
    setupKnob (mixSlider, mixLabel, "MIX");
    setupKnob (outputSlider, outputLabel, "OUTPUT");

    rateSlider.textFromValueFunction = [] (double v)
    {
        return v < 1.0 ? juce::String (v, 2) + " Hz" : juce::String (v, 1) + " Hz";
    };
    rateSlider.valueFromTextFunction = [] (const juce::String& t) { return (double) t.getFloatValue(); };

    auto percentText = [] (double v)
    {
        return juce::String ((int) std::lround (v * 100.0)) + " %";
    };
    auto percentValue = [] (const juce::String& t)
    {
        return (double) juce::jlimit (0.0f, 1.0f, t.getFloatValue() / 100.0f);
    };

    for (auto* slider : { &depthSlider, &depth2Slider, &mixSlider })
    {
        slider->textFromValueFunction = percentText;
        slider->valueFromTextFunction = percentValue;
    }

    ratioSlider.textFromValueFunction = [] (double v)
    {
        return "x" + juce::String (v, 2);
    };
    ratioSlider.valueFromTextFunction = [] (const juce::String& t) { return (double) t.getFloatValue(); };
    ratioSlider.setDoubleClickReturnValue (true, 1.618);

    centreSlider.textFromValueFunction = [] (double v)
    {
        return juce::String ((int) std::lround (v)) + " Hz";
    };
    centreSlider.valueFromTextFunction = [] (const juce::String& t) { return (double) t.getFloatValue(); };

    feedbackSlider.textFromValueFunction = [] (double v)
    {
        return juce::String::formatted ("%+d %%", (int) std::lround (v / 0.97 * 100.0));
    };
    feedbackSlider.valueFromTextFunction = [] (const juce::String& t)
    {
        return (double) juce::jlimit (-0.97f, 0.97f, t.getFloatValue() / 100.0f * 0.97f);
    };
    feedbackSlider.setDoubleClickReturnValue (true, 0.0);

    widthSlider.textFromValueFunction = [] (double v)
    {
        return juce::String ((int) std::lround (v)) + " deg";
    };
    widthSlider.valueFromTextFunction = [] (const juce::String& t) { return (double) t.getFloatValue(); };

    outputSlider.textFromValueFunction = [] (double v)
    {
        return juce::String (v, 1) + " dB";
    };
    outputSlider.valueFromTextFunction = [] (const juce::String& t) { return (double) t.getFloatValue(); };

    rateAttachment     = std::make_unique<SliderAttachment> (processorRef.apvts, "rate", rateSlider);
    depthAttachment    = std::make_unique<SliderAttachment> (processorRef.apvts, "depth", depthSlider);
    ratioAttachment    = std::make_unique<SliderAttachment> (processorRef.apvts, "ratio", ratioSlider);
    depth2Attachment   = std::make_unique<SliderAttachment> (processorRef.apvts, "depth2", depth2Slider);
    centreAttachment   = std::make_unique<SliderAttachment> (processorRef.apvts, "centre", centreSlider);
    feedbackAttachment = std::make_unique<SliderAttachment> (processorRef.apvts, "feedback", feedbackSlider);
    widthAttachment    = std::make_unique<SliderAttachment> (processorRef.apvts, "width", widthSlider);
    mixAttachment      = std::make_unique<SliderAttachment> (processorRef.apvts, "mix", mixSlider);
    outputAttachment   = std::make_unique<SliderAttachment> (processorRef.apvts, "output", outputSlider);

    //--- STAGES / WAVE segmented selectors ---------------------------------------
    stagesLabel.setText ("STAGES", juce::dontSendNotification);
    stagesLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    stagesLabel.setColour (juce::Label::textColourId, captionColour);
    stagesLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (stagesLabel);

    const char* stageTexts[4] = { "4", "6", "8", "12" };

    for (int i = 0; i < 4; ++i)
    {
        setupSegment (stageButtons[i], stageTexts[i], 101);
        stageButtons[i].onClick = [this, i]
        {
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (
                    processorRef.apvts.getParameter ("stages")))
                *choice = i;
        };
    }

    if (stageButtons[0].getRadioGroupId() != 0)
    {
        stageButtons[0].setConnectedEdges (juce::Button::ConnectedOnRight);
        stageButtons[1].setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
        stageButtons[2].setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
        stageButtons[3].setConnectedEdges (juce::Button::ConnectedOnLeft);
    }

    waveLabel.setText ("WAVE", juce::dontSendNotification);
    waveLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    waveLabel.setColour (juce::Label::textColourId, captionColour);
    waveLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (waveLabel);

    const char* waveTexts[2] = { "SINE", "TRI" };

    for (int i = 0; i < 2; ++i)
    {
        setupSegment (waveButtons[i], waveTexts[i], 102);
        waveButtons[i].onClick = [this, i]
        {
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (
                    processorRef.apvts.getParameter ("wave")))
                *choice = i;
        };
    }

    waveButtons[0].setConnectedEdges (juce::Button::ConnectedOnRight);
    waveButtons[1].setConnectedEdges (juce::Button::ConnectedOnLeft);

    swirlLabel.setText ("DUAL LFO SWIRL", juce::dontSendNotification);
    swirlLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    swirlLabel.setColour (juce::Label::textColourId, captionColour);
    swirlLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (swirlLabel);

    //--- MIDI strip ---------------------------------------------------------------
    onOffButton.setButtonText ("ON");
    onOffButton.onClick = [this]
    {
        if (auto* bypass = dynamic_cast<juce::AudioParameterBool*> (
                processorRef.apvts.getParameter ("bypass")))
            *bypass = ! bypass->get();
        refreshState();
    };
    addAndMakeVisible (onOffButton);

    bypassCcTitleLabel.setText ("BYPASS CC", juce::dontSendNotification);
    bypassCcTitleLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    bypassCcTitleLabel.setColour (juce::Label::textColourId, captionColour);
    bypassCcTitleLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (bypassCcTitleLabel);

    bypassCcLabel.setText ("65", juce::dontSendNotification);
    bypassCcLabel.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    bypassCcLabel.setColour (juce::Label::textColourId, textColour);
    bypassCcLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (bypassCcLabel);

    learnButton.setButtonText ("LEARN");
    learnButton.onClick = [this]
    {
        if (suppressLearnClick)
        {
            suppressLearnClick = false;
            return;
        }

        const auto target = processorRef.getLearnTarget();
        processorRef.setLearnTarget (target == YMVortexProcessor::LearnTarget::bypass
                                         ? YMVortexProcessor::LearnTarget::none
                                         : YMVortexProcessor::LearnTarget::bypass);
        refreshState();
    };
    learnRightClick.onRightClick = [this]
    {
        // Right-click LEARN: clear the assignment (and swallow the
        // left-click that may follow from the same press).
        suppressLearnClick = true;
        processorRef.setBypassCcNumber (0);
        processorRef.setLearnTarget (YMVortexProcessor::LearnTarget::none);
        refreshState();
    };
    learnButton.addMouseListener (&learnRightClick, false);
    addAndMakeVisible (learnButton);

    midiHintLabel.setText ("CC >= 64: ON / < 64: OFF - right-click LEARN to clear",
                           juce::dontSendNotification);
    midiHintLabel.setFont (juce::Font (juce::FontOptions (10.0f)));
    midiHintLabel.setColour (juce::Label::textColourId, captionColour);
    midiHintLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (midiHintLabel);

    refreshState();
    startTimerHz (12);

    setSize (600, 460);
}

YMVortexEditor::~YMVortexEditor()
{
    learnButton.removeMouseListener (&learnRightClick);
    stopTimer();
}

void YMVortexEditor::setupKnob (juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 18);
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

void YMVortexEditor::setupSegment (juce::TextButton& button, const juce::String& text, int radioGroup)
{
    button.setButtonText (text);
    button.setClickingTogglesState (true);
    button.setRadioGroupId (radioGroup);
    button.setColour (juce::TextButton::buttonColourId, knobColour);
    button.setColour (juce::TextButton::buttonOnColourId, headerColour);
    button.setColour (juce::TextButton::textColourOffId, textColour);
    button.setColour (juce::TextButton::textColourOnId, panelColour);
    addAndMakeVisible (button);
}

void YMVortexEditor::timerCallback()
{
    refreshState();
}

void YMVortexEditor::refreshState()
{
    // Segmented selectors follow the parameters (host automation, state
    // loads and other editors included).
    const int stageIdx = (int) processorRef.apvts.getRawParameterValue ("stages")->load();

    for (int i = 0; i < 4; ++i)
        stageButtons[i].setToggleState (stageIdx == i, juce::dontSendNotification);

    const int waveIdx = (int) processorRef.apvts.getRawParameterValue ("wave")->load();

    for (int i = 0; i < 2; ++i)
        waveButtons[i].setToggleState (waveIdx == i, juce::dontSendNotification);

    // ON/OFF button: lit amber "ON" while active, dim red "OFF" while
    // bypassed. Recolour only on change.
    const bool bypassed = processorRef.apvts.getRawParameterValue ("bypass")->load() > 0.5f;
    const int state = bypassed ? 0 : 1;

    if (state != lastOnOffState)
    {
        lastOnOffState = state;
        onOffButton.setButtonText (bypassed ? "OFF" : "ON");
        onOffButton.setColour (juce::TextButton::buttonColourId,
                               bypassed ? offColour : headerColour);
        onOffButton.setColour (juce::TextButton::textColourOffId,
                               bypassed ? textColour : panelColour);
    }

    const int cc = (int) processorRef.apvts.getRawParameterValue ("bypassCc")->load();
    bypassCcLabel.setText (cc == 0 ? "OFF" : juce::String (cc), juce::dontSendNotification);

    const bool learning = processorRef.getLearnTarget() != YMVortexProcessor::LearnTarget::none;
    learnButton.setButtonText (learning ? "TAP CC..." : "LEARN");
    learnButton.setColour (juce::TextButton::buttonColourId, learning ? headerColour : knobColour);
    learnButton.setColour (juce::TextButton::textColourOffId, learning ? panelColour : textColour);
}

//==============================================================================
void YMVortexEditor::paint (juce::Graphics& g)
{
    g.fillAll (panelColour);

    // Thin accent line under the header, matching the YM panel style.
    g.setColour (headerColour.withAlpha (0.55f));
    g.fillRect (16, 58, getWidth() - 32, 2);

    // Divider above the MIDI strip.
    g.setColour (padLineColour);
    g.fillRect (16, getHeight() - 62, getWidth() - 32, 1);
}

void YMVortexEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto header = area.removeFromTop (44);
    titleLabel.setBounds (header.removeFromLeft (230));
    captionLabel.setBounds (header.withTrimmedTop (16));

    area.removeFromTop (14);

    sweepMeter->setBounds (area.removeFromTop (38));

    area.removeFromTop (10);

    auto selectorRow = area.removeFromTop (28);
    stagesLabel.setBounds (selectorRow.removeFromLeft (58));

    for (int i = 0; i < 4; ++i)
    {
        stageButtons[i].setBounds (selectorRow.removeFromLeft (48));
        selectorRow.removeFromLeft (i < 3 ? 2 : 14);
    }

    waveLabel.setBounds (selectorRow.removeFromLeft (44));

    for (int i = 0; i < 2; ++i)
    {
        waveButtons[i].setBounds (selectorRow.removeFromLeft (58));
        selectorRow.removeFromLeft (2);
    }

    // MIDI strip along the bottom.
    auto midiStrip = area.removeFromBottom (38);
    area.removeFromBottom (8);

    onOffButton.setBounds (midiStrip.removeFromLeft (84));
    midiStrip.removeFromLeft (10);
    bypassCcTitleLabel.setBounds (midiStrip.removeFromLeft (72));
    bypassCcLabel.setBounds (midiStrip.removeFromLeft (40));
    learnButton.setBounds (midiStrip.removeFromLeft (76));
    midiHintLabel.setBounds (midiStrip);

    // Two knob rows of five cells.
    const int cellWidth = area.getWidth() / 5;
    const int rowHeight = area.getHeight() / 2;

    auto placeKnob = [] (juce::Rectangle<int> column, juce::Label& label, juce::Slider& slider)
    {
        label.setBounds (column.removeFromTop (20));
        slider.setBounds (column.reduced (6, 0));
    };

    auto row1 = area.removeFromTop (rowHeight);
    placeKnob (row1.removeFromLeft (cellWidth), rateLabel, rateSlider);
    placeKnob (row1.removeFromLeft (cellWidth), depthLabel, depthSlider);
    placeKnob (row1.removeFromLeft (cellWidth), ratioLabel, ratioSlider);
    placeKnob (row1.removeFromLeft (cellWidth), depth2Label, depth2Slider);
    placeKnob (row1, centreLabel, centreSlider);

    auto row2 = area;
    placeKnob (row2.removeFromLeft (cellWidth), feedbackLabel, feedbackSlider);
    placeKnob (row2.removeFromLeft (cellWidth), widthLabel, widthSlider);
    placeKnob (row2.removeFromLeft (cellWidth), mixLabel, mixSlider);
    placeKnob (row2.removeFromLeft (cellWidth), outputLabel, outputSlider);

    // Spare cell: a quiet caption, vertically centred like a knob label.
    auto spare = row2;
    spare.removeFromTop (rowHeight / 2 - 10);
    swirlLabel.setBounds (spare.removeFromTop (20));
}
