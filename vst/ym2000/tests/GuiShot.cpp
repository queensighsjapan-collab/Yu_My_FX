// Renders the plug-in editor to a PNG (headless) so the layout can be checked without a DAW.
#include "../Source/PluginEditor.h"
#include <cstdio>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    YM2000Processor p;
    p.prepareToPlay (48000, 256);
    if (argc > 2) p.setCurrentProgram (std::atoi (argv[2]));
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    ed->setSize (1716, 930);
    juce::Time::waitForMillisecondCounter (juce::Time::getMillisecondCounter() + 50);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File out (argc > 1 ? argv[1] : "shot.png");
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, os);
    std::printf ("wrote %s (%d x %d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return 0;
}
