#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const juce::Colour panelColour   { 0xff20202a };
    const juce::Colour headerColour  { 0xffffb84d };   // YM accent amber
    const juce::Colour textColour    { 0xffe8e8ee };
    const juce::Colour captionColour { 0xff9a9aa8 };
    const juce::Colour knobColour    { 0xff3a3a48 };
}

//==============================================================================
YMEchoEditor::YMEchoEditor (YMEchoProcessor& p)
    : AudioProcessorEditor (p), processorRef (p)
{
    titleLabel.setText ("YM ECHO", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, headerColour);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    captionLabel.setText ("TEST DELAY", juce::dontSendNotification);
    captionLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    captionLabel.setColour (juce::Label::textColourId, captionColour);
    captionLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (captionLabel);

    setupKnob (timeSlider,     timeLabel,     "Time");
    setupKnob (feedbackSlider, feedbackLabel, "Feedback");
    setupKnob (mixSlider,      mixLabel,      "Mix");

    pingPongButton.setColour (juce::ToggleButton::textColourId, textColour);
    pingPongButton.setColour (juce::ToggleButton::tickColourId, headerColour);
    addAndMakeVisible (pingPongButton);

    timeAttachment     = std::make_unique<SliderAttachment> (processorRef.apvts, "time",     timeSlider);
    feedbackAttachment = std::make_unique<SliderAttachment> (processorRef.apvts, "feedback", feedbackSlider);
    mixAttachment      = std::make_unique<SliderAttachment> (processorRef.apvts, "mix",      mixSlider);
    pingPongAttachment = std::make_unique<ButtonAttachment> (processorRef.apvts, "pingpong", pingPongButton);

    setSize (420, 300);
}

YMEchoEditor::~YMEchoEditor() = default;

void YMEchoEditor::setupKnob (juce::Slider& slider, juce::Label& label, const juce::String& text)
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
void YMEchoEditor::paint (juce::Graphics& g)
{
    g.fillAll (panelColour);

    // Thin accent line under the header, matching the YM panel style.
    g.setColour (headerColour.withAlpha (0.55f));
    g.fillRect (16, 58, getWidth() - 32, 2);
}

void YMEchoEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto header = area.removeFromTop (44);
    titleLabel.setBounds (header.removeFromLeft (170));
    captionLabel.setBounds (header.withTrimmedTop (16));

    area.removeFromTop (18);

    auto knobRow = area.removeFromTop (150);
    const int knobWidth = knobRow.getWidth() / 3;

    auto placeKnob = [] (juce::Rectangle<int> column, juce::Label& label, juce::Slider& slider)
    {
        label.setBounds (column.removeFromTop (20));
        slider.setBounds (column.reduced (6, 0));
    };

    placeKnob (knobRow.removeFromLeft (knobWidth), timeLabel, timeSlider);
    placeKnob (knobRow.removeFromLeft (knobWidth), feedbackLabel, feedbackSlider);
    placeKnob (knobRow, mixLabel, mixSlider);

    area.removeFromTop (10);
    pingPongButton.setBounds (area.removeFromTop (26).withSizeKeepingCentre (140, 26));
}
