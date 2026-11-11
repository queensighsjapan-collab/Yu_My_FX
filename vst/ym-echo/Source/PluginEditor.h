#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/** YM Echo editor: dark panel in the YM house style, three rotary knobs
    and a ping-pong toggle, all bound to the APVTS. English-only text. */
class YMEchoEditor : public juce::AudioProcessorEditor
{
public:
    explicit YMEchoEditor (YMEchoProcessor& processor);
    ~YMEchoEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    YMEchoProcessor& processorRef;

    juce::Label titleLabel;
    juce::Label captionLabel;

    juce::Label timeLabel, feedbackLabel, mixLabel;
    juce::Slider timeSlider, feedbackSlider, mixSlider;
    juce::ToggleButton pingPongButton { "Ping-Pong" };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> timeAttachment;
    std::unique_ptr<SliderAttachment> feedbackAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<ButtonAttachment> pingPongAttachment;

    void setupKnob (juce::Slider& slider, juce::Label& label, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YMEchoEditor)
};
