#pragma once
#include "PluginProcessor.h"
#include "Gui.h"

class YumyBacteriaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit YumyBacteriaEditor (YumyBacteriaProcessor&);
    ~YumyBacteriaEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void setFold (bool open, bool fromUser);       // SOUND DESIGN fold
    int designHeight() const { return designH; }

    static constexpr int designW = 1480;
    static constexpr int scrollW = 10;           // vertical scroll bar width (only reserved while SOUND DESIGN is open)

private:
    void timerCallback() override;
    void build();
    void applyRandom (yb::PatchState st);
    void showProgram (int idx);

    YumyBacteriaProcessor& proc;
    ybgui::YbLaf laf;
    juce::Viewport vp;
    juce::Component holder, content;
    juce::OwnedArray<juce::Component> owned;
    juce::ComponentBoundsConstrainer constrainer;

    ybgui::Lcd lcd;
    ybgui::SelectField* programBox = nullptr;
    ybgui::Meter* meter = nullptr;
    ybgui::TextLabel* vstat = nullptr;
    ybgui::TextLabel* sdInfo = nullptr;
    ybgui::FlatButton* sdBtn = nullptr;
    ybgui::PlainSlider* rInt = nullptr;
    std::vector<juce::Component*> sdComps;         // everything inside the fold
    std::vector<juce::Component*> rails;
    juce::Component *foot = nullptr, *panel2 = nullptr;
    int baseH = 0, sdH = 0, designH = 0;
    float scale = 1400.0f / (float) designW;
    bool sdOpen = false;

    // randomizer state
    yb::rnd::Rng rng;
    std::vector<yb::PatchState> history;
    bool locked[6] {};
    int rcat = 0, rndCount = 0;
    juce::String lcdName;
    int lastProg = -1, quietFrames = 0;
    float level = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YumyBacteriaEditor)
};
