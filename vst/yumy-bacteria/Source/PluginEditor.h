#pragma once
#include "PluginProcessor.h"

// Provisional editor (stage a): the host-style generic parameter list. The panel GUI comes in stage b.
class YumyBacteriaEditor : public juce::GenericAudioProcessorEditor
{
public:
    explicit YumyBacteriaEditor (YumyBacteriaProcessor& p) : juce::GenericAudioProcessorEditor (p) { setSize (560, 640); }
};
