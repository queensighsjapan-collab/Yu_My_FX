// Renders the plug-in editor to a PNG (headless) so the layout can be checked without a DAW.
//   usage: MiniMogumogu_GuiShot out.png [width] [state]   state: 0 default, 1 running + preset "Mogu Lead" + keys held
#include "../Source/PluginProcessor.h"
#include <cstdio>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    MoguProcessor p;
    p.setPlayConfigDetails (0, 2, 48000, 256);
    p.prepareToPlay (48000, 256);
    const int state = argc > 3 ? std::atoi (argv[3]) : 0;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    if (state == 1)
    {
        p.setCurrentProgram (2);
        p.setValue ("run", 1.f);
        p.keyState.noteOn (1, 48, 0.8f); p.keyState.noteOn (1, 55, 0.8f);
        juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer m;
        for (int i = 0; i < 40; ++i) { b.clear(); p.processBlock (b, m); }
    }
    const int w = argc > 2 ? std::atoi (argv[2]) : 1920;
    ed->setSize (w, (int) (w * 9.0 / 16.0));
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File out (argc > 1 ? argv[1] : "shot.png");
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, os);
    std::printf ("wrote %s (%d x %d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return 0;
}
