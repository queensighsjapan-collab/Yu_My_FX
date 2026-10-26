#include "PluginEditor.h"

using namespace ymgui;
using juce::Component;
using juce::Rectangle;
using juce::String;

// ------------------------------------------------------------------ static artwork
YM2000Editor::Backdrop::Backdrop()
{
    setInterceptsMouseClicks (false, false);
    setBufferedToImage (true);
}

void YM2000Editor::Backdrop::paint (juce::Graphics& g)
{
    using namespace juce;
    const float W = (float) designW, H = (float) designH;
    g.fillAll (pal::body);

    // wooden side cheeks
    for (int side = 0; side < 2; ++side)
    {
        Rectangle<float> r (side == 0 ? 0.0f : W - 36.0f, 0.0f, 36.0f, H);
        g.setGradientFill (ColourGradient (Colour (0xff9a6234), r.getX(), 0.0f, Colour (0xff6b3f1e), r.getRight(), 0.0f, false));
        g.fillRect (r);
        Random rnd (11 + side);
        for (float x = r.getX(); x < r.getRight(); x += 1.0f)
        {
            Path grain;
            const float ph = rnd.nextFloat() * 6.28f, amp = 0.6f + rnd.nextFloat() * 1.8f;
            grain.startNewSubPath (x, 0.0f);
            for (float y = 0.0f; y <= H; y += 20.0f) grain.lineTo (x + amp * std::sin (y * 0.011f + ph), y);
            g.setColour ((rnd.nextInt (3) == 0 ? Colours::white : Colours::black).withAlpha (0.04f + rnd.nextFloat() * 0.10f));
            g.strokePath (grain, PathStrokeType (0.8f));
        }
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.10f), 0.0f, 0.0f, Colours::transparentBlack, 0.0f, 120.0f, false));
        g.fillRect (r);
        g.setColour (Colours::black.withAlpha (0.55f));
        g.fillRect (side == 0 ? r.getRight() - 2.0f : r.getX(), 0.0f, 2.0f, H);
    }

    // cream panel
    Rectangle<float> panel (36.0f, 0.0f, W - 72.0f, 730.0f);
    g.setGradientFill (ColourGradient (pal::panelTop, 0.0f, 0.0f, pal::panelBot, 0.0f, panel.getBottom(), false));
    g.fillRect (panel);
    {
        Random rnd (3);
        for (int i = 0; i < 6000; ++i)
        {
            g.setColour ((rnd.nextBool() ? Colours::white : Colours::black).withAlpha (0.025f + rnd.nextFloat() * 0.03f));
            g.fillRect (panel.getX() + rnd.nextFloat() * panel.getWidth(), rnd.nextFloat() * panel.getHeight(), 1.4f, 1.4f);
        }
    }
    g.setColour (pal::ink.withAlpha (0.35f));
    g.fillRect (54.0f, 536.0f, W - 108.0f, 1.0f);       // divider above the vocoder / mod-seq row

    // brand
    g.setColour (pal::ink);
    g.setFont (Font (FontOptions (Font::getDefaultSansSerifFontName(), 34.0f, Font::bold)).withExtraKerningFactor (0.12f));
    g.drawText ("YM2000", Rectangle<float> (54.0f, 12.0f, 240.0f, 40.0f), Justification::centredLeft, false);
    drawPaw (g, { 262.0f, 38.0f }, 20.0f, pal::amber);
    g.setColour (pal::amber);
    g.fillRect (54.0f, 54.0f, 56.0f, 2.0f);
    g.setColour (pal::muted);
    g.setFont (uiFont (12.0f, true, 0.95f).withExtraKerningFactor (0.16f));
    g.drawText ("ANALOG MODELING SYNTHESIZER  /  Yu_My", Rectangle<float> (54.0f, 57.0f, 304.0f, 14.0f), Justification::centredLeft, false);

    // black keyboard deck
    Rectangle<float> deck (36.0f, 730.0f, W - 72.0f, 250.0f);
    g.setGradientFill (ColourGradient (Colour (0xff1b1b1b), 0.0f, deck.getY(), Colour (0xff0a0a0a), 0.0f, deck.getBottom(), false));
    g.fillRect (deck);
    g.setColour (Colour (0xff000000)); g.fillRect (deck.getX(), deck.getY(), deck.getWidth(), 3.0f);
    g.setColour (pal::inkLight.withAlpha (0.8f)); g.setFont (uiFont (12.5f));
    g.drawFittedText ("OCTAVE\nSHIFT", Rectangle<int> (60, 904, 80, 30), Justification::centred, 2);
    g.drawFittedText ("PITCH\nBEND", Rectangle<int> (140, 924, 70, 30), Justification::centred, 2);
    g.drawFittedText ("MOD", Rectangle<int> (196, 924, 70, 30), Justification::centred, 2);
}

// ------------------------------------------------------------------ editor
namespace
{
struct Captioned : Component
{
    Captioned (String cap, Component* inner, int w, int h) : caption (std::move (cap))
    {
        child.reset (inner);
        addAndMakeVisible (*child);
        setSize (w, h);
        child->setBounds (0, 16, w, h - 16);
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (pal::ink); g.setFont (uiFont (13.0f, true, 0.95f).withExtraKerningFactor (0.1f));
        g.drawText (caption, 0, 0, getWidth(), 14, juce::Justification::centredLeft, false);
    }
    String caption;
    std::unique_ptr<Component> child;
};
} // namespace

String YM2000Editor::programLabel (int i) const { return String::charToString ((juce::juce_wchar) ('A' + (i >> 4))) + "." + String ((i & 15) + 1).paddedLeft ('0', 2); }

YM2000Editor::YM2000Editor (YM2000Processor& p) : AudioProcessorEditor (&p), proc (p)
{
    laf.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xfff7f2e2));
    laf.setColour (juce::ComboBox::textColourId, pal::ink);
    laf.setColour (juce::ComboBox::outlineColourId, pal::ink.withAlpha (0.6f));
    laf.setColour (juce::ComboBox::arrowColourId, pal::ink);
    laf.setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xfff7f2e2));
    laf.setColour (juce::PopupMenu::textColourId, pal::ink);
    laf.setColour (juce::PopupMenu::highlightedBackgroundColourId, pal::amberDim);
    laf.setColour (juce::PopupMenu::highlightedTextColourId, pal::ink);

    addAndMakeVisible (backdrop);
    backdrop.setBounds (0, 0, designW, designH);
    addAndMakeVisible (content);
    content.setBounds (0, 0, designW, designH);

    auto prm = [&] (const String& id) { auto* q = proc.apvts.getParameter (id); jassert (q != nullptr); return q; };
    auto adopt = [&] (Component& parent, Component* c) { owned.add (c); parent.addAndMakeVisible (c); return c; };
    auto newSection = [&] (const String& title) -> Section&
    {
        auto* s = new Section (title);
        owned.add (s); content.addAndMakeVisible (s);
        return *s;
    };
    auto K = [&] (Section& s, const char* id, const char* label, juce::Colour c, float dia = 46.0f) -> Component*
    {
        auto* k = new Knob (prm (id), label, s.title, c, &lcd, dia);
        k->setBounds (Knob::sizeFor (dia, 0.0f, label));
        return adopt (s, k);
    };
    auto S = [&] (Section& s, const char* id, const char* label, juce::Colour c, float R) -> Component*
    {
        auto* q = prm (id);
        auto* k = new Knob (q, label, s.title, c, &lcd, 46.0f, R);
        k->setBounds (Knob::sizeFor (46.0f, R, label, q->getAllValueStrings()));
        return adopt (s, k);
    };
    auto Btn = [&] (Section& s, const char* id, const char* label) -> Component*
    {
        auto* b = new LedButton (prm (id), label, s.title, &lcd);
        b->setBounds (LedButton::sizeFor (label));
        return adopt (s, b);
    };
    auto Stack = [&] (Section& s, std::initializer_list<Component*> kids) -> Component*
    {
        auto* h = new Component();
        int w = 0, y = 0;
        for (auto* k : kids) { w = juce::jmax (w, k->getWidth()); }
        for (auto* k : kids) { s.removeChildComponent (k); h->addAndMakeVisible (k); k->setTopLeftPosition ((w - k->getWidth()) / 2, y); y += k->getHeight() + 2; }
        h->setSize (w, y);
        return adopt (s, h);
    };
    // lay sections of one row out side by side; widths follow their contents
    auto row = [&] (int x, int y, int totalW, int h, int gap, std::vector<std::pair<Section*, juce::Array<Component*>>> list)
    {
        float sum = 0;
        std::vector<float> mins;
        for (auto& e : list) { float m = (float) e.first->minWidthFor (e.second) + 12.0f; mins.push_back (m); sum += m; }
        const float scale = ((float) totalW - (float) gap * (float) (list.size() - 1)) / sum;
        float cx = (float) x;
        for (size_t i = 0; i < list.size(); ++i)
        {
            const int w = (int) (mins[i] * scale);
            list[i].first->setBounds ((int) cx, y, w, h);
            list[i].first->layoutRow (list[i].second);
            cx += (float) w + (float) gap;
        }
    };

    // ---- LCD
    content.addAndMakeVisible (lcd);
    lcd.setBounds (54, 78, 304, 66);
    lcd.defaultText = [this] { const int c = proc.getCurrentProgram(); return std::make_pair (programLabel (c) + " Timbre1", proc.getProgramName (c)); };

    // ---- MASTER
    auto& master = newSection ("MASTER");
    master.setBounds (54, 158, 304, 104);
    programKnob = std::make_unique<Knob> ("PROGRAM", "MASTER", pal::cMaster, &lcd, 46.0f, 0.0f, (float) ym::kNumFactory - 1.0f, 0.0f,
        [this] (float v) { proc.setCurrentProgram ((int) v); lcd.refresh(); },
        [this] (float v) { return programLabel ((int) v) + " " + proc.getProgramName ((int) v); });
    programKnob->setBounds (Knob::sizeFor (46.0f, 0.0f, "PROGRAM"));
    master.addAndMakeVisible (*programKnob);
    master.layoutRow ({ K (master, "vol", "VOLUME", pal::cMaster), K (master, "ai1", "AUDIO IN 1", pal::cMaster),
                        K (master, "ai2", "AUDIO IN 2", pal::cMaster), programKnob.get() });

    // ---- VIRTUAL PATCH
    auto& vp = newSection ("VIRTUAL PATCH");
    vp.setBounds (54, 276, 304, 256);
    {
        auto head = [&] (const String& t, int x, int w)
        {
            auto* l = new juce::Label ({}, t);
            l->setFont (uiFont (11.5f)); l->setColour (juce::Label::textColourId, pal::muted); l->setJustificationType (juce::Justification::centred);
            l->setBounds (x, 20, w, 14); adopt (vp, l);
        };
        head ("SOURCE", 28, 96); head ("DESTINATION", 128, 96); head ("INTENSITY", 232, 70);
        for (int i = 1; i <= 4; ++i)
        {
            const int y = 40 + (i - 1) * 53;
            auto* num = new juce::Label ({}, String (i));
            num->setFont (uiFont (15.0f)); num->setColour (juce::Label::textColourId, pal::ink); num->setBounds (4, y + 10, 20, 20);
            adopt (vp, num);
            for (int k = 0; k < 2; ++k)
            {
                auto* q = prm ((k == 0 ? "vs" : "vd") + String (i));
                auto* cb = new juce::ComboBox();
                cb->addItemList (q->getAllValueStrings(), 1);
                cb->setLookAndFeel (&laf);
                cb->setBounds (k == 0 ? 28 : 128, y + 8, 96, 24);
                adopt (vp, cb);
                comboAtts.add (new juce::ComboBoxParameterAttachment (*q, *cb));
            }
            auto* kn = new Knob (prm ("vi" + String (i)), "", "VP" + String (i) + " INTENSITY", pal::cMod, &lcd, 34.0f);
            kn->setBounds (238, y - 2, 58, 52);
            adopt (vp, kn);
        }
    }

    // ---- MID: oscillators / filter / amp / envelopes / LFOs
    auto& osc1 = newSection ("OSC 1"); auto& osc2 = newSection ("OSC 2"); auto& mixer = newSection ("MIXER");
    row (372, 16, 840, 160, 12, {
        { &osc1, { S (osc1, "o1w", "WAVE", pal::cOsc, 40.0f), K (osc1, "o1c1", "CTRL 1", pal::cOsc), K (osc1, "o1c2", "CTRL 2", pal::cOsc), K (osc1, "dwgs", "DWGS", pal::cOsc) } },
        { &osc2, { S (osc2, "o2w", "WAVE", pal::cOsc, 32.0f), S (osc2, "o2m", "MOD SELECT", pal::cOsc, 32.0f), K (osc2, "o2s", "SEMITONE", pal::cOsc), K (osc2, "o2t", "TUNE", pal::cOsc) } },
        { &mixer, { K (mixer, "mx1", "OSC 1", pal::cOsc), K (mixer, "mx2", "OSC 2", pal::cOsc), K (mixer, "mxn", "NOISE", pal::cOsc) } } });

    auto& filter = newSection ("FILTER"); auto& amp = newSection ("AMP"); auto& eg1 = newSection ("EG 1");
    row (372, 204, 840, 144, 12, {
        { &filter, { S (filter, "ft", "TYPE", pal::cFilter, 32.0f), K (filter, "cut", "CUTOFF", pal::cFilter), K (filter, "res", "RESO", pal::cFilter),
                     K (filter, "f1", "EG1 INT", pal::cFilter), K (filter, "fv", "VELO", pal::cFilter), K (filter, "fk", "KBD TRK", pal::cFilter) } },
        { &amp, { K (amp, "al", "LEVEL", pal::cFilter), K (amp, "ap", "PAN", pal::cFilter), Btn (amp, "dist", "DIST"), K (amp, "ak", "KBD TRK", pal::cFilter) } },
        { &eg1, { K (eg1, "e1a", "ATTACK", pal::cMod), K (eg1, "e1d", "DECAY", pal::cMod), K (eg1, "e1s", "SUSTAIN", pal::cMod), K (eg1, "e1r", "RELEASE", pal::cMod) } } });

    auto& eg2 = newSection ("EG 2"); auto& lfo1 = newSection ("LFO 1"); auto& lfo2 = newSection ("LFO 2");
    row (372, 374, 840, 144, 12, {
        { &eg2, { K (eg2, "e2a", "ATTACK", pal::cMod), K (eg2, "e2d", "DECAY", pal::cMod), K (eg2, "e2s", "SUSTAIN", pal::cMod), K (eg2, "e2r", "RELEASE", pal::cMod) } },
        { &lfo1, { S (lfo1, "l1w", "WAVE", pal::cMod, 32.0f), K (lfo1, "l1f", "FREQ", pal::cMod), Stack (lfo1, { Btn (lfo1, "l1k", "KEY SYNC"), Btn (lfo1, "l1t", "TEMPO SYNC") }) } },
        { &lfo2, { S (lfo2, "l2w", "WAVE", pal::cMod, 32.0f), K (lfo2, "l2f", "FREQ", pal::cMod), Stack (lfo2, { Btn (lfo2, "l2k", "KEY SYNC") }) } } });

    // cats sitting on the header bars of FILTER, AMP and LFO 2
    auto cat = [&] (Section& sec, int variant, int fromRight)
    {
        auto* c = new Cat (variant);
        owned.add (c); content.addAndMakeVisible (c);
        c->setTopLeftPosition (sec.getRight() - fromRight - c->getWidth(), sec.getY() - c->getHeight() + 3);
    };
    cat (filter, 0, 34); cat (amp, 1, 34); cat (lfo2, 2, 34);

    // ---- RIGHT: effects / pitch / EQ / arpeggiator
    auto& modfx = newSection ("MOD FX"); auto& delay = newSection ("DELAY FX");
    row (1226, 16, 420, 130, 12, {
        { &modfx, { S (modfx, "mft", "TYPE", pal::cFx, 27.0f), K (modfx, "mfs", "SPEED", pal::cFx, 40.0f), K (modfx, "mfd", "DEPTH", pal::cFx, 40.0f) } },
        { &delay, { S (delay, "dt", "TYPE", pal::cFx, 27.0f), K (delay, "dtm", "TIME", pal::cFx, 40.0f), K (delay, "ddp", "DEPTH", pal::cFx, 40.0f) } } });
    auto& pitch = newSection ("PITCH");
    row (1226, 158, 420, 100, 12, { { &pitch, { K (pitch, "ptn", "TUNE", pal::cPerf), K (pitch, "pbr", "BEND RANGE", pal::cPerf), K (pitch, "ptr", "TRANSP", pal::cPerf),
                                                 K (pitch, "pvi", "VIB INT", pal::cPerf), K (pitch, "ppo", "PORTA", pal::cPerf) } } });
    auto& eq = newSection ("EQ");
    row (1226, 268, 420, 100, 12, { { &eq, { K (eq, "eqhf", "HI FREQ", pal::cFx), K (eq, "eqhg", "HI GAIN", pal::cFx), K (eq, "eqlf", "LOW FREQ", pal::cFx), K (eq, "eqlg", "LOW GAIN", pal::cFx) } } });
    auto& arp = newSection ("ARPEGGIATOR");
    row (1226, 380, 420, 144, 12, { { &arp, { Stack (arp, { Btn (arp, "arpon", "ON/OFF"), Btn (arp, "arplt", "LATCH") }), S (arp, "arpt", "TYPE", pal::cPerf, 34.0f),
                                               K (arp, "arpr", "RANGE", pal::cPerf), K (arp, "arpg", "GATE TIME", pal::cPerf), K (arp, "arptm", "TEMPO", pal::cPerf) } } });

    // ---- VOCODER / MOD SEQUENCE row
    auto& voc = newSection ("VOCODER"); auto& seq = newSection ("MOD SEQUENCE");
    auto* lev = new Captioned ("CH LEVEL", new StepBars (proc.apvts, "vlev", "VOC CH LEVEL", false, &lcd), 330, 100);
    auto* pan = new Captioned ("CH PAN", new StepBars (proc.apvts, "vpan", "VOC CH PAN", true, &lcd), 330, 100);
    adopt (voc, lev); adopt (voc, pan);
    auto* sbars = new Captioned ("STEP VALUE", new StepBars (proc.apvts, "seq", "MOD SEQ STEP", false, &lcd, &proc.uiSeqPos), 420, 100);
    adopt (seq, sbars);
    row (54, 548, 1592, 174, 24, {
        { &voc, { S (voc, "vmode", "VOICE MODE", pal::cPerf, 36.0f), K (voc, "vfs", "FORMANT SHIFT", pal::cPerf), K (voc, "vcl", "CLARITY", pal::cPerf), lev, pan } },
        { &seq, { Stack (seq, { Btn (seq, "msq", "ON/OFF"), Btn (seq, "msk", "KEY SYNC") }), sbars } } });

    // ---- deck: octave buttons, wheels, keyboard
    auto* octDown = new PlainCap ("-", [this] { octave = juce::jmax (-2, octave - 1); keyboard->setKeyPressBaseOctave (4 + octave); }, [this] { return octave < 0; });
    auto* octUp = new PlainCap ("+", [this] { octave = juce::jmin (2, octave + 1); keyboard->setKeyPressBaseOctave (4 + octave); }, [this] { return octave > 0; });
    octDown->setBounds (62, 780, 44, 50); octUp->setBounds (62, 836, 44, 50);
    adopt (content, octDown); adopt (content, octUp);
    auto sendWheel = [this] (const juce::MidiMessage& m) { proc.midiCollector.addMessageToQueue (m.withTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001)); };
    auto* bend = new Wheel (true, [sendWheel] (float v) { sendWheel (juce::MidiMessage::pitchWheel (1, juce::jlimit (0, 16383, (int) (v * 16383.0f)))); }, &proc.uiBend, 0.5f);
    auto* mod = new Wheel (false, [sendWheel] (float v) { sendWheel (juce::MidiMessage::controllerEvent (1, 1, juce::jlimit (0, 127, (int) (v * 127.0f)))); }, &proc.uiMod, 0.0f);
    bend->setBounds (146, 760, 38, 150); mod->setBounds (200, 760, 38, 150);
    adopt (content, bend); adopt (content, mod);

    keyboard = std::make_unique<juce::MidiKeyboardComponent> (proc.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard);
    keyboard->setAvailableRange (41, 84);            // F2 to C6, 44 keys
    keyboard->setLowestVisibleKey (41);
    keyboard->setKeyWidth (53.0f);
    keyboard->setScrollButtonsVisible (false);
    keyboard->setBlackNoteLengthProportion (0.62f);
    keyboard->setMidiChannel (1);
    keyboard->setVelocity (0.8f, true);
    keyboard->setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xfffbfaf4));
    keyboard->setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff0e0e0e));
    keyboard->setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff9b9582));
    keyboard->setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, pal::amber.withAlpha (0.65f));
    keyboard->setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, juce::Colours::white.withAlpha (0.10f));
    keyboard->setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::black.withAlpha (0.35f));
    keyboard->setColour (juce::MidiKeyboardComponent::textLabelColourId, juce::Colour (0xff6e6a5c));
    keyboard->setBounds (262, 756, 1378, 196);
    content.addAndMakeVisible (*keyboard);

    setResizable (true, true);
    setResizeLimits (680, 392, 2550, 1470);
    getConstrainer()->setFixedAspectRatio ((double) designW / (double) designH);
    setSize (1360, 784);
    startTimerHz (15);
}

YM2000Editor::~YM2000Editor() { stopTimer(); }

void YM2000Editor::resized()
{
    const float s = (float) getWidth() / (float) designW;
    const auto t = juce::AffineTransform::scale (s);
    backdrop.setTransform (t);
    content.setTransform (t);
}

void YM2000Editor::timerCallback()
{
    const int c = proc.getCurrentProgram();
    if (c != shownProgram)
    {
        shownProgram = c;
        if (programKnob) programKnob->setExternal ((float) c);
        lcd.refresh();
    }
}
