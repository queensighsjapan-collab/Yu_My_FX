#include "PluginEditor.h"

namespace
{
    const juce::Colour creamHi   (0xfff4ead6), creamMid (0xffefe3cb), creamLo (0xffe9dcc0);
    const juce::Colour walnutHi  (0xff6e452a), walnutLo (0xff3a2110);
    const juce::Colour brassHi   (0xffd9b96a), brassMid (0xffb98f3e), brassLo (0xff96702c);
    const juce::Colour plateRed  (0xff7c1d12);
    const juce::Colour inkBrown  (0xff4c3a14);
    const juce::Colour labelCol  (0xff5c4a22);
    const juce::Colour valueCol  (0xff8a744c);
    const juce::Colour amber     (0xffffb340), amberDim (0xffe89b35), amberHi (0xffffc966);
    const juce::Colour lcdBgHi   (0xff2b1a05), lcdBgLo (0xff170d02);

    juce::Font serifFont (float h, bool bold = true)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), h,
                                             bold ? juce::Font::bold : juce::Font::plain));
    }
    juce::Font sansFont (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), h,
                                              bold ? juce::Font::bold : juce::Font::plain));
    }
    juce::Font monoFont (float h)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain));
    }
    juce::String spaced (const juce::String& s, int gap = 1)
    {
        juce::String out;
        for (int i = 0; i < s.length(); ++i)
        {
            if (i > 0) out += juce::String::repeatedString (" ", gap);
            out += juce::String::charToString (s[i]);
        }
        return out;
    }
}

//==============================================================================
TapeKnob::TapeKnob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, bool small)
    : isSmall (small)
{
    param = apvts.getParameter (paramId);
    refreshFromParam();
    setWantsKeyboardFocus (false);
}

void TapeKnob::refreshFromParam()
{
    if (param != nullptr)
        shownValue = param->getValue();
    repaint();
}

void TapeKnob::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto knob = bounds.withSizeKeepingCentre (juce::jmin (bounds.getWidth(), bounds.getHeight()),
                                              juce::jmin (bounds.getWidth(), bounds.getHeight()));
    // shadow
    g.setColour (juce::Colour (0x59402d0f));
    g.fillEllipse (knob.translated (0.0f, 3.0f));
    // body
    juce::ColourGradient grad (juce::Colour (0xff5a5348), knob.getCentreX(), knob.getY(),
                               juce::Colour (0xff211e1a), knob.getCentreX(), knob.getBottom(), false);
    g.setGradientFill (grad);
    g.fillEllipse (knob);
    auto inner = knob.reduced (7.0f);
    juce::ColourGradient grad2 (juce::Colour (0xff6e675c), inner.getCentreX(), inner.getY(),
                                juce::Colour (0xff2a2723), inner.getCentreX(), inner.getBottom(), false);
    g.setGradientFill (grad2);
    g.fillEllipse (inner);
    // pointer
    const float angle = juce::degreesToRadians (-135.0f + 270.0f * shownValue);
    const auto c = knob.getCentre();
    const float r0 = knob.getWidth() * 0.30f, r1 = knob.getWidth() * 0.46f;
    g.setColour (juce::Colour (0xfff3d98b));
    g.drawLine (c.x + r0 * std::sin (angle), c.y - r0 * std::cos (angle),
                c.x + r1 * std::sin (angle), c.y - r1 * std::cos (angle), 3.0f);
}

void TapeKnob::mouseDown (const juce::MouseEvent& e)
{
    dragStartY = e.y;
    dragStartValue = param != nullptr ? param->getValue() : shownValue;
    if (param != nullptr)
        param->beginChangeGesture();
}

void TapeKnob::mouseDrag (const juce::MouseEvent& e)
{
    if (param == nullptr) return;
    const float range = e.mods.isShiftDown() ? 600.0f : 160.0f;
    const float v = juce::jlimit (0.0f, 1.0f, dragStartValue + (float) (dragStartY - e.y) / range);
    param->setValueNotifyingHost (v);
    shownValue = v;
    repaint();
}

void TapeKnob::mouseUp (const juce::MouseEvent&)
{
    if (param != nullptr)
        param->endChangeGesture();
}

void TapeKnob::mouseDoubleClick (const juce::MouseEvent&)
{
    if (param != nullptr)
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->getDefaultValue());
        param->endChangeGesture();
        refreshFromParam();
    }
}

void TapeKnob::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (param == nullptr) return;
    param->beginChangeGesture();
    param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, param->getValue() + w.deltaY * 0.05f));
    param->endChangeGesture();
    refreshFromParam();
}

//==============================================================================
PresetButton::PresetButton (juce::AudioProcessorValueTreeState& apvts, int uiIndex, const juce::String& label)
    : index (uiIndex), text (label)
{
    param = apvts.getParameter ("preset");
    refreshFromParam();
    setWantsKeyboardFocus (false);
}

void PresetButton::refreshFromParam()
{
    if (param != nullptr)
        active = juce::roundToInt (param->convertFrom0to1 (param->getValue())) == index;
    repaint();
}

void PresetButton::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    // base shadow (the mock's 3px bottom ledge)
    g.setColour (juce::Colour (0xff8a744c));
    g.fillRoundedRectangle (b.withTrimmedTop (3.0f), 7.0f);
    auto face = b.withHeight (b.getHeight() - 3.0f);
    juce::ColourGradient grad (active ? juce::Colour (0xfff7efdb) : juce::Colour (0xffefe6cf),
                               face.getX(), face.getY(),
                               active ? juce::Colour (0xffd8c491) : juce::Colour (0xffcbb586),
                               face.getX(), face.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (face, 7.0f);
    g.setColour (juce::Colour (0xff7a6435));
    g.drawRoundedRectangle (face, 7.0f, 1.0f);

    // lamp
    const auto lamp = juce::Rectangle<float> (b.getCentreX() - 4.5f, 9.0f, 9.0f, 9.0f);
    if (active)
    {
        g.setColour (juce::Colour (0x55e64623));
        g.fillEllipse (lamp.expanded (4.0f));
        juce::ColourGradient lg (juce::Colour (0xffffb3a0), lamp.getCentreX(), lamp.getY(),
                                 juce::Colour (0xff8f1d0c), lamp.getCentreX(), lamp.getBottom(), false);
        g.setGradientFill (lg);
        g.fillEllipse (lamp);
    }
    else
    {
        juce::ColourGradient lg (juce::Colour (0xff6b5a3a), lamp.getCentreX(), lamp.getY(),
                                 juce::Colour (0xff4a3c20), lamp.getCentreX(), lamp.getBottom(), false);
        g.setGradientFill (lg);
        g.fillEllipse (lamp);
    }

    g.setColour (active ? juce::Colour (0xff6d1410) : inkBrown);
    g.setFont (sansFont (11.0f, true));
    g.drawText (text, getLocalBounds().withTrimmedTop (22), juce::Justification::centredTop, false);
}

void PresetButton::mouseUp (const juce::MouseEvent& e)
{
    if (param != nullptr && getLocalBounds().contains (e.getPosition()))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
        param->endChangeGesture();
        refreshFromParam();
    }
}

//==============================================================================
HalfSwitch::HalfSwitch (juce::AudioProcessorValueTreeState& apvts)
{
    param = apvts.getParameter ("halfSpeed");
    refreshFromParam();
    setWantsKeyboardFocus (false);
}

void HalfSwitch::refreshFromParam()
{
    if (param != nullptr)
        on = param->getValue() > 0.5f;
    repaint();
}

void HalfSwitch::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    juce::ColourGradient grad (juce::Colour (0xffc9b586), b.getX(), b.getY(),
                               juce::Colour (0xffa68f5c), b.getX(), b.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (juce::Colour (0xff7a6435));
    g.drawRoundedRectangle (b, 6.0f, 1.0f);
    g.setColour (juce::Colour (0x663c2805));
    g.fillRoundedRectangle (b.reduced (3.0f), 4.0f);

    auto lever = juce::Rectangle<float> (b.getX() + 5.0f, on ? b.getBottom() - 29.0f : b.getY() + 5.0f,
                                         b.getWidth() - 10.0f, 24.0f);
    juce::ColourGradient lg (juce::Colour (0xff4a443c), lever.getX(), lever.getY(),
                             juce::Colour (0xff1d1a16), lever.getX(), lever.getBottom(), false);
    g.setGradientFill (lg);
    g.fillRoundedRectangle (lever, 4.0f);
    g.setColour (juce::Colour (0x4dffffff));
    g.drawLine (lever.getX() + 3.0f, lever.getY() + 1.5f, lever.getRight() - 3.0f, lever.getY() + 1.5f, 1.0f);
}

void HalfSwitch::mouseUp (const juce::MouseEvent& e)
{
    if (param != nullptr && getLocalBounds().contains (e.getPosition()))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (on ? 0.0f : 1.0f);
        param->endChangeGesture();
        refreshFromParam();
    }
}

//==============================================================================
void Reel::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float R = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;

    g.setColour (juce::Colour (0xaa000000));
    g.fillEllipse (b.translated (0.0f, 3.0f).reduced (2.0f));
    g.setColour (juce::Colour (0xff191512));
    g.fillEllipse (b.reduced (1.0f));
    g.setColour (juce::Colour (0xff3d3425));
    g.drawEllipse (b.reduced (1.5f), 2.0f);
    g.setColour (juce::Colour (0xff0a0908));
    g.drawEllipse (b.reduced (7.0f), 7.0f);
    g.setColour (juce::Colour (0xff2a251e));
    g.fillEllipse (b.reduced (19.0f));

    // three cutouts + brass spokes, rotated
    for (int i = 0; i < 3; ++i)
    {
        const float a = angle + (float) i * juce::MathConstants<float>::twoPi / 3.0f;
        const auto dir = juce::Point<float> (std::sin (a), -std::cos (a));
        const auto side = juce::Point<float> (dir.y, -dir.x);
        // cutout: dark wedge sector approximated by a path
        juce::Path wedge;
        const auto p0 = c + dir * (R * 0.98f) + side * (R * 0.30f);
        const auto p1 = c + dir * (R * 0.98f) - side * (R * 0.30f);
        wedge.startNewSubPath (c + dir * (R * 0.30f) + side * (R * 0.13f));
        wedge.lineTo (p0);
        wedge.lineTo (p1);
        wedge.lineTo (c + dir * (R * 0.30f) - side * (R * 0.13f));
        wedge.closeSubPath();
        g.setColour (juce::Colour (0xff0f0d0a));
        g.fillPath (wedge);
        // spoke
        const auto s0 = c + dir * (R * 0.62f);
        const auto s1 = c + dir * (R * 0.97f);
        g.setColour (juce::Colour (0xffc8a452));
        g.drawLine (s0.x, s0.y, s1.x, s1.y, 7.0f);
    }

    g.setColour (juce::Colour (0xff4a4234));
    g.fillEllipse (juce::Rectangle<float> (c.x - 13.0f, c.y - 13.0f, 26.0f, 26.0f));
    g.setColour (juce::Colour (0xff191512));
    g.drawEllipse (juce::Rectangle<float> (c.x - 13.0f, c.y - 13.0f, 26.0f, 26.0f), 2.0f);
    g.setColour (juce::Colour (0xffc8a452));
    g.fillEllipse (juce::Rectangle<float> (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f));
}

//==============================================================================
void TapeKeyboard::drawWhiteNote (int, juce::Graphics& g, juce::Rectangle<float> area,
                                  bool isDown, bool, juce::Colour, juce::Colour)
{
    juce::ColourGradient grad (isDown ? juce::Colour (0xffe9e2cd) : juce::Colour (0xfffdfaf1),
                               area.getX(), area.getY(),
                               isDown ? juce::Colour (0xffcbbd97) : juce::Colour (0xffe2d9c2),
                               area.getX(), area.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRect (area);
    g.setColour (juce::Colour (0xff9a8f74));
    g.drawRect (area, 1.0f);
    if (isDown)
    {
        g.setColour (juce::Colour (0x30000000));
        g.fillRect (area.withHeight (10.0f));
    }
}

void TapeKeyboard::drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> area,
                                  bool isDown, bool, juce::Colour)
{
    // Slimmer than JUCE's default proportion, like the mock's keys.
    auto a = area.withSizeKeepingCentre (area.getWidth() * 0.82f, area.getHeight() * 0.88f)
                 .withY (area.getY());
    juce::ColourGradient grad (isDown ? juce::Colour (0xff241f18) : juce::Colour (0xff3a3630),
                               a.getX(), a.getY(),
                               isDown ? juce::Colour (0xff060504) : juce::Colour (0xff0c0b08),
                               a.getX(), a.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (a, 3.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (a, 3.0f, 1.0f);
    g.setColour (juce::Colour (0x22ffffff));
    g.fillRect (a.withTrimmedTop (a.getHeight() - 8.0f).reduced (3.0f, 0.0f));
}



//==============================================================================
MeronTronIIIEditor::MeronTronIIIEditor (MeronTronIIIProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    setWantsKeyboardFocus (true);

    addAndMakeVisible (reelL);
    addAndMakeVisible (reelR);

    const auto& names = MeronTronIIIProcessor::tapeSetNames();
    for (int i = 0; i < 5; ++i)
    {
        presetBtns[(size_t) i] = std::make_unique<PresetButton> (processor.apvts, i,
                                                                 names[i].toUpperCase());
        addAndMakeVisible (*presetBtns[(size_t) i]);
    }

    knobVolume  = std::make_unique<TapeKnob> (processor.apvts, "volume",  false);
    knobTone    = std::make_unique<TapeKnob> (processor.apvts, "tone",    false);
    knobAttack  = std::make_unique<TapeKnob> (processor.apvts, "attack",  false);
    knobRelease = std::make_unique<TapeKnob> (processor.apvts, "release", false);
    knobReverb  = std::make_unique<TapeKnob> (processor.apvts, "reverb",  true);
    halfSwitch  = std::make_unique<HalfSwitch> (processor.apvts);
    addAndMakeVisible (*knobVolume);
    addAndMakeVisible (*knobTone);
    addAndMakeVisible (*knobAttack);
    addAndMakeVisible (*knobRelease);
    addAndMakeVisible (*knobReverb);
    addAndMakeVisible (*halfSwitch);

    keyboard.setAvailableRange (55, 89);
    keyboard.setKeyWidth ((1008.0f - 20.0f) / 21.0f);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setWantsKeyboardFocus (false);
    addAndMakeVisible (keyboard);
    keyboardState.addListener (this);

    lastTickMs = juce::Time::getMillisecondCounter();
    startTimerHz (30);

    // NB: setSize must come last - it fires resized(), which positions
    // child components that are created above.
    setSize (panelW, panelH);
}

MeronTronIIIEditor::~MeronTronIIIEditor()
{
    keyboardState.removeListener (this);
    stopTimer();
}

//==============================================================================
void MeronTronIIIEditor::resized()
{
    // tape window 56,118 372x208; reels inside
    reelL.setBounds (56 + 30, 118 + 40, 128, 128);
    reelR.setBounds (56 + 372 - 30 - 128, 118 + 40, 128, 128);

    for (int i = 0; i < 5; ++i)
        presetBtns[(size_t) i]->setBounds (56 + i * (68 + 8), 338, 68, 52);

    knobVolume ->setBounds (454 + 15, 250, 62, 62);
    knobTone   ->setBounds (552 + 15, 250, 62, 62);
    knobAttack ->setBounds (650 + 15, 250, 62, 62);
    knobRelease->setBounds (748 + 15, 250, 62, 62);
    halfSwitch ->setBounds (862 + 29, 252, 34, 58);
    knobReverb ->setBounds (960 + 22, 257, 48, 48);

    keyboard.setBounds (66, 462, 988, 156);
}

//==============================================================================
void MeronTronIIIEditor::paint (juce::Graphics& g)
{
    // ---- panel body (cream enamel) ---------------------------------------
    auto panel = getLocalBounds().toFloat();
    juce::ColourGradient body (creamHi, 0.0f, 0.0f, creamLo, 0.0f, (float) panelH, false);
    body.addColour (0.45, creamMid);
    g.setGradientFill (body);
    g.fillRoundedRectangle (panel, 14.0f);

    // walnut cheeks
    for (auto side : { panel.withWidth (34.0f), panel.withX (panel.getRight() - 34.0f).withWidth (34.0f) })
    {
        juce::ColourGradient wg (walnutLo, side.getX(), 0.0f, walnutHi, side.getRight(), 0.0f, false);
        wg.addColour (0.45, juce::Colour (0xff6e452a));
        g.setGradientFill (wg);
        g.fillRect (side);
        g.setColour (juce::Colour (0x24000000));
        for (float y = 6.0f; y < (float) panelH; y += 9.0f)
            g.fillRect (side.withY (y).withHeight (2.0f));
    }

    // top walnut strip
    {
        auto strip = juce::Rectangle<float> (34.0f, 0.0f, (float) panelW - 68.0f, 14.0f);
        juce::ColourGradient sg (walnutHi, 0.0f, 0.0f, juce::Colour (0xff38200f), 0.0f, 14.0f, false);
        g.setGradientFill (sg);
        g.fillRect (strip);
    }

    // ---- nameplate ----------------------------------------------------------
    {
        auto plate = juce::Rectangle<float> (56.0f, 26.0f, 376.0f, 66.0f);
        juce::ColourGradient pg (brassHi, plate.getX(), plate.getY(), brassLo, plate.getX(), plate.getBottom(), false);
        pg.addColour (0.30, brassMid);
        pg.addColour (0.55, juce::Colour (0xffa37c31));
        pg.addColour (0.80, juce::Colour (0xffc8a452));
        g.setGradientFill (pg);
        g.fillRoundedRectangle (plate, 6.0f);
        g.setColour (juce::Colour (0xff6f551f));
        g.drawRoundedRectangle (plate, 6.0f, 1.0f);

        g.setColour (plateRed);
        g.setFont (serifFont (29.0f));
        g.drawText ("MERONTRON III", plate.withTrimmedTop (7.0f).withHeight (34.0f),
                    juce::Justification::centred, false);
        g.setColour (juce::Colour (0xff4a3208));
        g.setFont (sansFont (10.0f, true));
        g.drawText (spaced ("TAPE REPLAY INSTRUMENT", 2), plate.withTrimmedTop (44.0f).withHeight (14.0f),
                    juce::Justification::centred, false);
    }

    // ---- badge + version ------------------------------------------------------
    {
        auto badge = juce::Rectangle<float> (1018.0f, 26.0f, 46.0f, 46.0f);
        juce::ColourGradient bg (juce::Colour (0xff8a2a1a), badge.getCentreX(), badge.getY(),
                                 juce::Colour (0xff5e1709), badge.getCentreX(), badge.getBottom(), false);
        g.setGradientFill (bg);
        g.fillEllipse (badge);
        g.setColour (brassMid);
        g.drawEllipse (badge, 2.0f);
        g.setColour (juce::Colour (0xfff3d98b));
        g.setFont (serifFont (17.0f));
        g.drawText ("YM", badge, juce::Justification::centred, false);

        g.setColour (valueCol);
        g.setFont (sansFont (9.5f, true));
        g.drawText (spaced ("v1.0.1"), juce::Rectangle<float> (950.0f, 78.0f, 114.0f, 12.0f),
                    juce::Justification::right, false);
    }

    // ---- tape window ------------------------------------------------------------
    {
        auto win = juce::Rectangle<float> (56.0f, 118.0f, 372.0f, 208.0f);
        juce::ColourGradient wg (juce::Colour (0xff241d16), win.getX(), win.getY(),
                                 juce::Colour (0xff0e0b09), win.getX(), win.getBottom(), false);
        wg.addColour (0.55, juce::Colour (0xff171310));
        g.setGradientFill (wg);
        g.fillRoundedRectangle (win, 10.0f);
        g.setColour (juce::Colour (0xff57431f));
        g.drawRoundedRectangle (win, 10.0f, 1.0f);
        g.setColour (juce::Colour (0x88000000));
        g.drawRoundedRectangle (win.reduced (2.0f), 9.0f, 3.0f);

        g.setColour (juce::Colour (0x8ce6c88c));
        g.setFont (sansFont (9.0f, true));
        g.drawText (spaced ("TAPE TRANSPORT", 2), win.withTrimmedTop (8.0f).withHeight (12.0f),
                    juce::Justification::centred, false);

        // tape path + head
        auto path = juce::Rectangle<float> (56.0f + 92.0f, 118.0f + 208.0f - 31.0f, 372.0f - 184.0f, 5.0f);
        juce::ColourGradient tg (juce::Colour (0xff4a2c17), 0.0f, path.getY(), juce::Colour (0xff2c1909), 0.0f, path.getBottom(), false);
        g.setGradientFill (tg);
        g.fillRoundedRectangle (path, 2.5f);
        auto head = juce::Rectangle<float> (56.0f + 186.0f - 17.0f, 118.0f + 208.0f - 38.0f, 34.0f, 22.0f);
        juce::ColourGradient hg (juce::Colour (0xffc9c9c9), 0.0f, head.getY(), juce::Colour (0xff6d6d6d), 0.0f, head.getBottom(), false);
        hg.addColour (0.55, juce::Colour (0xff8d8d8d));
        g.setGradientFill (hg);
        g.fillRoundedRectangle (head, 3.0f);
        g.setColour (juce::Colour (0xa6282828));
        g.fillRect (head.withSizeKeepingCentre (2.0f, 14.0f));

        // glass sheen
        juce::Path sheen;
        sheen.startNewSubPath (win.getX(), win.getY());
        sheen.lineTo (win.getX() + 170.0f, win.getY());
        sheen.lineTo (win.getX() + 60.0f, win.getBottom());
        sheen.lineTo (win.getX(), win.getBottom());
        sheen.closeSubPath();
        g.setColour (juce::Colour (0x10ffffff));
        g.fillPath (sheen);
    }

    // ---- LCD ----------------------------------------------------------------------
    {
        auto lcd = juce::Rectangle<float> (448.0f, 118.0f, 616.0f, 118.0f);
        juce::ColourGradient lg (lcdBgHi, lcd.getX(), lcd.getY(), lcdBgLo, lcd.getX(), lcd.getBottom(), false);
        lg.addColour (0.6, juce::Colour (0xff1d1103));
        g.setGradientFill (lg);
        g.fillRoundedRectangle (lcd, 10.0f);
        g.setColour (juce::Colour (0xff4d3a12));
        g.drawRoundedRectangle (lcd, 10.0f, 1.0f);

        const int ui = juce::jlimit (0, 4, (int) processor.apvts.getRawParameterValue ("preset")->load());
        const bool ready = processor.isFontReady();
        const auto setName = MeronTronIIIProcessor::tapeSetNames()[ui];

        g.setColour (amber);
        g.setFont (monoFont (29.0f));
        g.drawText (ready ? "MERON " + setName.toUpperCase() : "NO TAPE LOADED",
                    lcd.reduced (20.0f, 0.0f).withTrimmedTop (13.0f).withHeight (34.0f),
                    juce::Justification::topLeft, false);

        g.setColour (amberDim);
        g.setFont (monoFont (13.0f));
        juce::String line2 = "TAPE SET " + juce::String (ui + 1) + " / 5";
        g.drawText (line2, lcd.reduced (20.0f, 0.0f).withTrimmedTop (58.0f).withHeight (18.0f),
                    juce::Justification::topLeft, false);
        g.drawText ("KEY RANGE G2-F5 - 35 TAPES",
                    lcd.reduced (210.0f, 0.0f).withTrimmedTop (58.0f).withHeight (18.0f),
                    juce::Justification::topLeft, false);

        g.setColour (amberHi);
        g.drawText (lcdNoteLine(), lcd.reduced (20.0f, 0.0f).withTrimmedTop (84.0f).withHeight (18.0f),
                    juce::Justification::topLeft, false);

        // scanlines
        g.setColour (juce::Colour (0x29140000));
        for (float y = lcd.getY() + 3.0f; y < lcd.getBottom(); y += 3.0f)
            g.fillRect (lcd.withY (y).withHeight (1.0f));
    }

    // ---- control labels + values ------------------------------------------------------
    {
        auto drawCtlLabel = [&] (float cellX, float cellW, const juce::String& label, const juce::String& val)
        {
            g.setColour (labelCol);
            g.setFont (sansFont (10.5f, true));
            g.drawText (spaced (label), juce::Rectangle<float> (cellX, 319.0f, cellW, 14.0f),
                        juce::Justification::centred, false);
            g.setColour (valueCol);
            g.setFont (monoFont (9.0f));
            g.drawText (val, juce::Rectangle<float> (cellX, 336.0f, cellW, 12.0f),
                        juce::Justification::centred, false);
        };
        auto pct = [&] (const char* id)
        {
            return juce::String (juce::roundToInt (processor.apvts.getRawParameterValue (id)->load() * 100.0f));
        };
        drawCtlLabel (454.0f, 92.0f, "VOLUME",  pct ("volume"));
        drawCtlLabel (552.0f, 92.0f, "TONE",    pct ("tone"));
        drawCtlLabel (650.0f, 92.0f, "ATTACK",  pct ("attack"));
        drawCtlLabel (748.0f, 92.0f, "RELEASE", pct ("release"));
        const bool half = halfSwitch != nullptr && halfSwitch->isOn();
        drawCtlLabel (862.0f, 92.0f, "HALF SPEED", half ? "ON 1/2" : "OFF");
        drawCtlLabel (960.0f, 92.0f, "REVERB",  pct ("reverb"));

        // divider between RELEASE and HALF SPEED
        juce::ColourGradient dg (juce::Colours::transparentBlack, 0.0f, 254.0f,
                                 juce::Colours::transparentBlack, 0.0f, 350.0f, false);
        dg.addColour (0.5, juce::Colour (0x807a6435));
        g.setGradientFill (dg);
        g.fillRect (juce::Rectangle<float> (849.0f, 254.0f, 1.0f, 96.0f));
    }

    // ---- keyboard surround ---------------------------------------------------------------
    {
        auto kb = juce::Rectangle<float> (56.0f, 452.0f, 1008.0f, 176.0f);
        juce::ColourGradient kg (juce::Colour (0xff6d1410), 0.0f, kb.getY(),
                                 juce::Colour (0xff571009), 0.0f, kb.getBottom(), false);
        g.setGradientFill (kg);
        g.fillRoundedRectangle (kb, 8.0f);
        g.setColour (juce::Colour (0x66000000));
        g.fillRect (kb.withHeight (6.0f));
    }

    // ---- corner screws -----------------------------------------------------------------------
    auto drawScrew = [&] (float cx, float cy, float rotDeg)
    {
        auto s = juce::Rectangle<float> (cx - 6.5f, cy - 6.5f, 13.0f, 13.0f);
        juce::ColourGradient sg (juce::Colour (0xffe8cd85), cx, s.getY(), brassLo, cx, s.getBottom(), false);
        sg.addColour (0.6, brassMid);
        g.setGradientFill (sg);
        g.fillEllipse (s);
        const float a = juce::degreesToRadians (rotDeg);
        g.setColour (juce::Colour (0xbf3c2805));
        g.drawLine (cx - 5.0f * std::cos (a), cy - 5.0f * std::sin (a),
                    cx + 5.0f * std::cos (a), cy + 5.0f * std::sin (a), 1.6f);
    };
    drawScrew (50.0f, 28.0f, 35.0f);
    drawScrew (1070.0f, 28.0f, 110.0f);
    drawScrew (50.0f, 632.0f, 75.0f);
    drawScrew (1070.0f, 632.0f, 150.0f);
}

//==============================================================================
juce::String MeronTronIIIEditor::noteName (int midi)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[midi % 12]) + juce::String (midi / 12 - 2);
}

juce::String MeronTronIIIEditor::lcdNoteLine() const
{
    using Status = MeronTronIIIProcessor::FontStatus;
    const auto st = processor.getFontStatus();
    if (st == Status::loading)
        return "LOADING TAPES...";
    if (st == Status::notFound || st == Status::error)
        return processor.getFontMessage();

    if (processor.getHeldNoteCount() > 0)
    {
        const int n = processor.getLastNote();
        if (n >= 55 && n <= 89)
            return "NOTE " + noteName (n) + " - TAPE " + juce::String (n - 54) + " / 35";
        return "NOTE " + noteName (juce::jlimit (0, 127, n));
    }
    return "- READY -";
}

//==============================================================================
void MeronTronIIIEditor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    const float dt = juce::jlimit (0.0f, 0.1f, (float) (now - lastTickMs) / 1000.0f);
    lastTickMs = now;

    if (processor.isAnyVoiceActive())
    {
        angleL += dt * juce::MathConstants<float>::twoPi / 1.15f;
        angleR += dt * juce::MathConstants<float>::twoPi / 1.50f;
        reelL.setAngle (angleL);
        reelR.setAngle (angleR);
        reelL.repaint();
        reelR.repaint();
    }

    knobVolume->refreshFromParam();
    knobTone->refreshFromParam();
    knobAttack->refreshFromParam();
    knobRelease->refreshFromParam();
    knobReverb->refreshFromParam();
    halfSwitch->refreshFromParam();
    for (auto& b : presetBtns)
        b->refreshFromParam();

    syncKeyboardFromProcessor();
    repaint();
}

void MeronTronIIIEditor::syncKeyboardFromProcessor()
{
    const auto mask = processor.getHeldMask();
    const auto diff = mask ^ keyboardMask;
    if (diff == 0)
        return;
    keyboardMask = mask;
    syncingKeyboard = true;
    for (int n = 55; n <= 89; ++n)
    {
        if ((diff >> (n - 55)) & 1)
        {
            if ((mask >> (n - 55)) & 1)
                keyboardState.noteOn (1, n, 0.8f);
            else
                keyboardState.noteOff (1, n, 0.0f);
        }
    }
    syncingKeyboard = false;
}

//==============================================================================
void MeronTronIIIEditor::handleNoteOn (juce::MidiKeyboardState*, int, int midiNote, float velocity)
{
    if (! syncingKeyboard)
        processor.noteOnFromGui (midiNote, velocity > 0.0f ? velocity : 0.85f);
}

void MeronTronIIIEditor::handleNoteOff (juce::MidiKeyboardState*, int, int midiNote, float)
{
    if (! syncingKeyboard)
        processor.noteOffFromGui (midiNote);
}

//==============================================================================
namespace
{
    int noteForKeyChar (juce::juce_wchar c)
    {
        switch (c)
        {
            case 'a': return 55; case 'w': return 56; case 's': return 57; case 'e': return 58;
            case 'd': return 59; case 'f': return 60; case 't': return 61; case 'g': return 62;
            case 'y': return 63; case 'h': return 64; case 'j': return 65; case 'u': return 66;
            case 'k': return 67; case 'o': return 68; case 'l': return 69; case 'p': return 70;
            case ';': return 71; case '\'': return 72;
            default: return -1;
        }
    }
}

bool MeronTronIIIEditor::keyPressed (const juce::KeyPress& key)
{
    const int note = noteForKeyChar (key.getTextCharacter());
    if (note < 0)
        return false;
    // Holding a key down makes the OS auto-repeat keyPressed events; only
    // the first press is a real note-on. A re-press after key-up still
    // retriggers, as a tape instrument should.
    if (! keyboardState.isNoteOnForChannels (1, note))
        keyboardState.noteOn (1, note, 0.85f);
    return true;
}

bool MeronTronIIIEditor::keyStateChanged (bool)
{
    // Release any mapped key that is no longer physically down.
    for (juce::juce_wchar c : { 'a', 'w', 's', 'e', 'd', 'f', 't', 'g', 'y', 'h', 'j', 'u', 'k', 'o', 'l', 'p', ';', '\'' })
    {
        const int note = noteForKeyChar (c);
        if (note >= 0 && keyboardState.isNoteOn (1, note)
            && ! juce::KeyPress::isKeyCurrentlyDown ((int) c))
            keyboardState.noteOff (1, note, 0.0f);
    }
    return false;
}
