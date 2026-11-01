// Renders the plug-in editor to a PNG (headless) so the layout can be checked without a DAW.
//   usage: YM303_GuiShot out.png [width] [state]   state: 0 default, 1 playing step highlight + track mode demo
#include "../Source/PluginProcessor.h"
#include <cstdio>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    YM303Processor p;
    p.setPlayConfigDetails (0, 2, 48000, 256);
    p.prepareToPlay (48000, 256);
    const int state = argc > 3 ? std::atoi (argv[3]) : 0;
    if (state == 1)
    {
        p.ui.cursor = 7;
        auto set = [&] (const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
        set ("pattern", 2); set ("section", 2);
    }
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    if (state == 2)      // keyboard-driven interaction test
    {
        int fails = 0;
        auto check = [&] (bool ok, const char* w) { std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", w); if (! ok) ++fails; };
        auto key = [&] (char c) { ed->keyPressed (juce::KeyPress (c, juce::ModifierKeys(), c)); ed->keyStateChanged (false); };
        p.bank.clearAll(); p.refreshEditView();
        key ('a'); key ('d'); key ('k'); key ('w');
        check (p.bank.getStep (0, 0, 0).type == ym303::Note && p.bank.getStep (0, 0, 0).note == 0, "key A writes C at step 1");
        check (p.bank.getStep (0, 0, 1).note == 4, "key D writes E at step 2");
        check (p.bank.getStep (0, 0, 2).note == 12, "key K writes high C at step 3");
        check (p.bank.getStep (0, 0, 3).note == 1, "key W writes C# at step 4");
        check (p.ui.cursor == 4, "cursor advanced to step 5");
        auto* ts = p.apvts.getParameter ("s02_type");
        check (std::abs (ts->convertFrom0to1 (ts->getValue()) - 6.0f) < 0.01f, "step parameter mirrors the written note (E = choice 6)");
        p.ui.pitch = false;                                   // normal mode: white keys 1-8 pick the pattern
        key ('s');                                            // 2nd white key -> pattern 2
        check ((int) p.apvts.getRawParameterValue ("pattern")->load() == 2, "normal mode: white key selects pattern 2");
        p.ui.pitch = true;
        key (' ');
        check (p.apvts.getRawParameterValue ("run")->load() > 0.5f, "space starts RUN");
        key (' ');
        check (p.apvts.getRawParameterValue ("run")->load() < 0.5f, "space stops RUN");
        std::printf (fails ? "GUI TEST FAILED\n" : "GUI TEST OK\n");
        return fails ? 1 : 0;
    }
    const int w = argc > 2 ? std::atoi (argv[2]) : 1248;
    ed->setSize (w, (int) (w * 634.0 / 1248.0));
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File out (argc > 1 ? argv[1] : "shot.png");
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, os);
    std::printf ("wrote %s (%d x %d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return 0;
}
