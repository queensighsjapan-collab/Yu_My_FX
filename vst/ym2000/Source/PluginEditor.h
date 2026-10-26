#pragma once
#include "Gui.h"
#include "PluginProcessor.h"

class YM2000Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit YM2000Editor (YM2000Processor&);
    ~YM2000Editor() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (ymgui::pal::body); }

    static constexpr int designW = 1716, designH = 972;

private:
    struct Backdrop : juce::Component
    {
        Backdrop();
        void paint (juce::Graphics&) override;
    };

    void timerCallback() override;
    juce::String programLabel (int i) const;

    YM2000Processor& proc;
    juce::LookAndFeel_V4 laf; // must outlive the components that use it
    Backdrop backdrop;
    juce::Component content;
    ymgui::Lcd lcd;
    juce::OwnedArray<juce::Component> owned;
    std::unique_ptr<ymgui::Knob> programKnob;
    std::unique_ptr<juce::MidiKeyboardComponent> keyboard;
    juce::OwnedArray<juce::ComboBoxParameterAttachment> comboAtts;
    int shownProgram = -1;
    int octave = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YM2000Editor)
};
