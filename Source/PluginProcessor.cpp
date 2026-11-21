#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <vector>

const juce::StringArray& VoxDoseProcessor::keyNames()
{
    static const juce::StringArray n { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n;
}

const juce::StringArray& VoxDoseProcessor::scaleNames()
{
    static const juce::StringArray n { "Chromatic", "Major", "Minor", "Harmonic Minor",
                                       "Pentatonic Major", "Pentatonic Minor", "Dorian", "Blues" };
    return n;
}

namespace
{
const std::vector<std::vector<int>>& scaleIntervals()
{
    static const std::vector<std::vector<int>> s = {
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 },
        { 0, 2, 3, 5, 7, 8, 11 }, { 0, 2, 4, 7, 9 }, { 0, 3, 5, 7, 10 }, { 0, 2, 3, 5, 7, 9, 10 },
        { 0, 3, 5, 6, 7, 10 } };
    return s;
}

struct Preset
{
    const char* name;
    float dose, tone, deess, gate, tune, speed, width, delay, delayTime, reverb, mix, output;
};
const Preset presets[] = {
    //  name              dose   tone   deess  gate  tune  speed width delay  time  reverb mix  out
    { "Default",         0.50f,  0.00f, 0.40f, 0.0f, 0.0f, 20.f, 0.00f, 0.00f, 280.f, 0.00f, 1.0f,  0.0f },
    { "Clean Vocal",     0.40f,  0.10f, 0.40f, 0.1f, 0.0f, 20.f, 0.10f, 0.00f, 280.f, 0.10f, 1.0f,  0.0f },
    { "Rap Punch",       0.70f,  0.15f, 0.45f, 0.2f, 0.0f, 20.f, 0.15f, 0.15f, 200.f, 0.12f, 1.0f, -1.0f },
    { "Airy Pop",        0.55f,  0.60f, 0.55f, 0.1f, 0.6f, 40.f, 0.40f, 0.20f, 300.f, 0.30f, 1.0f, -1.0f },
    { "Dark Plug",       0.65f, -0.50f, 0.35f, 0.2f, 0.0f, 20.f, 0.30f, 0.30f, 320.f, 0.40f, 1.0f, -1.0f },
    { "Hard Tune Plug",  0.60f,  0.10f, 0.45f, 0.15f, 1.0f, 0.f, 0.35f, 0.30f, 300.f, 0.30f, 1.0f, -1.0f },
    { "Spacey Main",     0.55f,  0.20f, 0.50f, 0.1f, 0.5f, 30.f, 0.60f, 0.40f, 330.f, 0.50f, 1.0f, -1.5f },
    { "Max Dose",        1.00f,  0.20f, 0.60f, 0.2f, 0.0f, 20.f, 0.20f, 0.10f, 250.f, 0.20f, 1.0f, -3.0f }
};
constexpr int numPresets = (int) (sizeof (presets) / sizeof (presets[0]));
}

VoxDoseProcessor::VoxDoseProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    doseP = apvts.getRawParameterValue ("dose");     toneP = apvts.getRawParameterValue ("tone");
    deessP = apvts.getRawParameterValue ("deess");   gateP = apvts.getRawParameterValue ("gate");
    tuneP = apvts.getRawParameterValue ("tune");     speedP = apvts.getRawParameterValue ("speed");
    keyP = apvts.getRawParameterValue ("key");       scaleP = apvts.getRawParameterValue ("scale");
    widthP = apvts.getRawParameterValue ("width");   delayP = apvts.getRawParameterValue ("delay");
    delayTimeP = apvts.getRawParameterValue ("delaytime");
    reverbP = apvts.getRawParameterValue ("reverb");
    mixP = apvts.getRawParameterValue ("mix");       outputP = apvts.getRawParameterValue ("output");
}

juce::AudioProcessorValueTreeState::ParameterLayout VoxDoseProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto addF = [&layout] (const char* id, const char* name, juce::NormalisableRange<float> r, float def)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def));
    };
    addF ("dose",  "Dose",   { 0.0f, 1.0f }, 0.5f);
    addF ("tone",  "Tone",   { -1.0f, 1.0f }, 0.0f);
    addF ("deess", "De-ess", { 0.0f, 1.0f }, 0.4f);
    addF ("gate",  "Gate",   { 0.0f, 1.0f }, 0.0f);
    addF ("tune",  "Tune",   { 0.0f, 1.0f }, 0.0f);
    addF ("speed", "Retune Speed", { 0.0f, 200.0f, 0.1f, 0.5f }, 20.0f);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "key", 1 }, "Key", keyNames(), 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "scale", 1 }, "Scale", scaleNames(), 0));
    addF ("width",     "Width",     { 0.0f, 1.0f }, 0.0f);
    addF ("delay",     "Delay",     { 0.0f, 1.0f }, 0.0f);
    addF ("delaytime", "Echo Time", { 60.0f, 800.0f, 1.0f }, 280.0f);
    addF ("reverb",    "Reverb",    { 0.0f, 1.0f }, 0.0f);
    addF ("mix",       "Dry/Wet",   { 0.0f, 1.0f }, 1.0f);
    addF ("output",    "Output",    { -12.0f, 12.0f, 0.1f }, 0.0f);
    return layout;
}

void VoxDoseProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    engine.prepare (sr, samplesPerBlock);
    corrector.prepare (sr);
    setLatencySamples (corrector.getLatency());
    dryBuf.setSize (2, samplesPerBlock);
    gateEnv = 0.0f;
    gateGain = 1.0f;
}

bool VoxDoseProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo();
}

void VoxDoseProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2) return;
    if (dryBuf.getNumSamples() < n) dryBuf.setSize (2, n, false, false, true);

    const float gate    = gateP->load();
    const float tuneAmt = tuneP->load();

    PitchCorrector::Settings st;
    st.retuneMs  = speedP->load();
    st.amount    = tuneAmt;
    st.stabilize = 0.5f;
    st.refA      = 440.0f;
    const int key   = juce::jlimit (0, 11, (int) std::lround (keyP->load()));
    const int scale = juce::jlimit (0, (int) scaleIntervals().size() - 1, (int) std::lround (scaleP->load()));
    for (int pc = 0; pc < 12; ++pc)
    {
        const int rel = ((pc - key) % 12 + 12) % 12;
        bool on = false;
        for (int v : scaleIntervals()[(size_t) scale]) if (v == rel) on = true;
        st.notes[pc] = on;
    }

    const float sr = (float) sampleRate;
    const float atkEnv = 1.0f - std::exp (-1.0f / (0.001f * sr));
    const float relEnv = 1.0f - std::exp (-1.0f / (0.060f * sr));
    const float gOpen  = 1.0f - std::exp (-1.0f / (0.004f * sr));
    const float gClose = 1.0f - std::exp (-1.0f / (0.080f * sr));
    const float thrGain = juce::Decibels::decibelsToGain (-70.0f + 35.0f * gate);
    const bool tuneOn = tuneAmt > 0.001f;

    float* l  = buffer.getWritePointer (0);
    float* r  = buffer.getWritePointer (1);
    float* dl = dryBuf.getWritePointer (0);
    float* dr = dryBuf.getWritePointer (1);

    for (int i = 0; i < n; ++i)
    {
        float xl = l[i], xr = r[i];

        if (gate > 0.001f)   // noise gate (before the tuner so it never tracks hiss)
        {
            const float lvl = juce::jmax (std::abs (xl), std::abs (xr));
            gateEnv += (lvl > gateEnv ? atkEnv : relEnv) * (lvl - gateEnv);
            const float target = gateEnv > thrGain ? 1.0f : 0.06f;
            gateGain += (target > gateGain ? gOpen : gClose) * (target - gateGain);
            xl *= gateGain; xr *= gateGain;
        }

        float wetL, wetR, dryL, dryR;
        corrector.processSample (xl, xr, wetL, wetR, dryL, dryR, st);   // fixed latency, always running
        l[i] = tuneOn ? wetL : dryL;
        r[i] = tuneOn ? wetR : dryR;
        dl[i] = dryL;
        dr[i] = dryR;
    }

    engine.process (buffer, dryBuf, doseP->load(), toneP->load(), deessP->load(), widthP->load(),
                    delayP->load(), delayTimeP->load(), reverbP->load(), mixP->load(), outputP->load());
}

int VoxDoseProcessor::getNumPrograms() { return numPresets; }

const juce::String VoxDoseProcessor::getProgramName (int index)
{
    return (index >= 0 && index < numPresets) ? juce::String (presets[index].name) : juce::String();
}

void VoxDoseProcessor::setReal (const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void VoxDoseProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= numPresets) return;
    currentProgram = index;
    const auto& p = presets[index];
    setReal ("dose", p.dose);     setReal ("tone", p.tone);     setReal ("deess", p.deess);
    setReal ("gate", p.gate);     setReal ("tune", p.tune);     setReal ("speed", p.speed);
    setReal ("width", p.width);   setReal ("delay", p.delay);   setReal ("delaytime", p.delayTime);
    setReal ("reverb", p.reverb); setReal ("mix", p.mix);       setReal ("output", p.output);
}

void VoxDoseProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void VoxDoseProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* VoxDoseProcessor::createEditor()
{
    return new VoxDoseEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VoxDoseProcessor();
}
