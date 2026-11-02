#include "PluginEditor.h"
#include <BinaryData.h>
#include <cmath>

using namespace juce;

namespace
{
const Colour kAmber (0xffffb23e), kInk (0xffe6e7ea), kDim (0xff8e9097), kLedRed (0xffff2a2a);

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

void txt (Graphics& g, const String& s, Font f, Colour c, Rectangle<float> r, Justification j = Justification::centred)
{
    g.setFont (f); g.setColour (c); g.drawText (s, r, j, false);
}

String fT (float v) { return v < 1.f ? String ((int) std::lround (v * 1000.f)) + "ms" : String (v, 2) + "s"; }
String fHz (float v) { return v >= 1000.f ? String (v / 1000.f, 2) + "k" : String (v, v < 10.f ? 2 : 0); }
String fP (float v) { return String ((int) std::lround (v * 100.f)) + "%"; }

String fmtValue (const String& id, float v)
{
    if (id == "tune") return String (v >= 0 ? "+" : "") + String ((int) std::lround (v)) + "c";
    if (id == "o2tune" || id == "o3tune") return String (v >= 0 ? "+" : "") + String (v / 100.f, 2) + "st";
    if (id == "glide") return v < 0.005f ? String ("OFF") : fT (v * v * 4.f);
    if (id == "cutoff" || id == "dlTone") return fHz (v);
    if (id == "fA" || id == "fD" || id == "aA" || id == "aD" || id == "dlTime") return fT (v);
    if (id == "modRate" || id == "chRate") return String (v, 1) + "Hz";
    if (id == "sqBpm") return String ((int) std::lround (v)) + "bpm";
    if (id == "sqLen") return String ((int) std::lround (v));
    return fP (v);
}

// ---- brushed black aluminium, generated once
const Image& brushed()
{
    static Image img;
    if (img.isValid()) return img;
    const int W = 1920, H = 1080;
    img = Image (Image::ARGB, W, H, false);
    Random rnd (20260);
    Image::BitmapData bd (img, Image::BitmapData::writeOnly);
    std::vector<float> rowGain ((size_t) H);
    for (int y = 0; y < H; ++y) rowGain[(size_t) y] = (rnd.nextFloat() - 0.5f) * 5.f;
    for (int y = 0; y < H; ++y)
    {
        float s = 0.f, s2 = 0.f;
        const float base = 26.f - 6.f * (float) y / (float) H;
        for (int x = 0; x < W; ++x)
        {
            s  += 0.35f * ((rnd.nextFloat() - 0.5f) * 5.f - s);
            s2 += 0.04f * ((rnd.nextFloat() - 0.5f) * 9.f - s2);
            const float hl = 5.f * std::exp (-std::pow ((x - W * 0.42f) / (W * 0.33f), 2.f));   // soft light band
            const float v = jlimit (0.f, 255.f, base + hl + rowGain[(size_t) y] + s + s2);
            const uint8 b = (uint8) v;
            bd.setPixelColour (x, y, Colour::fromRGB (b, b, (uint8) jmin (255.f, v + 1.f)));
        }
    }
    return img;
}

struct CardDef { Rectangle<int> r; const char* title; };
const CardDef kCards[] = {
    { { 14, 112, 172, 343 }, "CONTROLLERS" },       { { 198, 112, 668, 343 }, "OSCILLATORS" }, { { 878, 112, 258, 343 }, "MIXER" },
    { { 14, 466, 382, 202 }, "FILTER  24dB/oct LADDER" }, { { 407, 466, 236, 202 }, "FILTER EG" },
    { { 655, 466, 235, 202 }, "LOUDNESS EG" },      { { 902, 466, 234, 202 }, "MODULATION" },
    { { 14, 683, 1122, 207 }, "EFFECTS" },          { { 1150, 112, 756, 778 }, "SEQUENCER" },
    { { 14, 903, 1892, 163 }, "KEYBOARD" } };

// ================================================================== widgets
class Knob : public Component
{
public:
    Knob (MoguProcessor& p, const char* id_, const char* lab, int sz, bool bip = false)
        : proc (p), id (id_), label (lab), size (sz), bipolar (bip),
          param (p.apvts.getParameter (id_)),
          att (*param, [this] (float v) { value = v; repaint(); }, nullptr)
    {
        att.sendInitialUpdate();
        setSize (96, sz + 46);
    }
    void paint (Graphics& g) override
    {
        const float cx = getWidth() * 0.5f, cy = size * 0.5f + 4.f, R = size * 0.5f;
        const float n = param->convertTo0to1 (value);
        const float a0 = -135.f, a1 = a0 + 270.f * n;
        Path tr; tr.addCentredArc (cx, cy, R, R, 0.f, degreesToRadians (-135.f), degreesToRadians (135.f), true);
        g.setColour (Colour (0xff0b0b0c)); g.strokePath (tr, PathStrokeType (4.5f, PathStrokeType::curved, PathStrokeType::rounded));
        const float from = bipolar ? 0.f : -135.f;
        if (std::abs (a1 - from) > 0.5f)
        {
            Path v; v.addCentredArc (cx, cy, R, R, 0.f, degreesToRadians (jmin (from, a1)), degreesToRadians (jmax (from, a1)), true);
            g.setColour (kAmber.withAlpha (0.25f)); g.strokePath (v, PathStrokeType (7.f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (kAmber); g.strokePath (v, PathStrokeType (3.2f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        const float cr = R * 0.66f;
        g.setGradientFill (ColourGradient (Colour (0xff9a9ba2), cx - cr * 0.3f, cy - cr * 0.4f, Colour (0xff17171a), cx + cr, cy + cr, true));
        g.fillEllipse (cx - cr, cy - cr, cr * 2, cr * 2);
        g.setColour (Colours::black.withAlpha (0.6f)); g.drawEllipse (cx - cr, cy - cr, cr * 2, cr * 2, 1.f);
        const float ang = degreesToRadians (a1);
        g.setColour (Colour (0xfff4f4f6));
        g.drawLine (cx + std::sin (ang) * cr * 0.25f, cy - std::cos (ang) * cr * 0.25f, cx + std::sin (ang) * cr * 0.92f, cy - std::cos (ang) * cr * 0.92f, 2.4f);
        txt (g, label, F (1, 15.5f, 0.06f), kDim, { 0.f, (float) size + 8.f, (float) getWidth(), 18.f });
        txt (g, fmtValue (id, value), F (3, 14.5f), kAmber.withAlpha (0.9f), { 0.f, (float) size + 26.f, (float) getWidth(), 18.f });
    }
    void mouseDown (const MouseEvent& e) override { startY = e.position.y; startN = param->convertTo0to1 (value); att.beginGesture(); }
    void mouseDrag (const MouseEvent& e) override
    {
        const float k = e.mods.isShiftDown() ? 0.0009f : 0.006f;
        att.setValueAsPartOfGesture (param->convertFrom0to1 (jlimit (0.f, 1.f, startN + (startY - e.position.y) * k)));
    }
    void mouseUp (const MouseEvent&) override { att.endGesture(); }
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override
    {
        const float d = w.deltaY > 0 ? 0.02f : w.deltaY < 0 ? -0.02f : 0.f;
        att.setValueAsCompleteGesture (param->convertFrom0to1 (jlimit (0.f, 1.f, param->convertTo0to1 (value) + d)));
    }
    void mouseDoubleClick (const MouseEvent&) override { att.setValueAsCompleteGesture (param->convertFrom0to1 (param->getDefaultValue())); }
private:
    MoguProcessor& proc; String id, label; int size; bool bipolar;
    RangedAudioParameter* param; ParameterAttachment att;
    float value = 0.f, startY = 0.f, startN = 0.f;
};

class Pills : public Component
{
public:
    Pills (MoguProcessor& p, const char* id_, const char* lab, int cols_, int cw_, int ch_ = 30, bool label_ = true)
        : id (id_), label (lab), cols (cols_), cw (cw_), ch (ch_), hasLabel (label_ && lab[0] != 0),
          param (p.apvts.getParameter (id_)), att (*param, [this] (float v) { value = (int) (v + 0.5f); repaint(); }, nullptr)
    {
        opts.addTokens (mm::kSpecs[mm::specIndex (id_)].opts, "|", "");
        att.sendInitialUpdate();
        const int rows = (opts.size() + cols - 1) / cols;
        setSize (cols * (cw + gap) - gap, (hasLabel ? 22 : 0) + rows * (ch + gap) - gap);
    }
    Rectangle<float> cell (int i) const
    {
        return { (float) ((i % cols) * (cw + gap)), (float) ((hasLabel ? 22 : 0) + (i / cols) * (ch + gap)), (float) cw, (float) ch };
    }
    void paint (Graphics& g) override
    {
        if (hasLabel) txt (g, label, F (1, 15.5f, 0.08f), kDim, { 0.f, 0.f, (float) getWidth(), 18.f });
        for (int i = 0; i < opts.size(); ++i)
        {
            const auto r = cell (i); const bool on = i == value;
            if (on) { g.setColour (kAmber.withAlpha (0.22f)); g.fillRoundedRectangle (r.expanded (2.f), 7.f); }
            g.setColour (on ? kAmber : Colour (0xff0d0e0f)); g.fillRoundedRectangle (r, 5.f);
            g.setColour (on ? Colour (0xffffd58a) : Colour (0x30ffffff)); g.drawRoundedRectangle (r, 5.f, 1.f);
            txt (g, opts[i], F (1, ch >= 28 ? 16.5f : 14.5f, 0.04f), on ? Colour (0xff1a1205) : Colour (0xffc7c8cc), r);
        }
    }
    void mouseDown (const MouseEvent& e) override
    {
        for (int i = 0; i < opts.size(); ++i)
            if (cell (i).contains (e.position)) { att.setValueAsCompleteGesture ((float) i); return; }
    }
private:
    String id, label; int cols, cw, ch; bool hasLabel; int gap = 4, value = 0;
    StringArray opts; RangedAudioParameter* param; ParameterAttachment att;
};

class Btn : public Component
{
public:
    Btn (String t, std::function<void()> f, std::function<bool()> st = {}) : text (t), onClick (std::move (f)), state (std::move (st)) {}
    void paint (Graphics& g) override
    {
        const bool on = state && state();
        const auto r = getLocalBounds().toFloat().reduced (1.f);
        g.setColour (on ? kAmber : Colour (0xff17181a)); g.fillRoundedRectangle (r, 6.f);
        g.setColour (on ? Colour (0xffffd58a) : Colour (0x38ffffff)); g.drawRoundedRectangle (r, 6.f, 1.f);
        txt (g, on && altText.isNotEmpty() ? altText : text, F (1, 18.f, 0.08f), on ? Colour (0xff1a1205) : kInk, r);
    }
    void mouseDown (const MouseEvent&) override { if (onClick) onClick(); repaint(); }
    String text, altText;
    std::function<void()> onClick; std::function<bool()> state;
};

class PresetBox : public Component
{
public:
    explicit PresetBox (MoguProcessor& p) : proc (p) {}
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.f);
        g.setColour (Colour (0xff09090a)); g.fillRoundedRectangle (r, 8.f);
        g.setColour (Colour (0x40ffffff)); g.drawRoundedRectangle (r, 8.f, 1.f);
        txt (g, "<", F (2, 24.f), kDim, { 6.f, 0.f, 36.f, r.getHeight() });
        txt (g, ">", F (2, 24.f), kDim, { r.getWidth() - 36.f, 0.f, 36.f, r.getHeight() });
        txt (g, String (proc.getProgramName (proc.getCurrentProgram())).toUpperCase(), F (1, 22.f, 0.1f), kInk, r.reduced (40.f, 0.f));
    }
    void mouseDown (const MouseEvent& e) override
    {
        const int n = proc.getNumPrograms(), cur = proc.getCurrentProgram();
        if (e.x < 40) proc.setCurrentProgram ((cur + n - 1) % n);
        else if (e.x > getWidth() - 40) proc.setCurrentProgram ((cur + 1) % n);
        else
        {
            PopupMenu m;
            for (int i = 0; i < n; ++i) m.addItem (i + 1, proc.getProgramName (i), true, i == cur);
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [this] (int r) { if (r > 0) { proc.setCurrentProgram (r - 1); repaint(); } });
        }
        repaint();
    }
private:
    MoguProcessor& proc;
};

class Scope : public Component
{
public:
    explicit Scope (MoguProcessor& p) : proc (p) {}
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colour (0xff050506)); g.fillRoundedRectangle (r, 8.f);
        g.setColour (Colour (0x40ffffff)); g.drawRoundedRectangle (r.reduced (0.5f), 8.f, 1.f);
        const int W = MoguProcessor::kScope, w = proc.scopeW.load();
        float tmp[MoguProcessor::kScope];
        float peak = 0.f;
        for (int i = 0; i < W; ++i) { tmp[i] = proc.scopeBuf[(size_t) ((w + i) % W)]; peak = jmax (peak, std::abs (tmp[i])); }
        const float mid = r.getCentreY(), amp = r.getHeight() * 0.42f;
        Path p;
        if (peak < 1.0e-4f) { p.startNewSubPath (r.getX() + 8, mid); p.lineTo (r.getRight() - 8, mid); }
        else
        {
            int s = W - 1100;
            for (int i = W - 1100; i > 200; --i) if (tmp[i - 1] < 0.f && tmp[i] >= 0.f) { s = i; break; }
            const int len = 640;
            for (int i = 0; i < len; ++i)
            {
                const float x = r.getX() + 8 + (r.getWidth() - 16) * i / (len - 1);
                const float y = mid - jlimit (-1.f, 1.f, tmp[jmin (W - 1, s + i)] * 2.2f) * amp;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
        }
        g.setColour (kAmber.withAlpha (0.25f)); g.strokePath (p, PathStrokeType (4.f));
        g.setColour (kAmber); g.strokePath (p, PathStrokeType (1.6f));
    }
private:
    MoguProcessor& proc;
};

class StepCell : public Component
{
public:
    StepCell (MoguProcessor& p, int i) : proc (p), idx (i)
    {
        char b[16];
        std::snprintf (b, sizeof b, "s%02don", i + 1);   on = p.apvts.getParameter (b);
        std::snprintf (b, sizeof b, "s%02dnote", i + 1); note = p.apvts.getParameter (b);
        std::snprintf (b, sizeof b, "s%02dtie", i + 1);  tie = p.apvts.getParameter (b);
    }
    static String noteName (int n) { static const char* nn[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return String (nn[n % 12]) + String (n / 12 - 1); }
    bool isOn() const { return on->getValue() > 0.5f; }
    bool isTie() const { return tie->getValue() > 0.5f; }
    int noteNum() const { return (int) std::lround (note->convertFrom0to1 (note->getValue())); }
    Rectangle<float> ledR() const { return { getWidth() * 0.5f - 18.f, 40.f, 36.f, 36.f }; }
    Rectangle<float> noteR() const { return { 6.f, 92.f, getWidth() - 12.f, 74.f }; }
    Rectangle<float> tieR() const { return { 10.f, 184.f, getWidth() - 20.f, 36.f }; }
    void paint (Graphics& g) override
    {
        const bool lit = isOn(), cur = proc.currentStep() == idx && proc.seqRunning();
        const bool dim = idx >= (int) std::lround (proc.value ("sqLen"));
        auto r = getLocalBounds().toFloat().reduced (1.f);
        g.setColour (Colour (cur ? 0x30ff3a2a : 0x0cffffff)); g.fillRoundedRectangle (r, 8.f);
        g.setColour (cur ? Colour (0xaaff4a3a) : Colour (0x22ffffff)); g.drawRoundedRectangle (r, 8.f, 1.f);
        const float al = dim ? 0.35f : 1.f;
        txt (g, String (idx + 1), F (3, 15.f), kDim.withAlpha (al), { 0.f, 8.f, (float) getWidth(), 20.f });
        const auto lr = ledR();
        if (lit) { g.setColour (kLedRed.withAlpha (cur ? 0.55f : 0.28f * al)); g.fillEllipse (lr.expanded (cur ? 11.f : 7.f)); }
        g.setGradientFill (ColourGradient (lit ? Colour (0xffff7a6a) : Colour (0xff3a2020), lr.getCentreX() - 5.f, lr.getY() + 4.f,
                                           lit ? Colour (0xffb00000) : Colour (0xff160b0b), lr.getRight(), lr.getBottom(), true));
        g.fillEllipse (lr);
        g.setColour (Colours::black.withAlpha (0.7f)); g.drawEllipse (lr, 1.4f);
        const auto nr = noteR();
        g.setColour (Colour (0xff09090a)); g.fillRoundedRectangle (nr, 7.f);
        g.setColour (Colour (0x30ffffff)); g.drawRoundedRectangle (nr, 7.f, 1.f);
        txt (g, noteName (noteNum()), F (2, 32.f), (lit ? kInk : kDim).withAlpha (al), nr);
        const auto tr = tieR(); const bool t = isTie();
        g.setColour (t ? kAmber : Colour (0xff0d0e0f)); g.fillRoundedRectangle (tr, 6.f);
        g.setColour (t ? Colour (0xffffd58a) : Colour (0x30ffffff)); g.drawRoundedRectangle (tr, 6.f, 1.f);
        txt (g, "TIE", F (1, 17.f, 0.1f), t ? Colour (0xff1a1205) : kDim.withAlpha (al), tr);
    }
    void mouseDown (const MouseEvent& e) override
    {
        dragNote = false;
        if (ledR().expanded (8.f).contains (e.position)) toggle (on);
        else if (tieR().contains (e.position)) toggle (tie);
        else if (noteR().contains (e.position)) { dragNote = true; startY = e.position.y; startNote = noteNum(); note->beginChangeGesture(); }
    }
    void mouseDrag (const MouseEvent& e) override
    {
        if (dragNote) setNote (startNote + (int) std::lround ((startY - e.position.y) / 7.f));
    }
    void mouseUp (const MouseEvent&) override { if (dragNote) note->endChangeGesture(); dragNote = false; }
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override
    {
        if (w.deltaY == 0.f) return;
        note->beginChangeGesture(); setNote (noteNum() + (w.deltaY > 0 ? 1 : -1)); note->endChangeGesture();
    }
private:
    void toggle (RangedAudioParameter* p) { p->beginChangeGesture(); p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.f : 1.f); p->endChangeGesture(); repaint(); }
    void setNote (int n) { note->setValueNotifyingHost (note->convertTo0to1 ((float) jlimit (24, 72, n))); repaint(); }
    MoguProcessor& proc; int idx;
    RangedAudioParameter *on, *note, *tie;
    bool dragNote = false; float startY = 0; int startNote = 36;
};

class Wheel : public Component
{
public:
    Wheel (bool spring_, std::function<void (float)> cb_) : spring (spring_), cb (std::move (cb_)) {}
    float v = 0.f; bool dragging = false;
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.f);
        g.setColour (Colour (0xff060607)); g.fillRoundedRectangle (r, 8.f);
        for (float y = r.getY() + 4; y < r.getBottom() - 4; y += 7.f)
        {
            g.setColour (Colour (0xff3b3c41)); g.fillRect (r.getX() + 3, y, r.getWidth() - 6, 3.f);
        }
        g.setColour (Colour (0x30ffffff)); g.drawRoundedRectangle (r, 8.f, 1.f);
        const float th = 16.f, y = r.getY() + (1.f - v) * (r.getHeight() - th);
        g.setGradientFill (ColourGradient (Colour (0xffd8d9de), 0.f, y, Colour (0xff6a6b72), 0.f, y + th, false));
        g.fillRoundedRectangle (r.getX() + 1, y, r.getWidth() - 2, th, 4.f);
        g.setColour (kAmber); g.fillRect (r.getX() + 4, y + th * 0.5f - 1.f, r.getWidth() - 8, 2.f);
    }
    void mouseDown (const MouseEvent& e) override { dragging = true; at (e); }
    void mouseDrag (const MouseEvent& e) override { at (e); }
    void mouseUp (const MouseEvent&) override { dragging = false; if (spring) { v = 0.5f; cb (v); repaint(); } }
private:
    void at (const MouseEvent& e) { v = jlimit (0.f, 1.f, 1.f - (e.position.y - 10.f) / (float) (getHeight() - 20)); cb (v); repaint(); }
    bool spring; std::function<void (float)> cb;
};

class Keys : public Component
{
public:
    static constexpr int lo = 36, hi = 84;
    explicit Keys (MoguProcessor& p) : proc (p) { for (int n = lo; n <= hi; ++n) if (! isBlack (n)) ++nWhite; }
    static bool isBlack (int n) { const int k = n % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
    Rectangle<float> keyRect (int n) const
    {
        const float ww = getWidth() / (float) nWhite; int wi = 0;
        for (int m = lo; m < n; ++m) if (! isBlack (m)) ++wi;
        if (! isBlack (n)) return { wi * ww, 0.f, ww, (float) getHeight() };
        return { wi * ww - ww * 0.3f, 0.f, ww * 0.6f, getHeight() * 0.62f };
    }
    int noteAt (Point<float> p) const
    {
        for (int n = lo; n <= hi; ++n) if (isBlack (n) && keyRect (n).contains (p)) return n;
        for (int n = lo; n <= hi; ++n) if (! isBlack (n) && keyRect (n).contains (p)) return n;
        return -1;
    }
    void paint (Graphics& g) override
    {
        for (int pass = 0; pass < 2; ++pass)
            for (int n = lo; n <= hi; ++n)
            {
                if (isBlack (n) != (pass == 1)) continue;
                const bool down = proc.keyState.isNoteOnForChannels (0xffff, n);
                auto r = keyRect (n).reduced (pass ? 0.f : 1.f, 0.f);
                if (pass == 0)
                {
                    g.setGradientFill (ColourGradient (down ? Colour (0xffffc864) : Colour (0xfff2f2f4), 0.f, 0.f, down ? Colour (0xffd8932a) : Colour (0xffb9bac0), 0.f, r.getBottom(), false));
                    g.fillRoundedRectangle (r, 3.f);
                    if (n % 12 == 0) txt (g, "C" + String (n / 12 - 1), F (1, 14.f), Colour (0xff55565c), r.withTrimmedTop (r.getHeight() - 22.f));
                }
                else
                {
                    g.setGradientFill (ColourGradient (down ? Colour (0xffe09a2c) : Colour (0xff3d3e44), 0.f, 0.f, Colour (0xff060607), 0.f, r.getBottom(), false));
                    g.fillRoundedRectangle (r, 3.f);
                    g.setColour (Colour (0x30ffffff)); g.drawRoundedRectangle (r, 3.f, 1.f);
                }
            }
    }
    void mouseDown (const MouseEvent& e) override { press (noteAt (e.position)); }
    void mouseDrag (const MouseEvent& e) override { press (noteAt (e.position)); }
    void mouseUp (const MouseEvent&) override { press (-1); }
private:
    void press (int n)
    {
        if (n == cur) return;
        if (cur >= 0) proc.keyState.noteOff (1, cur, 0.f);
        cur = n;
        if (cur >= 0) proc.keyState.noteOn (1, cur, 0.8f);
        repaint();
    }
    MoguProcessor& proc; int nWhite = 0, cur = -1;
};
}  // namespace

// ================================================================== panel
class MoguEditor::Panel : public Component, private Timer
{
public:
    explicit Panel (MoguProcessor& p) : proc (p)
    {
        auto knob = [&] (const char* id, const char* lab, int cx, int top, int sz = 44, bool bip = false)
        {
            auto* k = new Knob (proc, id, lab, sz, bip); owned.add (k); addAndMakeVisible (k);
            k->setTopLeftPosition (cx - k->getWidth() / 2, top);
        };
        auto pills = [&] (const char* id, const char* lab, int x, int y, int cols, int cw, int ch = 30)
        {
            auto* c = new Pills (proc, id, lab, cols, cw, ch); owned.add (c); addAndMakeVisible (c); c->setTopLeftPosition (x, y);
        };
        // header
        scope = std::make_unique<Scope> (proc); addAndMakeVisible (*scope); scope->setBounds (330, 26, 230, 56);
        presetBox = std::make_unique<PresetBox> (proc); addAndMakeVisible (*presetBox); presetBox->setBounds (590, 34, 380, 42);
        pills ("hostSync", "HOST SYNC", 1010, 26, 2, 60, 30);
        knob ("vol", "VOLUME", 1840, 6, 46);
        // controllers
        knob ("tune", "TUNE", 14 + 45, 146, 42, true); knob ("glide", "GLIDE", 14 + 127, 146, 42);
        pills ("multi", "TRIGGER", 24, 250, 2, 72); pills ("bendRange", "BEND", 24, 322, 3, 48);
        // oscillators
        for (int i = 0; i < 3; ++i)
        {
            const int x = 210 + i * 214;
            pills (i == 0 ? "o1range" : i == 1 ? "o2range" : "o3range", "RANGE", x, 188, 6, 31, 30);
            pills (i == 0 ? "o1wave" : i == 1 ? "o2wave" : "o3wave", "WAVE", x + 4, 252, 3, 62, 30);
            if (i == 1) knob ("o2tune", "TUNE", x + 103, 350, 40, true);
            if (i == 2) { knob ("o3tune", "TUNE", x + 52, 350, 40, true); pills ("o3kbd", "KBD CTL", x + 116, 354, 2, 44, 30); }
        }
        // mixer
        knob ("m1", "OSC 1", 878 + 65, 150); knob ("m2", "OSC 2", 878 + 193, 150);
        knob ("m3", "OSC 3", 878 + 65, 252); knob ("mN", "NOISE", 878 + 193, 252);
        pills ("noiseType", "NOISE", 898, 364, 2, 98, 30);
        // filter
        knob ("cutoff", "CUTOFF", 14 + 60, 496, 54); knob ("emph", "EMPHASIS", 14 + 150, 500); knob ("contour", "CONTOUR", 14 + 240, 500); knob ("fdrive", "DRIVE", 14 + 330, 500);
        pills ("kbd", "KBD TRACK", 60, 604, 4, 70);
        knob ("fA", "ATTACK", 407 + 48, 500); knob ("fD", "DECAY", 407 + 118, 500); knob ("fS", "SUSTAIN", 407 + 188, 500);
        knob ("aA", "ATTACK", 655 + 48, 500); knob ("aD", "DECAY", 655 + 118, 500); knob ("aS", "SUSTAIN", 655 + 188, 500);
        pills ("rel", "RELEASE", 668, 604, 2, 96);
        pills ("modSrc", "SOURCE", 912, 494, 3, 70, 26);
        knob ("modRate", "RATE", 902 + 45, 578, 36); knob ("modPitch", "PITCH", 902 + 117, 578, 36); knob ("modFilt", "FILTER", 902 + 189, 578, 36);
        // effects
        knob ("drive", "AMOUNT", 14 + 66, 744, 46);
        knob ("chRate", "RATE", 205, 744, 46); knob ("chDepth", "DEPTH", 305, 744, 46); knob ("chMix", "MIX", 405, 744, 46);
        knob ("dlTime", "TIME", 530, 744, 46); knob ("dlFb", "FEEDBACK", 630, 744, 46); knob ("dlTone", "TONE", 730, 744, 46); knob ("dlMix", "MIX", 830, 744, 46);
        pills ("dlSync", "SYNC", 892, 724, 1, 70, 30);
        knob ("rvSize", "SIZE", 1030, 744, 46); knob ("rvMix", "MIX", 1100, 744, 46);
        // sequencer
        auto addBtn = [&] (const char* t, int x, int y, int w, std::function<void()> f, std::function<bool()> st = {})
        {
            auto* b = new Btn (t, std::move (f), std::move (st)); owned.add (b); addAndMakeVisible (b); b->setBounds (x, y, w, 36); return b;
        };
        auto* run = addBtn ("RUN", 1162, 186, 74, [this] { proc.setValue ("run", proc.value ("run") > 0.5f ? 0.f : 1.f); }, [this] { return proc.seqRunning(); });
        run->altText = "STOP";
        addBtn ("RND", 1242, 186, 56, [this] { proc.randomizeSteps(); });
        addBtn ("RESET", 1304, 186, 74, [this] { proc.resetSteps(); });
        knob ("sqBpm", "TEMPO", 1450, 142, 40); knob ("sqLen", "LENGTH", 1530, 142, 40); knob ("sqGate", "GATE", 1610, 142, 40); knob ("sqSwing", "SWING", 1690, 142, 40);
        pills ("sqRate", "RATE", 1764, 150, 3, 44, 30);
        pills ("sqDir", "DIRECTION", 1162, 256, 4, 64, 30);
        pills ("sqKey", "KEY>TRANS", 1450, 256, 2, 64, 30);
        for (int i = 0; i < 16; ++i)
        {
            auto* c = new StepCell (proc, i); cells.add (c); addAndMakeVisible (c);
            c->setBounds (1162 + (i % 8) * 92, 326 + (i / 8) * 282, 88, 268);
        }
        // keyboard
        pitchW = std::make_unique<Wheel> (true, [this] (float v) { proc.setBendUI (v * 2.f - 1.f); });
        modW = std::make_unique<Wheel> (false, [this] (float v) { proc.setWheelUI (v); });
        pitchW->v = 0.5f;
        addAndMakeVisible (*pitchW); addAndMakeVisible (*modW);
        pitchW->setBounds (26, 940, 34, 98); modW->setBounds (68, 940, 34, 98);
        keys = std::make_unique<Keys> (proc); addAndMakeVisible (*keys); keys->setBounds (120, 944, 1774, 94);
        setSize (designW, designH);
        startTimerHz (30);
    }
    ~Panel() override { stopTimer(); for (int n = 0; n < 128; ++n) if (pc[n]) proc.keyState.noteOff (1, n, 0.f); }

    void paint (Graphics& g) override
    {
        g.drawImageAt (brushed(), 0, 0);
        g.setColour (Colour (0x66000000)); g.fillRect (0, 0, designW, 104);
        g.setColour (Colour (0x22ffffff)); g.fillRect (0, 104, designW, 1);
        {
            const Font lf = F (2, 52.f, 0.0f);
            const float w1 = lf.getStringWidthFloat ("mini "), w2 = lf.getStringWidthFloat ("mogu");
            txt (g, "mini ", lf, Colour (0xfff2f2f4), { 28.f, 8.f, w1 + 4.f, 60.f }, Justification::centredLeft);
            txt (g, "mogu", lf, Colour (0xfff2f2f4), { 28.f + w1, 8.f, w2 + 4.f, 60.f }, Justification::centredLeft);
            txt (g, "mogu", lf, kAmber, { 28.f + w1 + w2, 8.f, w2 + 4.f, 60.f }, Justification::centredLeft);
        }
        txt (g, "MONOPHONIC ANALOG SYNTHESIZER", F (1, 14.f, 0.22f), kDim, { 30.f, 68.f, 300.f, 20.f }, Justification::centredLeft);
        for (const auto& c : kCards)
        {
            const auto r = c.r.toFloat();
            g.setColour (Colour (0x30000000)); g.fillRoundedRectangle (r.translated (0.f, 2.f), 10.f);
            g.setColour (Colour (0x16ffffff)); g.fillRoundedRectangle (r, 10.f);
            g.setColour (Colour (0x28ffffff)); g.drawRoundedRectangle (r, 10.f, 1.f);
            txt (g, c.title, F (1, 17.f, 0.3f), Colour (0xffb8b9bf), { r.getX() + 12.f, r.getY() + 6.f, r.getWidth() - 20.f, 24.f }, Justification::centredLeft);
        }
        for (int i = 0; i < 3; ++i)
        {
            const float x = 210.f + i * 214.f;
            g.setColour (Colour (0x10000000)); g.fillRoundedRectangle (x, 148.f, 206.f, 300.f, 8.f);
            txt (g, "OSC " + String (i + 1), F (2, 22.f, 0.2f), kInk, { x, 150.f, 206.f, 28.f });
        }
        auto sub = [&] (const char* t, int x) { txt (g, t, F (1, 16.f, 0.25f), kDim, { (float) x, 712.f, 200.f, 20.f }, Justification::centredLeft); };
        sub ("DRIVE", 28); sub ("CHORUS", 150); sub ("DELAY", 484); sub ("REVERB", 988);
        g.setColour (Colour (0x22ffffff));
        for (int x : { 135, 462, 982 }) g.fillRect (x, 712, 1, 160);
        txt (g, "TRANSPOSE", F (1, 15.5f, 0.08f), kDim, { 1620.f, 256.f, 120.f, 18.f });
        const int tr = proc.transpose();
        txt (g, String (tr >= 0 ? "+" : "") + String (tr), F (3, 26.f), kAmber, { 1620.f, 278.f, 120.f, 32.f });
        txt (g, proc.hostDriving() ? "CLOCK: DAW" : "CLOCK: INTERNAL", F (1, 15.5f, 0.1f), kDim, { 1740.f, 262.f, 160.f, 24.f }, Justification::centredRight);
        txt (g, "PITCH", F (1, 14.f, 0.1f), kDim, { 20.f, 1040.f, 46.f, 18.f }); txt (g, "MOD", F (1, 14.f, 0.1f), kDim, { 62.f, 1040.f, 46.f, 18.f });
        // red-LED dots around the sequencer title carry no meaning; keep the panel clean
    }

    bool handleKey (const KeyPress& k)
    {
        if (k.getKeyCode() == KeyPress::spaceKey) { proc.setValue ("run", proc.value ("run") > 0.5f ? 0.f : 1.f); return true; }
        const auto c = (juce_wchar) CharacterFunctions::toLowerCase (k.getTextCharacter());
        if (c == 'z') { oct = jmax (0, oct - 1); return true; }
        if (c == 'x') { oct = jmin (6, oct + 1); return true; }
        const int off = offsetFor (c);
        if (off < 0 || k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown()) return false;
        const int n = jlimit (0, 127, (oct + 1) * 12 + off);
        if (! pc[n]) { pc[n] = true; pcChar[n] = c; proc.keyState.noteOn (1, n, 0.8f); }
        return true;
    }
    void keysChanged()
    {
        for (int n = 0; n < 128; ++n)
            if (pc[n] && ! KeyPress::isKeyCurrentlyDown ((int) pcChar[n]) && ! KeyPress::isKeyCurrentlyDown ((int) CharacterFunctions::toUpperCase (pcChar[n])))
            { pc[n] = false; proc.keyState.noteOff (1, n, 0.f); }
    }

private:
    static int offsetFor (juce_wchar c)
    {
        switch (c) { case 'a': return 0; case 'w': return 1; case 's': return 2; case 'e': return 3; case 'd': return 4; case 'f': return 5; case 't': return 6;
                     case 'g': return 7; case 'y': return 8; case 'h': return 9; case 'u': return 10; case 'j': return 11; case 'k': return 12; case 'o': return 13;
                     case 'l': return 14; case 'p': return 15; case ';': return 16; default: return -1; }
    }
    void timerCallback() override
    {
        scope->repaint(); keys->repaint(); repaint (1150, 250, 760, 60);
        for (auto* c : cells) c->repaint();
        if (! modW->dragging) { const float w = proc.wheelShown(); if (std::abs (w - modW->v) > 0.002f) { modW->v = w; modW->repaint(); } }
        if (! pitchW->dragging) { const float b = proc.bendShown() * 0.5f + 0.5f; if (std::abs (b - pitchW->v) > 0.002f) { pitchW->v = b; pitchW->repaint(); } }
        presetBox->repaint();
    }

    MoguProcessor& proc;
    OwnedArray<Component> owned;
    OwnedArray<StepCell> cells;
    std::unique_ptr<Scope> scope; std::unique_ptr<PresetBox> presetBox;
    std::unique_ptr<Wheel> pitchW, modW; std::unique_ptr<Keys> keys;
    bool pc[128] = {}; juce_wchar pcChar[128] = {}; int oct = 3;
};

// ================================================================== editor
MoguEditor::MoguEditor (MoguProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    panel = std::make_unique<Panel> (p);
    addAndMakeVisible (*panel);
    panel->setBounds (0, 0, designW, designH);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) designW / designH);
    setResizeLimits (640, 360, 3840, 2160);
    setWantsKeyboardFocus (true);
    addMouseListener (this, true);
    setSize (1280, 720);
}

MoguEditor::~MoguEditor() { removeMouseListener (this); }

void MoguEditor::paint (Graphics& g) { g.fillAll (Colour (0xff141517)); }

void MoguEditor::resized()
{
    const float s = (float) getWidth() / (float) designW;
    panel->setTransform (AffineTransform::scale (s));
}

bool MoguEditor::keyPressed (const KeyPress& k) { return panel->handleKey (k); }
bool MoguEditor::keyStateChanged (bool) { panel->keysChanged(); return false; }
