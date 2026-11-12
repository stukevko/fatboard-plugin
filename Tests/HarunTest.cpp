// Harun v2 offline check, per template:
//  - width: L/R correlation drops, mono sum stays healthy (no comb hole)
//  - tone: the layer sits under a Scrub lead (less low end, presence dip, darker top)
//  - level: lands near the template's level
//  - space: adds a tail after the phrase ends, tail dies away, nothing runs off
//  - abuse: random block sizes / template + knob jumps / hot + silent input → finite, bounded
#include <juce_dsp/juce_dsp.h>
#include "HarunEngine.h"
#include <cstdio>

static const double sr = 48000.0;

static float bandDb (const juce::AudioBuffer<float>& b, int ch, float lo, float hi, int from, int to)
{
    using C = juce::dsp::IIR::Coefficients<float>;
    juce::dsp::IIR::Filter<float> hp[2], lp[2];
    for (int k = 0; k < 2; ++k) { hp[k].coefficients = C::makeHighPass (sr, lo, 0.707f); lp[k].coefficients = C::makeLowPass (sr, hi, 0.707f); }
    double a = 0; int n = 0;
    for (int i = juce::jmax (0, from - 4000); i < to; ++i) { float y = b.getSample (ch, i); for (int k = 0; k < 2; ++k) y = lp[k].processSample (hp[k].processSample (y)); if (i >= from) { a += (double) y * y; ++n; } }
    return 10.0f * (float) std::log10 (a / n + 1e-12);
}

int main()
{
    const int block = 256, total = (int) sr * 8;
    int fails = 0;
    // "voice": noise with a speech-ish tilt, phrases 0.6 s on / 0.4 s off, then silence from 5 s (to hear the tail)
    juce::Random rng (3);
    juce::AudioBuffer<float> in (2, total);
    float lp1 = 0, lp2 = 0;
    for (int i = 0; i < total; ++i)
    {
        const double t = i / sr;
        const float w = rng.nextFloat() * 2 - 1; lp1 += (w - lp1) * 0.25f; lp2 += (lp1 - lp2) * 0.5f;
        const float env = (t < 5.0 && std::fmod (t, 1.0) < 0.6) ? 1.0f : 0.0f;
        const float v = (lp2 * 0.6f + w * 0.05f) * env * 0.5f;
        in.setSample (0, i, v); in.setSample (1, i, v);
    }
    const int s0 = (int) (sr * 2.05), s1 = (int) (sr * 2.55);

    for (int ti = 0; ti < HarunTemplates::count; ++ti)
    {
        const auto& t = HarunTemplates::get (ti);
        HarunEngine e; e.prepare (sr, block, 2);
        juce::AudioBuffer<float> out; out.makeCopyOf (in);
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = juce::jmin (block, total - pos);
            juce::AudioBuffer<float> v (out.getArrayOfWritePointers(), 2, pos, n);
            e.setParams (ti, t.width, t.levelDb, t.space, t.spaceType, 140.0);
            e.process (v);
        }
        double xy = 0, xx = 0, yy = 0, mono = 0, lpow = 0, inpow = 0;
        for (int i = s0; i < s1; ++i)
        {
            const float x = out.getSample (0, i), y = out.getSample (1, i); xy += x * y; xx += x * x; yy += y * y;
            mono += 0.25 * (x + y) * (x + y); lpow += 0.5 * (x * x + y * y); inpow += (double) in.getSample (0, i) * in.getSample (0, i);
        }
        const double corr = xy / std::sqrt (xx * yy + 1e-12);
        const double monoLoss = 10 * std::log10 (mono / lpow + 1e-12);
        auto tone = [&] (float lo, float hi) { return 0.5f * (bandDb (out, 0, lo, hi, s0, s1) + bandDb (out, 1, lo, hi, s0, s1)) - bandDb (in, 0, lo, hi, s0, s1); };
        const float mid = tone (700, 1200);
        const float tailEarly = 0.5f * (bandDb (out, 0, 200, 8000, (int) (sr * 4.63), (int) (sr * 4.83)) + bandDb (out, 1, 200, 8000, (int) (sr * 4.63), (int) (sr * 4.83)));
        const float tailLate  = 0.5f * (bandDb (out, 0, 200, 8000, (int) (sr * 7.5), (int) (sr * 7.95)) + bandDb (out, 1, 200, 8000, (int) (sr * 7.5), (int) (sr * 7.95)));
        const float sing = 0.5f * (bandDb (out, 0, 200, 8000, s0, s1) + bandDb (out, 1, 200, 8000, s0, s1));
        float peak = 0; bool bad = false;
        for (int i = 0; i < total; ++i) for (int ch = 0; ch < 2; ++ch) { const float v = out.getSample (ch, i); if (! std::isfinite (v)) bad = true; peak = juce::jmax (peak, std::abs (v)); }

        std::printf ("%-8s corr %.2f  mono loss %+.1f dB  level %+.1f dB (template %+.1f)  |  vs. mid band: 100-160 Hz %+.1f  2.5-3.5k %+.1f  9-12k %+.1f dB  |  tail %+.1f dB under the layer, later %+.1f\n",
                     t.name, corr, monoLoss, 10 * std::log10 (lpow / inpow + 1e-12), t.levelDb,
                     tone (100, 160) - mid, tone (2500, 3500) - mid, tone (9000, 12000) - mid, tailEarly - sing, tailLate - sing);
        if (bad || peak > 2.0f)            { ++fails; std::printf ("  FAIL: not finite / too hot (peak %.2f)\n", peak); }
        if (corr > 0.8)                    { ++fails; std::printf ("  FAIL: not wide\n"); }
        if (monoLoss < -6.0)               { ++fails; std::printf ("  FAIL: mono sum collapses\n"); }
        if (tone (100, 160) - mid > -3.0f) { ++fails; std::printf ("  FAIL: low end not cleared for the lead\n"); }
        if (tone (2500, 3500) - mid > -1.5f) { ++fails; std::printf ("  FAIL: no presence dip\n"); }
        if (tailEarly - sing > -6.0f)      { ++fails; std::printf ("  FAIL: space louder than it should be\n"); }
        if (tailEarly - sing < -45.0f)     { ++fails; std::printf ("  FAIL: no audible space\n"); }
        if (tailLate > tailEarly - 6.0f)   { ++fails; std::printf ("  FAIL: tail does not die away\n"); }
    }

    // abuse
    {
        HarunEngine e; e.prepare (sr, 1024, 2);
        juce::Random r2 (8); float peak = 0; bool bad = false;
        for (int rep = 0; rep < 1500; ++rep)
        {
            const int n = 1 + r2.nextInt (1000); juce::AudioBuffer<float> b (2, n); const int kind = (rep / 60) % 4;
            for (int i = 0; i < n; ++i) { float s = 0; if (kind == 0) s = (r2.nextFloat() * 2 - 1) * 2.0f; else if (kind == 2) s = (i % 97 == 0) ? 1.0f : 0.0f; else if (kind == 3) s = (r2.nextFloat() * 2 - 1) * 1e-6f; b.setSample (0, i, s); b.setSample (1, i, -s * 0.3f); }
            e.setParams (r2.nextInt (4), r2.nextFloat(), -18.0f + r2.nextFloat() * 21.0f, r2.nextFloat(), r2.nextInt (3), 60.0 + r2.nextFloat() * 140.0);
            e.process (b);
            for (int i = 0; i < n; ++i) for (int ch = 0; ch < 2; ++ch) { const float v = b.getSample (ch, i); if (! std::isfinite (v)) bad = true; peak = juce::jmax (peak, std::abs (v)); }
        }
        std::printf ("abuse: peak %.2f (input peak 2.0), finite: %s\n", peak, bad ? "NO" : "yes");
        if (bad || peak > 8.0f) { ++fails; std::printf ("  FAIL: unstable\n"); }
    }
    std::printf (fails == 0 ? "\nALL OK\n" : "\n%d FAILED\n", fails);
    return fails;
}
