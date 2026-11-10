// Room + articulation test.
//  A) Room: full-band "beat" (pink-ish noise + 60 Hz sub) with a "vocal" (1–4 kHz bursts, 1 s on / 1 s off)
//     on the sidechain. Expect: 2.4 kHz of the beat drops while the vocal is on, returns when it stops,
//     60 Hz untouched, and without a sidechain the beat is bit-identical.
//  B) Clear mode: consonant bursts get lifted, steady vowels do not.
#include <juce_dsp/juce_dsp.h>
#include "RoomCarver.h"
#include "VocalModes.h"
#include <cstdio>

static float bandDb (const juce::AudioBuffer<float>& b, double sr, float hz, int from, int to)
{
    juce::dsp::IIR::Filter<float> f; f.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, hz, 3.0f);
    double a = 0; for (int i = juce::jmax (0, from - 4000); i < to; ++i) { const float y = f.processSample (b.getSample (0, i)); if (i >= from) a += y * y; }
    return 20.0f * (float) std::log10 (std::sqrt (a / (to - from)) + 1e-9);
}

int main()
{
    const double sr = 48000.0; const int block = 256; const int total = (int) sr * 6;
    juce::Random rng (9);
    juce::AudioBuffer<float> beat (2, total), vocal (2, total);
    juce::dsp::IIR::Filter<float> vf; vf.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, 2200.0f, 0.6f);
    float lp = 0;
    for (int i = 0; i < total; ++i)
    {
        const double t = i / sr;
        const float w = rng.nextFloat() * 2 - 1; lp += (w - lp) * 0.15f;
        const float b = lp * 0.5f + 0.3f * (float) std::sin (2 * juce::MathConstants<double>::pi * 60 * t);
        beat.setSample (0, i, b); beat.setSample (1, i, b);
        const bool on = std::fmod (t, 2.0) < 1.0;
        const float v = on ? vf.processSample (rng.nextFloat() * 2 - 1) * 0.5f : vf.processSample (0.0f);
        vocal.setSample (0, i, v); vocal.setSample (1, i, v);
    }
    int fails = 0;
    {
        RoomCarver r; r.prepare (sr, 2);
        juce::AudioBuffer<float> out (2, total); out.makeCopyOf (beat);
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = juce::jmin (block, total - pos);
            juce::AudioBuffer<float> v (out.getArrayOfWritePointers(), 2, pos, n), s (vocal.getArrayOfWritePointers(), 2, pos, n);
            r.process (v, &s, 1.0f);
        }
        const int on0 = (int) (sr * 2.3), on1 = (int) (sr * 2.9), off0 = (int) (sr * 3.5), off1 = (int) (sr * 3.95);
        const float dOn24 = bandDb (out, sr, 2400, on0, on1) - bandDb (beat, sr, 2400, on0, on1);
        const float dOff24 = bandDb (out, sr, 2400, off0, off1) - bandDb (beat, sr, 2400, off0, off1);
        const float dOn60 = bandDb (out, sr, 60, on0, on1) - bandDb (beat, sr, 60, on0, on1);
        const float dOn10k = bandDb (out, sr, 10000, on0, on1) - bandDb (beat, sr, 10000, on0, on1);
        std::printf ("ROOM  beat 2.4 kHz: vocal on %+.1f dB | vocal off %+.1f dB     60 Hz: %+.1f dB     10 kHz: %+.1f dB\n", dOn24, dOff24, dOn60, dOn10k);
        if (! (dOn24 < -3.0f)) { ++fails; std::printf ("  FAIL: not enough room while vocal is on\n"); }
        if (! (std::abs (dOff24) < 0.7f)) { ++fails; std::printf ("  FAIL: beat not restored between lines\n"); }
        if (! (std::abs (dOn60) < 0.3f)) { ++fails; std::printf ("  FAIL: sub touched\n"); }
    }
    {
        RoomCarver r; r.prepare (sr, 2);
        juce::AudioBuffer<float> out (2, total); out.makeCopyOf (beat);
        for (int pos = 0; pos < total; pos += block) { const int n = juce::jmin (block, total - pos); juce::AudioBuffer<float> v (out.getArrayOfWritePointers(), 2, pos, n); r.process (v, nullptr, 1.0f); }
        float maxDiff = 0; for (int i = 0; i < total; ++i) maxDiff = juce::jmax (maxDiff, std::abs (out.getSample (0, i) - beat.getSample (0, i)));
        std::printf ("ROOM  no sidechain: max difference to input %.2g (0 = untouched)\n", maxDiff);
        if (maxDiff > 1e-6f) ++fails;
    }
    // B) articulation
    {
        juce::AudioBuffer<float> v (2, total); double ph = 0;
        juce::dsp::IIR::Filter<float> cf; cf.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, 4500.0f, 1.0f);
        for (int i = 0; i < total; ++i)
        {
            const double t = i / sr; ph += 180.0 / sr; if (ph >= 1) ph -= 1;
            float x = (float) (2 * ph - 1) * 0.2f;                                   // vowel
            const double tt = std::fmod (t, 0.5);
            if (tt < 0.03) x += cf.processSample (rng.nextFloat() * 2 - 1) * 0.5f * (float) std::exp (-tt * 80);   // consonant every 0.5 s
            v.setSample (0, i, x); v.setSample (1, i, x);
        }
        for (int mode : { 1, 2 })
        {
            VocalChain c; c.prepare (sr, block, 2); c.setMode (mode, true); c.setAmount (0.7f);
            juce::AudioBuffer<float> out (2, total); out.makeCopyOf (v);
            float maxArt = 0; bool bad = false;
            for (int pos = 0; pos < total; pos += block)
            {
                const int n = juce::jmin (block, total - pos); juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, n);
                c.process (view); maxArt = juce::jmax (maxArt, c.getArticDb());
                for (int i = 0; i < n; ++i) if (! std::isfinite (view.getSample (0, i))) bad = true;
            }
            std::printf ("VOCAL %s: consonant lift peaks at %.1f dB, finite %s\n", FatboardModes::get (mode).name, maxArt, bad ? "NO" : "yes");
            if (bad) ++fails;
            if (mode == 1 && maxArt < 1.0f) { ++fails; std::printf ("  FAIL: Clear does not lift consonants\n"); }
            if (mode == 2 && maxArt > 0.01f) { ++fails; std::printf ("  FAIL: Soft should not lift\n"); }
        }
    }
    std::printf (fails ? "FAILED (%d)\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
