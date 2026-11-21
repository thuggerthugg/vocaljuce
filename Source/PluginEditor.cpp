#include "PluginEditor.h"

namespace
{
juce::String pct (double v)    { return juce::String ((int) std::round (v * 100.0)) + " %"; }
juce::String pctOff (double v) { return v < 0.01 ? juce::String ("Off") : pct (v); }
}

VoxDoseEditor::VoxDoseEditor (VoxDoseProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    laf.setColourScheme (juce::LookAndFeel_V4::getMidnightColourScheme());
    laf.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffb86bff));
    laf.setColour (juce::Slider::thumbColourId, juce::Colour (0xfff1e4ff));
    setLookAndFeel (&laf);
    setSize (920, 580);

    title.setText ("VOXDOSE  ALL-IN-ONE", juce::dontSendNotification);
    title.setFont (juce::Font (28.0f, juce::Font::bold));
    addAndMakeVisible (title);

    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    // big DOSE knob
    doseKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    doseKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 110, 26);
    doseKnob.setTextBoxIsEditable (false);
    addAndMakeVisible (doseKnob);
    doseAtt = std::make_unique<SliderAttachment> (proc.apvts, "dose", doseKnob);
    doseKnob.textFromValueFunction = pct;
    doseKnob.updateText();
    doseLabel.setText ("DOSE", juce::dontSendNotification);
    doseLabel.setFont (juce::Font (22.0f, juce::Font::bold));
    doseLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (doseLabel);

    // small knobs
    const char* ids[numKnobs]    = { "gate", "tone", "deess", "width", "tune", "speed",
                                     "reverb", "delay", "delaytime", "mix", "output" };
    const char* labels[numKnobs] = { "Gate", "Tone", "De-ess", "Width", "Tune", "Retune speed",
                                     "Reverb", "Delay", "Echo time", "Dry/Wet", "Output" };
    for (size_t i = 0; i < (size_t) numKnobs; ++i)
    {
        knobs[i].setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knobs[i].setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 20);
        knobs[i].setTextBoxIsEditable (false);
        addAndMakeVisible (knobs[i]);
        knobLabels[i].setText (labels[i], juce::dontSendNotification);
        knobLabels[i].setJustificationType (juce::Justification::centred);
        addAndMakeVisible (knobLabels[i]);
        knobAtt[i] = std::make_unique<SliderAttachment> (proc.apvts, ids[i], knobs[i]);
    }
    // text functions must be set AFTER the attachments (they install their own)
    knobs[0].textFromValueFunction  = pctOff;
    knobs[1].textFromValueFunction  = [] (double v)
    { return v < -0.05 ? juce::String ("Darker") : v > 0.05 ? juce::String ("Brighter") : juce::String ("Neutral"); };
    knobs[2].textFromValueFunction  = pct;
    knobs[3].textFromValueFunction  = pctOff;
    knobs[4].textFromValueFunction  = pctOff;
    knobs[5].textFromValueFunction  = [] (double v)
    { return v < 1.0 ? juce::String ("Instant") : juce::String ((int) std::round (v)) + " ms"; };
    knobs[6].textFromValueFunction  = pctOff;
    knobs[7].textFromValueFunction  = pctOff;
    knobs[8].textFromValueFunction  = [] (double v) { return juce::String ((int) std::round (v)) + " ms"; };
    knobs[9].textFromValueFunction  = pct;
    knobs[10].textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
    for (auto& k : knobs) k.updateText();

    // key + scale for the tuner
    keyLabel.setText ("Key", juce::dontSendNotification);
    scaleLabel.setText ("Scale", juce::dontSendNotification);
    addAndMakeVisible (keyLabel);
    addAndMakeVisible (scaleLabel);
    keyBox.addItemList (VoxDoseProcessor::keyNames(), 1);
    scaleBox.addItemList (VoxDoseProcessor::scaleNames(), 1);
    addAndMakeVisible (keyBox);
    addAndMakeVisible (scaleBox);
    keyAtt   = std::make_unique<ComboAttachment> (proc.apvts, "key", keyBox);
    scaleAtt = std::make_unique<ComboAttachment> (proc.apvts, "scale", scaleBox);
}

VoxDoseEditor::~VoxDoseEditor()
{
    setLookAndFeel (nullptr);
}

void VoxDoseEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a0f33), 0.0f, 0.0f,
                                             juce::Colour (0xff08050f), 0.0f, (float) getHeight(), false));
    g.fillAll();
    for (auto r : { leftPanel, rightPanel })
    {
        g.setColour (juce::Colour (0xff22163f));
        g.fillRoundedRectangle (r.toFloat(), 12.0f);
        g.setColour (juce::Colour (0xff3b2a66));
        g.drawRoundedRectangle (r.toFloat(), 12.0f, 1.0f);
    }
}

void VoxDoseEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto top = area.removeFromTop (46);
    title.setBounds (top.removeFromLeft (420));
    presetBox.setBounds (top.removeFromRight (220).reduced (0, 8));
    area.removeFromTop (8);

    leftPanel = area.removeFromLeft (300);
    area.removeFromLeft (10);
    rightPanel = area;

    auto lp = leftPanel.reduced (14);
    doseLabel.setBounds (lp.removeFromTop (34));
    doseKnob.setBounds (lp.withSizeKeepingCentre (250, 300));

    auto rp = rightPanel.reduced (10);
    const int cols = 4, rows = 3;
    const int cw = rp.getWidth() / cols, ch = rp.getHeight() / rows;
    for (int idx = 0; idx < numKnobs; ++idx)
    {
        juce::Rectangle<int> cell (rp.getX() + (idx % cols) * cw, rp.getY() + (idx / cols) * ch, cw, ch);
        cell = cell.reduced (6, 4);
        knobLabels[(size_t) idx].setBounds (cell.removeFromTop (22));
        knobs[(size_t) idx].setBounds (cell);
    }
    // last cell: key + scale menus
    juce::Rectangle<int> last (rp.getX() + 3 * cw, rp.getY() + 2 * ch, cw, ch);
    last = last.reduced (10, 14);
    keyLabel.setBounds (last.removeFromTop (20));
    keyBox.setBounds (last.removeFromTop (30));
    last.removeFromTop (8);
    scaleLabel.setBounds (last.removeFromTop (20));
    scaleBox.setBounds (last.removeFromTop (30));
}
