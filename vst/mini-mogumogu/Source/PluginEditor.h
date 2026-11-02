// mini mogumogu plug-in editor: the same 16:9 black brushed-aluminium panel as the browser version (synthesizer/mini-mogumogu.html).
// The panel is designed at 1920x1080 and scaled to the window (aspect ratio locked).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include "PluginProcessor.h"

class MoguEditor : public juce::AudioProcessorEditor
{
public:
    explicit MoguEditor (MoguProcessor&);
    ~MoguEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isDown) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }

    static constexpr int designW = 1920, designH = 1080;

private:
    class Panel;
    MoguProcessor& proc;
    std::unique_ptr<Panel> panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MoguEditor)
};
