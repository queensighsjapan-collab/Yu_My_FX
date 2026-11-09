#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const juce::Colour panelColour   { 0xff20202a };
    const juce::Colour headerColour  { 0xffffb84d };   // YM accent amber
    const juce::Colour textColour    { 0xffe8e8ee };
    const juce::Colour captionColour { 0xff9a9aa8 };
    const juce::Colour knobColour    { 0xff3a3a48 };
    const juce::Colour padBgColour   { 0xff17171f };
    const juce::Colour padLineColour { 0xff3d3d4d };
}

//==============================================================================
PositionPad::PositionPad (YMOrbitProcessor& proc,
                          juce::RangedAudioParameter& azimuth,
                          juce::RangedAudioParameter& distance)
    : processor (proc), azimuthParam (azimuth), distanceParam (distance)
{
    motionModeParam = processor.apvts.getParameter ("motionMode");
    startTimerHz (30);
}

juce::Point<float> PositionPad::pointFor (float azimuthDeg, float distance01) const
{
    const auto centre = getCentre();
    const float azRad = juce::degreesToRadians (azimuthDeg);
    const float r = distance01 * getMaxRadius();

    return { centre.x + std::sin (azRad) * r, centre.y - std::cos (azRad) * r };
}

float PositionPad::getAzimuthDeg() const
{
    return azimuthParam.convertFrom0to1 (azimuthParam.getValue());
}

float PositionPad::getDistance01() const
{
    return distanceParam.convertFrom0to1 (distanceParam.getValue());
}

juce::Point<float> PositionPad::getCentre() const
{
    return getLocalBounds().toFloat().getCentre();
}

float PositionPad::getMaxRadius() const
{
    return (float) juce::jmin (getWidth(), getHeight()) * 0.44f;
}

void PositionPad::timerCallback()
{
    const float az = getAzimuthDeg();
    const float di = getDistance01();
    const float eAz = processor.getEffectiveAzimuthDeg();
    const float eDi = processor.getEffectiveDistance01();
    const int mode = motionModeParam != nullptr
                         ? (int) motionModeParam->convertFrom0to1 (motionModeParam->getValue())
                         : 0;

    if (std::abs (az - lastAzimuth) > 0.01f || std::abs (di - lastDistance) > 0.0005f
         || std::abs (eAz - lastEffAzimuth) > 0.05f
         || std::abs (eDi - lastEffDistance) > 0.002f
         || mode != lastMotionMode)
    {
        lastAzimuth = az;
        lastDistance = di;
        lastEffAzimuth = eAz;
        lastEffDistance = eDi;
        lastMotionMode = mode;
        repaint();
    }
}

void PositionPad::setFromPoint (juce::Point<float> p)
{
    const auto centre = getCentre();
    const float dx = p.x - centre.x;
    const float dy = p.y - centre.y;
    const float maxR = juce::jmax (1.0f, getMaxRadius());

    const float radius = juce::jlimit (0.0f, 1.0f, std::sqrt (dx * dx + dy * dy) / maxR);

    // Screen up is front (0 deg); right is +90 deg: theta = atan2 (dx, -dy).
    const float deg = juce::radiansToDegrees (std::atan2 (dx, -dy));

    azimuthParam.setValueNotifyingHost (azimuthParam.convertTo0to1 (deg));
    distanceParam.setValueNotifyingHost (distanceParam.convertTo0to1 (radius));
    repaint();
}

void PositionPad::mouseDown (const juce::MouseEvent& e)
{
    azimuthParam.beginChangeGesture();
    distanceParam.beginChangeGesture();
    setFromPoint (e.position);
}

void PositionPad::mouseDrag (const juce::MouseEvent& e)
{
    setFromPoint (e.position);
}

void PositionPad::mouseUp (const juce::MouseEvent&)
{
    azimuthParam.endChangeGesture();
    distanceParam.endChangeGesture();
}

void PositionPad::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (padBgColour);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (padLineColour);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    const auto centre = getCentre();
    const float maxR = getMaxRadius();

    // Distance rings (1/3, 2/3, full).
    g.setColour (padLineColour.withAlpha (0.7f));

    for (float f : { 1.0f / 3.0f, 2.0f / 3.0f, 1.0f })
        g.drawEllipse (centre.x - maxR * f, centre.y - maxR * f,
                       maxR * f * 2.0f, maxR * f * 2.0f, 1.0f);

    // Axes.
    g.drawLine (centre.x - maxR, centre.y, centre.x + maxR, centre.y, 1.0f);
    g.drawLine (centre.x, centre.y - maxR, centre.x, centre.y + maxR, 1.0f);

    // Orientation markers.
    g.setColour (captionColour);
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText ("FRONT", juce::Rectangle<float> (centre.x - 30.0f, 4.0f, 60.0f, 14.0f),
                juce::Justification::centred, false);
    g.drawText ("L", juce::Rectangle<float> (6.0f, centre.y - 7.0f, 14.0f, 14.0f),
                juce::Justification::centred, false);
    g.drawText ("R", juce::Rectangle<float> ((float) getWidth() - 20.0f, centre.y - 7.0f, 14.0f, 14.0f),
                juce::Justification::centred, false);

    // Base position (what the user set) and, while MOTION runs, the
    // current effective position from the processor's atomics.
    const bool motionOn = motionModeParam != nullptr
        && (int) motionModeParam->convertFrom0to1 (motionModeParam->getValue()) != 0;

    const auto baseDot = pointFor (getAzimuthDeg(), getDistance01());
    const auto effDot  = pointFor (processor.getEffectiveAzimuthDeg(),
                                   processor.getEffectiveDistance01());

    // Line from the head to the position that matters right now.
    const auto& activeDot = motionOn ? effDot : baseDot;
    g.setColour (headerColour.withAlpha (0.35f));
    g.drawLine (centre.x, centre.y, activeDot.x, activeDot.y, 1.5f);

    // The listener's head: circle plus a nose tick pointing to the front.
    const float headR = 13.0f;
    g.setColour (knobColour);
    g.fillEllipse (centre.x - headR, centre.y - headR, headR * 2.0f, headR * 2.0f);
    g.setColour (textColour);
    g.drawEllipse (centre.x - headR, centre.y - headR, headR * 2.0f, headR * 2.0f, 1.5f);
    g.drawLine (centre.x, centre.y - headR, centre.x, centre.y - headR - 6.0f, 2.0f);

    auto drawDot = [&g] (juce::Point<float> dot, float alpha, bool ring)
    {
        g.setColour (headerColour.withAlpha (0.30f * alpha));
        g.fillEllipse (dot.x - 11.0f, dot.y - 11.0f, 22.0f, 22.0f);
        g.setColour (headerColour.withAlpha (alpha));
        g.fillEllipse (dot.x - 6.0f, dot.y - 6.0f, 12.0f, 12.0f);
        g.setColour (panelColour.withAlpha (alpha));
        g.fillEllipse (dot.x - 2.0f, dot.y - 2.0f, 4.0f, 4.0f);

        if (ring)
        {
            g.setColour (headerColour.withAlpha (0.8f));
            g.drawEllipse (dot.x - 14.0f, dot.y - 14.0f, 28.0f, 28.0f, 1.5f);
        }
    };

    if (motionOn)
    {
        drawDot (baseDot, 0.35f, false);   // where the user parked it (dim)
        drawDot (effDot, 1.0f, true);      // where it actually is (bright + ring)
    }
    else
    {
        drawDot (baseDot, 1.0f, false);
    }
}

//==============================================================================
YMOrbitEditor::YMOrbitEditor (YMOrbitProcessor& p)
    : AudioProcessorEditor (p), processorRef (p)
{
    titleLabel.setText ("YM ORBIT", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, headerColour);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    captionLabel.setText ("3D SPATIAL PANNER", juce::dontSendNotification);
    captionLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    captionLabel.setColour (juce::Label::textColourId, captionColour);
    captionLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (captionLabel);

    auto* azimuthParam = processorRef.apvts.getParameter ("azimuth");
    auto* distanceParam = processorRef.apvts.getParameter ("distance");
    pad = std::make_unique<PositionPad> (processorRef, *azimuthParam, *distanceParam);
    addAndMakeVisible (pad.get());

    // Readouts (refreshed by the editor timer).
    for (auto* label : { &azimuthReadout, &elevationReadout })
    {
        label->setFont (juce::Font (juce::FontOptions (13.0f)));
        label->setColour (juce::Label::textColourId, textColour);
        label->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (label);
    }

    hintLabel.setText ("Drag the dot to move the source.\n"
                       "Up on the pad = in front of you.\n"
                       "MOTION swirls it on its own;\n"
                       "the ringed dot is where it is now.\n"
                       "Best judged on headphones.",
                       juce::dontSendNotification);
    hintLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    hintLabel.setColour (juce::Label::textColourId, captionColour);
    hintLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (hintLabel);

    // Elevation: vertical slider with a degree readout in its text box.
    elevationLabel.setText ("ELEVATION", juce::dontSendNotification);
    elevationLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    elevationLabel.setColour (juce::Label::textColourId, textColour);
    elevationLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (elevationLabel);

    elevationSlider.setSliderStyle (juce::Slider::LinearVertical);
    elevationSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
    elevationSlider.setColour (juce::Slider::trackColourId, knobColour);
    elevationSlider.setColour (juce::Slider::thumbColourId, headerColour);
    elevationSlider.setColour (juce::Slider::textBoxTextColourId, textColour);
    elevationSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    elevationSlider.textFromValueFunction = [] (double v)
    {
        return juce::String ((int) std::lround (v)) + juce::String::fromUTF8 ("°");
    };
    elevationSlider.valueFromTextFunction = [] (const juce::String& t)
    {
        return (double) t.getIntValue();
    };
    addAndMakeVisible (elevationSlider);

    setupKnob (azimuthSlider, azimuthLabel, "AZIMUTH");
    setupKnob (distanceSlider, distanceLabel, "DISTANCE");
    setupKnob (roomSlider, roomLabel, "ROOM");
    setupKnob (widthSlider, widthLabel, "WIDTH");
    setupKnob (outputSlider, outputLabel, "OUTPUT");

    azimuthSlider.textFromValueFunction = [] (double v)
    {
        return juce::String ((int) std::lround (v)) + juce::String::fromUTF8 ("°");
    };
    azimuthSlider.valueFromTextFunction = [] (const juce::String& t)
    {
        return (double) t.getIntValue();
    };

    // The distance knob reads in metres, not the raw 0..1 parameter.
    distanceSlider.textFromValueFunction = [] (double v)
    {
        return juce::String (YMOrbitProcessor::distanceToMetres ((float) v), 1) + " m";
    };
    distanceSlider.valueFromTextFunction = [] (const juce::String& t)
    {
        return (double) juce::jlimit (0.0f, 1.0f,
                                      (t.getFloatValue() - 0.3f) / 7.7f);
    };

    azimuthAttachment   = std::make_unique<SliderAttachment> (processorRef.apvts, "azimuth",   azimuthSlider);
    distanceAttachment  = std::make_unique<SliderAttachment> (processorRef.apvts, "distance",  distanceSlider);
    elevationAttachment = std::make_unique<SliderAttachment> (processorRef.apvts, "elevation", elevationSlider);
    roomAttachment      = std::make_unique<SliderAttachment> (processorRef.apvts, "room",      roomSlider);
    widthAttachment     = std::make_unique<SliderAttachment> (processorRef.apvts, "width",     widthSlider);
    outputAttachment    = std::make_unique<SliderAttachment> (processorRef.apvts, "output",    outputSlider);

    //--- MOTION strip ---------------------------------------------------------
    motionLabel.setText ("MOTION", juce::dontSendNotification);
    motionLabel.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    motionLabel.setColour (juce::Label::textColourId, headerColour);
    motionLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (motionLabel);

    if (auto* modeParam = dynamic_cast<juce::AudioParameterChoice*> (
            processorRef.apvts.getParameter ("motionMode")))
        motionModeBox.addItemList (modeParam->choices, 1);

    motionModeBox.setColour (juce::ComboBox::backgroundColourId, knobColour);
    motionModeBox.setColour (juce::ComboBox::textColourId, textColour);
    motionModeBox.setColour (juce::ComboBox::outlineColourId, padLineColour);
    motionModeBox.setColour (juce::ComboBox::arrowColourId, headerColour);
    motionModeBox.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (motionModeBox);

    setupKnob (rateSlider, rateLabel, "RATE");
    setupKnob (depthSlider, depthLabel, "DEPTH");

    rateSlider.textFromValueFunction = [] (double v)
    {
        return juce::String (v, 2) + " Hz";
    };
    rateSlider.valueFromTextFunction = [] (const juce::String& t)
    {
        return (double) t.getFloatValue();
    };

    depthSlider.textFromValueFunction = [] (double v)
    {
        return juce::String ((int) std::lround (v * 100.0)) + " %";
    };
    depthSlider.valueFromTextFunction = [] (const juce::String& t)
    {
        return (double) juce::jlimit (0.0f, 1.0f, t.getFloatValue() / 100.0f);
    };

    motionModeAttachment = std::make_unique<ComboBoxAttachment> (processorRef.apvts, "motionMode", motionModeBox);
    rateAttachment       = std::make_unique<SliderAttachment> (processorRef.apvts, "motionRate",  rateSlider);
    depthAttachment      = std::make_unique<SliderAttachment> (processorRef.apvts, "motionDepth", depthSlider);

    setSize (600, 500);
    startTimerHz (15);
}

YMOrbitEditor::~YMOrbitEditor()
{
    stopTimer();
}

void YMOrbitEditor::timerCallback()
{
    const float az = processorRef.apvts.getParameter ("azimuth")->getValue();
    const float el = processorRef.apvts.getParameter ("elevation")->getValue();
    auto* azParam = processorRef.apvts.getParameter ("azimuth");
    auto* elParam = processorRef.apvts.getParameter ("elevation");

    azimuthReadout.setText ("AZ  " + juce::String ((int) std::lround (
                                azParam->convertFrom0to1 (az))) + juce::String::fromUTF8 ("°"),
                            juce::dontSendNotification);
    elevationReadout.setText ("EL  " + juce::String ((int) std::lround (
                                  elParam->convertFrom0to1 (el))) + juce::String::fromUTF8 ("°"),
                              juce::dontSendNotification);
}

void YMOrbitEditor::setupKnob (juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
    slider.setColour (juce::Slider::rotarySliderFillColourId, headerColour);
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, knobColour);
    slider.setColour (juce::Slider::thumbColourId, textColour);
    slider.setColour (juce::Slider::textBoxTextColourId, textColour);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::Font (juce::FontOptions (13.0f)));
    label.setColour (juce::Label::textColourId, textColour);
    label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (label);
}

//==============================================================================
void YMOrbitEditor::paint (juce::Graphics& g)
{
    g.fillAll (panelColour);

    // Thin accent line under the header, matching the YM panel style.
    g.setColour (headerColour.withAlpha (0.55f));
    g.fillRect (16, 58, getWidth() - 32, 2);
}

void YMOrbitEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto header = area.removeFromTop (44);
    titleLabel.setBounds (header.removeFromLeft (190));
    captionLabel.setBounds (header.withTrimmedTop (16));

    area.removeFromTop (18);

    // MOTION strip along the very bottom: label + mode selector + 2 knobs.
    auto motionRow = area.removeFromBottom (86);
    motionLabel.setBounds (motionRow.removeFromLeft (64).withTrimmedTop (24));
    motionModeBox.setBounds (motionRow.removeFromLeft (130).withTrimmedTop (26).withHeight (26));
    motionRow.removeFromLeft (20);

    auto placeMotionKnob = [] (juce::Rectangle<int> column, juce::Label& label, juce::Slider& slider)
    {
        label.setBounds (column.removeFromTop (16));
        slider.setBounds (column.reduced (14, 0));
    };

    placeMotionKnob (motionRow.removeFromLeft (110), rateLabel, rateSlider);
    placeMotionKnob (motionRow.removeFromLeft (110), depthLabel, depthSlider);

    area.removeFromBottom (10);

    // Bottom row: five rotary knobs.
    auto knobRow = area.removeFromBottom (128);
    const int knobWidth = knobRow.getWidth() / 5;

    auto placeKnob = [] (juce::Rectangle<int> column, juce::Label& label, juce::Slider& slider)
    {
        label.setBounds (column.removeFromTop (20));
        slider.setBounds (column.reduced (6, 0));
    };

    placeKnob (knobRow.removeFromLeft (knobWidth), azimuthLabel, azimuthSlider);
    placeKnob (knobRow.removeFromLeft (knobWidth), distanceLabel, distanceSlider);
    placeKnob (knobRow.removeFromLeft (knobWidth), roomLabel, roomSlider);
    placeKnob (knobRow.removeFromLeft (knobWidth), widthLabel, widthSlider);
    placeKnob (knobRow, outputLabel, outputSlider);

    // Middle row: pad | elevation | readouts.
    auto middle = area;

    const int padSize = juce::jmin (middle.getHeight(), 236);
    pad->setBounds (middle.removeFromLeft (padSize).withHeight (padSize));

    middle.removeFromLeft (14);

    auto elevColumn = middle.removeFromLeft (78);
    elevationLabel.setBounds (elevColumn.removeFromTop (20));
    elevationSlider.setBounds (elevColumn.reduced (7, 0));

    middle.removeFromLeft (14);

    auto info = middle;
    azimuthReadout.setBounds (info.removeFromTop (24));
    elevationReadout.setBounds (info.removeFromTop (24));
    info.removeFromTop (10);
    hintLabel.setBounds (info.removeFromTop (90));
}
