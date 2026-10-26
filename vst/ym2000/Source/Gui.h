// YM2000 GUI parts. Layout follows the hardware-style panel of the HTML version; the colours are original.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>

namespace ymgui
{
using namespace juce;

// ------------------------------------------------------------------ palette
namespace pal
{
const Colour body        { 0xff2a1a10 };
const Colour panelTop    { 0xffefe7d0 };   // cream panel
const Colour panelBot    { 0xffe2d8bb };
const Colour ink         { 0xff26211b };   // silk-screen print on the cream panel
const Colour inkLight    { 0xfff1ead8 };   // print on black parts
const Colour muted       { 0xff6e6656 };
const Colour amber       { 0xffe2561a };   // active / accent orange
const Colour amberDim    { 0xfff1c9a8 };
const Colour lcdBg       { 0xffb5d394 };
const Colour lcdCell     { 0xffa6c685 };
const Colour lcdInk      { 0xff1d3414 };
const Colour ledOn       { 0xffff2a1c };
const Colour ledOff      { 0xff6b2a22 };
const Colour header      { 0xff121212 };
const Colour field       { 0xffd9cfb0 };   // recessed fields (step bars, boxes)
// section accent colours (thin ring around the knobs)
const Colour cOsc        { 0xffffb02e };
const Colour cFilter     { 0xff38c7d9 };
const Colour cMod        { 0xffd86bd1 };
const Colour cFx         { 0xff7fd16a };
const Colour cPerf       { 0xffff7a50 };
const Colour cMaster     { 0xffece6d6 };
} // namespace pal

inline Font uiFont (float h, bool bold = true, float hscale = 0.88f)
{
    return Font (FontOptions (Font::getDefaultSansSerifFontName(), h, bold ? Font::bold : Font::plain)).withHorizontalScale (hscale);
}

// ------------------------------------------------------------------ LCD
class Lcd : public Component, private Timer
{
public:
    std::function<std::pair<String, String>()> defaultText;

    void flash (const String& a, const String& b)
    {
        l0 = a; l1 = b; showing = true;
        startTimer (2200);
        repaint();
    }
    void refresh() { if (! showing) repaint(); }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colour (0xff0a0a0a)); g.fillRoundedRectangle (r, 5.0f);
        auto inner = r.reduced (4.0f);
        g.setGradientFill (ColourGradient (pal::lcdBg.brighter (0.08f), inner.getX(), inner.getY(), pal::lcdBg.darker (0.06f), inner.getX(), inner.getBottom(), false));
        g.fillRoundedRectangle (inner, 3.0f);
        String a, b;
        if (showing) { a = l0; b = l1; }
        else if (defaultText) { auto t = defaultText(); a = t.first; b = t.second; }
        const float cw = (inner.getWidth() - 16.0f) / 16.0f, ch = (inner.getHeight() - 10.0f) / 2.0f;
        Font f (FontOptions (Font::getDefaultMonospacedFontName(), ch * 0.88f, Font::bold));
        g.setFont (f);
        for (int line = 0; line < 2; ++line)
        {
            const String t = (line == 0 ? a : b).paddedRight (' ', 16).substring (0, 16);
            for (int i = 0; i < 16; ++i)
            {
                const Rectangle<float> cell (inner.getX() + 8.0f + cw * (float) i, inner.getY() + 5.0f + ch * (float) line, cw, ch);
                g.setColour (pal::lcdCell);                          // unlit cell
                g.fillRect (cell.reduced (1.0f, 1.5f));
                g.setColour (pal::lcdInk);
                g.drawText (t.substring (i, i + 1), cell, Justification::centred, false);
            }
        }
        // dot-matrix grid
        g.setColour (pal::lcdBg.withAlpha (0.45f));
        for (float y = inner.getY(); y < inner.getBottom(); y += 3.0f) g.drawHorizontalLine ((int) y, inner.getX(), inner.getRight());
        for (float x = inner.getX(); x < inner.getRight(); x += 3.0f) g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
        g.setColour (Colours::white.withAlpha (0.18f));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    }

private:
    void timerCallback() override { showing = false; stopTimer(); repaint(); }
    String l0, l1;
    bool showing = false;
};

// ------------------------------------------------------------------ knob / selector
class Knob : public Component
{
public:
    // numSteps > 0: selector knob with printed labels (choice parameters)
    Knob (RangedAudioParameter* param, String label, String section, Colour ring, Lcd* lcdPtr, float diameter = 46.0f, float labelRadius = 0.0f)
        : p (param), name (std::move (label)), sec (std::move (section)), accent (ring), lcd (lcdPtr), dia (diameter), R (labelRadius)
    {
        if (p != nullptr)
        {
            mn = p->convertFrom0to1 (0.0f); mx = p->convertFrom0to1 (1.0f);
            def = p->convertFrom0to1 (p->getDefaultValue());
            integer = p->isDiscrete();
            if (R > 0.0f) labels = p->getAllValueStrings();
            att = std::make_unique<ParameterAttachment> (*p, [this] (float nv) { v = nv; repaint(); });
            att->sendInitialUpdate();
        }
        setRepaintsOnMouseActivity (false);
    }

    // knob that is not a parameter (PROGRAM)
    Knob (String label, String section, Colour ring, Lcd* lcdPtr, float diameter, float lo, float hi, float init,
          std::function<void (float)> onSet, std::function<String (float)> text)
        : name (std::move (label)), sec (std::move (section)), accent (ring), lcd (lcdPtr), dia (diameter), mn (lo), mx (hi), def (init), v (init),
          integer (true), custom (std::move (onSet)), customText (std::move (text))
    {
    }

    void setExternal (float nv) { if (std::abs (nv - v) > 1e-4f) { v = nv; repaint(); } }

    static Rectangle<int> sizeFor (float diameter, float labelRadius, const String& label, const StringArray& labels = {})
    {
        const float labelW = uiFont (12.5f).getStringWidthFloat (label.toUpperCase()) + 8.0f;
        if (labelRadius > 0.0f)
        {
            const int n = labels.size();
            float ext = 20.0f;
            for (int i = 0; i < n; ++i)
            {
                const float deg = -135.0f + 270.0f * (float) i / (float) jmax (1, n - 1);
                const float s = std::sin (degreesToRadians (deg));
                const float tw = uiFont (10.5f).getStringWidthFloat (labels[i]);
                const float x = labelRadius * s + (s > 0.45f ? 3.0f : s < -0.45f ? -3.0f - tw : -tw * 0.5f);
                ext = jmax (ext, std::abs (x) + 2.0f, std::abs (x + tw) + 2.0f);
            }
            return { 0, 0, (int) jmax (2.0f * ext, labelW) + 4, (int) (2.0f * labelRadius + 34.0f + 16.0f) };
        }
        return { 0, 0, (int) jmax (diameter + 10.0f, labelW), (int) diameter + 12 + 16 };
    }

    void paint (Graphics& g) override
    {
        const bool sel = R > 0.0f;
        const float cx = getWidth() * 0.5f;
        const float knobR = sel ? 13.0f : dia * 0.3f;
        const float cy = sel ? (R + 17.0f) : (dia * 0.5f + 5.0f);
        const float a0 = -135.0f, a1 = 135.0f;
        auto pt = [&] (float rad, float deg) { const float t = degreesToRadians (deg); return Point<float> (cx + rad * std::sin (t), cy - rad * std::cos (t)); };
        const float norm = mx > mn ? jlimit (0.0f, 1.0f, (v - mn) / (mx - mn)) : 0.0f;

        // ticks / printed labels
        g.setColour (pal::ink.withAlpha (0.8f));
        if (sel)
        {
            const int n = labels.size();
            const int active = roundToInt (v - mn);
            for (int i = 0; i < n; ++i)
            {
                const float deg = a0 + (a1 - a0) * (float) i / (float) jmax (1, n - 1);
                g.setColour (pal::ink.withAlpha (0.8f));
                g.drawLine (Line<float> (pt (knobR + 3.5f, deg), pt (knobR + 7.0f, deg)), 1.0f);
                const auto tp = pt (R, deg);
                const float s = std::sin (degreesToRadians (deg)), co = std::cos (degreesToRadians (deg));
                Font f = uiFont (10.5f);
                const float tw = f.getStringWidthFloat (labels[i]);
                float x = tp.x, y = tp.y + (co > 0.45f ? -2.0f : co < -0.45f ? 11.0f : 4.0f);
                if (s > 0.45f) x += 3.0f; else if (s < -0.45f) x -= 3.0f + tw; else x -= tw * 0.5f;
                g.setFont (f);
                g.setColour (i == active ? pal::amber : pal::ink.withAlpha (0.85f));
                g.drawText (labels[i], Rectangle<float> (x - 1.0f, y - 10.0f, tw + 2.0f, 12.0f), Justification::centredLeft, false);
            }
        }
        else
        {
            for (int i = 0; i <= 10; ++i)
            {
                const float deg = a0 + (a1 - a0) * (float) i / 10.0f;
                const bool lng = i % 5 == 0;
                g.drawLine (Line<float> (pt (knobR + 3.5f, deg), pt (knobR + (lng ? 8.0f : 6.2f), deg)), 1.0f);
            }
        }
        (void) accent;

        // body: black knob with an orange pointer
        g.setColour (Colours::black.withAlpha (0.28f));
        g.fillEllipse (cx - knobR - 1.0f, cy - knobR + 1.6f, 2 * knobR + 2.0f, 2 * knobR + 2.0f);
        g.setColour (Colour (0xff050505));
        g.fillEllipse (cx - knobR - 1.0f, cy - knobR - 1.0f, 2 * knobR + 2.0f, 2 * knobR + 2.0f);
        g.setGradientFill (ColourGradient (Colour (0xff5a5a5a), cx - knobR * 0.45f, cy - knobR * 0.55f, Colour (0xff0c0c0c), cx + knobR * 0.9f, cy + knobR * 0.9f, true));
        g.fillEllipse (cx - knobR, cy - knobR, 2 * knobR, 2 * knobR);
        g.setColour (Colours::white.withAlpha (0.10f));
        g.drawEllipse (cx - knobR + 2.5f, cy - knobR + 2.5f, 2 * knobR - 5.0f, 2 * knobR - 5.0f, 0.8f);
        // indicator
        const float deg = a0 + (a1 - a0) * norm;
        g.setColour (Colour (0xffff7a1a));
        g.drawLine (Line<float> (pt (knobR * 0.35f, deg), pt (knobR * 0.94f, deg)), 2.6f);

        // label
        g.setColour (pal::ink);
        g.setFont (uiFont (12.5f));
        g.drawText (name.toUpperCase(), Rectangle<float> (0.0f, (float) getHeight() - 15.0f, (float) getWidth(), 14.0f), Justification::centred, false);
    }

    void mouseDown (const MouseEvent& e) override
    {
        y0 = e.position.y; v0 = v;
        if (att) att->beginGesture();
        show();
    }
    void mouseDrag (const MouseEvent& e) override
    {
        const float dy = y0 - e.position.y;
        const float range = mx - mn;
        const float per = R > 0.0f ? 22.0f : (integer ? jmax (6.0f, 170.0f / jmax (1.0f, range)) : 170.0f / range);
        float nv = v0 + dy / per * (e.mods.isShiftDown() ? 0.25f : 1.0f);
        if (R > 0.0f) nv = v0 + dy / 22.0f;
        setValue (nv);
    }
    void mouseUp (const MouseEvent&) override { if (att) att->endGesture(); }
    void mouseDoubleClick (const MouseEvent&) override
    {
        if (att) { att->beginGesture(); setValue (def); att->endGesture(); } else setValue (def);
    }
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override
    {
        const float step = (integer ? 1.0f : (mx - mn) / 64.0f) * (w.deltaY > 0 ? 1.0f : -1.0f);
        if (att) att->beginGesture();
        setValue (v + step);
        if (att) att->endGesture();
    }

private:
    void setValue (float nv)
    {
        nv = jlimit (mn, mx, nv);
        if (integer || R > 0.0f) nv = std::round (nv);
        if (att) att->setValueAsPartOfGesture (nv);
        else if (custom) custom (nv);
        v = nv; repaint(); show();
    }
    void show()
    {
        if (lcd == nullptr) return;
        String txt = customText ? customText (v) : (p ? p->getText (p->convertTo0to1 (v), 0) : String());
        lcd->flash (sec + " " + name, txt);
    }

    RangedAudioParameter* p = nullptr;
    std::unique_ptr<ParameterAttachment> att;
    String name, sec; Colour accent; Lcd* lcd = nullptr;
    float dia = 46.0f, R = 0.0f, mn = 0, mx = 1, def = 0, v = 0, y0 = 0, v0 = 0;
    bool integer = false;
    StringArray labels;
    std::function<void (float)> custom;
    std::function<String (float)> customText;
};

// ------------------------------------------------------------------ on/off button with LED
class LedButton : public Component
{
public:
    LedButton (RangedAudioParameter* param, String label, String section, Lcd* lcdPtr)
        : p (param), name (std::move (label)), sec (std::move (section)), lcd (lcdPtr)
    {
        att = std::make_unique<ParameterAttachment> (*p, [this] (float nv) { on = nv > 0.5f; repaint(); });
        att->sendInitialUpdate();
    }
    static Rectangle<int> sizeFor (const String& label)
    {
        return { 0, 0, jmax (46, (int) (uiFont (12.5f).getStringWidthFloat (label.toUpperCase()) + 8.0f)), 50 };
    }
    void paint (Graphics& g) override
    {
        const float cx = getWidth() * 0.5f;
        if (on) { g.setColour (pal::ledOn.withAlpha (0.30f)); g.fillEllipse (cx - 7.0f, 2.5f, 14.0f, 14.0f); }
        g.setColour (pal::ledOff.darker (0.5f)); g.fillEllipse (cx - 4.2f, 5.3f, 8.4f, 8.4f);
        g.setColour (on ? pal::ledOn : pal::ledOff);
        g.fillEllipse (cx - 3.5f, 6.0f, 7.0f, 7.0f);
        if (on) { g.setColour (Colours::white.withAlpha (0.55f)); g.fillEllipse (cx - 1.8f, 7.0f, 2.4f, 2.4f); }
        Rectangle<float> cap (cx - 15.0f, 19.0f + (down ? 1.0f : 0.0f), 30.0f, 15.0f);
        g.setGradientFill (ColourGradient (down || on ? Colour (0xff1c1c1c) : Colour (0xff4a4a4a), 0.0f, cap.getY(), Colour (0xff111111), 0.0f, cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 2.5f);
        g.setColour (Colours::white.withAlpha (0.16f)); g.drawHorizontalLine ((int) cap.getY(), cap.getX() + 2, cap.getRight() - 2);
        g.setColour (pal::ink);
        g.setFont (uiFont (12.5f));
        g.drawText (name.toUpperCase(), Rectangle<float> (0.0f, 35.0f, (float) getWidth(), 14.0f), Justification::centred, false);
    }
    void mouseDown (const MouseEvent&) override
    {
        down = true;
        att->setValueAsCompleteGesture (on ? 0.0f : 1.0f);
        if (lcd) lcd->flash (sec + " " + name, on ? "OFF" : "ON");
        repaint();
    }
    void mouseUp (const MouseEvent&) override { down = false; repaint(); }

private:
    RangedAudioParameter* p; std::unique_ptr<ParameterAttachment> att;
    String name, sec; Lcd* lcd; bool on = false, down = false;
};

// ------------------------------------------------------------------ plain cap button (no parameter)
class PlainCap : public Component
{
public:
    PlainCap (String label, std::function<void()> onPress, std::function<bool()> ledState)
        : name (std::move (label)), press (std::move (onPress)), led (std::move (ledState)) {}
    void paint (Graphics& g) override
    {
        const float cx = getWidth() * 0.5f;
        const bool on = led && led();
        g.setColour (on ? pal::ledOn : pal::ledOff); g.fillEllipse (cx - 3.5f, 6.0f, 7.0f, 7.0f);
        Rectangle<float> cap (cx - 15.0f, 19.0f + (down ? 1.0f : 0.0f), 30.0f, 15.0f);
        g.setGradientFill (ColourGradient (down ? Colour (0xff1c1c1c) : Colour (0xff4a4a4a), 0.0f, cap.getY(), Colour (0xff111111), 0.0f, cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 2.5f);
        g.setColour (pal::inkLight); g.setFont (uiFont (12.0f));
        g.drawText (name, Rectangle<float> (0.0f, 35.0f, (float) getWidth(), 14.0f), Justification::centred, false);
    }
    void mouseDown (const MouseEvent&) override { down = true; if (press) press(); repaint(); }
    void mouseUp (const MouseEvent&) override { down = false; repaint(); }
private:
    String name; std::function<void()> press; std::function<bool()> led; bool down = false;
};

// ------------------------------------------------------------------ 16 step bars (mod sequence / vocoder level / pan)
class StepBars : public Component, private Timer
{
public:
    StepBars (AudioProcessorValueTreeState& apvts, const String& prefix, String title, bool centred, Lcd* lcdPtr, std::atomic<int>* seqPos = nullptr)
        : name (std::move (title)), center (centred), lcd (lcdPtr), pos (seqPos)
    {
        for (int i = 0; i < 16; ++i)
        {
            auto* prm = apvts.getParameter (prefix + String (i + 1));
            params[i] = prm;
            att[i] = std::make_unique<ParameterAttachment> (*prm, [this, i] (float nv) { val[i] = nv; repaint(); });
            att[i]->sendInitialUpdate();
        }
        if (pos != nullptr) startTimerHz (20);
    }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const float barH = r.getHeight() - 12.0f;
        const float w = r.getWidth() / 16.0f;
        g.setColour (pal::field); g.fillRoundedRectangle (r.withHeight (barH), 2.0f);
        g.setColour (pal::ink.withAlpha (0.35f)); g.drawRoundedRectangle (r.withHeight (barH).reduced (0.5f), 2.0f, 1.0f);
        for (int i = 0; i < 16; ++i)
        {
            const float x = w * (float) i;
            const float h = barH * val[i] / 127.0f;
            const bool cur = pos != nullptr && lastPos == i;
            g.setColour (cur ? pal::amber : Colour (0xff1c1a17));
            if (center) { const float mid = barH * 0.5f, y = barH * (1.0f - val[i] / 127.0f); g.fillRect (x + 1.5f, jmin (y, mid), w - 3.0f, jmax (2.0f, std::abs (y - mid))); }
            else g.fillRect (x + 1.5f, barH - h, w - 3.0f, h);
            g.setColour (pal::ink.withAlpha (0.75f));
            g.setFont (uiFont (10.0f));
            g.drawText (String (i + 1), Rectangle<float> (x, barH + 1.0f, w, 11.0f), Justification::centred, false);
        }
        if (center) { g.setColour (pal::amber.withAlpha (0.8f)); g.drawHorizontalLine ((int) (barH * 0.5f), 0.0f, r.getWidth()); }
        if (pos != nullptr && lastPos >= 0) { g.setColour (pal::ledOn); g.drawRect (Rectangle<float> (w * (float) lastPos, 0.0f, w, barH), 1.0f); }
    }
    void mouseDown (const MouseEvent& e) override { set (e); }
    void mouseDrag (const MouseEvent& e) override { set (e); }
    void mouseUp (const MouseEvent&) override { if (last >= 0) att[last]->endGesture(); last = -1; }

private:
    void set (const MouseEvent& e)
    {
        const float barH = (float) getHeight() - 12.0f;
        const int i = jlimit (0, 15, (int) (e.position.x / ((float) getWidth() / 16.0f)));
        const float v = std::round (jlimit (0.0f, 1.0f, 1.0f - e.position.y / barH) * 127.0f);
        if (i != last) { if (last >= 0) att[last]->endGesture(); att[i]->beginGesture(); last = i; }
        att[i]->setValueAsPartOfGesture (v);
        if (lcd) lcd->flash (name + " " + String (i + 1), "Value " + String ((int) v));
    }
    void timerCallback() override { const int p2 = pos->load(); if (p2 != lastPos) { lastPos = p2; repaint(); } }
    String name; bool center; Lcd* lcd; std::atomic<int>* pos; int lastPos = -1, last = -1;
    RangedAudioParameter* params[16] {}; std::unique_ptr<ParameterAttachment> att[16]; float val[16] {};
};

// ------------------------------------------------------------------ wheels
class Wheel : public Component, private Timer
{
public:
    Wheel (bool springBack, std::function<void (float)> onChange, std::atomic<float>* external, float init)
        : spring (springBack), cb (std::move (onChange)), ext (external), v (init), home (init)
    {
        if (ext != nullptr) startTimerHz (30);
    }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colour (0xff050607)); g.fillRoundedRectangle (r, 6.0f);
        auto in = r.reduced (3.0f);
        g.saveState(); g.reduceClipRegion (in.toNearestInt());
        const float off = std::fmod (v * 280.0f, 9.0f);
        for (float y = in.getY() - 9.0f + off; y < in.getBottom(); y += 9.0f)
        {
            g.setColour (Colour (0xff2c3032)); g.fillRect (in.getX(), y, in.getWidth(), 2.0f);
            g.setColour (Colour (0xff101213)); g.fillRect (in.getX(), y + 2.0f, in.getWidth(), 3.0f);
            g.setColour (Colour (0xff3d4244)); g.fillRect (in.getX(), y + 5.0f, in.getWidth(), 1.0f);
            g.setColour (Colour (0xff0c0d0e)); g.fillRect (in.getX(), y + 6.0f, in.getWidth(), 3.0f);
        }
        g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.85f), 0.0f, in.getY(), Colours::transparentBlack, 0.0f, in.getY() + in.getHeight() * 0.28f, false));
        g.fillRect (in);
        g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.85f), 0.0f, in.getBottom(), Colours::transparentBlack, 0.0f, in.getBottom() - in.getHeight() * 0.28f, false));
        g.fillRect (in);
        g.restoreState();
        if (spring) { g.setColour (pal::ledOn.withAlpha (0.75f)); g.fillRect (r.getX(), r.getCentreY() - 1.0f, r.getWidth(), 2.0f); }
    }
    void mouseDown (const MouseEvent& e) override { dragging = true; y0 = e.position.y; v0 = v; }
    void mouseDrag (const MouseEvent& e) override { v = jlimit (0.0f, 1.0f, v0 + (y0 - e.position.y) / 130.0f); cb (v); repaint(); }
    void mouseUp (const MouseEvent&) override
    {
        dragging = false;
        if (spring) { springing = true; startTimerHz (60); }
    }

private:
    void timerCallback() override
    {
        if (springing)
        {
            v += (home - v) * 0.28f;
            if (std::abs (v - home) < 0.003f) { v = home; springing = false; if (ext == nullptr) stopTimer(); else startTimerHz (30); }
            cb (v); repaint(); return;
        }
        if (! dragging && ext != nullptr)
        {
            const float target = spring ? (ext->load() + 1.0f) * 0.5f : ext->load();
            if (std::abs (target - v) > 0.002f) { v = target; repaint(); }
        }
    }
    bool spring, dragging = false, springing = false;
    std::function<void (float)> cb; std::atomic<float>* ext; float v, home, y0 = 0, v0 = 0;
};

// ------------------------------------------------------------------ section frame
// paw print (pad + four toes), drawn in code
inline void drawPaw (Graphics& g, Point<float> c, float s, Colour col)
{
    g.setColour (col);
    g.fillEllipse (c.x - 0.30f * s, c.y - 0.02f * s, 0.60f * s, 0.46f * s);                 // pad
    g.fillEllipse (c.x - 0.46f * s, c.y - 0.30f * s, 0.20f * s, 0.27f * s);                 // toes
    g.fillEllipse (c.x - 0.24f * s, c.y - 0.50f * s, 0.20f * s, 0.29f * s);
    g.fillEllipse (c.x + 0.04f * s, c.y - 0.50f * s, 0.20f * s, 0.29f * s);
    g.fillEllipse (c.x + 0.26f * s, c.y - 0.30f * s, 0.20f * s, 0.27f * s);
}

// a small sitting cat (head and shoulders). variant 0 tabby, 1 black & white, 2 grey.
class Cat : public Component
{
public:
    explicit Cat (int v) : variant (v) { setInterceptsMouseClicks (false, false); setSize (64, 36); }
    void paint (Graphics& g) override
    {
        const Colour fur   = variant == 0 ? Colour (0xffe8a050) : variant == 1 ? Colour (0xff1f1f21) : Colour (0xffa4aab0);
        const Colour dark  = variant == 0 ? Colour (0xffb86a22) : variant == 1 ? Colour (0xff000000) : Colour (0xff6f757c);
        const Colour light = variant == 0 ? Colour (0xfff7e3c4) : Colour (0xfff6f1e6);
        const Colour pink  = Colour (0xffee9aa4);

        Path tail;
        tail.startNewSubPath (54.0f, 34.0f);
        tail.cubicTo (64.0f, 32.0f, 66.0f, 14.0f, 58.0f, 12.0f);
        g.setColour (dark.withAlpha (0.9f));
        g.strokePath (tail, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (fur);
        g.strokePath (tail, PathStrokeType (3.6f, PathStrokeType::curved, PathStrokeType::rounded));

        g.setColour (fur);                                           // body
        g.fillEllipse (10.0f, 15.0f, 44.0f, 40.0f);
        if (variant != 0) { g.setColour (light); g.fillEllipse (22.0f, 22.0f, 20.0f, 30.0f); }
        g.setColour (dark.withAlpha (0.35f)); g.drawEllipse (10.0f, 15.0f, 44.0f, 40.0f, 1.0f);

        auto ear = [&] (float x0, float x1, float xt)
        {
            Path e; e.addTriangle (x0, 11.0f, xt, 0.5f, x1, 8.0f);
            g.setColour (fur); g.fillPath (e);
            Path in; in.addTriangle (x0 + 2.2f, 9.5f, xt + (xt < 32.0f ? 0.6f : -0.6f), 4.0f, x1 - 1.6f, 8.0f);
            g.setColour (pink); g.fillPath (in);
            g.setColour (dark.withAlpha (0.5f)); g.strokePath (e, PathStrokeType (0.8f));
        };
        ear (19.0f, 30.0f, 21.0f);
        ear (34.0f, 45.0f, 43.0f);

        g.setColour (fur);                                           // head
        g.fillEllipse (17.0f, 4.0f, 30.0f, 25.0f);
        g.setColour (dark.withAlpha (0.5f)); g.drawEllipse (17.0f, 4.0f, 30.0f, 25.0f, 0.9f);
        if (variant == 1) { g.setColour (light); g.fillEllipse (25.0f, 14.0f, 14.0f, 13.0f); }
        if (variant == 2) { g.setColour (light); g.fillEllipse (26.0f, 15.0f, 12.0f, 12.0f); }
        if (variant == 0)
        {
            g.setColour (dark);
            for (float dx : { -3.0f, 0.0f, 3.0f }) g.drawLine (32.0f + dx, 5.5f, 32.0f + dx * 1.4f, 10.0f, 1.2f);
            g.setColour (light); g.fillEllipse (26.0f, 16.0f, 12.0f, 11.0f);
        }

        // face
        g.setColour (Colour (0xff15130f));
        if (variant == 2)   // happy closed eyes
        {
            Path a; a.addCentredArc (26.5f, 15.5f, 2.6f, 2.0f, 0.0f, -1.2f, 1.2f, true);
            Path b; b.addCentredArc (37.5f, 15.5f, 2.6f, 2.0f, 0.0f, -1.2f, 1.2f, true);
            g.strokePath (a, PathStrokeType (1.2f)); g.strokePath (b, PathStrokeType (1.2f));
        }
        else
        {
            const Colour iris = variant == 1 ? Colour (0xffd8e03a) : Colour (0xff5ea85a);
            for (float ex : { 26.5f, 37.5f })
            {
                g.setColour (iris); g.fillEllipse (ex - 2.6f, 12.4f, 5.2f, 6.0f);
                g.setColour (Colour (0xff15130f)); g.fillEllipse (ex - 1.0f, 13.0f, 2.0f, 4.8f);
                g.setColour (Colours::white.withAlpha (0.85f)); g.fillEllipse (ex - 1.6f, 13.2f, 1.3f, 1.3f);
            }
        }
        g.setColour (pink);
        Path nose; nose.addTriangle (30.0f, 19.2f, 34.0f, 19.2f, 32.0f, 21.4f); g.fillPath (nose);
        g.setColour (Colour (0xff15130f).withAlpha (0.8f));
        g.drawLine (32.0f, 21.4f, 32.0f, 23.0f, 0.8f);
        Path m; m.startNewSubPath (29.0f, 23.4f); m.quadraticTo (30.5f, 25.0f, 32.0f, 23.0f); m.quadraticTo (33.5f, 25.0f, 35.0f, 23.4f);
        g.strokePath (m, PathStrokeType (0.8f));
        g.setColour (Colours::white.withAlpha (variant == 1 ? 0.85f : 0.55f));
        for (int s = -1; s <= 1; s += 2)
            for (int k = 0; k < 3; ++k)
                g.drawLine (32.0f + (float) s * 5.0f, 21.5f + (float) k * 1.6f, 32.0f + (float) s * 13.0f, 19.5f + (float) k * 3.2f, 0.7f);

        // front paws on the edge of the bar
        g.setColour (light);
        g.fillEllipse (22.0f, 30.0f, 8.0f, 7.0f); g.fillEllipse (34.0f, 30.0f, 8.0f, 7.0f);
        g.setColour (dark.withAlpha (0.4f));
        g.drawEllipse (22.0f, 30.0f, 8.0f, 7.0f, 0.7f); g.drawEllipse (34.0f, 30.0f, 8.0f, 7.0f, 0.7f);
    }
private:
    int variant;
};

class Section : public Component
{
public:
    static constexpr int headerH = 18;
    explicit Section (String t) : title (std::move (t)) {}
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (pal::ink.withAlpha (0.30f));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        Path bar; bar.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), (float) headerH, 3.0f, 3.0f, true, true, false, false);
        g.setColour (pal::header); g.fillPath (bar);
        g.setColour (pal::inkLight);
        g.setFont (uiFont (13.0f, true, 0.95f).withExtraKerningFactor (0.08f));
        g.drawText (title, Rectangle<float> (8.0f, 0.0f, r.getWidth() - 30.0f, (float) headerH), Justification::centredLeft, false);
        drawPaw (g, { r.getRight() - 12.0f, 11.0f }, 9.0f, pal::inkLight.withAlpha (0.9f));
    }
    // children are placed "space-around" like the HTML panel
    void layoutRow (const Array<Component*>& kids, int top = 22)
    {
        int total = 0;
        for (auto* c : kids) total += c->getWidth();
        const float gap = (float) (getWidth() - total) / (float) jmax (1, kids.size());
        float x = gap * 0.5f;
        const int areaH = getHeight() - top;
        for (auto* c : kids) { c->setTopLeftPosition ((int) x, top + jmax (0, (areaH - c->getHeight()) / 2)); x += (float) c->getWidth() + gap; }
    }
    int minWidthFor (const Array<Component*>& kids) const
    {
        int total = 0;
        for (auto* c : kids) total += c->getWidth() + 6;
        return total;
    }
    String title;
};
} // namespace ymgui
