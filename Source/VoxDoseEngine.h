#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>

// ------------------------------------------------------------------
//  All-in-one vocal chain (after gate + tuner, which live in the processor):
//  high-pass -> compressor -> mud cut / presence / air EQ -> soft saturation
//  -> de-esser -> doubler/width -> ping-pong delay -> reverb -> dry/wet + output.
//  The DOSE amount drives compression depth, EQ lift and saturation together.
// ------------------------------------------------------------------
class VoxDoseEngine
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlock, 2 };
        hpf.prepare (spec); mud.prepare (spec); presence.prepare (spec); air.prepare (spec);
        comp.prepare (spec);
        doubler.prepare (spec);
        reverb.prepare (spec);
        delayLine.setMaximumDelayInSamples ((int) sampleRate + 1);
        delayLine.prepare (spec);
        *hpf.state = *Coeffs::makeHighPass (sr, 80.0f, 0.707f);
        for (auto& f : band)
        {
            f.prepare (spec);
            f.coefficients = Coeffs::makeBandPass (sr, 7000.0f, 1.2f);
        }
        deessAtk = 1.0f - std::exp (-1.0f / (0.001f * (float) sr));
        deessRel = 1.0f - std::exp (-1.0f / (0.050f * (float) sr));
        reset();
    }

    void reset()
    {
        hpf.reset(); mud.reset(); presence.reset(); air.reset(); comp.reset();
        doubler.reset(); reverb.reset(); delayLine.reset();
        for (auto& f : band) f.reset();
        env = 0.0f;
        lpD[0] = lpD[1] = 0.0f;
        lastDose = -1.0f; lastTone = -2.0f;
    }

    // 'dryRef' is the (latency-aligned) untouched input used for the dry/wet blend.
    void process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>& dryRef,
                  float dose, float tone, float deess, float width,
                  float delayAmt, float delayMs, float reverbAmt, float mix, float outDb)
    {
        const int n = buffer.getNumSamples();
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);

        // 1. clean low end
        hpf.process (ctx);

        // 2. compression - deeper with more dose
        const float thr   = -12.0f - 18.0f * dose;
        const float ratio = 2.0f + 4.0f * dose;
        comp.setThreshold (thr);
        comp.setRatio (ratio);
        comp.setAttack (6.0f);
        comp.setRelease (120.0f);
        comp.process (ctx);
        buffer.applyGain (juce::Decibels::decibelsToGain (-thr * (1.0f - 1.0f / ratio) * 0.45f));

        // 3. EQ: cut mud, add presence and air (tone: -1 dark .. +1 bright)
        if (dose != lastDose || tone != lastTone)
        {
            lastDose = dose; lastTone = tone;
            *mud.state      = *Coeffs::makePeakFilter (sr, 300.0f, 1.0f,
                                  juce::Decibels::decibelsToGain (-2.5f * dose));
            *presence.state = *Coeffs::makePeakFilter (sr, 3200.0f, 0.9f,
                                  juce::Decibels::decibelsToGain (3.5f * dose));
            *air.state      = *Coeffs::makeHighShelf (sr, 10000.0f, 0.7f,
                                  juce::Decibels::decibelsToGain (6.0f * tone + 2.0f * dose));
        }
        mud.process (ctx); presence.process (ctx); air.process (ctx);

        // 4. soft saturation, level-matched
        const float drive = 1.0f + 3.0f * dose;
        const float satMix = 0.5f * dose;
        const float norm = 1.0f / std::tanh (drive);
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                const float y = std::tanh (d[i] * drive) * norm;
                d[i] = d[i] * (1.0f - satMix) + y * satMix;
            }
        }

        // 5. de-esser (reduces only the 5-9 kHz band when it gets loud)
        if (deess > 0.001f && numCh > 1)
        {
            const float thrGain = juce::Decibels::decibelsToGain (-20.0f - 20.0f * deess);
            float* l = buffer.getWritePointer (0);
            float* r = buffer.getWritePointer (1);
            for (int i = 0; i < n; ++i)
            {
                const float bl = band[0].processSample (l[i]);
                const float br = band[1].processSample (r[i]);
                const float lvl = juce::jmax (std::abs (bl), std::abs (br));
                env += (lvl > env ? deessAtk : deessRel) * (lvl - env);
                const float g = env > thrGain ? std::pow (thrGain / env, 0.7f) : 1.0f;
                l[i] -= bl * (1.0f - g);
                r[i] -= br * (1.0f - g);
            }
        }

        // 6. doubler / stereo width
        if (width > 0.01f)
        {
            doubler.setRate (0.35f);
            doubler.setDepth (0.35f);
            doubler.setCentreDelay (16.0f);
            doubler.setFeedback (0.0f);
            doubler.setMix (0.45f * width);
            doubler.process (ctx);
        }

        // 7. ping-pong delay
        if (delayAmt > 0.01f && numCh > 1)
        {
            delayLine.setDelay (juce::jlimit (1.0f, (float) sr, delayMs * 0.001f * (float) sr));
            const float level = 0.55f * delayAmt;
            const float fb = 0.38f;
            const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 4500.0f / (float) sr);
            float* l = buffer.getWritePointer (0);
            float* r = buffer.getWritePointer (1);
            for (int i = 0; i < n; ++i)
            {
                const float mono = 0.5f * (l[i] + r[i]);
                const float eL = delayLine.popSample (0);
                const float eR = delayLine.popSample (1);
                lpD[0] += a * ((mono + eR * fb) - lpD[0]);
                lpD[1] += a * ((eL * fb) - lpD[1]);
                delayLine.pushSample (0, lpD[0]);
                delayLine.pushSample (1, lpD[1]);
                l[i] += eL * level;
                r[i] += eR * level;
            }
        }

        // 8. reverb
        if (reverbAmt > 0.01f)
        {
            juce::dsp::Reverb::Parameters rp;
            rp.roomSize   = 0.6f;
            rp.damping    = 0.55f;
            rp.wetLevel   = 0.22f * reverbAmt;
            rp.dryLevel   = 1.0f;
            rp.width      = 1.0f;
            rp.freezeMode = 0.0f;
            reverb.setParameters (rp);
            reverb.process (ctx);
        }

        // 9. dry/wet and output
        const float outGain = juce::Decibels::decibelsToGain (outDb);
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* w = buffer.getWritePointer (ch);
            auto* d = dryRef.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
                w[i] = (d[i] * (1.0f - mix) + w[i] * mix) * outGain;
        }
    }

private:
    using Coeffs = juce::dsp::IIR::Coefficients<float>;
    using IIRDup = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, Coeffs>;

    double sr = 44100.0;
    IIRDup hpf, mud, presence, air;
    juce::dsp::Compressor<float> comp;
    juce::dsp::IIR::Filter<float> band[2];
    juce::dsp::Chorus<float> doubler;
    juce::dsp::Reverb reverb;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 48000 };
    float lpD[2] { 0.0f, 0.0f };
    float env = 0.0f, deessAtk = 0.1f, deessRel = 0.001f;
    float lastDose = -1.0f, lastTone = -2.0f;
};
