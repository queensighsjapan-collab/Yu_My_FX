#include "PluginEditor.h"
juce::AudioProcessorEditor* YumyBacteriaProcessor::createEditor() { return new YumyBacteriaEditor (*this); }
