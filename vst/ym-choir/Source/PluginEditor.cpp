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
    const juce::Colour dotColour     { 0xffcfd6e4 };   // pale singer dots
}

//==============================================================================
ChoirCanvas::ChoirCanvas (YMChoirProcessor& p)
    : processor (p)
{
    voicesParam = processor.apvts.getRawParameterValue ("voices");
    spreadParam = processor.apvts.getRawParameterValue ("spread");
    startTimerHz (30);
}

void ChoirCanvas::timerCallback()
{
    ++tick;
    repaint();
}

void ChoirCanvas::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (padBgColour);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (padLineColour);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    const int numVoices = voicesParam != nullptr
        ? juce::jlimit (1, YMChoirProcessor::maxVoices, (int) voicesParam->load())
        : 4;
    const float spread = spreadParam != nullptr ? spreadParam->load() : 0.7f;

    const float centreX = bounds.getCentreX();
    const float centreY = bounds.getCentreY();
    const float maxOffset = bounds.getWidth() * 0.40f * spread;

    for (int i = 0; i < YMChoirProcessor::maxVoices; ++i)
    {
        const float posNorm = YMChoirProcessor::maxVoices <= 1
            ? 0.0f
            : (2.0f * (float) i / (float) (YMChoirProcessor::maxVoices - 1) - 1.0f);
        const bool lit = i < numVoices;

        const float bob = std::sin ((float) tick * 0.11f + (float) i * 1.93f) * 3.0f;
        const float cx = centreX + posNorm * maxOffset;
        const float cy = centreY + (lit ? bob : 0.0f);
        const float r = lit ? 6.0f : 4.0f;

        if (lit)
        {
            g.setColour (dotColour.withAlpha (0.20f));
            g.fillEllipse (cx - r * 2.0f, cy - r * 2.0f, r * 4.0f, r * 4.0f);
            g.setColour (i == 0 || i == numVoices - 1 ? headerColour : dotColour);
            g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
        }
        else
        {
            g.setColour (captionColour.withAlpha (0.45f));
            g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.0f);
        }
    }
}

//==============================================================================
YMChoirEditor::YMChoirEditor (YMChoirProcessor& p)
    : AudioProcessorEditor (p), processorRef (p)
{
    titleLabel.setText ("YM CHOIR", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, headerColour);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    captionLabel.setText ("VOICE MULTIPLIER", juce::dontSendNotification);
    captionLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    captionLabel.setColour (juce::Label::textColourId, captionColour);
    captionLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (captionLabel);

    canvas = std::make_unique<ChoirCanvas> (processorRef);
    addAndMakeVisible (canvas.get());

    hintLabel.setText ("One voice in, a whole choir out: detuned, delayed, vibrato'd copies "
                       "of your voice, layered over the dry signal.",
                       juce::dontSendNotification);
    hintLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    hintLabel.setColour (juce::Label::textColourId, captionColour);
    hintLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (hintLabel);

    setupKnob (voicesSlider, voicesLabel, "VOICES");
    setupKnob (detuneSlider, detuneLabel, "DETUNE");
    setupKnob (timingSlider, timingLabel, "TIMING");
    setupKnob (vibratoSlider, vibratoLabel, "VIBRATO");
    setupKnob (spreadSlider, spreadLabel, "SPREAD");
    setupKnob (toneSlider, toneLabel, "TONE");
    setupKnob (mixSlider, mixLabel, "MIX");
    setupKnob (outputSlider, outputLabel, "OUTPUT");

    voicesSlider.textFromValueFunction = [] (double v)
    {
        return juce::String ((int) std::lround (v));
    };
    voicesSlider.valueFromTextFunction = [] (const juce::String& t)
    {
        return (double) t.getIntValue();
    };

    detuneSlider.textFromValueFunction = [] (double v)
    {
        return juce::String ((int) std::lround (v)) + " ct";
    };
    detuneSlider.valueFromTextFunction = [] (const juce::String& t)
    {
        return (double) t.getFloatValue();
    };

    timingSlider.textFromValueFunction = [] (double v)
    {
        return juce::String ((int) std::lround (v)) + " ms";
    };
    timingSlider.valueFromTextFunction = [] (const juce::String& t)
    {
        return (double) t.getFloatValue();
    };

    auto percentText = [] (double v)
    {
        return juce::String ((int) std::lround (v * 100.0)) + " %";
    };
    auto percentValue = [] (const juce::String& t)
    {
        return (double) juce::jlimit (0.0f, 1.0f, t.getFloatValue() / 100.0f);
    };

    for (auto* slider : { &vibratoSlider, &spreadSlider, &mixSlider })
    {
        slider->textFromValueFunction = percentText;
        slider->valueFromTextFunction = percentValue;
    }

    toneSlider.textFromValueFunction = [] (double v)
    {
        return juce::String::formatted ("%+.1f", v);
    };
    toneSlider.valueFromTextFunction = [] (const juce::String& t)
    {
        return (double) t.getFloatValue();
    };
    toneSlider.setDoubleClickReturnValue (true, 0.0);

    outputSlider.textFromValueFunction = [] (double v)
    {
        return juce::String (v, 1) + " dB";
    };
    outputSlider.valueFromTextFunction = [] (const juce::String& t)
    {
        return (double) t.getFloatValue();
    };

    voicesAttachment  = std::make_unique<SliderAttachment> (processorRef.apvts, "voices",  voicesSlider);
    detuneAttachment  = std::make_unique<SliderAttachment> (processorRef.apvts, "detune",  detuneSlider);
    timingAttachment  = std::make_unique<SliderAttachment> (processorRef.apvts, "timing",  timingSlider);
    vibratoAttachment = std::make_unique<SliderAttachment> (processorRef.apvts, "vibrato", vibratoSlider);
    spreadAttachment  = std::make_unique<SliderAttachment> (processorRef.apvts, "spread",  spreadSlider);
    toneAttachment    = std::make_unique<SliderAttachment> (processorRef.apvts, "tone",    toneSlider);
    mixAttachment     = std::make_unique<SliderAttachment> (processorRef.apvts, "mix",     mixSlider);
    outputAttachment  = std::make_unique<SliderAttachment> (processorRef.apvts, "output",  outputSlider);

    //--- ON / BYPASS ----------------------------------------------------------
    powerButton.setColour (juce::TextButton::buttonColourId, headerColour);
    powerButton.setColour (juce::TextButton::textColourOffId, panelColour);
    powerButton.onClick = [this]
    {
        const bool bypassed = bypassRaw != nullptr && bypassRaw->load() > 0.5f;
        processorRef.setBypassed (! bypassed);
    };
    addAndMakeVisible (powerButton);

    //--- MIDI CC strip ----------------------------------------------------------
    bypassCcRaw = processorRef.apvts.getRawParameterValue ("bypassCc");
    bypassRaw   = processorRef.apvts.getRawParameterValue ("bypass");

    bypassCcLabel.setText ("BYPASS", juce::dontSendNotification);
    bypassCcLabel.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    bypassCcLabel.setColour (juce::Label::textColourId, captionColour);
    bypassCcLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (bypassCcLabel);

    bypassCcReadout.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    bypassCcReadout.setColour (juce::Label::textColourId, textColour);
    bypassCcReadout.setColour (juce::Label::backgroundColourId, padBgColour);
    bypassCcReadout.setColour (juce::Label::outlineColourId, padLineColour);
    bypassCcReadout.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (bypassCcReadout);

    bypassLearnButton.setButtonText ("LEARN");
    bypassLearnButton.setColour (juce::TextButton::buttonColourId, knobColour);
    bypassLearnButton.setColour (juce::TextButton::textColourOffId, textColour);
    bypassLearnButton.onClick = [this]
    {
        if (processorRef.getLearnTarget() == YMChoirProcessor::LearnTarget::bypass)
            processorRef.setLearnTarget (YMChoirProcessor::LearnTarget::none);
        else
            processorRef.setLearnTarget (YMChoirProcessor::LearnTarget::bypass);

        refreshMidiStrip();
    };
    bypassLearnButton.onRightClick = [this]
    {
        processorRef.setBypassCcNumber (0);   // 0 = Off: no CC assigned

        if (processorRef.getLearnTarget() == YMChoirProcessor::LearnTarget::bypass)
            processorRef.setLearnTarget (YMChoirProcessor::LearnTarget::none);

        refreshMidiStrip();
    };
    addAndMakeVisible (bypassLearnButton);

    refreshMidiStrip();
    startTimerHz (10);

    setSize (600, 460);
}

YMChoirEditor::~YMChoirEditor() = default;

void YMChoirEditor::setupKnob (juce::Slider& slider, juce::Label& label, const juce::String& text)
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
juce::String YMChoirEditor::ccReadoutText (std::atomic<float>* rawParam)
{
    if (rawParam == nullptr)
        return "Off";

    const int cc = (int) rawParam->load();
    return cc >= 1 ? "CC " + juce::String (cc) : juce::String ("Off");
}

void YMChoirEditor::refreshMidiStrip()
{
    bypassCcReadout.setText (ccReadoutText (bypassCcRaw), juce::dontSendNotification);

    const bool learnArmed = processorRef.getLearnTarget() == YMChoirProcessor::LearnTarget::bypass;
    bypassLearnButton.setButtonText (learnArmed ? "LEARN..." : "LEARN");
    bypassLearnButton.setColour (juce::TextButton::buttonColourId,
                                 learnArmed ? headerColour : knobColour);
    bypassLearnButton.setColour (juce::TextButton::textColourOffId,
                                 learnArmed ? panelColour : textColour);

    const bool bypassed = bypassRaw != nullptr && bypassRaw->load() > 0.5f;

    if (bypassed != lastBypassShown || powerButton.getButtonText().isEmpty())
    {
        lastBypassShown = bypassed;
        powerButton.setButtonText (bypassed ? "BYPASS" : "ON");
        powerButton.setColour (juce::TextButton::buttonColourId,
                               bypassed ? knobColour : headerColour);
        powerButton.setColour (juce::TextButton::textColourOffId,
                               bypassed ? captionColour : panelColour);
    }
}

void YMChoirEditor::timerCallback()
{
    refreshMidiStrip();
}

//==============================================================================
void YMChoirEditor::paint (juce::Graphics& g)
{
    g.fillAll (panelColour);

    // Thin accent line under the header, matching the YM panel style.
    g.setColour (headerColour.withAlpha (0.55f));
    g.fillRect (16, 58, getWidth() - 32, 2);
}

void YMChoirEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto header = area.removeFromTop (44);
    titleLabel.setBounds (header.removeFromLeft (210));
    captionLabel.setBounds (header.withTrimmedTop (16));

    area.removeFromTop (10);

    canvas->setBounds (area.removeFromTop (52));

    area.removeFromTop (10);

    hintLabel.setBounds (area.removeFromBottom (20));
    area.removeFromBottom (6);

    //--- Bottom strip: ON/BYPASS power button on the left, the bypass
    //    MIDI CC assignment group on the right. -------------------------
    auto strip = area.removeFromBottom (30);
    area.removeFromBottom (8);

    powerButton.setBounds (strip.removeFromLeft (96).withHeight (24)
                               .withY (strip.getY() + 3));

    auto group = strip.removeFromRight (198);
    bypassCcLabel.setBounds (group.removeFromLeft (58));
    group.removeFromLeft (4);
    bypassCcReadout.setBounds (group.removeFromLeft (52).withHeight (24)
                                   .withY (group.getY() + 3));
    group.removeFromLeft (4);
    bypassLearnButton.setBounds (group.removeFromLeft (62).withHeight (24)
                                     .withY (group.getY() + 3));

    // Two knob rows of four cells: VOICES DETUNE TIMING VIBRATO /
    // SPREAD TONE MIX OUTPUT.
    const int cellWidth = area.getWidth() / 4;
    const int rowHeight = area.getHeight() / 2;

    auto placeKnob = [] (juce::Rectangle<int> column, juce::Label& label, juce::Slider& slider)
    {
        label.setBounds (column.removeFromTop (20));
        slider.setBounds (column.reduced (6, 0));
    };

    auto row1 = area.removeFromTop (rowHeight);
    placeKnob (row1.removeFromLeft (cellWidth), voicesLabel, voicesSlider);
    placeKnob (row1.removeFromLeft (cellWidth), detuneLabel, detuneSlider);
    placeKnob (row1.removeFromLeft (cellWidth), timingLabel, timingSlider);
    placeKnob (row1, vibratoLabel, vibratoSlider);

    auto row2 = area;
    placeKnob (row2.removeFromLeft (cellWidth), spreadLabel, spreadSlider);
    placeKnob (row2.removeFromLeft (cellWidth), toneLabel, toneSlider);
    placeKnob (row2.removeFromLeft (cellWidth), mixLabel, mixSlider);
    placeKnob (row2, outputLabel, outputSlider);
}
