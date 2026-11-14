#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>

// ============================================================================
//  Sog – the move in front of (or behind) a word        v0.2
//
//  Four moves, each one render from the captured sound:
//    Reverb   the classic: reverse → reverb → reverse again. The swell that pulls into the word.
//    Echo     the same trick with a ping-pong echo: repeats that come closer and louder.
//    Stutter  the first slice of the word, again and again, right up to the word.
//    Spin     the DJ move: the record spins back into the word / winds down after it.
//
//  Two directions:
//    Before   the clip ENDS where the word begins (riser).   Put its END on the word.
//    After    the clip STARTS with the word (throw / tail).  Put its START on the word.
//
//  The clip is exactly as long as the Length you picked at the song tempo.
//  Pure function, no state: same input + same settings = same file.
//  All TUNE values are in Params defaults and the constants below.
// ============================================================================

struct SogParams
{
    double bpm = 140.0;
    float  bars = 1.0f;           // 0.25 / 0.5 / 1 / 2
    float  size = 0.5f;           // 0 = late, steep pull · 1 = long wash   (Echo: how many repeats · Spin: how far)
    float  tone = 0.5f;           // 0 dark · 1 bright
    float  width = 0.6f;          // 0 mono · 1 wide
    float  tremolo = 0.0f;        // depth 0..1
    int    tremDiv = 0;           // "Rate": 0 off, 1 = 1/8, 2 = 1/16, 3 = 1/32  (Echo: repeat time · Stutter: slice)
    bool   lowCut = true;
    float  level = 0.7f;          // 0..1 → peak of the clip
    int    type = 0;              // SogRenderer::Type
    int    dir = 0;               // SogRenderer::Dir
    float  drive = 0.0f;          // 0 clean · 1 pushed hard: denser, louder at the same peak
};

namespace SogRenderer
{
    enum Type { Reverb = 0, Echo, Stutter, Spin };
    enum Dir  { Before = 0, After };     // Before: the clip ENDS on the word (riser). After: it STARTS on the word (throw).

    // TUNE
    static constexpr float sourceSeconds = 0.9f;     // how much of the captured sound feeds the reverse reverb / echo
    static constexpr float lowCutHz = 180.0f;
    static constexpr float peakAtFull = 0.89f;       // -1 dB at Level 100 %
    static constexpr float echoGlue = 0.22f;         // a little room on the echoes so they are not naked
    static constexpr float stutterGate = 0.82f;      // part of each slice that sounds
    static constexpr float spinCurve = 2.2f;         // Spin before the word: how late the speed-up comes

    inline double lengthSeconds (const SogParams& p) { return (double) p.bars * 4.0 * 60.0 / juce::jlimit (40.0, 300.0, p.bpm); }
    inline double beatSamples (const SogParams& p, double sr) { return 60.0 / juce::jlimit (40.0, 300.0, p.bpm) * sr; }
    /** Rate as samples; `fallback` is used when Rate is Off (1 = 1/8, 2 = 1/16, 3 = 1/32). */
    inline int rateSamples (const SogParams& p, double sr, int fallback)
    {
        const int d = p.tremDiv > 0 ? p.tremDiv : fallback;
        return juce::jmax (32, (int) std::round (beatSamples (p, sr) * (d == 1 ? 0.5 : d == 2 ? 0.25 : 0.125)));
    }
    /** True when the clip is meant to replace the word instead of sitting next to it. */
    inline bool replacesWord (const SogParams& p) { return p.type == Spin && p.dir == After; }

    namespace detail
    {
        inline void reverbWet (juce::AudioBuffer<float>& b, double sr, const SogParams& p, float mix)
        {
            juce::Reverb reverb; reverb.setSampleRate (sr);
            juce::Reverb::Parameters rp;
            rp.roomSize = 0.80f + 0.17f * p.size;
            rp.damping  = 0.75f - 0.6f * p.tone;
            rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.width = 1.0f; rp.freezeMode = 0.0f;
            reverb.setParameters (rp);
            if (mix >= 1.0f) { reverb.processStereo (b.getWritePointer (0), b.getWritePointer (1), b.getNumSamples()); return; }
            juce::AudioBuffer<float> w; w.makeCopyOf (b);
            reverb.processStereo (w.getWritePointer (0), w.getWritePointer (1), w.getNumSamples());
            for (int ch = 0; ch < 2; ++ch) b.addFrom (ch, 0, w, ch, 0, b.getNumSamples(), mix);
        }

        /** Ping-pong echo, wet only: repeat 1 left, 2 right, ... each one quieter and a bit darker. */
        inline void echoWet (juce::AudioBuffer<float>& b, int delay, const SogParams& p)
        {
            const int n = b.getNumSamples();
            const float g = 0.50f + 0.42f * p.size;                    // level of each repeat against the one before
            const float a = 0.30f + 0.65f * p.tone;                    // one-pole damping in the loop
            std::vector<float> dl[2] { std::vector<float> ((size_t) delay, 0.0f), std::vector<float> ((size_t) delay, 0.0f) };
            float lp[2] { 0.0f, 0.0f }; int idx = 0;
            float* l = b.getWritePointer (0); float* r = b.getWritePointer (1);
            for (int i = 0; i < n; ++i)
            {
                const float in = 0.5f * (l[i] + r[i]);
                const float d0 = dl[0][(size_t) idx], d1 = dl[1][(size_t) idx];
                lp[0] += (d0 - lp[0]) * a; lp[1] += (d1 - lp[1]) * a;
                dl[0][(size_t) idx] = in + g * lp[1];
                dl[1][(size_t) idx] = g * lp[0];
                l[i] = d0; r[i] = d1;
                if (++idx >= delay) idx = 0;
            }
        }

        inline float readLin (const float* s, int len, double pos)
        {
            if (pos < 0.0 || pos >= (double) (len - 1)) return 0.0f;
            const int i = (int) pos; const float f = (float) (pos - (double) i);
            return s[i] + (s[i + 1] - s[i]) * f;
        }
    }

    /** source: the captured sound from its first moment on (1 or 2 channels). Returns a stereo clip, exactly Length long. */
    inline juce::AudioBuffer<float> render (const juce::AudioBuffer<float>& source, double sr, const SogParams& p)
    {
        const int outLen = juce::jmax (64, (int) std::round (lengthSeconds (p) * sr));
        juce::AudioBuffer<float> out (2, outLen); out.clear();
        const int fullLen = source.getNumSamples();
        const int srcLen = juce::jmin (fullLen, (int) (sourceSeconds * sr));
        if (srcLen < 32) return out;
        const bool before = p.dir != After;
        const int type = juce::jlimit (0, 3, p.type);
        auto src = [&] (int ch) { return source.getReadPointer (juce::jmin (ch, source.getNumChannels() - 1)); };

        // ---- 1. the body of the clip ----
        if (before && (type == Reverb || type == Echo))
        {
            // reversed source, then silence for the tail we are going to keep
            const int total = srcLen + outLen;
            juce::AudioBuffer<float> work (2, total); work.clear();
            const int fade = juce::jmin (srcLen / 2, (int) (0.03 * sr));
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* s = src (ch);
                float* w = work.getWritePointer (ch);
                for (int i = 0; i < srcLen; ++i)
                {
                    float g = 1.0f;
                    if (i < fade) g = (float) i / (float) fade;                       // the cut end of the source fades in (reversed: it comes first)
                    w[i] = s[srcLen - 1 - i] * g;
                }
            }
            if (type == Reverb) detail::reverbWet (work, sr, p, 1.0f);
            else              { detail::echoWet (work, rateSamples (p, sr, 1), p); detail::reverbWet (work, sr, p, echoGlue); }

            // reverse again and keep only what now sits BEFORE the sound: the tail, rising
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* w = work.getReadPointer (ch); float* o = out.getWritePointer (ch);
                for (int i = 0; i < outLen; ++i) o[i] = w[total - 1 - i];
            }
        }
        else if (type == Reverb || type == Echo)
        {
            // After: the whole captured sound goes in, only the wet comes out, starting with the word
            const int n = juce::jmin (fullLen, outLen);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, 0, src (ch), n);
            if (type == Reverb) detail::reverbWet (out, sr, p, 1.0f);
            else              { detail::echoWet (out, rateSamples (p, sr, 1), p); detail::reverbWet (out, sr, p, echoGlue); }
        }
        else if (type == Stutter)
        {
            // the first slice of the sound, again and again. Before: the last repeat ends on the word.
            // After: the first repeat starts one slice after the word began (the word itself is slice 0).
            const int d = rateSamples (p, sr, 2), slice = juce::jmin (d, fullLen);
            const int gate = juce::jmax (16, (int) ((float) slice * stutterGate)), fin = juce::jmin (gate / 4, (int) (0.003 * sr)), fout = juce::jmax (1, gate / 5);
            for (int k = 1; k < 4096; ++k)
            {
                const int start = before ? outLen - k * d : k * d;
                if (start + gate <= 0 || start >= outLen) break;
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float* s = src (ch); float* o = out.getWritePointer (ch);
                    for (int i = 0; i < gate; ++i)
                    {
                        const int t = start + i; if (t < 0 || t >= outLen) continue;
                        float g = 1.0f;
                        if (i < fin) g = (float) i / (float) fin;
                        if (i >= gate - fout) g *= (float) (gate - 1 - i) / (float) fout;
                        o[t] = s[i] * g;
                    }
                }
            }
        }
        else
        {
            // Spin. Before: the sound runs backwards and speeds up into the word (the record is spun to its start).
            //       After:  the sound starts at normal speed and winds down to a stop (tape stop). It replaces the word.
            const double c = 2.0 - 1.5 * (double) p.size;                                        // After: small = long, lazy stop
            const double reach = juce::jmin ((double) (fullLen - 2), (double) outLen * (0.5 + (double) p.size));
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* s = src (ch); float* o = out.getWritePointer (ch);
                for (int i = 0; i < outLen; ++i)
                {
                    const double x = (double) i / (double) (outLen - 1);
                    const double pos = before ? reach * (1.0 - std::pow (x, (double) spinCurve))
                                              : (double) outLen * (1.0 - std::pow (1.0 - x, c + 1.0)) / (c + 1.0);
                    o[i] = detail::readLin (s, fullLen, pos);
                }
            }
        }

        // ---- 2. tone + low cut ----
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

        // ---- 3. width (mid/side) ----
        {
            float* l = out.getWritePointer (0); float* r = out.getWritePointer (1);
            const float sGain = juce::jlimit (0.0f, 1.2f, p.width * 1.2f);
            for (int i = 0; i < outLen; ++i) { const float m = 0.5f * (l[i] + r[i]), s = 0.5f * (l[i] - r[i]) * sGain; l[i] = m + s; r[i] = m - s; }
        }

        // ---- 4. shape. Reverse reverb: our own rise, so the swell always fills the length, whatever the reverb did.
        //    Size small = stays quiet, then pulls hard at the end. Size big = long wash.
        const float steepBase = 6.5f - 5.0f * p.size;
        const float steep = type == Reverb ? steepBase : type == Stutter ? steepBase * 0.6f : type == Spin ? steepBase * 0.5f : 0.0f;
        const double beat = beatSamples (p, sr);
        const bool tremOn = (type == Reverb || type == Spin) && p.tremolo > 0.0f && p.tremDiv > 0;       // Echo and Stutter use Rate as their time
        const double tremLen = ! tremOn ? 0.0 : p.tremDiv == 1 ? beat * 0.5 : p.tremDiv == 2 ? beat * 0.25 : beat * 0.125;
        if (before && type == Reverb)
        {
            // level out the reverb's own decay (it rises towards the end already): estimate with a slow envelope
            for (int ch = 0; ch < 2; ++ch)
            {
                float* o = out.getWritePointer (ch);
                float env = 0.0f; std::vector<float> e ((size_t) outLen);
                const float c = 1.0f - std::exp (-1.0f / (0.05f * (float) sr));
                for (int i = outLen - 1; i >= 0; --i) { env += (std::abs (o[i]) - env) * c; e[(size_t) i] = env; }
                float eMax = 1.0e-6f; for (float v : e) eMax = juce::jmax (eMax, v);
                for (int i = 0; i < outLen; ++i) o[i] *= juce::jmin (20.0f, eMax / (e[(size_t) i] + eMax * 0.05f));
            }
        }

        // Drive: push the clip into a soft ceiling. Done before the shape, so the rise stays a rise.
        if (p.drive > 0.001f)
        {
            const float pk = juce::jmax (out.getMagnitude (0, 0, outLen), out.getMagnitude (1, 0, outLen));
            if (pk > 1.0e-6f)
            {
                const float g = 1.0f + 5.0f * p.drive * p.drive + 2.0f * p.drive, norm = 1.0f / std::tanh (g);
                for (int ch = 0; ch < 2; ++ch) { float* o = out.getWritePointer (ch); for (int i = 0; i < outLen; ++i) o[i] = std::tanh (o[i] / pk * g) * norm; }
            }
        }

        if (before)
        {
            const int inFade = juce::jmin (outLen / 4, (int) (0.02 * sr)), endFade = juce::jmin (outLen / 8, (int) (0.004 * sr));
            for (int i = 0; i < outLen; ++i)
            {
                const float x = (float) i / (float) (outLen - 1);
                float g = std::exp (-steep * (1.0f - x));
                if (i < inFade) g *= (float) i / (float) inFade;
                if (i >= outLen - endFade) g *= 0.25f + 0.75f * (float) (outLen - 1 - i) / (float) endFade;    // no click when the word lands
                if (tremLen > 0.0)
                {
                    // counted from the END, so the last pulse always lands on the word
                    const double ph = std::fmod ((double) (outLen - 1 - i), tremLen) / tremLen;
                    const float t = 0.5f + 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * ph);
                    g *= 1.0f - p.tremolo * (1.0f - t);
                }
                out.getWritePointer (0)[i] *= g; out.getWritePointer (1)[i] *= g;
            }
        }
        else
        {
            // After: starts with the word, has to be gone when the clip ends
            const int inFade = juce::jmin (outLen / 8, (int) (0.003 * sr)), endFade = juce::jmax (1, outLen / 5);
            const float fall = type == Stutter ? steepBase * 0.6f : 0.0f;
            for (int i = 0; i < outLen; ++i)
            {
                const float x = (float) i / (float) (outLen - 1);
                float g = std::exp (-fall * x);
                if (i < inFade) g *= (float) i / (float) inFade;
                if (i >= outLen - endFade) g *= (float) (outLen - 1 - i) / (float) endFade;
                if (tremLen > 0.0)
                {
                    const double ph = std::fmod ((double) i, tremLen) / tremLen;                              // counted from the word
                    const float t = 0.5f + 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * ph);
                    g *= 1.0f - p.tremolo * (1.0f - t);
                }
                out.getWritePointer (0)[i] *= g; out.getWritePointer (1)[i] *= g;
            }
        }

        // ---- 5. level: the peak lands where Level says ----
        const float peak = juce::jmax (out.getMagnitude (0, 0, outLen), out.getMagnitude (1, 0, outLen));
        if (peak > 1.0e-6f) out.applyGain (peakAtFull * juce::jlimit (0.0f, 1.0f, p.level) / peak);
        return out;
    }
}
