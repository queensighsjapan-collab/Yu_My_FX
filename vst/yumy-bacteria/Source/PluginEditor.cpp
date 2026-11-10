#include "PluginEditor.h"
#include <BinaryData.h>
#include <cmath>

using namespace juce;
using namespace ybgui;

juce::AudioProcessorEditor* YumyBacteriaProcessor::createEditor() { return new YumyBacteriaEditor (*this); }

namespace
{
constexpr int KW = 52, KG = 6;
constexpr int PX = 34, PW = 1412;      // metal panel inside the red rails
constexpr int P1H = 522;
constexpr int FOOT = 26;

class Logo : public Component
{
public:
    Logo() : img (ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize)) { setInterceptsMouseClicks (false, false); }
    void paint (Graphics& g) override
    {
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.drawImage (img, getLocalBounds().toFloat(), RectanglePlacement::xLeft | RectanglePlacement::yTop);
    }
private:
    Image img;
};

class Foot : public Component
{
public:
    Foot() { setInterceptsMouseClicks (false, false); }
    void paint (Graphics& g) override
    {
        g.fillAll (Colour (0xff0d0e10));
        g.setFont (sans (11.0f));
        g.setColour (Colour (0xff888888));
        g.drawText ("YuMy Bacteria  /  VST  -  ENGINE v0.8  (VOCODER 8-32 via SIDECHAIN / RANDOMIZER+LOCK / UNISON / WT / HYPER x2 / LFO SYNC)",
                    getLocalBounds().withTrimmedLeft (14), Justification::centredLeft, false);
    }
};

String f1 (double v) { return String (v, 1); }
String pct (int v) { return String ((int) std::lround (v / 127.0 * 100.0)) + "%"; }
String secT (int v) { const double x = v / 127.0; return String (x * x * 3.0, 2) + " s"; }
String bip12 (int v) { return f1 ((v - 64) / 64.0 * 12.0) + " dB"; }
String pid (const char* name) { return String (name).replaceCharacter (' ', '_'); }
}

YumyBacteriaEditor::YumyBacteriaEditor (YumyBacteriaProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    content.setLookAndFeel (&laf);
    addAndMakeVisible (content);
    build();
    sdOpen = (bool) proc.apvts.state.getProperty ("sdOpen", false);

    setConstrainer (&constrainer);
    constrainer.setMinimumSize (560, 260);
    constrainer.setMaximumSize (3200, 3200);
    setResizable (true, true);
    setFold (sdOpen, false);
    showProgram (proc.getCurrentProgram());
    lastProg = proc.getCurrentProgram();
    startTimerHz (20);
}

YumyBacteriaEditor::~YumyBacteriaEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
    content.setLookAndFeel (nullptr);
}

void YumyBacteriaEditor::paint (Graphics& g) { g.fillAll (Colour (0xff111111)); }

void YumyBacteriaEditor::resized()
{
    const float s = (float) getWidth() / (float) designW;
    content.setTransform (AffineTransform::scale (s));
    content.setBounds (0, 0, designW, designH);
}

void YumyBacteriaEditor::showProgram (int idx)
{
    lcdName = proc.getProgramName (idx);
    lcd.setProgram (lcdName, "A" + String (idx + 1).paddedLeft ('0', 3));
    if (programBox != nullptr) programBox->setIndex (idx);
}

void YumyBacteriaEditor::timerCallback()
{
    const int pr = proc.getCurrentProgram();
    if (pr != lastProg) { lastProg = pr; showProgram (pr); }
    const float pk = proc.inPeak.load();
    level = std::max (pk, level * 0.88f);
    if (meter != nullptr) meter->set (std::sqrt (level) * 1.1f);
    quietFrames = pk > 0.0003f ? 0 : quietFrames + 1;
    if (vstat != nullptr)
    {
        if (quietFrames < 20) vstat->setText ("SIDECHAIN SIGNAL OK (VOCODER ACTIVE)", Colour (0xff0a6b2a));
        else vstat->setText ("NO SIDECHAIN SIGNAL (VOCODER BYPASSED) - ROUTE A TRACK TO THIS PLUG-IN'S SIDECHAIN", Colour (0xff33363b));
    }
}

void YumyBacteriaEditor::applyRandom (yb::PatchState st)
{
    auto cur = proc.capturePatch();
    cur.name = lcdName;
    history.push_back (cur);
    if (history.size() > 20) history.erase (history.begin());
    yb::rnd::applyLocks (st, cur, locked);
    proc.applyPatch (st);
    lcdName = st.name;
    lcd.setProgram (lcdName, "EDIT");
    lcd.show ("RANDOM", lcdName, 0.5f);
}

void YumyBacteriaEditor::setFold (bool open, bool fromUser)
{
    sdOpen = open;
    for (auto* c : sdComps) c->setVisible (open);
    if (sdBtn != nullptr)
        sdBtn->setText (open ? String (CharPointer_UTF8 ("HIDE SOUND DESIGN \xe2\x96\xb2")) : String (CharPointer_UTF8 ("SHOW SOUND DESIGN \xe2\x96\xbc")));
    if (sdInfo != nullptr) sdInfo->setText ("VOCODER / EFFECTS EXTRA / MOD MATRIX / ARP SETTINGS / LFO SETUP / OSC EXTRA", Colour (0xff33363b));
    const int p2End = open ? baseH + sdH : baseH;
    panel2->setBounds (PX, P1H, PW, p2End - P1H);
    foot->setBounds (PX, p2End, PW, FOOT);
    designH = p2End + FOOT;
    for (auto* r : rails) r->setSize (PX, designH);
    rails[1]->setTopLeftPosition (designW - PX, 0);
    if (fromUser) proc.apvts.state.setProperty ("sdOpen", open, nullptr);

    // window size: keep the current scale, but never taller than the screen
    const auto area = Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
                          ? Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea : Rectangle<int> (0, 0, 1920, 1080);
    float scale = getWidth() > 0 ? (float) getWidth() / (float) designW : 1400.0f / (float) designW;
    scale = std::min (scale, (float) area.getHeight() * 0.92f / (float) designH);
    constrainer.setFixedAspectRatio ((double) designW / (double) designH);
    content.setBounds (0, 0, designW, designH);
    setSize (roundToInt ((float) designW * scale), roundToInt ((float) designH * scale));
}

// ======================================================================= layout
void YumyBacteriaEditor::build()
{
    auto& apvts = proc.apvts;
    auto add = [this] (auto* c) { content.addAndMakeVisible (c); owned.add (c); return c; };
    auto setB = [] (Component* c, int x, int y, int w, int h) { c->setBounds (x, y, w, h); return c; };
    auto sec = [&] (const String& title, int x, int y, int w, int h) { return setB (add (new Sec (title)), x, y - 9, w, h + 9); };
    auto prm = [&] (const char* name) { return apvts.getParameter (pid (name)); };
    auto knob = [&] (int x, int y, const char* name, const String& label, bool red = false, bool big = false)
    { return setB (add (new KnobCell (prm (name), label, &lcd, red, big)), x, y, big ? 64 : KW, big ? 96 : 72); };
    auto sel = [&] (int x, int y, int w, const String& label, const char* id)
    { return setB (add (new SelectField (label, apvts, id, &lcd)), x, y, w, 40); };
    auto sld = [&] (int x, int y, int w, const String& label, const char* name, std::function<String (int)> f = nullptr, bool showLabel = true, int vw = 54)
    { return setB (add (new SliderField (label, apvts, pid (name), std::move (f), showLabel, vw, &lcd)), x, y, w, 40); };
    auto secW = [] (int n) { return 20 + n * KW + (n - 1) * KG; };

    // ---- rails + panels
    for (int i = 0; i < 2; ++i) { auto* r = add (new Rail()); r->setSize (PX, 100); rails.push_back (r); }
    rails[0]->setTopLeftPosition (0, 0);
    setB (add (new MetalPanel()), PX, 0, PW, P1H);
    panel2 = add (new MetalPanel());

    // ================= panel 1 =================
    const int r1 = 20, h1 = 155, r2 = r1 + h1 + 12, h2 = 152, r3 = r2 + h2 + 12, h3 = 152;
    {
        const int x = PX + 18;
        setB (add (new Logo()), x, r1, 226, 50);
        setB (&lcd, x, r1 + 57, 230, 98); content.addAndMakeVisible (lcd);

        int sx = x + 240;
        sec ("MASTER", sx, r1, 20 + 64 + 2 * KW + 2 * KG + 4, h1);
        knob (sx + 10, r1 + 20, "Master Volume", "VOLUME", true, true);
        knob (sx + 10 + 64 + KG + 4, r1 + 20, "Param", "PARAM");
        knob (sx + 10 + 64 + 2 * KG + 4 + KW, r1 + 20, "Value", "VALUE");
        sx += 20 + 64 + 2 * KW + 2 * KG + 4 + 10;

        sec ("CONTROL", sx, r1, 20 + 3 * 58, h1);
        setB (add (new LedAction ("PREV", [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n); lcd.show ("PROGRAM", proc.getProgramName (proc.getCurrentProgram()), 0.5f); })), sx + 10, r1 + 20, KW, 50);
        setB (add (new LedAction ("NEXT", [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); lcd.show ("PROGRAM", proc.getProgramName (proc.getCurrentProgram()), 0.5f); })), sx + 10 + 58, r1 + 20, KW, 50);
        setB (add (new LedToggle (apvts.getParameter ("hsync"), "HOST SYNC", &lcd)), sx + 10 + 116, r1 + 20, KW, 50);
        sx += 20 + 3 * 58 + 10;

        sec ("OSCILLATOR 1", sx, r1, secW (4), h1);
        const char* o1[] = { "Osc1 Shape", "Osc1 PW", "Osc1 Semi", "Osc1 Keyfollow" };
        const char* l1[] = { "SHAPE", "WAVE SEL\nPULSE W", "SEMITONE", "KEY\nFOLLOW" };
        for (int i = 0; i < 4; ++i) knob (sx + 10 + i * (KW + KG), r1 + 20, o1[i], l1[i]);
        sx += secW (4) + 10;
        sec ("OSCILLATOR 2", sx, r1, secW (4), h1);
        const char* o2[] = { "Osc2 Shape", "Osc2 PW", "Osc2 Semi", "Osc2 Detune" };
        const char* l2[] = { "SHAPE", "WAVE SEL\nPULSE W", "SEMITONE", "DETUNE" };
        for (int i = 0; i < 4; ++i) knob (sx + 10 + i * (KW + KG), r1 + 20, o2[i], l2[i]);
    }
    {
        int sx = PX + 18;
        sec ("OSC / SUB / NOISE", sx, r2, secW (6), h2);
        const char* n[] = { "Osc Balance", "Sub Osc Vol", "Noise Vol", "FM Amount", "Init Phase", "Portamento" };
        const char* l[] = { "OSC\nBALANCE", "SUB OSC\nVOLUME", "NOISE\nVOLUME", "FM\nAMOUNT", "OSC\nINIT PHASE", "PORTA-\nMENTO" };
        for (int i = 0; i < 6; ++i) knob (sx + 10 + i * (KW + KG), r2 + 20, n[i], l[i]);
        sx += secW (6) + 10;

        sec ("FILTER", sx, r2, secW (6), h2);
        const char* fn[] = { "Cutoff", "Cutoff 2", "Resonance", "Filter Env Amt", "Filter Keyfollow", "Filter Balance" };
        const char* fl[] = { "CUTOFF", "CUTOFF 2", "RESONANCE", "ENV\nAMOUNT", "KEY\nFOLLOW", "FILTER\nBALANCE" };
        for (int i = 0; i < 6; ++i) knob (sx + 10 + i * (KW + KG), r2 + 20, fn[i], fl[i], i < 2);
        const char* fm[] = { "LP", "HP", "BP", "BS" };
        for (int i = 0; i < 4; ++i) setB (add (new LedChoice (apvts.getParameter ("f1mode"), i, fm[i], &lcd)), sx + 10 + i * (KW + KG), r2 + 20 + 72 + 6, KW, 50);
        sx += secW (6) + 10;

        auto env = [&] (const char* title, const char* pre)
        {
            sec (title, sx, r2, secW (5), h2);
            const char* sfx[] = { "Attack", "Decay", "Sustain", "Sus Time", "Release" };
            const char* lab[] = { "ATTACK", "DECAY", "SUSTAIN", "SUS.TIME", "RELEASE" };
            for (int i = 0; i < 5; ++i) knob (sx + 10 + i * (KW + KG), r2 + 20, (String (pre) + " " + sfx[i]).toRawUTF8(), lab[i]);
            sx += secW (5) + 10;
        };
        env ("FILTER ENVELOPE", "Flt");
        env ("AMP ENVELOPE", "Amp");
    }
    {
        int sx = PX + 18;
        for (int i = 1; i <= 3; ++i)
        {
            sec ("LFO " + String (i), sx, r3, secW (3), h3);
            const String p = "LFO" + String (i);
            knob (sx + 10, r3 + 20, (p + " Rate").toRawUTF8(), "RATE");
            knob (sx + 10 + KW + KG, r3 + 20, (p + " Shape").toRawUTF8(), "SHAPE");
            knob (sx + 10 + 2 * (KW + KG), r3 + 20, (p + " Amount").toRawUTF8(), "AMOUNT");
            sx += secW (3) + 10;
        }
        sec ("EFFECTS", sx, r3, secW (6), h3);
        const char* n[] = { "Delay Send", "Delay Time", "Delay Feedback", "Reverb Send", "Chorus Mix", "Distortion" };
        const char* l[] = { "DELAY\nSEND", "DELAY\nTIME", "FEEDBACK", "REVERB\nSEND", "CHORUS\nMIX", "DISTORT" };
        for (int i = 0; i < 6; ++i) knob (sx + 10 + i * (KW + KG), r3 + 20, n[i], l[i]);
        sx += secW (6) + 10;
        sec ("ARPEGGIATOR", sx, r3, secW (3), h3);
        knob (sx + 10, r3 + 20, "Arp Tempo", "TEMPO");
        knob (sx + 10 + KW + KG, r3 + 20, "Arp Range", "RANGE");
        knob (sx + 10 + 2 * (KW + KG), r3 + 20, "Arp Swing", "SWING");
        setB (add (new LedToggle (apvts.getParameter ("arp"), "ARP ON", &lcd)), sx + 10, r3 + 20 + 72 + 6, KW, 50);
        setB (add (new LedToggle (apvts.getParameter ("ahold"), "HOLD", &lcd)), sx + 10 + KW + KG, r3 + 20 + 72 + 6, KW, 50);
        sx += secW (3) + 10;
        sec ("PERFORM", sx, r3, secW (2), h3);
        knob (sx + 10, r3 + 20, "Transpose", "TRANSPOSE");
        knob (sx + 10 + KW + KG, r3 + 20, "Bend Range", "BEND\nRANGE");
    }

    // ================= panel 2 (always visible part) =================
    const int a1 = P1H + 26, a2 = a1 + 78 + 16;
    {
        int sx = PX + 18; const int Y = a1 + 20;
        sec ("PROGRAM / OSCILLATOR MODE", sx, a1, 664, 78);
        StringArray names; for (int i = 0; i < proc.getNumPrograms(); ++i) names.add (String (i + 1).paddedLeft ('0', 2) + " " + proc.getProgramName (i));
        programBox = new SelectField ("PROGRAM", names, [this] (int i)
        { proc.setCurrentProgram (i); lcd.show ("PROGRAM", proc.getProgramName (i), 0.5f); });
        setB (add (programBox), sx + 10, Y + 10, 170, 40);
        sel (sx + 192, Y + 10, 110, "VOICE", "mono");
        sel (sx + 314, Y + 10, 100, "OSC1 MODE", "osc1mode");
        sel (sx + 426, Y + 10, 100, "OSC2 MODE", "osc2mode");
        setB (add (new LedToggle (apvts.getParameter ("sync"), "OSC SYNC", &lcd)), sx + 538, Y, KW, 50);
        setB (add (new LedToggle (apvts.getParameter ("ring"), "RING MOD", &lcd)), sx + 538 + 62, Y, KW, 50);
        sx += 664 + 10;
        sec ("FILTER MODE / ROUTING", sx, a1, 252, 78);
        sel (sx + 10, Y + 10, 100, "FILTER 2 MODE", "f2mode");
        sel (sx + 122, Y + 10, 120, "ROUTING", "routing");
        sx += 252 + 10;
        sec ("LFO DESTINATION (AMOUNT = LFO AMOUNT KNOB)", sx, a1, 430, 78);
        for (int i = 0; i < 3; ++i) sel (sx + 10 + i * 140, Y + 10, 128, "LFO " + String (i + 1) + String (CharPointer_UTF8 (" \xe2\x86\x92")), ("ld" + String (i + 1)).toRawUTF8());
    }
    {
        int sx = PX + 18; const int Y = a2 + 20;
        sec ("RANDOMIZER", sx, a2, 800, 78);
        auto* cat = new SelectField ("CATEGORY", StringArray { "ANY", "LEAD", "BASS", "PAD", "PLUCK", "ARP", "FX" }, [this] (int i) { rcat = i; });
        setB (add (cat), sx + 10, Y + 10, 100, 40);
        rInt = new PlainSlider ("INTENSITY", 50, [] (int v) { return String (v) + "%"; });
        setB (add (rInt), sx + 122, Y + 10, 150, 40);
        int bx = sx + 288;
        setB (add (new FlatButton ("RANDOMIZE", [this] { applyRandom (yb::rnd::genPatch (rng, rcat, rInt->get(), proc.capturePatch(), rndCount)); })), bx, Y + 23, 78, 26);
        bx += 86;
        setB (add (new FlatButton ("MUTATE", [this] { auto cur = proc.capturePatch(); cur.name = lcdName; applyRandom (yb::rnd::mutatePatch (rng, rInt->get(), cur)); })), bx, Y + 23, 64, 26);
        bx += 72;
        setB (add (new FlatButton ("BACK", [this]
        {
            if (history.empty()) { lcd.show ("RANDOM", "NO HISTORY", 0.0f); return; }
            auto s = history.back(); history.pop_back();
            proc.applyPatch (s); lcdName = s.name; lcd.setProgram (lcdName, "EDIT"); lcd.show ("BACK", s.name, 0.5f);
        })), bx, Y + 23, 50, 26);
        bx += 70;
        setB (add (new TextLabel ("LOCK (KEEP)", 9.0f, Colour (0xff202226))), bx, Y + 10, 90, 12);
        for (int i = 0; i < 6; ++i)
        {
            const char* nm = yb::rnd::lockNames()[i];
            setB (add (new Chip (nm, [this, i, nm] (bool on) { locked[i] = on; lcd.show (String ("LOCK ") + nm, on ? "ON" : "OFF", on ? 1.0f : 0.0f); })), bx + i * 44, Y + 24, 40, 24);
        }
        sx += 800 + 10;
        sec ("EFFECT SETTINGS", sx, a2, 364, 78);
        sel (sx + 10, Y + 10, 100, "CHORUS TYPE", "cmode");
        sel (sx + 122, Y + 10, 120, "DELAY SYNC", "dsync");
        sel (sx + 254, Y + 10, 100, "REVERB SIZE", "rsize");
    }
    const int bar = a2 + 78 + 18;
    {
        const int sx = PX + 18;
        sec ("", sx, bar, 1376, 40);
        sdBtn = new FlatButton ("", [this] { setFold (! sdOpen, true); });
        setB (add (sdBtn), sx + 10, bar + 8, 190, 24);
        sdInfo = new TextLabel ("", 10.0f, Colour (0xff33363b), true, Justification::centredLeft);
        setB (add (sdInfo), sx + 214, bar + 8, 1100, 24);
    }
    baseH = bar + 40 + 14;

    // ================= panel 2 (SOUND DESIGN fold) =================
    const int sdStart = (int) owned.size();
    int y = baseH + 14;
    const int GAP = 20;
    {   // ---- VOCODER
        const int sx = PX + 18, h = 184;
        sec ("VOCODER (MODULATOR = DAW SIDECHAIN INPUT / CARRIER = SYNTH VOICES)", sx, y, 1016, h);
        const int Y = y + 20;
        sel (sx + 10, Y, 100, "BANDS", "vbands");
        setB (add (new TextLabel ("INPUT LEVEL", 9.0f, Colour (0xff202226))), sx + 126, Y, 90, 12);
        meter = new Meter(); setB (add (meter), sx + 126, Y + 20, 100, 9);
        vstat = new TextLabel ("", 10.0f, Colour (0xff33363b), true, Justification::centredLeft);
        setB (add (vstat), sx + 244, Y + 14, 760, 24);
        const char* n[] = { "Vocoder Mix", "Voc Input", "Voc Release", "Voc Q", "Voc Shift", "Voc Noise", "Voc Tilt", "Voc Level" };
        const char* l[] = { "VOCODER MIX", "INPUT GAIN", "ENV RELEASE", "BAND WIDTH (Q)", "FORMANT SHIFT", "SIBILANCE (NOISE)", "HIGH TILT (EQ)", "OUTPUT LEVEL" };
        std::function<String (int)> f[] = {
            [] (int v) { return pct (v); },
            [] (int v) { return String ((int) std::lround ((v - 64) / 64.0 * 18.0)) + " dB"; },
            [] (int v) { return String ((int) std::lround (5.0 * std::pow (60.0, v / 127.0))) + " ms"; },
            [] (int v) { return f1 (3.0 + v / 127.0 * 12.0); },
            [] (int v) { return f1 ((v - 64) / 64.0 * 9.0) + " st"; },
            [] (int v) { return pct (v); },
            [] (int v) { return bip12 (v); },
            [] (int v) { return bip12 (v); } };
        for (int i = 0; i < 8; ++i) sld (sx + 10 + (i % 6) * 168, Y + 48 + (i / 6) * 46, 156, l[i], n[i], f[i]);
        setB (add (new TextLabel ("INPUT = DAW SIDECHAIN (ROUTE ANY TRACK TO THIS PLUG-IN'S SIDECHAIN) / PLAY KEYS = CARRIER (SAW / HYPERSAW + UNISON WORK WELL) / NO INPUT = VOCODER BYPASSED",
                                  9.0f, Colour (0xff333333))), sx + 10, y + h - 24, 990, 14);
        y += h + GAP;
    }
    {   // ---- EFFECTS EXTRA
        const int sx = PX + 18, h = 116;
        sec ("EFFECTS EXTRA - PHASER / RING MOD / EQ / FORMANT", sx, y, 1016, h);
        const int Y = y + 20;
        const char* n[] = { "Phaser Mix", "Phaser Rate", "Ring Mix", "Ring Freq", "EQ Low", "EQ Mid", "EQ High", "Formant Mix", "Formant Vowel" };
        const char* l[] = { "PHASER MIX", "PHASER RATE", "RING MOD MIX", "RING MOD FREQ", "EQ LOW 200Hz", "EQ MID 1kHz", "EQ HIGH 5kHz", "FORMANT MIX", "FORMANT VOWEL" };
        std::function<String (int)> f[] = {
            nullptr,
            [] (int v) { return String (0.05 * std::pow (2.0, v / 127.0 * 7.0), 2) + " Hz"; },
            nullptr,
            [] (int v) { return String ((int) std::lround (40.0 * std::pow (2.0, v / 127.0 * 6.6))) + " Hz"; },
            [] (int v) { return bip12 (v); }, [] (int v) { return bip12 (v); }, [] (int v) { return bip12 (v); },
            nullptr,
            [] (int v) { const double p = v / 127.0 * 4.0; return String ("AEIOU"[(int) std::lround (p)]) + " (" + f1 (p) + ")"; } };
        for (int i = 0; i < 9; ++i) sld (sx + 10 + (i % 6) * 168, Y + (i / 6) * 46, 156, l[i], n[i], f[i]);
        y += h + GAP;
    }
    {   // ---- MODULATION MATRIX
        const int sx = PX + 18, h = 326;
        sec ("MODULATION MATRIX - 6 SLOTS x 3 DESTINATIONS", sx, y, 1046, h);
        const int X0 = sx + 10, Y0 = y + 20;
        for (int i = 0; i < 6; ++i)
        {
            const int ry = Y0 + i * 46;
            setB (add (new TextLabel ("SLOT " + String (i + 1), 9.0f, pal::redD)), X0, ry + 22, 44, 14);
            sel (X0 + 48, ry, 110, "SOURCE", ("mxs" + String (i + 1)).toRawUTF8());
            for (int j = 0; j < 3; ++j)
            {
                const int bx = X0 + 166 + j * 280;
                sel (bx, ry, 130, "DEST " + String (j + 1), ("mxd" + String (i + 1) + "_" + String (j + 1)).toRawUTF8());
                sld (bx + 136, ry, 134, "", ("mxa" + String (i + 1) + "_" + String (j + 1)).toRawUTF8(),
                     [] (int v) { return String (v > 0 ? "+" : "") + String (v) + "%"; }, false, 40);
            }
        }
        setB (add (new TextLabel ("PITCH +-100% = +-24 SEMITONES / CUTOFF +-100% = +-6 OCTAVES / SOFT 1,2 = PARAM, VALUE KNOBS / DOUBLE-CLICK SLIDER = RESET",
                                  9.0f, Colour (0xff333333))), X0, y + h - 24, 1000, 14);
        y += h + GAP;
    }
    {   // ---- ARP SETTINGS
        const int sx = PX + 18, h = 70;
        sec ("ARPEGGIATOR SETTINGS", sx, y, 536, h);
        const int Y = y + 20;
        sel (sx + 10, Y, 120, "MODE", "amode");
        sel (sx + 142, Y, 120, "RESOLUTION", "ares");
        sel (sx + 274, Y, 120, "NOTE LENGTH", "alen");
        sel (sx + 406, Y, 120, "PATTERN", "apat");
        y += h + GAP;
    }
    {   // ---- LFO SETUP + OSC EXTRA
        const int sx = PX + 18, h = 178;
        sec ("LFO SETUP (SYNC / KEY / PHASE / DELAY / FADE)", sx, y, 800, h);
        const int X0 = sx + 10, Y0 = y + 20;
        for (int i = 1; i <= 3; ++i)
        {
            const int ry = Y0 + (i - 1) * 50;
            setB (add (new TextLabel ("LFO " + String (i), 9.0f, pal::redD)), X0, ry + 24, 44, 14);
            sel (X0 + 48, ry + 10, 150, "TEMPO SYNC", ("l" + String (i) + "s").toRawUTF8());
            setB (add (new LedToggle (apvts.getParameter ("l" + String (i) + "k"), "KEY SYNC", &lcd)), X0 + 206, ry, KW, 50);
            const String p = "LFO" + String (i);
            sld (X0 + 270, ry + 10, 156, "START PHASE", (p + " Phase").toRawUTF8(), [] (int v) { return String ((int) std::lround (v / 127.0 * 360.0)) + " deg"; });
            sld (X0 + 270 + 164, ry + 10, 156, "DELAY", (p + " Delay").toRawUTF8(), secT);
            sld (X0 + 270 + 328, ry + 10, 156, "FADE IN", (p + " Fade").toRawUTF8(), secT);
        }
        const int ox = sx + 800 + 10;
        sec ("OSCILLATOR EXTRA", ox, y, 366, h);
        const int OX = ox + 10, OY = y + 20;
        sld (OX, OY, 156, "OSC1 FINE TUNE", "Osc1 Fine", [] (int v) { return String ((int) std::lround ((v - 64) / 64.0 * 100.0)) + " ct"; });
        sel (OX + 168, OY, 156, "UNISON", "uni");
        sld (OX, OY + 46, 156, "UNISON DETUNE", "Unison Detune", [] (int v) { return String ((int) std::lround (v / 127.0 * 60.0)) + " ct"; });
        sld (OX + 168, OY + 46, 156, "UNISON STEREO SPREAD", "Unison Spread", [] (int v) { return pct (v); });
        setB (add (new TextLabel ("WAVETABLE: SHAPE KNOB MORPHS SINE-TRI-SAW-SQR-ORGAN-VOWEL-BUZZ-GLASS / PULSE W = PHASE WARP", 9.0f, Colour (0xff333333))), OX, OY + 98, 340, 26);
        y += h + GAP;
    }
    sdH = y - GAP + 14 - baseH;   // from baseH to the end of the last section (+ bottom padding)
    for (int i = sdStart; i < owned.size(); ++i) sdComps.push_back (owned[i]);

    foot = add (new Foot());
}
