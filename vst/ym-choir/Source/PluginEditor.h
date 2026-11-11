#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/** Decorative strip: a row of "singer" dots. As many dots are lit as
    VOICES adds singers; lit dots spread sideways with SPREAD and bob
    gently, each at its own pace. Pure decoration - reads parameters
    only. */
class ChoirCanvas : public juce::Component,
                    private juce::Timer
{
public:
    explicit ChoirCanvas (YMChoirProcessor& processor);

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;

    YMChoirProcessor& processor;
    std::atomic<float>* voicesParam = nullptr;
    std::atomic<float>* spreadParam = nullptr;
    int tick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoirCanvas)
};

//==============================================================================
/** A TextButton that also reports right-clicks (used by the LEARN
    button: left-click arms MIDI Learn, right-click clears the
    assignment to Off). */
class LearnButton : public juce::TextButton
{
public:
    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown())
        {
            if (onRightClick != nullptr)
                onRightClick();

            return;   // swallow: a right-click must not also "click"
        }

        juce::TextButton::mouseDown (e);
    }
};

//==============================================================================
/** YM Choir editor: dark panel in the YM house style. Eight rotary
    knobs (VOICES / DETUNE / TIMING / VIBRATO / SPREAD / TONE / MIX /
    OUTPUT), a singer-dots canvas strip, the ON/BYPASS power button
    and a MIDI CC strip (bypass assignment readout + LEARN button).
    Everything is bound to the APVTS. English-only text. */
class YMChoirEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit YMChoirEditor (YMChoirProcessor& processor);
    ~YMChoirEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshMidiStrip();
    static juce::String ccReadoutText (std::atomic<float>* rawParam);

    YMChoirProcessor& processorRef;

    juce::Label titleLabel;
    juce::Label captionLabel;
    juce::Label hintLabel;

    std::unique_ptr<ChoirCanvas> canvas;

    juce::Label voicesLabel, detuneLabel, timingLabel, vibratoLabel;
    juce::Label spreadLabel, toneLabel, mixLabel, outputLabel;
    juce::Slider voicesSlider, detuneSlider, timingSlider, vibratoSlider;
    juce::Slider spreadSlider, toneSlider, mixSlider, outputSlider;

    juce::TextButton powerButton;

    // MIDI CC strip
    juce::Label bypassCcLabel;
    juce::Label bypassCcReadout;
    LearnButton bypassLearnButton;

    std::atomic<float>* bypassCcRaw = nullptr;
    std::atomic<float>* bypassRaw   = nullptr;
    bool lastBypassShown = false;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> voicesAttachment, detuneAttachment;
    std::unique_ptr<SliderAttachment> timingAttachment, vibratoAttachment;
    std::unique_ptr<SliderAttachment> spreadAttachment, toneAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment, outputAttachment;

    void setupKnob (juce::Slider& slider, juce::Label& label, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YMChoirEditor)
};
