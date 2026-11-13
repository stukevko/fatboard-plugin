#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>

// ============================================================================
//  Sog – reverse reverb   (working title, v0.1)
//
//  The swell that pulls into a word or a hit. Classic studio trick:
//  reverse the sound → reverb → reverse again. Here it is one render:
//
//    captured sound (its first moments) ─► reversed ─► reverb ─► reversed again
//         ─► keep exactly the part BEFORE the sound ─► shape the rise ─► tone, width, tremolo
//
//  The result is exactly as long as the Length you picked at the song tempo and
//  ends where the captured sound begins: put its END on the word, done.
//
//  Pure function, no state: same input + same settings = same file.
//  All TUNE values are in Params defaults and the constants below.
// ============================================================================

struct SogParams
{
    double bpm = 140.0;
    float  bars = 1.0f;           // 0.25 / 0.5 / 1 / 2
    float  size = 0.5f;           // 0 = late, steep pull · 1 = long wash
    float  tone = 0.5f;           // 0 dark · 1 bright
    float  width = 0.6f;          // 0 mono · 1 wide
    float  tremolo = 0.0f;        // depth 0..1
    int    tremDiv = 0;           // 0 off, 1 = 1/8, 2 = 1/16, 3 = 1/32
    bool   lowCut = true;
    float  level = 0.7f;          // 0..1 → peak of the swell
};

namespace SogRenderer
{
    // TUNE
    static constexpr float sourceSeconds = 0.9f;     // how much of the captured sound feeds the reverb
    static constexpr float lowCutHz = 180.0f;
    static constexpr float peakAtFull = 0.89f;       // -1 dB at Level 100 %

    inline double lengthSeconds (const SogParams& p) { return (double) p.bars * 4.0 * 60.0 / juce::jlimit (40.0, 300.0, p.bpm); }

    /** source: the captured sound from its first moment on (1 or 2 channels). Returns a stereo swell. */
    inline juce::AudioBuffer<float> render (const juce::AudioBuffer<float>& source, double sr, const SogParams& p)
    {
        const int outLen = juce::jmax (64, (int) std::round (lengthSeconds (p) * sr));
        juce::AudioBuffer<float> out (2, outLen); out.clear();
        const int srcLen = juce::jmin (source.getNumSamples(), (int) (sourceSeconds * sr));
        if (srcLen < 32) return out;

        // 1. reversed source, then silence for the tail we are going to keep
        const int total = srcLen + outLen;
        juce::AudioBuffer<float> work (2, total); work.clear();
        const int fade = juce::jmin (srcLen / 2, (int) (0.03 * sr));
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* s = source.getReadPointer (juce::jmin (ch, source.getNumChannels() - 1));
            float* w = work.getWritePointer (ch);
            for (int i = 0; i < srcLen; ++i)
            {
                float g = 1.0f;
                if (i < fade) g = (float) i / (float) fade;                       // the cut end of the source fades in (reversed: it comes first)
                w[i] = s[srcLen - 1 - i] * g;
            }
        }

        // 2. reverb, wet only
        juce::Reverb reverb; reverb.setSampleRate (sr);
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.80f + 0.17f * p.size;
        rp.damping  = 0.75f - 0.6f * p.tone;
        rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.width = 1.0f; rp.freezeMode = 0.0f;
        reverb.setParameters (rp);
        reverb.processStereo (work.getWritePointer (0), work.getWritePointer (1), total);

        // 3. reverse again and keep only what now sits BEFORE the sound: the tail, rising
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* w = work.getReadPointer (ch); float* o = out.getWritePointer (ch);
            for (int i = 0; i < outLen; ++i) o[i] = w[total - 1 - i];
        }

        // 4. tone + low cut
        using C = juce::dsp::IIR::Coefficients<float>;
        const float lpHz = juce::jmin (2200.0f * std::pow (8.0f, p.tone), (float) sr * 0.45f);          // 2.2 k … 17.6 k
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::dsp::IIR::Filter<float> lp, hp[2];
            lp.coefficients = C::makeLowPass (sr, lpHz, 0.707f);
            hp[0].coefficients = hp[1].coefficients = C::makeHighPass (sr, p.lowCut ? lowCutHz : 30.0f, 0.707f);
            float* o = out.getWritePointer (ch);
            for (int i = 0; i < outLen; ++i) o[i] = hp[1].processSample (hp[0].processSample (lp.processSample (o[i])));
        }

        // 5. width (mid/side)
        {
            float* l = out.getWritePointer (0); float* r = out.getWritePointer (1);
            const float sGain = juce::jlimit (0.0f, 1.2f, p.width * 1.2f);
            for (int i = 0; i < outLen; ++i) { const float m = 0.5f * (l[i] + r[i]), s = 0.5f * (l[i] - r[i]) * sGain; l[i] = m + s; r[i] = m - s; }
        }

        // 6. the rise: our own curve, so the swell always fills the length, whatever the reverb did.
        //    Size small = stays quiet, then pulls hard at the end. Size big = long wash.
        const float steep = 6.5f - 5.0f * p.size;
        const double beat = 60.0 / juce::jlimit (40.0, 300.0, p.bpm) * sr;
        const double tremLen = p.tremDiv == 1 ? beat * 0.5 : p.tremDiv == 2 ? beat * 0.25 : p.tremDiv == 3 ? beat * 0.125 : 0.0;
        const int inFade = juce::jmin (outLen / 4, (int) (0.02 * sr)), endFade = juce::jmin (outLen / 8, (int) (0.004 * sr));
        // normalise on the un-enveloped tail first so the shape below is what you hear
        for (int ch = 0; ch < 2; ++ch)
        {
            float* o = out.getWritePointer (ch);
            // level out the reverb's own decay (it rises towards the end already): estimate with a slow envelope
            float env = 0.0f; std::vector<float> e ((size_t) outLen);
            const float c = 1.0f - std::exp (-1.0f / (0.05f * (float) sr));
            for (int i = outLen - 1; i >= 0; --i) { env += (std::abs (o[i]) - env) * c; e[(size_t) i] = env; }
            float eMax = 1.0e-6f; for (float v : e) eMax = juce::jmax (eMax, v);
            for (int i = 0; i < outLen; ++i) o[i] *= juce::jmin (20.0f, eMax / (e[(size_t) i] + eMax * 0.05f));
        }
        for (int i = 0; i < outLen; ++i)
        {
            const float x = (float) i / (float) (outLen - 1);
            float g = std::exp (-steep * (1.0f - x));
            if (i < inFade) g *= (float) i / (float) inFade;
            if (i >= outLen - endFade) g *= 0.25f + 0.75f * (float) (outLen - 1 - i) / (float) endFade;    // no click when the word lands
            if (tremLen > 0.0 && p.tremolo > 0.0f)
            {
                // counted from the END, so the last pulse always lands on the word
                const double ph = std::fmod ((double) (outLen - 1 - i), tremLen) / tremLen;
                const float t = 0.5f + 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * ph);
                g *= 1.0f - p.tremolo * (1.0f - t);
            }
            out.getWritePointer (0)[i] *= g; out.getWritePointer (1)[i] *= g;
        }

        // 7. level: the peak lands where Level says
        const float peak = juce::jmax (out.getMagnitude (0, 0, outLen), out.getMagnitude (1, 0, outLen));
        if (peak > 1.0e-6f) out.applyGain (peakAtFull * juce::jlimit (0.0f, 1.0f, p.level) / peak);
        return out;
    }
}
