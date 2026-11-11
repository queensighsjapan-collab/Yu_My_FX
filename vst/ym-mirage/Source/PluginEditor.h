#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/** Decorative strip: dots drift and fade like grains in a cloud. Their
    brightness follows DENSITY; while FREEZE is on they stop drifting and
    turn amber. Pure decoration - reads parameters only. */
class CloudCanvas : public juce::Component,
                    private juce::Timer
{
public:
    explicit CloudCanvas (YMMirageProcessor& processor);

    void paint (juce::Graphics& g) override;

private:
    struct Dot
    {
        float x = 0.0f, y = 0.0f;      // normalised position
        float phase = 0.0f;            // 0..1 fade cycle
        float drift = 0.0f;            // x speed per tick
        float radius = 3.0f;
    };

    void timerCallback() override;
    void resetDot (Dot& dot, bool randomPhase);

    YMMirageProcessor& processor;
    std::atomic<float>* densityParam = nullptr;
    std::atomic<float>* freezeParam = nullptr;
    std::array<Dot, 42> dots;
    juce::Random rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CloudCanvas)
};

//==============================================================================
/** A TextButton that also reports right-clicks (used by the LEARN
    buttons: left-click arms MIDI Learn, right-click clears the
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
/** YM Mirage editor: dark panel in the YM house style. Seven rotary
    knobs (SIZE / DENSITY / PITCH / SPREAD / SMEAR / MIX / OUTPUT), a
    wide latching FREEZE button with the ON/BYPASS power button under
    it, the cloud canvas strip, and a MIDI CC strip (assignment
    readouts + LEARN buttons for PITCH / FREEZE / BYPASS).
    Everything is bound to the APVTS. English-only text. */
class YMMirageEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit YMMirageEditor (YMMirageProcessor& processor);
    ~YMMirageEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshMidiStrip();
    void setupLearnButton (LearnButton& button,
                           YMMirageProcessor::LearnTarget target,
                           std::function<void (int)> clearAssignment);
    static juce::String ccReadoutText (std::atomic<float>* rawParam);

    YMMirageProcessor& processorRef;

    juce::Label titleLabel;
    juce::Label captionLabel;
    juce::Label hintLabel;

    std::unique_ptr<CloudCanvas> canvas;

    juce::Label sizeLabel, densityLabel, pitchLabel, spreadLabel;
    juce::Label smearLabel, mixLabel, outputLabel;
    juce::Slider sizeSlider, densitySlider, pitchSlider, spreadSlider;
    juce::Slider smearSlider, mixSlider, outputSlider;

    juce::TextButton freezeButton;
    juce::TextButton powerButton;

    // MIDI CC strip
    juce::Label pitchCcLabel, freezeCcLabel, bypassCcLabel;
    juce::Label pitchCcReadout, freezeCcReadout, bypassCcReadout;
    LearnButton pitchLearnButton, freezeLearnButton, bypassLearnButton;

    std::atomic<float>* pitchCcRaw  = nullptr;
    std::atomic<float>* freezeCcRaw = nullptr;
    std::atomic<float>* bypassCcRaw = nullptr;
    std::atomic<float>* bypassRaw   = nullptr;
    bool lastBypassShown = false;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> sizeAttachment, densityAttachment;
    std::unique_ptr<SliderAttachment> pitchAttachment, spreadAttachment;
    std::unique_ptr<SliderAttachment> smearAttachment, mixAttachment;
    std::unique_ptr<SliderAttachment> outputAttachment;

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<ButtonAttachment> freezeAttachment;

    void setupKnob (juce::Slider& slider, juce::Label& label, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YMMirageEditor)
};
