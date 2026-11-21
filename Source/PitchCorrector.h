#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>
#include <cmath>
#include <cstdint>

// ------------------------------------------------------------------
//  Real-time monophonic pitch corrector.
//    1. YIN pitch detection on a decimated copy of the input
//    2. Note targeting (scale mask, hysteresis, retune-speed smoothing)
//    3. Time-domain pitch shifting with pitch-synchronous overlap-add (PSOLA-style)
//  Latency is fixed (reported to the host) so that the dry signal can be aligned.
// ------------------------------------------------------------------
class PitchCorrector
{
public:
    struct Settings
    {
        float retuneMs  = 20.0f;    // 0 = instant (hard tune)
        float amount    = 1.0f;     // 0..1 correction strength
        float stabilize = 0.5f;     // 0..1 note hold (hysteresis)
        float refA      = 440.0f;   // reference pitch of A4
        bool  notes[12] { true, true, true, true, true, true, true, true, true, true, true, true };
    };

    PitchCorrector()
    {
        for (int c = 0; c < 2; ++c)
        {
            inBuf[c].assign ((size_t) B, 0.0f);
            outBuf[c].assign ((size_t) B, 0.0f);
        }
    }

    void prepare (double sampleRate)
    {
        sr     = sampleRate;
        dec    = juce::jmax (1, (int) std::lround (sr / 22050.0));
        srDec  = sr / (double) dec;
        tMax   = sr / 80.0;
        tMin   = sr / 1000.0;
        latency = (int) std::ceil (2.5 * tMax) + 8;
        frame.assign ((size_t) yinW, 0.0f);
        decRing.assign ((size_t) yinW, 0.0f);
        diff.assign ((size_t) yinW / 2 + 2, 0.0f);
        cmnd.assign ((size_t) yinW / 2 + 2, 0.0f);
        ratioCoef  = 1.0f - std::exp (-1.0f / (0.003f * (float) sr));
        voicedCoef = 1.0f - std::exp (-1.0f / (0.006f * (float) sr));
        reset();
    }

    int getLatency() const { return latency; }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (inBuf[c].begin(), inBuf[c].end(), 0.0f);
            std::fill (outBuf[c].begin(), outBuf[c].end(), 0.0f);
        }
        std::fill (decRing.begin(), decRing.end(), 0.0f);
        wr = 0; s = 0.0; cPrev = 0.0;
        hopCounter = 0; decAcc = 0.0f; decCount = 0; decPos = 0;
        shift = 0.0; ratio = 1.0f; ratioSm = 1.0f; voicedMix = 0.0f;
        Tcur = sr / 200.0;
        voiced = false; haveTarget = false; unvoicedCount = 0;
        detMidi = 0.0; curTarget = 0.0;
        hopped = false; detectedMidi = -1.0f; outputMidi = -1.0f;
    }

    // Process one stereo frame.
    //  wetL/R : corrected signal (already crossfaded to dry when no pitch is found)
    //  dryL/R : the input delayed by the plugin latency (for dry/wet mixing)
    void processSample (float inL, float inR, float& wetL, float& wetR,
                        float& dryL, float& dryR, const Settings& st)
    {
        const int64_t wi = wr & MASK;
        inBuf[0][(size_t) wi] = inL;
        inBuf[1][(size_t) wi] = inR;

        decAcc += 0.5f * (inL + inR);
        if (++decCount >= dec)
        {
            decRing[(size_t) decPos] = decAcc / (float) dec;
            decPos = (decPos + 1) % yinW;
            decAcc = 0.0f;
            decCount = 0;
        }
        ++wr;

        hopped = false;
        if (++hopCounter >= hop)
        {
            hopCounter = 0;
            analyse (st);
            hopped = true;
        }

        ratioSm   += ratioCoef  * (ratio - ratioSm);
        voicedMix += voicedCoef * ((voiced ? 1.0f : 0.0f) - voicedMix);

        const int64_t o = wr - 1 - latency;   // time index of the sample we output now
        while (s - Tcur <= (double) o)
        {
            makeGrain (o);
            s += Tcur / (double) ratioSm;
        }

        const size_t idx = (size_t) (o & MASK);
        const float yL = outBuf[0][idx], yR = outBuf[1][idx];
        outBuf[0][idx] = 0.0f;
        outBuf[1][idx] = 0.0f;
        dryL = inBuf[0][idx];
        dryR = inBuf[1][idx];
        wetL = voicedMix * yL + (1.0f - voicedMix) * dryL;
        wetR = voicedMix * yR + (1.0f - voicedMix) * dryR;
    }

    // info for the display (valid when hopped == true)
    bool  hopped = false;
    float detectedMidi = -1.0f;   // -1 when unvoiced
    float outputMidi   = -1.0f;

private:
    static constexpr int B = 32768;
    static constexpr int64_t MASK = B - 1;
    static constexpr int yinW = 1024;     // YIN frame (decimated samples)
    static constexpr int hop  = 256;      // analysis hop (input samples)

    void makeGrain (int64_t o)
    {
        const double T = Tcur;
        const int H = juce::jmax (8, (int) std::lround (T));

        double n = std::round ((s - cPrev) / T);
        if (n < 0.0) n = 0.0;
        double c = cPrev + n * T;
        if (std::abs (c - s) > 1.5 * T) c = s;       // (re)synchronise
        cPrev = c;

        const int64_t ci = (int64_t) std::llround (c);
        const int64_t si = (int64_t) std::llround (s);
        const float g = (float) ((T / (double) ratioSm) / (double) H);   // overlap-add normalisation

        for (int k = -H; k <= H; ++k)
        {
            const int64_t outIdx = si + k;
            if (outIdx < o) continue;
            const int64_t inIdx = ci + k;
            if (inIdx > wr - 1) continue;
            const float w = 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * (float) k / (float) H));
            const float gw = g * w;
            outBuf[0][(size_t) (outIdx & MASK)] += gw * inBuf[0][(size_t) (inIdx & MASK)];
            outBuf[1][(size_t) (outIdx & MASK)] += gw * inBuf[1][(size_t) (inIdx & MASK)];
        }
    }

    void analyse (const Settings& st)
    {
        for (int i = 0; i < yinW; ++i)
            frame[(size_t) i] = decRing[(size_t) ((decPos + i) % yinW)];   // oldest -> newest

        double energy = 0.0;
        for (int i = 0; i < yinW; ++i) energy += (double) frame[(size_t) i] * (double) frame[(size_t) i];
        const double rms = std::sqrt (energy / (double) yinW);

        float tau = 0.0f;
        const bool found = (rms > 0.003) && yin (tau);

        if (found)
        {
            unvoicedCount = 0;
            const double period = (double) tau * (double) dec;
            const double f0 = sr / period;

            if (! voiced || std::abs (period - Tcur) > 0.15 * Tcur) Tcur = period;
            else Tcur += 0.4 * (period - Tcur);
            Tcur = juce::jlimit (tMin, tMax, Tcur);

            const double midi = 69.0 + 12.0 * std::log2 (f0 / (double) st.refA);
            const bool onset = ! voiced;
            if (onset || std::abs (midi - detMidi) > 1.0) { detMidi = midi; haveTarget = false; }
            else detMidi += 0.35 * (midi - detMidi);

            const double hyst = 0.5 + 0.5 * (double) st.stabilize;
            if (! haveTarget || ! noteAllowed (curTarget, st) || std::abs (detMidi - curTarget) > hyst)
            {
                curTarget = nearestNote (detMidi, st);
                haveTarget = true;
            }

            const double targetShift = (curTarget - detMidi) * (double) st.amount;
            if (onset) shift = 0.0;

            const double retuneSec = (double) st.retuneMs * 0.001;
            const double alpha = retuneSec < 0.0005 ? 1.0
                                 : 1.0 - std::exp (-((double) hop / sr) / retuneSec);
            shift += alpha * (targetShift - shift);
            shift = juce::jlimit (-12.0, 12.0, shift);

            ratio = (float) std::pow (2.0, shift / 12.0);
            voiced = true;
            detectedMidi = (float) detMidi;
            outputMidi   = (float) (detMidi + shift);
        }
        else
        {
            if (++unvoicedCount > 3)
            {
                voiced = false;
                ratio = 1.0f;
                haveTarget = false;
            }
            detectedMidi = -1.0f;
            outputMidi   = -1.0f;
        }
    }

    // YIN pitch detector on 'frame'. Returns period in decimated samples.
    bool yin (float& tauOut)
    {
        const int half   = yinW / 2;
        const int tauMin = juce::jmax (2, (int) (srDec / 900.0));
        const int tauMax = juce::jmin (half - 2, (int) (srDec / 80.0));
        if (tauMax <= tauMin + 2) return false;

        const float* x = frame.data();
        for (int tau = 1; tau <= tauMax + 1; ++tau)
        {
            float sum = 0.0f;
            for (int j = 0; j < half; ++j)
            {
                const float d = x[j] - x[j + tau];
                sum += d * d;
            }
            diff[(size_t) tau] = sum;
        }

        cmnd[0] = 1.0f;
        float running = 0.0f;
        for (int tau = 1; tau <= tauMax + 1; ++tau)
        {
            running += diff[(size_t) tau];
            cmnd[(size_t) tau] = running > 1.0e-12f ? diff[(size_t) tau] * (float) tau / running : 1.0f;
        }

        const float threshold = 0.15f;
        int best = -1;
        for (int tau = tauMin; tau <= tauMax; ++tau)
        {
            if (cmnd[(size_t) tau] < threshold)
            {
                while (tau + 1 <= tauMax && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0) return false;

        const float a = cmnd[(size_t) best - 1], b = cmnd[(size_t) best], c = cmnd[(size_t) best + 1];
        const float den = a - 2.0f * b + c;
        float refined = (float) best;
        if (std::abs (den) > 1.0e-9f)
            refined += juce::jlimit (-1.0f, 1.0f, 0.5f * (a - c) / den);
        tauOut = refined;
        return true;
    }

    static bool noteAllowed (double midiNote, const Settings& st)
    {
        const int n = (int) std::lround (midiNote);
        return st.notes[((n % 12) + 12) % 12];
    }

    static double nearestNote (double m, const Settings& st)
    {
        const int r = (int) std::lround (m);
        double best = m, bestDist = 1.0e9;
        for (int n = r - 7; n <= r + 7; ++n)
        {
            if (! st.notes[((n % 12) + 12) % 12]) continue;
            const double d = std::abs ((double) n - m);
            if (d < bestDist) { bestDist = d; best = (double) n; }
        }
        return best;   // if no notes are enabled, no correction
    }

    double sr = 44100.0, srDec = 22050.0, tMax = 551.0, tMin = 44.0;
    int dec = 2, latency = 1400;

    std::vector<float> inBuf[2], outBuf[2];
    int64_t wr = 0;
    double s = 0.0, cPrev = 0.0;

    std::vector<float> frame, decRing, diff, cmnd;
    int decPos = 0, decCount = 0, hopCounter = 0;
    float decAcc = 0.0f;

    double Tcur = 220.0, shift = 0.0, detMidi = 0.0, curTarget = 0.0;
    float ratio = 1.0f, ratioSm = 1.0f, voicedMix = 0.0f;
    float ratioCoef = 0.01f, voicedCoef = 0.01f;
    bool voiced = false, haveTarget = false;
    int unvoicedCount = 0;
};
