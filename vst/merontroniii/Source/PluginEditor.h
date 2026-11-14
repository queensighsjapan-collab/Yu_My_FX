#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Rotary knob bound to an APVTS parameter (vertical drag + wheel,
// double-click resets). Draws the mock's dark bakelite knob with a gold
// pointer; the label and 0-100 readout are drawn by the editor.
class TapeKnob final : public juce::Component
{
public:
    TapeKnob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, bool small);
    void refreshFromParam();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::RangedAudioParameter* param = nullptr;
    bool isSmall;
    float shownValue = 0.0f;
    float dragStartValue = 0.0f;
    int dragStartY = 0;
};

//==============================================================================
// One tape-set selector button (lamp + name), in panel order.
class PresetButton final : public juce::Component
{
public:
    PresetButton (juce::AudioProcessorValueTreeState& apvts, int uiIndex, const juce::String& label);
    void refreshFromParam();
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter* param = nullptr;
    int index;
    juce::String text;
    bool active = false;
};

//==============================================================================
// HALF SPEED lever switch bound to the halfSpeed bool parameter.
class HalfSwitch final : public juce::Component
{
public:
    explicit HalfSwitch (juce::AudioProcessorValueTreeState& apvts);
    void refreshFromParam();
    bool isOn() const noexcept { return on; }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter* param = nullptr;
    bool on = false;
};

//==============================================================================
// A spinning tape reel; the editor advances the angle while notes play.
class Reel final : public juce::Component
{
public:
    void setAngle (float radians) { angle = radians; }
    void paint (juce::Graphics&) override;

private:
    float angle = 0.0f;
};

//==============================================================================
// The 35-key preview keyboard (G2-F5) in the mock's colours.
class TapeKeyboard final : public juce::MidiKeyboardComponent
{
public:
    using juce::MidiKeyboardComponent::MidiKeyboardComponent;
    void drawWhiteNote (int midiNote, juce::Graphics&, juce::Rectangle<float>,
                        bool isDown, bool isOver, juce::Colour lineColour,
                        juce::Colour textColour) override;
    void drawBlackNote (int midiNote, juce::Graphics&, juce::Rectangle<float>,
                        bool isDown, bool isOver, juce::Colour noteFillColour) override;
};

//==============================================================================
class MeronTronIIIEditor final : public juce::AudioProcessorEditor,
                                 private juce::Timer,
                                 private juce::MidiKeyboardState::Listener
{
public:
    explicit MeronTronIIIEditor (MeronTronIIIProcessor&);
    ~MeronTronIIIEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool) override;

private:
    void timerCallback() override;
    void handleNoteOn (juce::MidiKeyboardState*, int channel, int midiNote, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int channel, int midiNote, float velocity) override;
    void syncKeyboardFromProcessor();
    juce::String lcdNoteLine() const;
    static juce::String noteName (int midi);

    MeronTronIIIProcessor& processor;

    juce::MidiKeyboardState keyboardState;
    TapeKeyboard keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
    juce::uint64 keyboardMask = 0;
    bool syncingKeyboard = false;

    Reel reelL, reelR;
    float angleL = 0.0f, angleR = 0.0f;
    juce::uint32 lastTickMs = 0;

    std::array<std::unique_ptr<PresetButton>, 5> presetBtns;
    std::unique_ptr<TapeKnob> knobVolume, knobTone, knobAttack, knobRelease, knobReverb;
    std::unique_ptr<HalfSwitch> halfSwitch;

    // Layout (1120x660, from the approved mock)
    static constexpr int panelW = 1120, panelH = 660;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeronTronIIIEditor)
};
