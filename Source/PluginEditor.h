#pragma once
#include "PluginProcessor.h"
#include <array>
#include <memory>

class VoxDoseEditor : public juce::AudioProcessorEditor
{
public:
    explicit VoxDoseEditor (VoxDoseProcessor&);
    ~VoxDoseEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    static constexpr int numKnobs = 11;

    VoxDoseProcessor& proc;
    juce::LookAndFeel_V4 laf;

    juce::Label title, doseLabel, keyLabel, scaleLabel;
    juce::ComboBox presetBox, keyBox, scaleBox;
    std::unique_ptr<ComboAttachment> keyAtt, scaleAtt;
    juce::Slider doseKnob;
    std::unique_ptr<SliderAttachment> doseAtt;

    std::array<juce::Slider, numKnobs> knobs;
    std::array<juce::Label, numKnobs> knobLabels;
    std::array<std::unique_ptr<SliderAttachment>, numKnobs> knobAtt;

    juce::Rectangle<int> leftPanel, rightPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxDoseEditor)
};
