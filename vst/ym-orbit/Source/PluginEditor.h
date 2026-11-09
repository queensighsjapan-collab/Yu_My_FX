#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/** The 2D positioning pad: a top-down room view. The listener's head sits
    at the centre (front = up); the dot is the sound source. Its angle sets
    the azimuth parameter, its radius the distance parameter. Dragging the
    dot writes both parameters (with change gestures); host automation or
    preset changes move the dot via the timer refresh.

    When MOTION is active the pad shows two dots: the base position the
    user set (dim) and the current effective position read from the
    processor's atomics (bright, with an outer ring). */
class PositionPad : public juce::Component,
                    private juce::Timer
{
public:
    PositionPad (YMOrbitProcessor& processor,
                 juce::RangedAudioParameter& azimuth,
                 juce::RangedAudioParameter& distance);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    void setFromPoint (juce::Point<float> p);
    juce::Point<float> getCentre() const;
    float getMaxRadius() const;
    float getAzimuthDeg() const;
    float getDistance01() const;
    juce::Point<float> pointFor (float azimuthDeg, float distance01) const;

    YMOrbitProcessor& processor;
    juce::RangedAudioParameter& azimuthParam;
    juce::RangedAudioParameter& distanceParam;
    juce::RangedAudioParameter* motionModeParam = nullptr;
    float lastAzimuth = 1.0e9f, lastDistance = 1.0e9f;
    float lastEffAzimuth = 1.0e9f, lastEffDistance = 1.0e9f;
    int lastMotionMode = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PositionPad)
};

//==============================================================================
/** YM Orbit editor: dark panel in the YM house style. The positioning pad
    is the centrepiece; AZIMUTH / DISTANCE / ROOM / WIDTH / OUTPUT are
    rotary knobs, ELEVATION a vertical slider, and a MOTION strip along
    the bottom carries the mode selector plus RATE / DEPTH knobs.
    Everything is bound to the APVTS. English-only text. */
class YMOrbitEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit YMOrbitEditor (YMOrbitProcessor& processor);
    ~YMOrbitEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    YMOrbitProcessor& processorRef;

    juce::Label titleLabel;
    juce::Label captionLabel;

    std::unique_ptr<PositionPad> pad;

    juce::Label azimuthReadout, elevationReadout, hintLabel;

    juce::Label elevationLabel;
    juce::Slider elevationSlider;

    juce::Label azimuthLabel, distanceLabel, roomLabel, widthLabel, outputLabel;
    juce::Slider azimuthSlider, distanceSlider, roomSlider, widthSlider, outputSlider;

    // MOTION strip.
    juce::Label motionLabel;
    juce::ComboBox motionModeBox;
    juce::Label rateLabel, depthLabel;
    juce::Slider rateSlider, depthSlider;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> azimuthAttachment, distanceAttachment;
    std::unique_ptr<SliderAttachment> elevationAttachment;
    std::unique_ptr<SliderAttachment> roomAttachment, widthAttachment, outputAttachment;
    std::unique_ptr<SliderAttachment> rateAttachment, depthAttachment;

    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<ComboBoxAttachment> motionModeAttachment;

    void setupKnob (juce::Slider& slider, juce::Label& label, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YMOrbitEditor)
};
