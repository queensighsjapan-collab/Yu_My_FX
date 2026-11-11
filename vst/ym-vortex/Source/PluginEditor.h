#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/** Two-lane sweep display: a dot per channel travels along a track as
    the combined LFO modulation value moves (-1..+1). With WIDTH at 90
    the dots travel in quadrature; at 180 they mirror each other.
    Pure decoration - reads the processor's modulation atomics. */
class SweepMeter : public juce::Component,
                   private juce::Timer
{
public:
    explicit SweepMeter (YMVortexProcessor& processor);
    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }
    YMVortexProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SweepMeter)
};

//==============================================================================
/** YM Vortex editor: dark panel in the YM house style. Nine rotary
    knobs (RATE / DEPTH / RATIO / DEPTH2 / CENTRE / FEEDBACK / WIDTH /
    MIX / OUTPUT), segmented STAGES and WAVE selectors, a sweep meter,
    and the house-standard MIDI strip (ON/OFF button, BYPASS CC
    readout, LEARN). Everything is bound to the APVTS. English-only. */
// Dedicated right-click watcher for the LEARN button (juce::Component
// already is a MouseListener, so the editor cannot inherit it again).
struct LearnRightClickListener : public juce::MouseListener
{
    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown() && onRightClick != nullptr)
            onRightClick();
    }
};

class YMVortexEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit YMVortexEditor (YMVortexProcessor& processor);
    ~YMVortexEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    void setupKnob (juce::Slider& slider, juce::Label& label, const juce::String& text);
    void setupSegment (juce::TextButton& button, const juce::String& text, int radioGroup);
    void refreshState();   // segmented buttons, ON/OFF, CC readout, LEARN

    YMVortexProcessor& processorRef;

    juce::Label titleLabel;
    juce::Label captionLabel;

    std::unique_ptr<SweepMeter> sweepMeter;

    juce::Label rateLabel, depthLabel, ratioLabel, depth2Label, centreLabel;
    juce::Label feedbackLabel, widthLabel, mixLabel, outputLabel;
    juce::Slider rateSlider, depthSlider, ratioSlider, depth2Slider, centreSlider;
    juce::Slider feedbackSlider, widthSlider, mixSlider, outputSlider;

    juce::Label stagesLabel;
    juce::TextButton stageButtons[4];
    juce::Label waveLabel;
    juce::TextButton waveButtons[2];

    juce::Label swirlLabel;

    // MIDI strip (house standard bypass).
    juce::TextButton onOffButton;
    juce::Label bypassCcTitleLabel;
    juce::Label bypassCcLabel;
    juce::TextButton learnButton;
    LearnRightClickListener learnRightClick;
    juce::Label midiHintLabel;
    bool suppressLearnClick = false;
    int lastOnOffState = -1;   // 1 = active, 0 = bypassed, -1 = unknown

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> rateAttachment, depthAttachment;
    std::unique_ptr<SliderAttachment> ratioAttachment, depth2Attachment;
    std::unique_ptr<SliderAttachment> centreAttachment, feedbackAttachment;
    std::unique_ptr<SliderAttachment> widthAttachment, mixAttachment;
    std::unique_ptr<SliderAttachment> outputAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YMVortexEditor)
};
