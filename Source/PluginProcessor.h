#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include "VoxDoseEngine.h"
#include "PitchCorrector.h"

class VoxDoseProcessor : public juce::AudioProcessor
{
public:
    VoxDoseProcessor();
    ~VoxDoseProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "VoxDose"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static const juce::StringArray& keyNames();
    static const juce::StringArray& scaleNames();

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setReal (const juce::String& id, float value);

    VoxDoseEngine engine;
    PitchCorrector corrector;
    juce::AudioBuffer<float> dryBuf;
    double sampleRate = 44100.0;
    float gateEnv = 0.0f, gateGain = 1.0f;

    std::atomic<float>* doseP = nullptr;   std::atomic<float>* toneP = nullptr;
    std::atomic<float>* deessP = nullptr;  std::atomic<float>* gateP = nullptr;
    std::atomic<float>* tuneP = nullptr;   std::atomic<float>* speedP = nullptr;
    std::atomic<float>* keyP = nullptr;    std::atomic<float>* scaleP = nullptr;
    std::atomic<float>* widthP = nullptr;  std::atomic<float>* delayP = nullptr;
    std::atomic<float>* delayTimeP = nullptr; std::atomic<float>* reverbP = nullptr;
    std::atomic<float>* mixP = nullptr;    std::atomic<float>* outputP = nullptr;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxDoseProcessor)
};
