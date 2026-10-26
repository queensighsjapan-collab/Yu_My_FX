#include "PluginEditor.h"
#include "BinaryData.h"

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
    const float cheek = 28.0f, panelH = 748.0f;
    g.fillAll (pal::body);

    // wooden side cheeks
    for (int side = 0; side < 2; ++side)
    {
        Rectangle<float> r (side == 0 ? 0.0f : W - cheek, 0.0f, cheek, H);
        g.setGradientFill (ColourGradient (Colour (0xff8a3a18), r.getX(), 0.0f, Colour (0xff5a1f0a), r.getRight(), 0.0f, false));
        g.fillRect (r);
        Random rnd (11 + side);
        for (float x = r.getX(); x < r.getRight(); x += 1.0f)
        {
            Path grain;
            const float ph = rnd.nextFloat() * 6.28f, amp = 0.6f + rnd.nextFloat() * 1.8f;
            grain.startNewSubPath (x, 0.0f);
            for (float y = 0.0f; y <= H; y += 20.0f) grain.lineTo (x + amp * std::sin (y * 0.011f + ph), y);
            g.setColour ((rnd.nextInt (3) == 0 ? Colour (0xffd9803a) : Colours::black).withAlpha (0.05f + rnd.nextFloat() * 0.12f));
            g.strokePath (grain, PathStrokeType (0.8f));
        }
        g.setColour (Colours::black.withAlpha (0.55f));
        g.fillRect (side == 0 ? r.getRight() - 2.0f : r.getX(), 0.0f, 2.0f, H);
    }

    // peach-cream panel
    Rectangle<float> panel (cheek, 0.0f, W - 2.0f * cheek, panelH);
    g.setGradientFill (ColourGradient (pal::panelTop, 0.0f, 0.0f, pal::panelBot, 0.0f, panel.getBottom(), false));
    g.fillRect (panel);
    {
        Random rnd (3);
        for (int i = 0; i < 6000; ++i)
        {
            g.setColour ((rnd.nextBool() ? Colours::white : Colour (0xff8a5a20)).withAlpha (0.03f + rnd.nextFloat() * 0.04f));
            g.fillRect (panel.getX() + rnd.nextFloat() * panel.getWidth(), rnd.nextFloat() * panel.getHeight(), 1.4f, 1.4f);
        }
    }
    g.setGradientFill (ColourGradient (Colours::transparentBlack, panel.getCentreX(), panel.getCentreY(), Colour (0xff8a4a10).withAlpha (0.16f), panel.getX(), 0.0f, true));
    g.fillRect (panel);

    // brand
    g.setColour (pal::ink);
    g.setFont (Font (FontOptions (Font::getDefaultSansSerifFontName(), 46.0f, Font::bold)).withHorizontalScale (0.92f).withExtraKerningFactor (0.03f));
    g.drawText ("YM2000", Rectangle<float> (46.0f, 4.0f, 250.0f, 54.0f), Justification::centredLeft, false);
    drawPaw (g, { 288.0f, 40.0f }, 30.0f, Colour (0xffe8923a));
    g.setColour (pal::ink);
    g.setFont (uiFont (12.5f, true, 0.9f).withExtraKerningFactor (0.10f));
    g.drawText ("ANALOG MODELING SYNTHESIZER", Rectangle<float> (48.0f, 58.0f, 290.0f, 16.0f), Justification::centredLeft, false);

    // black keyboard deck
    Rectangle<float> deck (cheek, panelH, W - 2.0f * cheek, H - panelH);
    g.setGradientFill (ColourGradient (Colour (0xff222120), 0.0f, deck.getY(), Colour (0xff0b0b0b), 0.0f, deck.getBottom(), false));
    g.fillRect (deck);
    g.setColour (Colour (0xff000000)); g.fillRect (deck.getX(), deck.getY(), deck.getWidth(), 3.0f);
    g.setColour (pal::inkLight.withAlpha (0.9f)); g.setFont (uiFont (12.5f));
    g.drawFittedText ("OCTAVE\nSHIFT", Rectangle<int> (52, 920, 80, 30), Justification::centred, 2);
    g.drawFittedText ("PITCH\nBEND", Rectangle<int> (132, 920, 70, 30), Justification::centred, 2);
    g.drawFittedText ("MOD", Rectangle<int> (186, 920, 70, 30), Justification::centred, 2);
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
    auto K = [&] (Section& s, const char* id, const char* label, juce::Colour c, float dia = 50.0f) -> Component*
    {
        auto* k = new Knob (prm (id), label, s.title, c, &lcd, dia);
        k->setBounds (Knob::sizeFor (dia, 0.0f, label));
        return adopt (s, k);
    };
    auto S = [&] (Section& s, const char* id, const char* label, juce::Colour c, float R) -> Component*
    {
        auto* q = prm (id);
        auto* k = new Knob (q, label, s.title, c, &lcd, 50.0f, R);
        k->setBounds (Knob::sizeFor (50.0f, R, label, q->getAllValueStrings()));
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
    auto row = [&] (int x, int y, int totalW, int h, int gap, std::vector<std::pair<Section*, juce::Array<Component*>>> list, int top = Section::headerH + 2)
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
            list[i].first->layoutRow (list[i].second, top);
            cx += (float) w + (float) gap;
        }
    };

    // cats (cut out of the design mock-up); added first so that the section header bars cover their lower edge
    auto catPic = [&] (const void* data, int size, int x, int y, int h)
    {
        auto* ic = new juce::ImageComponent();
        auto img = juce::ImageCache::getFromMemory (data, size);
        ic->setImage (img);
        ic->setInterceptsMouseClicks (false, false);
        ic->setBounds (x, y, (int) ((float) img.getWidth() * (float) h / (float) img.getHeight()), h);
        owned.add (ic); content.addAndMakeVisible (ic);
        return ic;
    };
    // (positions are set after the sections exist; see below)
    auto* catA = catPic (BinaryData::cat1_png, BinaryData::cat1_pngSize, 0, 0, 66);
    auto* catB = catPic (BinaryData::cat2_png, BinaryData::cat2_pngSize, 0, 0, 64);
    auto* catC = catPic (BinaryData::cat3_png, BinaryData::cat3_pngSize, 0, 0, 66);

    // ---- LCD
    content.addAndMakeVisible (lcd);
    lcd.setBounds (46, 80, 292, 72);
    lcd.defaultText = [this] { const int c = proc.getCurrentProgram(); return std::make_pair (programLabel (c) + " " + proc.getProgramName (c), String ("Timbre1 Single")); };

    // ---- MASTER
    auto& master = newSection ("MASTER");
    master.setBounds (44, 164, 294, 116);
    programKnob = std::make_unique<Knob> ("PROGRAM", "MASTER", pal::cMaster, &lcd, 50.0f, 0.0f, (float) ym::kNumFactory - 1.0f, 0.0f,
        [this] (float v) { proc.setCurrentProgram ((int) v); lcd.refresh(); },
        [this] (float v) { return programLabel ((int) v) + " " + proc.getProgramName ((int) v); });
    programKnob->setBounds (Knob::sizeFor (46.0f, 0.0f, "PROGRAM"));
    programKnob->setSize (60, 78);
    master.addAndMakeVisible (*programKnob);
    {
        auto* k1 = K (master, "vol", "VOLUME", pal::cMaster); auto* k2 = K (master, "ai1", "AUDIO IN 1", pal::cMaster); auto* k3 = K (master, "ai2", "AUDIO IN 2", pal::cMaster);
        k1->setSize (52, 78); k2->setSize (66, 78); k3->setSize (66, 78);
        master.layoutRow ({ k1, k2, k3, programKnob.get() });
    }

    // ---- VIRTUAL PATCH
    auto& vp = newSection ("VIRTUAL PATCH");
    vp.setBounds (44, 292, 294, 306);
    {
        auto head = [&] (const String& t, int x, int w)
        {
            auto* l = new juce::Label ({}, t);
            l->setFont (uiFont (12.5f)); l->setColour (juce::Label::textColourId, pal::ink); l->setJustificationType (juce::Justification::centred);
            l->setBounds (x, 32, w, 14); adopt (vp, l);
        };
        head ("SOURCE", 28, 100); head ("DESTINATION", 134, 100); head ("INTENSITY", 236, 56);
        for (int i = 1; i <= 4; ++i)
        {
            const int y = 52 + (i - 1) * 62;
            auto* num = new juce::Label ({}, String (i));
            num->setFont (uiFont (18.0f)); num->setColour (juce::Label::textColourId, pal::ink); num->setBounds (6, y + 12, 20, 24);
            adopt (vp, num);
            for (int k = 0; k < 2; ++k)
            {
                auto* q = prm ((k == 0 ? "vs" : "vd") + String (i));
                auto* cb = new juce::ComboBox();
                cb->addItemList (q->getAllValueStrings(), 1);
                cb->setLookAndFeel (&laf);
                cb->setBounds (k == 0 ? 28 : 134, y + 12, 100, 26);
                adopt (vp, cb);
                comboAtts.add (new juce::ComboBoxParameterAttachment (*q, *cb));
            }
            auto* kn = new Knob (prm ("vi" + String (i)), "", "VP" + String (i) + " INTENSITY", pal::cMod, &lcd, 40.0f);
            kn->setBounds (240, y + 1, 52, 52);
            adopt (vp, kn);
        }
    }

    // ---- MID: oscillators / mixer, right: MOD FX / DELAY FX
    auto& osc1 = newSection ("OSC 1"); auto& osc2 = newSection ("OSC 2"); auto& mixer = newSection ("MIXER");
    row (356, 30, 920, 160, 8, {
        { &osc1, { S (osc1, "o1w", "WAVE", pal::cOsc, 40.0f), K (osc1, "o1c1", "CTRL 1", pal::cOsc), K (osc1, "o1c2", "CTRL 2", pal::cOsc), K (osc1, "dwgs", "DWGS", pal::cOsc) } },
        { &osc2, { S (osc2, "o2w", "WAVE", pal::cOsc, 32.0f), S (osc2, "o2m", "MOD SELECT", pal::cOsc, 32.0f), K (osc2, "o2s", "SEMITONE", pal::cOsc), K (osc2, "o2t", "TUNE", pal::cOsc) } },
        { &mixer, { K (mixer, "mx1", "OSC 1", pal::cOsc), K (mixer, "mx2", "OSC 2", pal::cOsc), K (mixer, "mxn", "NOISE", pal::cOsc) } } });
    auto& modfx = newSection ("MOD FX"); auto& delay = newSection ("DELAY FX");
    row (1288, 30, 392, 160, 8, {
        { &modfx, { S (modfx, "mft", "TYPE", pal::cFx, 24.0f), K (modfx, "mfs", "SPEED", pal::cFx, 40.0f), K (modfx, "mfd", "DEPTH", pal::cFx, 40.0f) } },
        { &delay, { S (delay, "dt", "TYPE", pal::cFx, 24.0f), K (delay, "dtm", "TIME", pal::cFx, 40.0f), K (delay, "ddp", "DEPTH", pal::cFx, 40.0f) } } });

    // ---- filter / amp / EG 1 (cat zone under the header bar), EG 2 / LFOs
    const int catTop = Section::headerH + 2 + 58;
    auto& filter = newSection ("FILTER"); auto& amp = newSection ("AMP"); auto& eg1 = newSection ("EG 1");
    row (356, 198, 920, 196, 8, {
        { &filter, { S (filter, "ft", "TYPE", pal::cFilter, 32.0f), K (filter, "cut", "CUTOFF", pal::cFilter), K (filter, "res", "RESO", pal::cFilter),
                     K (filter, "f1", "EG1 INT", pal::cFilter), K (filter, "fv", "VELO", pal::cFilter), K (filter, "fk", "KBD TRK", pal::cFilter) } },
        { &amp, { K (amp, "al", "LEVEL", pal::cFilter), K (amp, "ap", "PAN", pal::cFilter), Btn (amp, "dist", "DIST"), K (amp, "ak", "KBD TRK", pal::cFilter) } },
        { &eg1, { K (eg1, "e1a", "ATTACK", pal::cMod), K (eg1, "e1d", "DECAY", pal::cMod), K (eg1, "e1s", "SUSTAIN", pal::cMod), K (eg1, "e1r", "RELEASE", pal::cMod) } } }, catTop);

    auto& eg2 = newSection ("EG 2"); auto& lfo1 = newSection ("LFO 1"); auto& lfo2 = newSection ("LFO 2");
    row (356, 402, 920, 196, 8, {
        { &eg2, { K (eg2, "e2a", "ATTACK", pal::cMod), K (eg2, "e2d", "DECAY", pal::cMod), K (eg2, "e2s", "SUSTAIN", pal::cMod), K (eg2, "e2r", "RELEASE", pal::cMod) } },
        { &lfo1, { S (lfo1, "l1w", "WAVE", pal::cMod, 32.0f), K (lfo1, "l1f", "FREQ", pal::cMod), Stack (lfo1, { Btn (lfo1, "l1k", "KEY SYNC"), Btn (lfo1, "l1t", "TEMPO SYNC") }) } },
        { &lfo2, { S (lfo2, "l2w", "WAVE", pal::cMod, 32.0f), K (lfo2, "l2f", "FREQ", pal::cMod), Stack (lfo2, { Btn (lfo2, "l2k", "KEY SYNC") }) } } }, catTop);

    // cats sit in the free area of FILTER, AMP and LFO 2, right under the header
    auto place = [&] (Component* c, Section& sec, int fromRight) { c->setTopLeftPosition (sec.getRight() - fromRight - c->getWidth(), sec.getY() + Section::headerH + 60 - c->getHeight()); };
    place (catA, filter, 14); place (catB, amp, 14); place (catC, lfo2, 14);

    // ---- RIGHT: pitch / EQ / arpeggiator
    auto& pitch = newSection ("PITCH");
    row (1288, 198, 392, 112, 8, { { &pitch, { K (pitch, "ptn", "TUNE", pal::cPerf), K (pitch, "pbr", "BEND RANGE", pal::cPerf), K (pitch, "ptr", "TRANSP", pal::cPerf),
                                                 K (pitch, "pvi", "VIB INT", pal::cPerf), K (pitch, "ppo", "PORTA", pal::cPerf) } } });
    auto& eq = newSection ("EQ");
    row (1288, 318, 392, 112, 8, { { &eq, { K (eq, "eqhf", "HI FREQ", pal::cFx), K (eq, "eqhg", "HI GAIN", pal::cFx), K (eq, "eqlf", "LOW FREQ", pal::cFx), K (eq, "eqlg", "LOW GAIN", pal::cFx) } } });
    auto& arp = newSection ("ARPEGGIATOR");
    row (1288, 438, 392, 160, 8, { { &arp, { Stack (arp, { Btn (arp, "arpon", "ON/OFF"), Btn (arp, "arplt", "LATCH") }), S (arp, "arpt", "TYPE", pal::cPerf, 34.0f),
                                               K (arp, "arpr", "RANGE", pal::cPerf), K (arp, "arpg", "GATE TIME", pal::cPerf), K (arp, "arptm", "TEMPO", pal::cPerf) } } });

    // ---- VOCODER / CH LEVEL / CH PAN / MOD SEQUENCE row
    auto& voc = newSection ("VOCODER"); auto& chl = newSection ("CH LEVEL"); auto& chp = newSection ("CH PAN"); auto& seq = newSection ("MOD SEQUENCE");
    auto* lev = new StepBars (proc.apvts, "vlev", "VOC CH LEVEL", false, &lcd);
    auto* pan = new StepBars (proc.apvts, "vpan", "VOC CH PAN", true, &lcd);
    auto* sbars = new StepBars (proc.apvts, "seq", "MOD SEQ STEP", false, &lcd, &proc.uiSeqPos);
    adopt (chl, lev); adopt (chp, pan); adopt (seq, sbars);
    lev->setSize (10, 10); pan->setSize (10, 10); sbars->setSize (300, 10);
    auto* chlSpacer = new Component(); chlSpacer->setSize (420, 10); adopt (chl, chlSpacer);
    auto* chpSpacer = new Component(); chpSpacer->setSize (420, 10); adopt (chp, chpSpacer);
    auto* seqStack = Stack (seq, { Btn (seq, "msq", "ON/OFF"), Btn (seq, "msk", "KEY SYNC") });
    row (44, 608, 1636, 132, 8, {
        { &voc, { S (voc, "vmode", "VOICE MODE", pal::cPerf, 22.0f), K (voc, "vfs", "FORMANT SHIFT", pal::cPerf), K (voc, "vcl", "CLARITY", pal::cPerf) } },
        { &chl, { chlSpacer } }, { &chp, { chpSpacer } },
        { &seq, { seqStack, sbars } } });
    // the bars fill their sections
    lev->setBounds (12, Section::headerH + 8, chl.getWidth() - 24, chl.getHeight() - Section::headerH - 16);
    pan->setBounds (12, Section::headerH + 8, chp.getWidth() - 24, chp.getHeight() - Section::headerH - 16);
    sbars->setBounds (84, Section::headerH + 8, seq.getWidth() - 96, seq.getHeight() - Section::headerH - 16);
    seqStack->setTopLeftPosition (12, Section::headerH + (seq.getHeight() - Section::headerH - seqStack->getHeight()) / 2);

    // ---- deck: octave buttons, wheels, keyboard
    auto* octDown = new PlainCap ("-", [this] { octave = juce::jmax (-2, octave - 1); keyboard->setKeyPressBaseOctave (4 + octave); }, [this] { return octave < 0; });
    auto* octUp = new PlainCap ("+", [this] { octave = juce::jmin (2, octave + 1); keyboard->setKeyPressBaseOctave (4 + octave); }, [this] { return octave > 0; });
    octDown->setBounds (62, 784, 44, 50); octUp->setBounds (62, 842, 44, 50);
    adopt (content, octDown); adopt (content, octUp);
    auto sendWheel = [this] (const juce::MidiMessage& m) { proc.midiCollector.addMessageToQueue (m.withTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001)); };
    auto* bend = new Wheel (true, [sendWheel] (float v) { sendWheel (juce::MidiMessage::pitchWheel (1, juce::jlimit (0, 16383, (int) (v * 16383.0f)))); }, &proc.uiBend, 0.5f);
    auto* mod = new Wheel (false, [sendWheel] (float v) { sendWheel (juce::MidiMessage::controllerEvent (1, 1, juce::jlimit (0, 127, (int) (v * 127.0f)))); }, &proc.uiMod, 0.0f);
    bend->setBounds (140, 768, 38, 146); mod->setBounds (194, 768, 38, 146);
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
    keyboard->setBounds (262, 768, 1378, 186);
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
