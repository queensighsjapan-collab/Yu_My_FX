// YM303 plug-in editor: the same landscape panel as the browser version (synthesizer/yu-my-303.html).
// The panel is designed at a fixed size and scaled to the window (aspect ratio locked).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include "PluginProcessor.h"

class YM303Editor : public juce::AudioProcessorEditor
{
public:
    explicit YM303Editor (YM303Processor&);
    ~YM303Editor() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isDown) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }

    static constexpr int designW = 1248, designH = 634;

private:
    class Panel;
    YM303Processor& proc;
    std::unique_ptr<Panel> panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YM303Editor)
};
