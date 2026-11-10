// YuMy Bacteria GUI parts. Look follows the HTML version's panel (brushed metal, red rails, blue LCD, red/grey knobs, LED caps).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>

namespace ybgui
{
using namespace juce;

namespace pal
{
const Colour red     { 0xffa3141b };
const Colour redD    { 0xff5e0a0f };
const Colour redL    { 0xffd1262e };
const Colour metal1  { 0xffc9ccd1 };
const Colour metal2  { 0xff9ea3aa };
const Colour ink     { 0xff15171a };
const Colour lcdBg   { 0xff0b2a4a };
const Colour lcdFg   { 0xff7fd0ff };
const Colour ledOff  { 0xff3a0d0f };
const Colour ledOn   { 0xffff3b30 };
const Colour field   { 0xff1b1d21 };
const Colour fieldTx { 0xffff6a5f };
}

inline Font sans (float h, bool bold = true) { return Font (FontOptions (Font::getDefaultSansSerifFontName(), h, bold ? Font::bold : Font::plain)); }
inline float textW (const Font& f, const String& t)
{
    GlyphArrangement ga; ga.addLineOfText (f, t, 0.0f, 0.0f);
    return ga.getBoundingBox (0, -1, true).getWidth();
}
inline Font mono (float h, bool bold = true) { return Font (FontOptions (Font::getDefaultMonospacedFontName(), h, bold ? Font::bold : Font::plain)); }

// ------------------------------------------------------------------ look and feel (combo boxes, sliders, popups)
class YbLaf : public LookAndFeel_V4
{
public:
    YbLaf()
    {
        setColour (ComboBox::backgroundColourId, pal::field);
        setColour (ComboBox::textColourId, pal::fieldTx);
        setColour (ComboBox::outlineColourId, Colour (0xff555555));
        setColour (ComboBox::arrowColourId, pal::fieldTx);
        setColour (PopupMenu::backgroundColourId, pal::field);
        setColour (PopupMenu::textColourId, pal::fieldTx);
        setColour (PopupMenu::highlightedBackgroundColourId, pal::red);
        setColour (PopupMenu::highlightedTextColourId, Colours::white);
    }
    Font getComboBoxFont (ComboBox&) override { return mono (11.0f); }
    Font getPopupMenuFont() override { return mono (12.0f); }
    void drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox&) override
    {
        auto r = Rectangle<float> (0, 0, (float) w, (float) h);
        g.setColour (pal::field); g.fillRoundedRectangle (r, 3.0f);
        g.setColour (Colour (0xff555555)); g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        Path p; const float cx = (float) w - 10.0f, cy = (float) h * 0.5f;
        p.addTriangle (cx - 3.5f, cy - 2.0f, cx + 3.5f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour (pal::fieldTx); g.fillPath (p);
    }
    void positionComboBoxText (ComboBox& b, Label& l) override { l.setBounds (4, 0, b.getWidth() - 18, b.getHeight()); l.setFont (getComboBoxFont (b)); }
    void drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle, Slider& s) override
    {
        const float cy = (float) y + (float) h * 0.5f;
        g.setColour (Colour (0xff2a2d33)); g.fillRoundedRectangle ((float) x, cy - 2.0f, (float) w, 4.0f, 2.0f);
        g.setColour (Colour (0x55000000)); g.drawRoundedRectangle ((float) x, cy - 2.0f, (float) w, 4.0f, 2.0f, 1.0f);
        // centre mark for bipolar sliders
        if (s.getMinimum() < 0.0 && s.getMaximum() > 0.0)
        {
            const float zx = (float) x + (float) w * (float) ((0.0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum()));
            g.setColour (Colour (0x66000000)); g.drawVerticalLine ((int) zx, cy - 5.0f, cy + 5.0f);
        }
        Rectangle<float> th (pos - 4.0f, cy - 7.0f, 8.0f, 14.0f);
        g.setGradientFill (ColourGradient (pal::redL, 0.0f, th.getY(), pal::redD, 0.0f, th.getBottom(), false));
        g.fillRoundedRectangle (th, 2.0f);
        g.setColour (Colours::white.withAlpha (0.35f)); g.drawHorizontalLine ((int) th.getY() + 1, th.getX() + 1.5f, th.getRight() - 1.5f);
    }
};

// ------------------------------------------------------------------ static decoration
class Sec : public Component
{
public:
    explicit Sec (String t) : title (std::move (t)) { setInterceptsMouseClicks (false, false); setBufferedToImage (true); }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f).withTrimmedTop (9.0f);   // component starts 9px above the box (title tag overhangs)
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.18f), 0, r.getY(), Colours::black.withAlpha (0.05f), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (Colour (0x8c282a2e)); g.drawRoundedRectangle (r, 6.0f, 1.5f);
        if (title.isEmpty()) return;
        g.setFont (sans (11.0f));
        const float tw = textW (sans (11.0f), title) + 2.0f * title.length() + 16.0f;   // letter-spacing ~2px
        Rectangle<float> tag (12.0f, 1.5f, tw, 15.0f);
        g.setColour (pal::metal1); g.fillRoundedRectangle (tag, 3.0f);
        g.setColour (pal::redD);
        // draw with manual letter spacing
        float x = tag.getX() + 8.0f;
        for (int i = 0; i < title.length(); ++i)
        {
            const String ch = title.substring (i, i + 1);
            const float cw = textW (sans (11.0f), ch);
            g.drawText (ch, Rectangle<float> (x, tag.getY(), cw + 2.0f, tag.getHeight()), Justification::centredLeft, false);
            x += cw + 2.0f;
        }
    }
private:
    String title;
};

class MetalPanel : public Component
{
public:
    MetalPanel() { setInterceptsMouseClicks (false, false); setBufferedToImage (true); }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        ColourGradient gr (Colour (0xffd6d9de), 0, 0, pal::metal2, 0, r.getBottom(), false);
        gr.addColour (0.40, pal::metal1);
        g.setGradientFill (gr); g.fillRect (r);
        g.setColour (Colours::white.withAlpha (0.07f));
        for (float x = 0; x < r.getWidth(); x += 3.0f) g.drawVerticalLine ((int) x, 0.0f, r.getBottom());
        g.setGradientFill (ColourGradient (pal::redL, 0, 0, pal::redD, 0, 6.0f, false));
        if (stripe) g.fillRect (0.0f, 0.0f, r.getWidth(), 6.0f);
        g.setColour (Colour (0xff4a4e55)); g.fillRect (0.0f, r.getBottom() - 3.0f, r.getWidth(), 3.0f);
    }
    bool stripe = true;
};

class Rail : public Component
{
public:
    Rail() { setInterceptsMouseClicks (false, false); setBufferedToImage (true); }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        ColourGradient gr (pal::redD, 0, 0, pal::redD, r.getWidth(), 0, false);
        gr.addColour (0.45, pal::red);
        g.setGradientFill (gr); g.fillRect (r);
        g.setColour (Colours::black.withAlpha (0.35f));
        g.drawRect (r, 3.0f);
    }
};

class TextLabel : public Component
{
public:
    TextLabel (String t, float h, Colour c, bool isMono = false, Justification j = Justification::topLeft)
        : text (std::move (t)), size (h), col (c), mn (isMono), just (j) { setInterceptsMouseClicks (false, false); }
    void setText (const String& t, Colour c) { if (t != text || c != col) { text = t; col = c; repaint(); } }
    void paint (Graphics& g) override
    {
        g.setColour (col); g.setFont (mn ? mono (size) : sans (size));
        g.drawFittedText (text, getLocalBounds(), just, 3, 0.9f);
    }
private:
    String text; float size; Colour col; bool mn; Justification just;
};

// ------------------------------------------------------------------ LCD
class Lcd : public Component
{
public:
    void setProgram (const String& name, const String& idx) { nm = name; id = idx; repaint(); }
    void show (const String& param, const String& val, float frac)
    { pn = param.toUpperCase(); pv = val; fr = jlimit (0.0f, 1.0f, frac); repaint(); }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colour (0xff222222)); g.fillRoundedRectangle (r, 6.0f);
        g.setColour (Colours::black); g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 2.0f);
        auto in = r.reduced (7.0f);
        g.setGradientFill (ColourGradient (Colour (0xff0e3a66), 0, in.getY(), pal::lcdBg, 0, in.getBottom(), false));
        g.fillRoundedRectangle (in, 3.0f);
        auto t = in.reduced (10.0f, 7.0f);
        auto glow = [&] (const String& s, Rectangle<float> b, Font f, Justification j, float a)
        {
            g.setFont (f);
            g.setColour (Colour (0xff3ab0ff).withAlpha (0.35f * a));
            for (auto d : { Point<float> (-1, 0), Point<float> (1, 0), Point<float> (0, -1), Point<float> (0, 1) }) g.drawText (s, b.translated (d.x, d.y), j, false);
            g.setColour (pal::lcdFg.withAlpha (a)); g.drawText (s, b, j, false);
        };
        glow ("PART 1", t.removeFromTop (14.0f).withTrimmedRight (t.getWidth() * 0.5f), mono (12.0f, false), Justification::centredLeft, 0.85f);
        glow (id, Rectangle<float> (t.getX(), t.getY() - 14.0f, t.getWidth(), 14.0f), mono (12.0f, false), Justification::centredRight, 0.85f);
        glow (nm, t.removeFromTop (27.0f), mono (22.0f), Justification::centredLeft, 1.0f);
        auto l3 = t.removeFromTop (16.0f);
        glow (pn, l3.withTrimmedRight (l3.getWidth() * 0.4f), mono (13.0f, false), Justification::centredLeft, 1.0f);
        glow (pv, l3.withLeft (l3.getRight() - l3.getWidth() * 0.4f), mono (13.0f, false), Justification::centredRight, 1.0f);
        auto bar = Rectangle<float> (t.getX(), t.getBottom() - 6.0f, t.getWidth(), 6.0f);
        g.setColour (pal::lcdFg.withAlpha (0.2f)); g.fillRect (bar);
        g.setColour (pal::lcdFg); g.fillRect (bar.withWidth (bar.getWidth() * fr));
    }
private:
    String nm = "INIT BACTERIA", id = "A001", pn = "---", pv = "---";
    float fr = 0.5f;
};

// ------------------------------------------------------------------ knob
class KnobCell : public Component
{
public:
    KnobCell (RangedAudioParameter* param, String label, Lcd* lcdPtr, bool isRed = false, bool isBig = false)
        : p (param), text (std::move (label)), lcd (lcdPtr), red (isRed), big (isBig)
    {
        mn = p->convertFrom0to1 (0.0f); mx = p->convertFrom0to1 (1.0f); def = p->convertFrom0to1 (p->getDefaultValue());
        att = std::make_unique<ParameterAttachment> (*p, [this] (float nv) { v = nv; repaint(); });
        att->sendInitialUpdate();
    }
    void paint (Graphics& g) override
    {
        const float cx = getWidth() * 0.5f;
        g.setColour (Colour (0xff202226)); g.setFont (sans (9.5f));
        g.drawFittedText (text, Rectangle<int> (0, 0, getWidth(), 22), Justification::centred, 2, 0.9f);
        const float d = big ? 62.0f : 42.0f, cy = 26.0f + d * 0.5f;
        const float R = d * 0.5f;
        const float norm = mx > mn ? jlimit (0.0f, 1.0f, (v - mn) / (mx - mn)) : 0.0f;
        g.setColour (Colours::black.withAlpha (0.4f)); g.fillEllipse (cx - R - 3.5f, cy - R - 1.0f, d + 8.0f, d + 8.0f);
        g.setColour (Colour (0xff333333)); g.fillEllipse (cx - R - 4.0f, cy - R - 4.0f, d + 8.0f, d + 8.0f);
        g.setColour (Colour (0xff7c8189)); g.fillEllipse (cx - R - 3.0f, cy - R - 3.0f, d + 6.0f, d + 6.0f);
        const Colour c1 = red ? Colour (0xffe24048) : Colour (0xff5a5f68), c2 = red ? Colour (0xff6a0b10) : Colour (0xff1d1f23);
        g.setGradientFill (ColourGradient (c1, cx - R * 0.3f, cy - R * 0.44f, c2, cx + R * 0.5f, cy + R * 0.7f, true));
        g.fillEllipse (cx - R, cy - R, d, d);
        g.setColour (Colours::white.withAlpha (0.25f)); g.drawEllipse (cx - R + 0.5f, cy - R + 0.5f, d - 1.0f, d - 1.0f, 1.0f);
        const float ang = degreesToRadians (-135.0f + 270.0f * norm);
        const float ir0 = R - (big ? 24.0f : 17.0f), ir1 = R - 4.0f;
        const Point<float> a (cx + ir0 * std::sin (ang), cy - ir0 * std::cos (ang)), b (cx + ir1 * std::sin (ang), cy - ir1 * std::cos (ang));
        g.setColour (Colours::white.withAlpha (0.35f)); g.drawLine (Line<float> (a, b), 4.5f);
        g.setColour (Colours::white); g.drawLine (Line<float> (a, b), 3.0f);
    }
    void mouseDown (const MouseEvent& e) override { y0 = e.position.y; v0 = v; att->beginGesture(); show(); }
    void mouseDrag (const MouseEvent& e) override
    {
        const float dy = y0 - e.position.y;
        setValue (v0 + dy * (mx - mn) / 200.0f * (e.mods.isShiftDown() ? 0.2f : 1.0f));
    }
    void mouseUp (const MouseEvent&) override { att->endGesture(); }
    void mouseDoubleClick (const MouseEvent&) override { att->beginGesture(); setValue (def); att->endGesture(); }
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override
    {
        att->beginGesture(); setValue (v + (w.deltaY > 0 ? 1.0f : -1.0f)); att->endGesture();
    }
private:
    void setValue (float nv)
    {
        nv = std::round (jlimit (mn, mx, nv));
        att->setValueAsPartOfGesture (nv); v = nv; repaint(); show();
    }
    void show()
    {
        if (lcd != nullptr) lcd->show (p->getName (40), String ((int) v), (v - mn) / (mx - mn));
    }
    RangedAudioParameter* p; std::unique_ptr<ParameterAttachment> att;
    String text; Lcd* lcd; bool red, big;
    float mn = 0, mx = 127, def = 0, v = 0, y0 = 0, v0 = 0;
};

// ------------------------------------------------------------------ LED cap buttons
class CapBase : public Component
{
public:
    void paintCap (Graphics& g, bool on, bool down, const String& label, float labelSize = 9.0f)
    {
        const float cx = getWidth() * 0.5f;
        g.setColour (Colour (0xff222222)); g.fillEllipse (cx - 5.5f, 1.5f, 11.0f, 11.0f);
        if (on) { g.setColour (pal::ledOn.withAlpha (0.35f)); g.fillEllipse (cx - 9.0f, -2.0f, 18.0f, 18.0f); }
        g.setColour (on ? pal::ledOn : pal::ledOff); g.fillEllipse (cx - 4.5f, 2.5f, 9.0f, 9.0f);
        Rectangle<float> cap (cx - 23.0f, 15.0f + (down ? 1.0f : 0.0f), 46.0f, 20.0f);
        g.setColour (Colours::black.withAlpha (0.5f)); g.fillRoundedRectangle (cap.translated (0, 2.0f), 3.0f);
        g.setGradientFill (ColourGradient (Colour (0xff464a52), 0, cap.getY(), Colour (0xff1d1f23), 0, cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (Colours::white.withAlpha (0.25f)); g.drawHorizontalLine ((int) cap.getY(), cap.getX() + 2.0f, cap.getRight() - 2.0f);
        g.setColour (pal::ink); g.setFont (sans (labelSize));
        g.drawFittedText (label, Rectangle<int> (0, 38, getWidth(), 11), Justification::centred, 1, 0.8f);
    }
};

class LedToggle : public CapBase
{
public:
    LedToggle (RangedAudioParameter* param, String label, Lcd* lcdPtr) : p (param), text (std::move (label)), lcd (lcdPtr)
    {
        att = std::make_unique<ParameterAttachment> (*p, [this] (float nv) { on = nv > 0.5f; repaint(); });
        att->sendInitialUpdate();
    }
    void paint (Graphics& g) override { paintCap (g, on, down, text); }
    void mouseDown (const MouseEvent&) override
    {
        down = true; att->setValueAsCompleteGesture (on ? 0.0f : 1.0f);
        if (lcd) lcd->show (p->getName (40), on ? "OFF" : "ON", on ? 0.0f : 1.0f);
        repaint();
    }
    void mouseUp (const MouseEvent&) override { down = false; repaint(); }
private:
    RangedAudioParameter* p; std::unique_ptr<ParameterAttachment> att; String text; Lcd* lcd; bool on = false, down = false;
};

// one LED per choice value (filter 1 mode)
class LedChoice : public CapBase
{
public:
    LedChoice (RangedAudioParameter* param, int value, String label, Lcd* lcdPtr) : p (param), idx (value), text (std::move (label)), lcd (lcdPtr)
    {
        att = std::make_unique<ParameterAttachment> (*p, [this] (float nv) { on = (int) std::lround (nv) == idx; repaint(); });
        att->sendInitialUpdate();
    }
    void paint (Graphics& g) override { paintCap (g, on, down, text); }
    void mouseDown (const MouseEvent&) override
    {
        down = true; att->setValueAsCompleteGesture ((float) idx);
        if (lcd) lcd->show (p->getName (40), text, 0.5f);
        repaint();
    }
    void mouseUp (const MouseEvent&) override { down = false; repaint(); }
private:
    RangedAudioParameter* p; std::unique_ptr<ParameterAttachment> att; int idx; String text; Lcd* lcd; bool on = false, down = false;
};

class LedAction : public CapBase
{
public:
    LedAction (String label, std::function<void()> f) : text (std::move (label)), fn (std::move (f)) {}
    void paint (Graphics& g) override { paintCap (g, false, down, text); }
    void mouseDown (const MouseEvent&) override { down = true; if (fn) fn(); repaint(); }
    void mouseUp (const MouseEvent&) override { down = false; repaint(); }
private:
    String text; std::function<void()> fn; bool down = false;
};

// flat text button (.btn)
class FlatButton : public Component
{
public:
    FlatButton (String label, std::function<void()> f) : text (std::move (label)), fn (std::move (f)) {}
    void setText (const String& t) { text = t; repaint(); }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.0f, 2.0f);
        if (down) r = r.translated (0.0f, 1.0f);
        g.setColour (Colours::black.withAlpha (0.5f)); g.fillRoundedRectangle (r.translated (0, 1.5f), 3.0f);
        g.setGradientFill (ColourGradient (Colour (0xff464a52), 0, r.getY(), Colour (0xff1d1f23), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (Colour (0xffe8e8e8)); g.setFont (sans (9.0f)); g.drawText (text, r, Justification::centred, false);
    }
    void mouseDown (const MouseEvent&) override { down = true; repaint(); if (fn) fn(); }
    void mouseUp (const MouseEvent&) override { down = false; repaint(); }
private:
    String text; std::function<void()> fn; bool down = false;
};

// lock chip (.ck)
class Chip : public Component
{
public:
    Chip (String label, std::function<void (bool)> f) : text (std::move (label)), fn (std::move (f)) {}
    bool on = false;
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (on ? pal::fieldTx : pal::field); g.fillRoundedRectangle (r, 3.0f);
        g.setColour (on ? pal::fieldTx : Colour (0xff555555)); g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setColour (on ? pal::field : Colour (0xff888888)); g.setFont (mono (10.0f)); g.drawText (text, r, Justification::centred, false);
    }
    void mouseDown (const MouseEvent&) override { on = ! on; repaint(); if (fn) fn (on); }
private:
    String text; std::function<void (bool)> fn;
};

// ------------------------------------------------------------------ labelled fields
class FieldBase : public Component
{
public:
    explicit FieldBase (String l) : label (std::move (l)) {}
    void paint (Graphics& g) override
    {
        g.setColour (Colour (0xff202226)); g.setFont (sans (9.0f));
        g.drawText (label, Rectangle<int> (0, 0, getWidth(), 12), Justification::centredLeft, false);
    }
protected:
    String label;
};

class SelectField : public FieldBase
{
public:
    // parameter-attached
    SelectField (String l, AudioProcessorValueTreeState& apvts, const String& id, Lcd* lcdPtr = nullptr) : FieldBase (std::move (l))
    {
        auto* p = apvts.getParameter (id);
        box.addItemList (dynamic_cast<AudioParameterChoice*> (p)->choices, 1);
        att = std::make_unique<ComboBoxParameterAttachment> (*p, box);
        init (lcdPtr, p->getName (40));
    }
    // free
    SelectField (String l, const StringArray& items, std::function<void (int)> onChange) : FieldBase (std::move (l))
    {
        box.addItemList (items, 1);
        box.onChange = [this, onChange] { if (! quiet) onChange (box.getSelectedItemIndex()); };
        addAndMakeVisible (box);
    }
    void setIndex (int i) { quiet = true; box.setSelectedItemIndex (i, dontSendNotification); quiet = false; }
    void resized() override { box.setBounds (0, 13, getWidth(), 26); }
private:
    void init (Lcd* lcdPtr, const String& nm)
    {
        if (lcdPtr != nullptr)
            box.onChange = [this, lcdPtr, nm] { lcdPtr->show (nm, box.getText(), 0.5f); };
        addAndMakeVisible (box);
    }
    ComboBox box; std::unique_ptr<ComboBoxParameterAttachment> att; bool quiet = false;
};

class SliderField : public FieldBase
{
public:
    // int parameter slider with a value read-out. fmt==nullptr: plain integer.
    SliderField (String l, AudioProcessorValueTreeState& apvts, const String& id, std::function<String (int)> fmt = nullptr,
                 bool showLabel = true, int valW = 54, Lcd* lcdPtr = nullptr)
        : FieldBase (showLabel ? std::move (l) : String()), format (std::move (fmt)), vw (valW), lcd (lcdPtr)
    {
        auto* p = apvts.getParameter (id);
        const auto range = p->getNormalisableRange();
        slider.setSliderStyle (Slider::LinearHorizontal);
        slider.setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
        slider.setRange (range.start, range.end, 1.0);
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        slider.setPopupDisplayEnabled (false, false, nullptr);
        slider.onValueChange = [this, p] { update(); if (lcd) lcd->show (p->getName (40), valText(), (float) ((slider.getValue() - slider.getMinimum()) / (slider.getMaximum() - slider.getMinimum()))); };
        att = std::make_unique<SliderParameterAttachment> (*p, slider);
        addAndMakeVisible (slider);
        addAndMakeVisible (value);
        value.setInterceptsMouseClicks (false, false);
        update();
    }
    void resized() override { slider.setBounds (0, 15, getWidth() - vw - 4, 22); value.setBounds (getWidth() - vw, 15, vw, 22); }
private:
    String valText() const { const int v = (int) std::lround (slider.getValue()); return format ? format (v) : String (v); }
    void update() { value.setText (valText(), pal::ink); }
    Slider slider; TextLabel value { "0", 10.0f, pal::ink, true, Justification::centredRight };
    std::function<String (int)> format; int vw; Lcd* lcd; std::unique_ptr<SliderParameterAttachment> att;
};

class Meter : public Component
{
public:
    void set (float v) { v = jlimit (0.0f, 1.0f, v); if (std::abs (v - level) > 0.004f) { level = v; repaint(); } }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (pal::field); g.fillRoundedRectangle (r, 3.0f);
        g.setColour (pal::fieldTx); g.fillRect (r.reduced (1.0f).withWidth ((r.getWidth() - 2.0f) * level));
        g.setColour (Colour (0xff555555)); g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    }
private:
    float level = 0.0f;
};

// slider that is not a parameter (randomizer intensity)
class PlainSlider : public FieldBase
{
public:
    PlainSlider (String l, int init, std::function<String (int)> fmt) : FieldBase (std::move (l)), format (std::move (fmt)), def (init)
    {
        slider.setSliderStyle (Slider::LinearHorizontal);
        slider.setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
        slider.setRange (0.0, 100.0, 1.0);
        slider.setValue (init, dontSendNotification);
        slider.setDoubleClickReturnValue (true, init);
        slider.onValueChange = [this] { value.setText (format ((int) slider.getValue()), pal::ink); };
        addAndMakeVisible (slider); addAndMakeVisible (value);
        value.setText (format (init), pal::ink);
    }
    int get() const { return (int) std::lround (slider.getValue()); }
    void resized() override { slider.setBounds (0, 15, getWidth() - 44, 22); value.setBounds (getWidth() - 40, 15, 40, 22); }
private:
    Slider slider; TextLabel value { "", 10.0f, pal::ink, true, Justification::centredRight };
    std::function<String (int)> format; int def;
};
} // namespace ybgui
