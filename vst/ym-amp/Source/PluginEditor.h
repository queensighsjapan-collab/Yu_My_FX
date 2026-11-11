#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/** Metal rack knob look: dark gradient body, gold pointer, no arc. */
class RackLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RackLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

//==============================================================================
/** A rotary slider with painted tick marks, a value readout and a
    caption. Two layouts: "stacked" (dial on top, value + label below)
    and "mini" (dial left, value + label to its right) for the FX band. */
class RackKnob : public juce::Component
{
public:
    RackKnob (const juce::String& labelText,
              std::function<juce::String (double)> formatter,
              bool miniLayout = false);

    juce::Slider slider;

    void resized() override;
    void paint (juce::Graphics& g) override;
    void refreshValue();

private:
    juce::String labelText;
    std::function<juce::String (double)> formatValue;
    juce::String valueText;
    bool mini = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackKnob)
};

//==============================================================================
/** LCD readout: amber style (dark text on amber) or dark style (amber
    text on near-black), two lines, monospace. */
class LcdDisplay : public juce::Component
{
public:
    explicit LcdDisplay (bool amberStyle = true);

    void setLines (const juce::String& line1, const juce::String& line2);
    void setFontSizes (float line1Size, float line2Size);
    void paint (juce::Graphics& g) override;

private:
    juce::String text1, text2;
    bool amber = true;
    float size1 = 15.0f, size2 = 10.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LcdDisplay)
};

//==============================================================================
/** Vertical 8-segment LED meter. */
class LedMeter : public juce::Component
{
public:
    LedMeter() = default;

    void setLevel (float newLevel);
    void paint (juce::Graphics& g) override;

private:
    float level = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LedMeter)
};

//==============================================================================
/** Small round LED indicator. */
class LedDot : public juce::Component
{
public:
    LedDot() = default;

    void setOn (bool shouldBeOn);
    void paint (juce::Graphics& g) override;

private:
    bool on = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LedDot)
};

//==============================================================================
/** A TextButton that also reports right-clicks (LEARN: left-click arms
    MIDI Learn, right-click clears the assignment to Off). */
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

            return;
        }

        juce::TextButton::mouseDown (e);
    }
};

//==============================================================================
/** Painted triangle button (preset prev/next). */
class RackArrowButton : public juce::Button
{
public:
    explicit RackArrowButton (bool pointingLeft) : juce::Button ({}), left (pointingLeft) {}

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

private:
    bool left;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackArrowButton)
};

//==============================================================================
/** Painted power switch (lever + slot). State mirrors the bypass
    parameter (on = effect active). */
class PowerSwitch : public juce::Button
{
public:
    PowerSwitch() : juce::Button ({}) {}

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PowerSwitch)
};

//==============================================================================
/** YM Amp editor: a fixed-size 1240 x 430 rackmount faceplate matching
    the approved HTML mock - dark charcoal brushed metal, corner
    screws, gold brand block, amber model LCD, GUITAR/FILE source
    selector with stem-player LCD, five big tone knobs, CAB section,
    GATE/MASTER + LED meters, a BUILT-IN FX band (BOOST / CHORUS /
    DELAY / REVERB) and the MIDI CC bottom strip. English only. */
class YMAmpEditor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    explicit YMAmpEditor (YMAmpProcessor& processor);
    ~YMAmpEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshAll();
    void chooseModel();
    void chooseIr();
    void chooseAudioFile();
    void askSavePreset();

    YMAmpProcessor& processorRef;
    RackLookAndFeel rackLookAndFeel;

    //--- helpers ---------------------------------------------------------
    RackKnob* addKnob (const juce::String& paramId, const juce::String& label,
                       std::function<juce::String (double)> formatter, bool mini = false);
    juce::TextButton* addMetalButton (const juce::String& text);
    juce::Label* addSectionLabel (const juce::String& text);
    juce::Label* addLabel (const juce::String& text, float fontSize,
                           juce::Colour colour, juce::Justification justification);

    std::vector<std::unique_ptr<RackKnob>> knobs;
    std::map<juce::String, RackKnob*> knobById;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<juce::TextButton>> ownedButtons;
    std::vector<std::unique_ptr<juce::Label>> ownedLabels;

    //--- brand / power -----------------------------------------------------
    juce::Label* brandLabel = nullptr;
    juce::Label* brandSubLabel = nullptr;
    juce::Label* powerStateLabel = nullptr;
    PowerSwitch powerSwitch;

    //--- input section -------------------------------------------------------
    juce::TextButton* guitarButton = nullptr;
    juce::TextButton* fileButton = nullptr;
    LedMeter inputMeter;
    juce::Label* inputMeterCaption = nullptr;
    LcdDisplay fileLcd { false };
    juce::TextButton* fileLoadButton = nullptr;
    juce::TextButton* filePlayButton = nullptr;
    juce::TextButton* fileLoopButton = nullptr;

    //--- model section ---------------------------------------------------------
    LcdDisplay modelLcd { true };
    juce::TextButton* loadModelButton = nullptr;
    RackArrowButton presetPrevButton { true };
    RackArrowButton presetNextButton { false };
    LcdDisplay presetLcd { true };
    juce::TextButton* savePresetButton = nullptr;
    juce::Label* presetNoteLabel = nullptr;

    //--- cab section ---------------------------------------------------------
    LcdDisplay irLcd { true };
    juce::TextButton* loadIrButton = nullptr;
    juce::TextButton* cabToggleButton = nullptr;

    //--- output section ---------------------------------------------------------
    LedMeter outputMeter;
    juce::Label* outputMeterCaption = nullptr;

    //--- FX band -------------------------------------------------------------------
    struct FxBlock
    {
        juce::TextButton toggle;
        LedDot led;
        juce::String onParamId;
    };
    std::array<FxBlock, 4> fxBlocks;

    //--- bottom strip ------------------------------------------------------------------
    juce::Label* midiCaptionLabel = nullptr;
    juce::Label* midiReadoutLabel = nullptr;
    juce::Label* midiBypassLabel = nullptr;
    LearnButton learnButton;
    juce::Label* statusLabel = nullptr;
    juce::Label* versionLabel = nullptr;

    //--- state caches ---------------------------------------------------------------------
    float inputMeterShown = 0.0f, outputMeterShown = 0.0f;
    bool lastPowerShown = false;
    int lastPresetShown = -1;

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> saveWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YMAmpEditor)
};
