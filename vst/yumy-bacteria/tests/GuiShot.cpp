// Renders the plug-in editor to PNG files (headless) so the layout can be compared with the HTML panel without a DAW.
//   GuiShot <out.png> [program] [fold: 0|1] [width]
#include "../Source/PluginEditor.h"
#include <cstdio>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    YumyBacteriaProcessor p;
    p.prepareToPlay (48000, 256);
    if (argc > 2) p.setCurrentProgram (std::atoi (argv[2]));
    const bool open = argc > 3 && std::atoi (argv[3]) != 0;
    const int width = argc > 4 ? std::atoi (argv[4]) : YumyBacteriaEditor::designW;
    std::unique_ptr<YumyBacteriaEditor> ed (static_cast<YumyBacteriaEditor*> (p.createEditor()));
    ed->setFold (open, true);
    const int h = (int) std::lround ((double) ed->designHeight() * width / YumyBacteriaEditor::designW);
    ed->setSize (width, h);
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
