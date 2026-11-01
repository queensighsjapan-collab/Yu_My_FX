#include "PluginEditor.h"
#include <BinaryData.h>
#include <set>

using namespace juce;

namespace
{
constexpr float W = (float) YM303Editor::designW, H = (float) YM303Editor::designH;

namespace pal
{
const Colour panelHi { 0xffd6d9dd }, panelLo { 0xffadb2b9 }, ink { 0xff16181b }, led { 0xffff3b2e }, ledOff { 0xff5b1713 },
             accent { 0xffe8541f }, keyW { 0xffebe6d8 }, keyB { 0xff2b2d31 }, btnHi { 0xff4d5056 }, btnLo { 0xff2a2c30 },
             screen { 0xff0a0b0d }, lcdRed { 0xffff4b36 }, grey { 0xff5c6168 };
}

// face: 0 medium, 1 semibold, 2 bold, 3 mono
Font F (int face, float h, float kern = 0.0f)
{
    static Typeface::Ptr t[4];
    if (t[0] == nullptr)
    {
        t[0] = Typeface::createSystemTypefaceFor (BinaryData::BarlowCondensedMedium_ttf, (size_t) BinaryData::BarlowCondensedMedium_ttfSize);
        t[1] = Typeface::createSystemTypefaceFor (BinaryData::BarlowCondensedSemiBold_ttf, (size_t) BinaryData::BarlowCondensedSemiBold_ttfSize);
        t[2] = Typeface::createSystemTypefaceFor (BinaryData::BarlowCondensedBold_ttf, (size_t) BinaryData::BarlowCondensedBold_ttfSize);
        t[3] = Typeface::createSystemTypefaceFor (BinaryData::ShareTechMonoRegular_ttf, (size_t) BinaryData::ShareTechMonoRegular_ttfSize);
    }
    return Font (FontOptions (t[face])).withHeight (h).withExtraKerningFactor (kern);
}

void text (Graphics& g, const String& s, Font f, Colour c, float cx, float cy, Justification j = Justification::centred, float w = 400.0f, float h = 40.0f)
{
    g.setFont (f); g.setColour (c);
    float x = cx;
    if (j.testFlags (Justification::horizontallyCentred)) x = cx - w * 0.5f;
    else if (j.testFlags (Justification::right)) x = cx - w;
    g.drawText (s, Rectangle<float> (x, cy - h * 0.5f, w, h), j, false);
}

void drawLed (Graphics& g, float cx, float cy, bool on)
{
    if (on) { g.setColour (pal::led.withAlpha (0.28f)); g.fillEllipse (cx - 8.0f, cy - 8.0f, 16.0f, 16.0f); }
    g.setColour (on ? pal::led : pal::ledOff);
    g.fillEllipse (cx - 4.0f, cy - 4.0f, 8.0f, 8.0f);
    g.setColour (Colours::white.withAlpha (on ? 0.5f : 0.12f));
    g.fillEllipse (cx - 2.2f, cy - 2.8f, 2.6f, 2.0f);
}

void drawCap (Graphics& g, Rectangle<float> r, bool pressed, Colour hi = pal::btnHi, Colour lo = pal::btnLo, float rad = 3.0f)
{
    if (pressed) r = r.translated (0.0f, 1.5f);
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (0.0f, 2.0f), rad);
    g.setGradientFill (ColourGradient (pressed ? hi.darker (0.25f) : hi, 0.0f, r.getY(), lo, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, rad);
    g.setColour (Colours::white.withAlpha (0.16f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + rad, r.getRight() - rad);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
}

void labelBar (Graphics& g, Rectangle<float> r, const String& s, float size = 15.0f)
{
    g.setColour (pal::ink);
    g.fillRoundedRectangle (r, 3.5f);
    text (g, s, F (2, size, 0.09f), Colours::white, r.getCentreX(), r.getCentreY(), Justification::centred, r.getWidth(), r.getHeight());
}

Point<float> polar (float cx, float cy, float rad, float deg)
{
    const float t = degreesToRadians (deg);
    return { cx + rad * std::sin (t), cy - rad * std::cos (t) };
}

const char* kNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
String noteName (int midi) { return String (kNames[midi % 12]) + String (midi / 12 - 1); }
const char* kGroups[] = { "I", "II", "III", "IV" };
const char* kModes[] = { "TRACK WRITE", "TRACK PLAY", "PATTERN PLAY", "PATTERN WRITE" };

// ------------------------------------------------------------------ rotary knob bound to a parameter
class Knob : public Component
{
public:
    Knob (RangedAudioParameter& param, float radius, const String& label, const String& lo, const String& hi, std::function<void (const String&)> onShow)
        : p (param), R (radius), name (label), loText (lo), hiText (hi), show (std::move (onShow)),
          att (param, [this] (float v) { norm = p.convertTo0to1 (v); repaint(); })
    {
        att.sendInitialUpdate();
        setSize ((int) (R * 2.0f + 56.0f), (int) (R * 2.0f + 52.0f));
        setMouseCursor (MouseCursor::UpDownResizeCursor);
    }

    Point<float> centre() const { return { getWidth() * 0.5f, getHeight() * 0.5f }; }

    void paint (Graphics& g) override
    {
        const auto c = centre();
        const float sk = R, cap = R * 0.66f;
        const float a0 = -135.0f, a1 = 135.0f;
        // scale
        g.setColour (pal::ink.withAlpha (0.85f));
        for (int i = 0; i <= 10; ++i)
        {
            const float deg = a0 + (a1 - a0) * (float) i / 10.0f;
            const bool lng = i % 5 == 0;
            g.drawLine (Line<float> (polar (c.x, c.y, sk + 3.0f, deg), polar (c.x, c.y, sk + (lng ? 9.0f : 6.5f), deg)), lng ? 1.3f : 1.0f);
        }
        const Font sf = F (1, 10.5f);
        if (loText.isEmpty())
        {
            text (g, "5", sf, pal::ink, c.x, c.y - sk - 17.0f, Justification::centred, 30.0f, 12.0f);
            text (g, "0", sf, pal::ink, c.x - sk * 0.72f - 9.0f, c.y + sk * 0.9f + 8.0f, Justification::centred, 30.0f, 12.0f);
            text (g, "10", sf, pal::ink, c.x + sk * 0.72f + 11.0f, c.y + sk * 0.9f + 8.0f, Justification::centred, 30.0f, 12.0f);
        }
        else
        {
            text (g, loText, sf, pal::ink, c.x - sk * 0.72f - 11.0f, c.y + sk * 0.9f + 12.0f, Justification::centred, 40.0f, 12.0f);
            text (g, hiText, sf, pal::ink, c.x + sk * 0.72f + 11.0f, c.y + sk * 0.9f + 12.0f, Justification::centred, 40.0f, 12.0f);
        }
        // body
        g.setColour (Colours::black.withAlpha (0.30f));
        g.fillEllipse (c.x - sk - 1.0f, c.y - sk + 1.5f, sk * 2.0f + 2.0f, sk * 2.0f + 2.0f);
        g.setGradientFill (ColourGradient (Colour (0xff3a3c41), c.x - sk * 0.3f, c.y - sk * 0.6f, Colour (0xff0f1012), c.x + sk * 0.5f, c.y + sk, true));
        g.fillEllipse (c.x - sk, c.y - sk, sk * 2.0f, sk * 2.0f);
        g.setGradientFill (ColourGradient (Colour (0xff5a5d63), c.x - cap * 0.35f, c.y - cap * 0.5f, Colour (0xff17181b), c.x + cap * 0.8f, c.y + cap * 0.9f, true));
        g.fillEllipse (c.x - cap, c.y - cap, cap * 2.0f, cap * 2.0f);
        g.setColour (Colours::white.withAlpha (0.10f));
        g.drawEllipse (c.x - cap, c.y - cap, cap * 2.0f, cap * 2.0f, 1.0f);
        // pointer
        const float deg = a0 + (a1 - a0) * norm;
        g.setColour (Colour (0xfff2f2ee));
        g.drawLine (Line<float> (polar (c.x, c.y, cap * 0.12f, deg), polar (c.x, c.y, cap * 0.95f, deg)), 2.4f);
    }

    void mouseDown (const MouseEvent& e) override { y0 = e.position.y; n0 = norm; att.beginGesture(); announce(); }
    void mouseDrag (const MouseEvent& e) override
    {
        set (n0 + (y0 - e.position.y) / (e.mods.isShiftDown() ? 680.0f : 170.0f));
    }
    void mouseUp (const MouseEvent&) override { att.endGesture(); }
    void mouseDoubleClick (const MouseEvent&) override
    {
        att.beginGesture(); att.setValueAsPartOfGesture (p.convertFrom0to1 (p.getDefaultValue())); att.endGesture();
        norm = p.getDefaultValue(); repaint(); announce();
    }
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override
    {
        att.beginGesture(); set (norm + (w.deltaY > 0 ? 0.02f : -0.02f)); att.endGesture();
    }

private:
    void set (float n)
    {
        norm = jlimit (0.0f, 1.0f, n);
        att.setValueAsPartOfGesture (p.convertFrom0to1 (norm));
        repaint(); announce();
    }
    void announce() { if (show) show (name + "  " + p.getText (norm, 8)); }

    RangedAudioParameter& p;
    float R, norm = 0.0f, y0 = 0.0f, n0 = 0.0f;
    String name, loText, hiText;
    std::function<void (const String&)> show;
    ParameterAttachment att;
};
} // namespace

// ================================================================== Panel
class YM303Editor::Panel : public Component, private Timer
{
public:
    Panel (YM303Processor& pr) : proc (pr), bank (pr.bank), ui (pr.ui)
    {
        backdrop = std::make_unique<Backdrop>();
        addAndMakeVisible (*backdrop);
        backdrop->setBounds (0, 0, designW, designH);

        auto show = [this] (const String& s) { flash (s); };
        auto addKnob = [&] (const char* id, float cx, float cy, float R, const String& label, const String& lo = {}, const String& hi = {})
        {
            auto* k = knobs.add (new Knob (*proc.apvts.getParameter (id), R, label, lo, hi, show));
            addAndMakeVisible (k);
            k->setCentrePosition ((int) cx, (int) cy);
        };
        addKnob ("tuning", 396, 54, 24, "TUNING");
        addKnob ("cutoff", 506, 54, 24, "CUT OFF FREQ");
        addKnob ("reso", 617, 54, 24, "RESONANCE");
        addKnob ("envmod", 728, 54, 24, "ENV MOD");
        addKnob ("decay", 838, 54, 24, "DECAY");
        addKnob ("accent", 949, 54, 24, "ACCENT");
        addKnob ("tempo", 91, 195, 29, "TEMPO", "Slow", "Fast");
        addKnob ("volume", 1112, 190, 29, "VOLUME", "Off", "Max");

        // hit regions (design coordinates)
        auto add = [this] (Id id, int arg, float x, float y, float w, float h) { hits.push_back ({ id, arg, { x, y, w, h } }); };
        add (Clear, 0, 29, 410, 100, 36);    add (Run, 0, 29, 488, 100, 43);
        add (BtnPitch, 0, 144, 439, 100, 35);   add (BtnTime, 0, 144, 527, 100, 35);
        const float blackX[5] = { 292, 361, 498, 566, 635 };  const int blackI[5] = { 1, 3, 6, 8, 10 };
        for (int i = 0; i < 5; ++i) add (Key, blackI[i], blackX[i], 413, 63, 35);
        const float whiteX[8] = { 258, 327, 395, 464, 533, 600, 669, 738 };  const int whiteI[8] = { 0, 2, 4, 5, 7, 9, 11, 12 };
        for (int i = 0; i < 8; ++i) add (Key, whiteI[i], whiteX[i], 483, 63, 39);
        const float fnX[4] = { 815, 893, 970, 1048 };
        for (int i = 0; i < 4; ++i) add (Fn, i, fnX[i], 454, 69, 36);
        const float secX[3] = { 815, 918, 1022 };
        for (int i = 0; i < 3; ++i) add (Sec, i, secX[i], 543, 95, 29);
        add (Back, 0, 1131, 397, 88, 36);    add (Next, 0, 1131, 470, 88, 44);
        add (LenDn, 0, 1006, 285, 29, 22);   add (LenUp, 0, 1064, 285, 29, 22);
        add (Del, 0, 1108, 285, 48, 22);     add (Ins, 0, 1163, 285, 45, 22);
        add (Host, 0, 1089, 606, 132, 21);   add (Wave, 0, 225, 53, 87, 24);
        for (int i = 0; i < 4; ++i) add (ModeItem, i, 578, 161.0f + 19.0f * (float) i, 160, 19);
        const float cellW = 68.3f, stride = 73.3f;
        for (int i = 0; i < 16; ++i) add (Cell, i, 40.0f + stride * (float) i, 311, cellW, 47);
        add (GroupRot, 0, 316, 153, 76, 76);
        add (ModeRot, 0, 497, 164, 54, 54);

        setWantsKeyboardFocus (false);
        startTimerHz (30);
    }

    ~Panel() override { stopTimer(); }

    // ---- computer keyboard
    bool handleKey (const KeyPress& k)
    {
        const auto c = (juce_wchar) CharacterFunctions::toLowerCase (k.getTextCharacter());
        if (k.getKeyCode() == KeyPress::spaceKey) { act (Run, 0); return true; }
        static const String keys = "awsedftgyhujk";
        const int idx = keys.indexOfChar (c);
        if (idx < 0 || k.getModifiers().isAnyModifierKeyDown()) return false;
        if (down.insert (c).second) act (Key, idx);
        return true;
    }
    void keysChanged()
    {
        for (auto it = down.begin(); it != down.end();)
            it = KeyPress::isKeyCurrentlyDown ((int) CharacterFunctions::toUpperCase (*it)) ? std::next (it) : down.erase (it);
    }

    void mouseDown (const MouseEvent& e) override
    {
        for (auto it = hits.rbegin(); it != hits.rend(); ++it)
            if (it->r.contains (e.position))
            {
                int arg = it->arg;
                if (it->id == GroupRot) { arg = e.position.x < it->r.getCentreX() ? -1 : 1; }
                if (it->id == ModeRot)  { arg = e.position.x < it->r.getCentreX() ? -1 : 1; }
                act (it->id, arg);
                return;
            }
    }
    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        for (auto& h : hits)
            if ((h.id == GroupRot || h.id == ModeRot) && h.r.contains (e.position))
                act (h.id, w.deltaY > 0 ? 1 : -1);
    }

    void paintOverChildren (Graphics& g) override
    {
        const bool trk = trackMode(), pw = patWrite(), running = proc.isPlayingNow();
        const int ent = proc.playingEntry();
        const int ePat = proc.selectedPattern(), eSec = proc.editSection();
        const int vPat = ent >= 0 ? ent / 32 : ePat, vSec = ent >= 0 ? (ent / 16) & 1 : eSec, vStep = ent >= 0 ? (ent & 15) : ui.cursor;
        const auto cur = bank.getStep (vPat, vSec, vStep);
        const int len = bank.getLen (vPat, vSec);

        // ---- waveform switch
        const bool sqr = proc.apvts.getRawParameterValue ("wave")->load() > 0.5f;
        text (g, "SAW", F (2, 14.0f, 0.09f), pal::ink.withAlpha (sqr ? 0.4f : 1.0f), 241, 42, Justification::centred, 50.0f, 18.0f);
        text (g, "SQR", F (2, 14.0f, 0.09f), pal::ink.withAlpha (sqr ? 1.0f : 0.4f), 297, 42, Justification::centred, 50.0f, 18.0f);
        {
            Rectangle<float> s (225, 53, 87, 24);
            g.setColour (Colour (0xff08090b)); g.fillRoundedRectangle (s, 4.0f);
            g.setColour (Colours::white.withAlpha (0.18f)); g.drawRoundedRectangle (s.reduced (0.5f), 4.0f, 1.0f);
            Rectangle<float> knob (sqr ? s.getRight() - 44.0f : s.getX() + 2.0f, s.getY() + 2.0f, 42.0f, s.getHeight() - 4.0f);
            g.setGradientFill (ColourGradient (Colour (0xff6a6d73), 0.0f, knob.getY(), Colour (0xff2a2c30), 0.0f, knob.getBottom(), false));
            g.fillRoundedRectangle (knob, 3.0f);
            g.setColour (Colours::black.withAlpha (0.55f));
            for (float x = knob.getX() + 5.0f; x < knob.getRight() - 3.0f; x += 4.0f) g.drawVerticalLine ((int) x, knob.getY() + 3.0f, knob.getBottom() - 3.0f);
        }

        // ---- tempo LCD
        {
            Rectangle<float> r (154, 171, 87, 36);
            g.setColour (pal::screen); g.fillRoundedRectangle (r, 5.0f);
            g.setColour (Colours::white.withAlpha (0.14f)); g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
            g.setFont (F (3, 31.0f, 0.05f));
            g.setColour (pal::lcdRed.withAlpha (0.10f)); g.drawText ("888", r, Justification::centred, false);
            g.setColour (pal::lcdRed);
            g.drawText (String (roundToInt (proc.apvts.getRawParameterValue ("tempo")->load())), r, Justification::centred, false);
        }

        // ---- rotary selectors
        const int grp = jlimit (0, 3, (int) proc.apvts.getRawParameterValue ("group")->load());
        const int mode = modeIndex();
        drawRotary (g, 354, 191, 31, kGroupAng[grp]);
        drawRotary (g, 524, 191, 23, kModeAng[mode]);
        for (int i = 0; i < 4; ++i)
        {
            drawLed (g, 580, 170.0f + 19.0f * (float) i, i == mode);
            text (g, kModes[i], F (2, 16.0f, 0.07f), pal::ink, 596, 170.0f + 19.0f * (float) i, Justification::centredLeft, 150.0f, 18.0f);
        }

        // ---- step monitor
        {
            const String st = statusText (trk, vPat, vSec, vStep, cur, running);
            g.setFont (F (3, 15.5f, 0.02f));
            g.setColour (pal::lcdRed);
            g.drawText (st, Rectangle<float> (40, 285, 925, 22), Justification::centredLeft, true);
            text (g, "LEN", F (3, 14.0f), pal::lcdRed, 975, 296, Justification::centredLeft, 40.0f, 18.0f);
            text (g, String (bank.getLen (ePat, eSec)), F (3, 16.0f), Colours::white, 1050, 296, Justification::centred, 36.0f, 20.0f);
            for (auto id : { LenDn, LenUp, Del, Ins })
            {
                const auto r = rectOf (id, 0);
                drawCap (g, r, isPressed (id, 0), Colour (0xff3b3e44), Colour (0xff1d1f23), 4.0f);
                const String s = id == LenDn ? String (CharPointer_UTF8 ("\xe2\x88\x92")) : id == LenUp ? "+" : id == Del ? "DEL" : "INS";
                text (g, s, F (2, 14.0f, 0.06f), Colours::white, r.getCentreX(), r.getCentreY(), Justification::centred, r.getWidth(), r.getHeight());
            }
            const auto stepsSec = [&] (int i) { return bank.getStep (vPat, vSec, i); };
            const int vlen = len;
            for (int i = 0; i < 16; ++i)
            {
                const auto r = rectOf (Cell, i);
                const auto s = stepsSec (i);
                const bool out = i >= vlen, isCur = (i == ui.cursor && ent < 0 && ! trk), isPlay = (ent >= 0 && i == vStep);
                g.setColour (isPlay ? Colour (0xff3d1713) : Colour (0xff0f1013));
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (isPlay ? pal::led : isCur ? pal::accent : Colour (0xff2b2c30));
                g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, isCur || isPlay ? 1.6f : 1.0f);
                const float a = out ? 0.28f : 1.0f;
                text (g, String (i + 1), F (1, 10.5f), Colour (0xff9a9fa7).withAlpha (a), r.getX() + 6.0f, r.getY() + 8.0f, Justification::centredLeft, 24.0f, 12.0f);
                if (isCur || isPlay) drawLed (g, r.getRight() - 8.0f, r.getY() + 8.0f, true);
                const Colour nc = pal::lcdRed.withAlpha (a);
                if (s.type == ym303::Note)
                {
                    text (g, s.note == 12 ? "C+" : kNames[s.note % 12], F (3, 21.0f), nc, r.getCentreX() - (s.up || s.dn ? 3.0f : 0.0f), r.getY() + 23.0f, Justification::centred, 50.0f, 24.0f);
                    if (s.up || s.dn)
                    {
                        Path tri; const float tx = r.getCentreX() + 17.0f, ty = r.getY() + 25.0f;
                        if (s.up) tri.addTriangle (tx - 4.0f, ty + 3.0f, tx + 4.0f, ty + 3.0f, tx, ty - 4.0f);
                        else      tri.addTriangle (tx - 4.0f, ty - 3.0f, tx + 4.0f, ty - 3.0f, tx, ty + 4.0f);
                        g.setColour (nc); g.fillPath (tri);
                    }
                }
                else if (s.type == ym303::Tie) { g.setColour (nc); g.fillRect (r.getCentreX() - 8.0f, r.getY() + 25.0f, 16.0f, 2.2f); }
                else { g.setColour (nc.withAlpha (a * 0.8f)); g.fillRect (r.getCentreX() - 5.0f, r.getY() + 24.0f, 10.0f, 2.0f); }
                const String dot = String (CharPointer_UTF8 ("\xc2\xb7"));
                const String fl = (s.acc ? String ("A") : dot) + (s.sld ? String ("S") : dot);
                text (g, fl, F (3, 11.5f), Colour (0xffd9a441).withAlpha (a), r.getCentreX(), r.getBottom() - 8.0f, Justification::centred, 40.0f, 12.0f);
            }
        }

        // ---- left column
        text (g, armClear() ? "SURE?" : "PATTERN", F (2, 15.0f, 0.08f), pal::ink, 79, 384, Justification::centred, 120.0f, 16.0f);
        text (g, armClear() ? "PRESS AGAIN" : "CLEAR", F (2, 15.0f, 0.08f), pal::ink, 79, 398, Justification::centred, 120.0f, 16.0f);
        drawCap (g, rectOf (Clear, 0), isPressed (Clear, 0));
        drawLed (g, 59, 477, running);
        drawCap (g, rectOf (Run, 0), isPressed (Run, 0));
        drawLed (g, 194, 430, ui.pitch);  drawCap (g, rectOf (BtnPitch, 0), isPressed (BtnPitch, 0));
        drawLed (g, 194, 518, ui.time);   drawCap (g, rectOf (BtnTime, 0), isPressed (BtnTime, 0));

        // ---- keys
        const bool timeW = ui.time && ui.write;
        for (auto& h : hits)
        {
            if (h.id != Key) continue;
            const bool black = (h.arg == 1 || h.arg == 3 || h.arg == 6 || h.arg == 8 || h.arg == 10);
            const bool lit = (cur.type == ym303::Note && cur.note == h.arg) && (ent >= 0 || (! trk && ui.pitch));
            drawLed (g, h.r.getCentreX(), h.r.getY() - (black ? 9.0f : 10.0f), lit);
            if (black) drawCap (g, h.r, isPressed (Key, h.arg), Colour (0xff4a4d53), pal::keyB);
            else       drawCap (g, h.r, isPressed (Key, h.arg), Colour (0xfff4f0e4), Colour (0xffd9d3c2));
        }
        {
            const float whiteX[8] = { 258, 327, 395, 464, 533, 600, 669, 738 };
            for (int i = 0; i < 8; ++i)
            {
                const bool on = ! ui.pitch && ! ui.time && i == (int) proc.apvts.getRawParameterValue ("pattern")->load() - 1;
                text (g, String (i + 1), F (2, 16.0f), on ? pal::led : pal::accent.withAlpha (0.75f), whiteX[i] + 31.5f, 535, Justification::centred, 40.0f, 18.0f);
            }
        }

        // ---- function buttons and sections
        for (int i = 0; i < 4; ++i)
        {
            const auto r = rectOf (Fn, i);
            bool on = false;
            if (i == 0) on = timeW ? cur.type == ym303::Note : cur.dn;
            if (i == 1) on = timeW ? cur.type == ym303::Tie : cur.up;
            if (i == 2) on = timeW ? cur.type == ym303::Rest : cur.acc;
            if (i == 3) on = cur.sld;
            drawLed (g, r.getCentreX(), r.getY() - 9.0f, on);
            drawCap (g, r, isPressed (Fn, i));
        }
        const int sec = jlimit (0, 2, (int) proc.apvts.getRawParameterValue ("section")->load());
        for (int i = 0; i < 3; ++i)
        {
            const auto r = rectOf (Sec, i);
            drawLed (g, r.getCentreX(), r.getY() - 9.0f, ! trk && sec == i);
            drawCap (g, r, isPressed (Sec, i));
        }
        drawCap (g, rectOf (Back, 0), isPressed (Back, 0));
        drawCap (g, rectOf (Next, 0), isPressed (Next, 0));

        // ---- footer: host sync
        {
            const bool hs = proc.apvts.getRawParameterValue ("hostsync")->load() > 0.5f;
            const auto r = rectOf (Host, 0);
            g.setColour (Colours::white.withAlpha (hs ? 0.7f : 0.3f));
            g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
            drawLed (g, r.getX() + 13.0f, r.getCentreY(), hs);
            text (g, hs ? "HOST SYNC  ON" : "HOST SYNC  OFF",
                  F (2, 13.0f, 0.12f), Colours::white.withAlpha (0.9f), r.getX() + 24.0f, r.getCentreY(), Justification::centredLeft, 200.0f, 16.0f);
        }
        juce::ignoreUnused (pw);
    }

private:
    enum Id { Clear, Run, BtnPitch, BtnTime, Key, Fn, Sec, Back, Next, LenDn, LenUp, Del, Ins, Host, Wave, Cell, ModeItem, GroupRot, ModeRot };
    struct Hit { Id id; int arg; Rectangle<float> r; };

    static constexpr float kGroupAng[4] = { -125.0f, -45.0f, 45.0f, 125.0f };
    static constexpr float kModeAng[4]  = { 58.0f, 80.0f, 100.0f, 122.0f };

    // ------------------------------------------------------------ static artwork (cached)
    class Backdrop : public Component
    {
    public:
        Backdrop() { setInterceptsMouseClicks (false, false); setBufferedToImage (true); }
        void paint (Graphics& g) override
        {
            // body
            Rectangle<float> body (0, 0, W, H);
            g.setColour (Colour (0xff17181a)); g.fillAll();
            g.setGradientFill (ColourGradient (pal::panelHi, 0, 0, pal::panelLo, 0, H, false));
            g.fillRoundedRectangle (body, 16.0f);
            Random rnd (7);
            for (float x = 4.0f; x < W - 4.0f; x += 1.0f)
            {
                g.setColour ((rnd.nextBool() ? Colours::white : Colours::black).withAlpha (0.01f + rnd.nextFloat() * 0.035f));
                g.drawVerticalLine ((int) x, 3.0f, H - 3.0f);
            }
            g.setColour (Colours::white.withAlpha (0.55f)); g.drawRoundedRectangle (body.reduced (1.0f), 16.0f, 1.2f);
            for (float sx : { 15.0f, W - 15.0f })
            {
                g.setColour (Colour (0xff8a8e95)); g.fillEllipse (sx - 6.0f, 10.0f, 12.0f, 12.0f);
                g.setColour (Colour (0xffe6e8eb)); g.fillEllipse (sx - 4.5f, 11.0f, 9.0f, 9.0f);
                g.setColour (Colour (0xff6d7077)); g.drawLine (sx - 4.0f, 18.0f, sx + 4.0f, 13.5f, 1.4f);
            }

            // brand and header
            {
                Path w;
                for (int k = 0; k < 3; ++k)
                {
                    const float y = 55.0f + 9.0f * (float) k;
                    w.startNewSubPath (36.0f, y);
                    for (float x = 36.0f; x <= 81.0f; x += 1.0f) w.lineTo (x, y + 3.4f * std::sin ((x - 36.0f) * 0.30f));
                }
                g.setColour (pal::ink); g.strokePath (w, PathStrokeType (3.2f, PathStrokeType::curved, PathStrokeType::rounded));
            }
            text (g, "Yu_My", F (2, 44.0f), pal::ink, 89, 63, Justification::centredLeft, 150.0f, 52.0f);
            text (g, "WAVEFORM", F (2, 14.0f, 0.09f), pal::ink, 268, 88, Justification::centred, 120.0f, 18.0f);
            const char* names[6] = { "TUNING", "CUT OFF FREQ", "RESONANCE", "ENV MOD", "DECAY", "ACCENT" };
            const float xs[6] = { 396, 506, 617, 728, 838, 949 };
            for (int i = 0; i < 6; ++i)
            {
                if (i == 1)
                {
                    text (g, "CUT OFF", F (2, 15.0f, 0.07f), pal::ink, xs[i], 97, Justification::centred, 120.0f, 18.0f);
                    text (g, "FREQ", F (2, 15.0f, 0.07f), pal::ink, xs[i], 110, Justification::centred, 120.0f, 18.0f);
                }
                else text (g, names[i], F (2, 15.0f, 0.07f), pal::ink, xs[i], 97, Justification::centred, 120.0f, 18.0f);
            }
            g.setColour (pal::ink.withAlpha (0.35f));
            g.fillRect (332.0f, 12.0f, 1.0f, 105.0f);
            g.fillRect (1013.0f, 12.0f, 1.0f, 105.0f);
            g.fillRect (27.0f, 123.0f, W - 54.0f, 1.0f);
            g.setColour (Colours::white.withAlpha (0.5f)); g.fillRect (27.0f, 124.0f, W - 54.0f, 1.0f);
            text (g, "Bass Line", F (2, 42.0f), pal::ink, 1122, 63, Justification::centred, 220.0f, 52.0f);

            // mid band
            text (g, "TEMPO", F (2, 15.0f, 0.08f), pal::ink, 91, 250, Justification::centred, 100.0f, 18.0f);
            text (g, "BPM", F (2, 15.0f, 0.08f), pal::ink, 197, 220, Justification::centred, 60.0f, 18.0f);
            text (g, "PATT. GROUP", F (2, 15.0f, 0.08f), pal::ink, 340, 262, Justification::centred, 140.0f, 18.0f);
            for (int i = 0; i < 4; ++i)
            {
                const auto a = polar (354.0f, 191.0f, 38.0f, kGroupAng[i]), b = polar (354.0f, 191.0f, 43.0f, kGroupAng[i]);
                g.setColour (pal::ink); g.drawLine (a.x, a.y, b.x, b.y, 1.3f);
            }
            text (g, "I", F (2, 13.0f), pal::ink, 310, 220, Justification::centred, 24.0f, 16.0f);
            text (g, "II", F (2, 13.0f), pal::ink, 317, 153, Justification::centred, 24.0f, 16.0f);
            text (g, "III", F (2, 13.0f), pal::ink, 390, 153, Justification::centred, 24.0f, 16.0f);
            text (g, "IV", F (2, 13.0f), pal::ink, 397, 220, Justification::centred, 24.0f, 16.0f);
            text (g, "MODE", F (2, 15.0f, 0.08f), pal::ink, 524, 247, Justification::centred, 100.0f, 18.0f);
            for (int i = 0; i < 4; ++i)
            {
                const auto a = polar (524.0f, 191.0f, 28.0f, kModeAng[i]), b = polar (524.0f, 191.0f, 33.0f, kModeAng[i]);
                g.setColour (pal::ink); g.drawLine (a.x, a.y, b.x, b.y, 1.2f);
            }
            {
                const float tw = F (2, 58.0f).getStringWidthFloat ("Yu_My ") + F (2, 58.0f).getStringWidthFloat ("303");
                const float x0 = 868.0f - tw * 0.5f;
                text (g, "Yu_My ", F (2, 58.0f), pal::ink, x0, 186, Justification::centredLeft, 300.0f, 66.0f);
                text (g, "303", F (2, 58.0f), pal::accent, x0 + F (2, 58.0f).getStringWidthFloat ("Yu_My "), 186, Justification::centredLeft, 200.0f, 66.0f);
                text (g, "ACID BASS LINE", F (0, 15.0f, 0.30f), pal::grey, 868, 228, Justification::centred, 300.0f, 18.0f);
            }
            text (g, "VOLUME", F (2, 15.0f, 0.08f), pal::ink, 1112, 251, Justification::centred, 100.0f, 18.0f);

            // step monitor frame
            {
                Rectangle<float> r (29, 279, 1191, 87);
                g.setColour (Colours::black.withAlpha (0.35f)); g.fillRoundedRectangle (r.translated (0, 2), 8.0f);
                g.setColour (pal::screen); g.fillRoundedRectangle (r, 8.0f);
                g.setColour (Colours::white.withAlpha (0.10f)); g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
            }

            // key bed labels
            labelBar (g, { 143, 379, 100, 37 }, "", 15.0f);
            text (g, "PITCH", F (2, 15.0f, 0.09f), Colours::white, 193, 391, Justification::centred, 100.0f, 16.0f);
            text (g, "MODE", F (2, 15.0f, 0.09f), Colours::white, 193, 405, Justification::centred, 100.0f, 16.0f);
            labelBar (g, { 143, 482, 100, 23 }, "TIME MODE", 14.0f);
            text (g, "RUN / STOP", F (2, 15.0f, 0.08f), pal::ink, 79, 459, Justification::centred, 120.0f, 16.0f);
            text (g, "RUN", F (2, 14.0f, 0.08f), pal::ink, 70, 476, Justification::centredLeft, 60.0f, 16.0f);
            const char* bl[5] = { "C#", "D#", "F#", "G#", "A#" };
            const float bx[5] = { 323, 392, 529, 597, 666 };
            for (int i = 0; i < 5; ++i) labelBar (g, { bx[i] - 16.0f, 378.0f, 32.0f, 17.0f }, bl[i], 13.0f);
            const char* wl[8] = { "C", "D", "E", "F", "G", "A", "B", "C" };
            const float wx[8] = { 258, 327, 395, 464, 533, 600, 669, 738 };
            for (int i = 0; i < 8; ++i) text (g, wl[i], F (2, 15.0f), pal::ink, wx[i] + 31.5f, 457, Justification::centred, 30.0f, 16.0f);
            text (g, "PATTERN", F (1, 12.0f, 0.18f), pal::grey, 529, 552, Justification::centred, 120.0f, 14.0f);

            labelBar (g, { 815, 378, 302, 24 }, String (CharPointer_UTF8 ("TRANSPOSE \xc2\xb7 TIME")), 14.0f);
            const char* fn[4] = { "DOWN", "UP", "ACCENT", "SLIDE" };
            const char* sub[4] = { "\xe2\x97\x8f NOTE", "\xe2\x97\x8b TIE", "REST", "" };
            const float fx[4] = { 850, 927, 1005, 1082 };
            for (int i = 0; i < 4; ++i)
            {
                text (g, fn[i], F (2, 15.0f, 0.08f), pal::ink, fx[i], 416, Justification::centred, 90.0f, 16.0f);
                text (g, String (CharPointer_UTF8 (sub[i])), F (1, 11.0f, 0.08f), pal::grey, fx[i], 431, Justification::centred, 90.0f, 14.0f);
            }
            labelBar (g, { 815, 497, 302, 24 }, "PATT. SECTION", 14.0f);
            text (g, "A", F (2, 15.0f), pal::ink, 862, 579, Justification::centred, 60.0f, 16.0f);
            text (g, "B", F (2, 15.0f), pal::ink, 966, 579, Justification::centred, 60.0f, 16.0f);
            text (g, String (CharPointer_UTF8 ("A\xe2\x86\x92""B")), F (2, 15.0f), pal::ink, 1069, 579, Justification::centred, 60.0f, 16.0f);
            text (g, "BACK", F (2, 15.0f, 0.08f), pal::ink, 1175, 384, Justification::centred, 100.0f, 16.0f);
            text (g, "WRITE /", F (2, 15.0f, 0.08f), pal::ink, 1175, 449, Justification::centred, 100.0f, 16.0f);
            text (g, "NEXT", F (2, 15.0f, 0.08f), pal::ink, 1175, 462, Justification::centred, 100.0f, 16.0f);

            // footer
            Rectangle<float> f (0, 600, W, 34);
            Path fp; fp.addRoundedRectangle (f.getX(), f.getY(), f.getWidth(), f.getHeight(), 14.0f, 14.0f, false, false, true, true);
            g.setGradientFill (ColourGradient (Colour (0xff2c2e32), 0, f.getY(), Colour (0xff111214), 0, f.getBottom(), false));
            g.fillPath (fp);
            text (g, "16 STEP SEQUENCER", F (1, 12.5f, 0.22f), Colour (0xffb4b8bf), 27, 617, Justification::centredLeft, 300.0f, 16.0f);
            text (g, "YU_MY 303 / ACID BASS LINE", F (1, 12.5f, 0.22f), Colour (0xffb4b8bf), 616, 617, Justification::centred, 400.0f, 16.0f);
        }
    };

    // ------------------------------------------------------------ helpers
    static void drawRotary (Graphics& g, float cx, float cy, float R, float deg)
    {
        g.setColour (Colours::black.withAlpha (0.28f)); g.fillEllipse (cx - R - 1.0f, cy - R + 1.5f, 2 * R + 2.0f, 2 * R + 2.0f);
        g.setGradientFill (ColourGradient (Colour (0xff3a3c41), cx - R * 0.3f, cy - R * 0.6f, Colour (0xff0f1012), cx + R * 0.5f, cy + R, true));
        g.fillEllipse (cx - R, cy - R, 2 * R, 2 * R);
        g.setColour (Colours::white.withAlpha (0.08f)); g.drawEllipse (cx - R, cy - R, 2 * R, 2 * R, 1.0f);
        // pointer bar through the centre
        Path bar; bar.addRoundedRectangle (-R * 0.16f, -R * 0.95f, R * 0.32f, R * 1.15f, R * 0.06f);
        g.setGradientFill (ColourGradient (Colour (0xff6a6d73), cx, cy - R, Colour (0xff2a2c30), cx, cy + R * 0.2f, false));
        g.fillPath (bar, AffineTransform::rotation (degreesToRadians (deg)).translated (cx, cy));
        g.setColour (Colour (0xfff2f2ee));
        const auto a = polar (cx, cy, R * 0.15f, deg), b = polar (cx, cy, R * 0.85f, deg);
        g.drawLine (a.x, a.y, b.x, b.y, 2.2f);
    }

    Rectangle<float> rectOf (Id id, int arg) const
    {
        for (auto& h : hits) if (h.id == id && h.arg == arg) return h.r;
        return {};
    }
    bool isPressed (Id id, int arg) const { return pressedId == (int) id * 100 + arg && Time::getMillisecondCounter() < pressedUntil; }
    bool armClear() const { return Time::getMillisecondCounter() < armUntil; }

    bool trackMode() const { return proc.apvts.getRawParameterValue ("playmode")->load() > 0.5f; }
    bool patWrite() const { return ! trackMode() && ui.write; }
    int modeIndex() const { return trackMode() ? (ui.write ? 0 : 1) : (ui.write ? 3 : 2); }

    void flash (const String& s) { flashText = s; flashUntil = Time::getMillisecondCounter() + 1400; }

    String statusText (bool trk, int vPat, int vSec, int vStep, const ym303::Step& cur, bool running) const
    {
        if (Time::getMillisecondCounter() < flashUntil) return flashText.toUpperCase();
        if (armClear()) return "PRESS PATTERN CLEAR AGAIN TO ERASE";
        auto trackList = [this]
        {
            String s;
            for (int i = 0; i < bank.trackLength(); ++i) { const int p = bank.trackAt (i); s << (i ? " " : "") << kGroups[p >> 3] << "-" << ((p & 7) + 1); }
            return s;
        };
        const int pid = proc.selectedPattern();
        if (trk && ui.write)
            return "TRACK WRITE | " + (bank.trackLength() ? trackList() : String ("EMPTY - PRESS PATTERN 1-8")) + " | NEXT: " + kGroups[pid >> 3] + "-" + String ((pid & 7) + 1);
        if (trk) return "TRACK PLAY | " + (bank.trackLength() ? trackList() : String ("EMPTY"));
        const int sec = (int) proc.apvts.getRawParameterValue ("section")->load();
        const String lbl = String (ui.write ? "WRITE " : "PLAY ") + kGroups[pid >> 3] + "-" + String ((pid & 7) + 1) + " " + (sec == 2 ? "A>B" : sec == 1 ? "B" : "A");
        const String nm = cur.type == ym303::Note ? noteName (cur.midi()) : cur.type == ym303::Tie ? "TIE" : "REST";
        juce::ignoreUnused (vPat, vSec, running);
        return lbl + " | STEP " + String (vStep + 1).paddedLeft ('0', 2) + " | " + nm + (cur.acc ? " ACC" : "") + (cur.sld ? " SLD" : "");
    }

    void timerCallback() override { repaint(); }

    // ------------------------------------------------------------ parameter helpers
    void setNorm (const char* id, float real)
    {
        if (auto* p = proc.apvts.getParameter (id))
        {
            p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (real)); p->endChangeGesture();
        }
    }
    float getReal (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    // ------------------------------------------------------------ operation logic (port of the browser version)
    int pat() const { return proc.selectedPattern(); }
    int esec() const { return proc.editSection(); }
    ym303::Step curStep() const { return bank.getStep (pat(), esec(), ui.cursor); }
    void writeStep (const ym303::Step& s) { bank.setStep (pat(), esec(), ui.cursor, s); proc.refreshEditView(); }
    void advance() { ui.cursor = (ui.cursor + 1) % jmax (1, bank.getLen (pat(), esec())); }

    void press (Id id, int arg) { pressedId = (int) id * 100 + arg; pressedUntil = Time::getMillisecondCounter() + 120; }

    static int whiteSel (int idx) { switch (idx) { case 0: return 0; case 2: return 1; case 4: return 2; case 5: return 3; case 7: return 4; case 9: return 5; case 11: return 6; case 12: return 7; default: return -1; } }

    void pressKey (int idx)
    {
        press (Key, idx);
        const bool trk = trackMode();
        const int sel = whiteSel (idx);
        if (trk || (! ui.pitch && ! ui.time))
        {
            if (sel >= 0)
            {
                const int pid = jlimit (0, 3, (int) getReal ("group")) * 8 + sel;
                setNorm ("pattern", (float) (sel + 1));
                if (trk && ui.write && bank.trackLength() < ym303::kMaxTrack)
                {
                    int t[ym303::kMaxTrack]; const int n = bank.trackLength();
                    for (int i = 0; i < n; ++i) t[i] = bank.trackAt (i);
                    t[n] = pid; bank.setTrack (t, n + 1);
                }
                ui.cursor = 0;
            }
            return;
        }
        if (ui.pitch && patWrite())
        {
            auto s = curStep(); s.note = idx; s.type = ym303::Note;
            writeStep (s);
            if (proc.isPlayingNow()) return;
            proc.previewNote (s.midi(), s.acc);
            advance();
        }
        else if (ui.pitch)
        {
            ym303::Step s; s.note = idx; s.type = ym303::Note;
            proc.previewNote (s.midi(), false);
        }
    }

    void onFn (int which)
    {
        press (Fn, which);
        if (! patWrite()) return;
        auto s = curStep();
        if (ui.time)
        {
            if (which == 0) s.type = ym303::Note; else if (which == 1) s.type = ym303::Tie; else if (which == 2) s.type = ym303::Rest; else s.sld = ! s.sld;
            writeStep (s); advance(); return;
        }
        if (which == 0) { s.dn = ! s.dn; s.up = false; }
        else if (which == 1) { s.up = ! s.up; s.dn = false; }
        else if (which == 2) s.acc = ! s.acc;
        else s.sld = ! s.sld;
        writeStep (s);
    }

    void editLen (int d)
    {
        if (! patWrite()) return;
        const int L = jlimit (1, 16, bank.getLen (pat(), esec()) + d);
        bank.setLen (pat(), esec(), L);
        if (ui.cursor >= L) ui.cursor = L - 1;
        proc.refreshEditView();
    }

    void act (Id id, int arg)
    {
        if (id != Key && id != GroupRot && id != ModeRot) press (id, arg);
        switch (id)
        {
            case Key: pressKey (arg); break;
            case Fn:  onFn (arg); break;
            case BtnPitch: ui.pitch = ! ui.pitch; if (ui.pitch) ui.time = false; break;
            case BtnTime:  ui.time = ! ui.time;  if (ui.time) ui.pitch = false; break;
            case Next: if (patWrite()) advance(); break;
            case Back: if (patWrite()) { const int L = jmax (1, bank.getLen (pat(), esec())); ui.cursor = (ui.cursor + L - 1) % L; } break;
            case LenDn: editLen (-1); break;
            case LenUp: editLen (1); break;
            case Cell: ui.cursor = arg; break;
            case Sec:
                if (! trackMode()) { setNorm ("section", (float) arg); ui.cursor = jmin (ui.cursor, jmax (0, bank.getLen (pat(), arg == 1 ? 1 : 0) - 1)); }
                break;
            case Wave: setNorm ("wave", getReal ("wave") > 0.5f ? 0.0f : 1.0f); break;
            case Host: setNorm ("hostsync", getReal ("hostsync") > 0.5f ? 0.0f : 1.0f); break;
            case GroupRot: setNorm ("group", (float) jlimit (0, 3, (int) getReal ("group") + arg)); break;
            case ModeRot: setMode (jlimit (0, 3, modeIndex() + arg)); break;
            case ModeItem: setMode (arg); break;
            case Run:
                if (proc.hostIsDriving()) { flash ("DAW TRANSPORT IS THE CLOCK - PLAY IN THE DAW (OR HOST SYNC OFF)"); break; }
                if (getReal ("run") < 0.5f && trackMode() && bank.trackLength() == 0) { flash ("TRACK EMPTY"); break; }
                setNorm ("run", getReal ("run") > 0.5f ? 0.0f : 1.0f);
                break;
            case Del:
                if (trackMode() && ui.write) { const int n = bank.trackLength(); if (n > 0) { int t[ym303::kMaxTrack]; for (int i = 0; i < n - 1; ++i) t[i] = bank.trackAt (i); bank.setTrack (t, n - 1); } break; }
                if (! patWrite()) break;
                shiftSteps (false);
                break;
            case Ins: if (patWrite()) shiftSteps (true); break;
            case Clear:
                if (! ui.write) break;
                if (trackMode()) { bank.setTrack (nullptr, 0); break; }
                if (! armClear()) { armUntil = Time::getMillisecondCounter() + 2200; break; }
                armUntil = 0;
                for (int s = 0; s < 2; ++s) { for (int i = 0; i < 16; ++i) bank.setStep (pat(), s, i, ym303::Step()); bank.setLen (pat(), s, 16); }
                ui.cursor = 0; proc.refreshEditView();
                break;
            default: break;
        }
    }

    void setMode (int m)
    {
        setNorm ("playmode", m < 2 ? 1.0f : 0.0f);
        ui.write = (m == 0 || m == 3);
        armUntil = 0;
        if (m < 2) setNorm ("section", 0.0f);
    }

    void shiftSteps (bool insert)
    {
        const int p = pat(), s = esec(), L = bank.getLen (p, s), c = jmin (ui.cursor, L - 1);
        ym303::Step v[16];
        for (int i = 0; i < 16; ++i) v[i] = bank.getStep (p, s, i);
        if (insert) { for (int i = L - 1; i > c; --i) v[i] = v[i - 1]; v[c] = ym303::Step(); }
        else        { for (int i = c; i < L - 1; ++i) v[i] = v[i + 1]; v[L - 1] = ym303::Step(); }
        for (int i = 0; i < 16; ++i) bank.setStep (p, s, i, v[i]);
        proc.refreshEditView();
    }

    YM303Processor& proc;
    ym303::Bank& bank;
    YM303Processor::UiState& ui;
    std::unique_ptr<Backdrop> backdrop;
    OwnedArray<Knob> knobs;
    std::vector<Hit> hits;
    std::set<juce_wchar> down;
    String flashText; uint32 flashUntil = 0, armUntil = 0, pressedUntil = 0; int pressedId = -1;
};

// ================================================================== Editor
YM303Editor::YM303Editor (YM303Processor& p) : AudioProcessorEditor (p), proc (p)
{
    panel = std::make_unique<Panel> (p);
    addAndMakeVisible (*panel);
    panel->setBounds (0, 0, designW, designH);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) designW / designH);
    setResizeLimits (624, 317, 1872, 951);
    setWantsKeyboardFocus (true);
    addMouseListener (this, true);
    setSize (1040, (int) (1040.0 * designH / designW));
}

YM303Editor::~YM303Editor() { removeMouseListener (this); }

void YM303Editor::paint (Graphics& g) { g.fillAll (Colour (0xff17181a)); }

void YM303Editor::resized()
{
    const float s = (float) getWidth() / (float) designW;
    panel->setTransform (AffineTransform::scale (s));
}

bool YM303Editor::keyPressed (const KeyPress& k) { return panel->handleKey (k); }
bool YM303Editor::keyStateChanged (bool) { panel->keysChanged(); return false; }
