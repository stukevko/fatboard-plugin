// Sog (reverse reverb) offline check.
//  - the swell is exactly Length long at the tempo, for every length
//  - it rises: the last quarter is far louder than the first, the peak sits at the end
//  - Size changes the shape (small = late pull), Tone changes brightness, Width the side signal
//  - Low Cut removes the rumble, Tremolo pulses at the right rate and lands on the end
//  - Level sets the peak, never above -1 dB, everything finite
#include "SogRenderer.h"
#include <cstdio>

static const double sr = 48000.0;
static float rmsDb (const juce::AudioBuffer<float>& b, int a, int z) { double s = 0; for (int i = a; i < z; ++i) for (int ch = 0; ch < 2; ++ch) s += (double) b.getSample (ch, i) * b.getSample (ch, i); return 10.0f * (float) std::log10 (s / (2.0 * (z - a)) + 1e-12); }
static float bandDb (const juce::AudioBuffer<float>& b, float lo, float hi)
{
    using C = juce::dsp::IIR::Coefficients<float>; juce::dsp::IIR::Filter<float> hp, lp; hp.coefficients = C::makeHighPass (sr, lo, 0.707f); lp.coefficients = C::makeLowPass (sr, hi, 0.707f);
    double s = 0; for (int i = 0; i < b.getNumSamples(); ++i) { const float y = lp.processSample (hp.processSample (b.getSample (0, i))); s += (double) y * y; }
    return 10.0f * (float) std::log10 (s / b.getNumSamples() + 1e-12);
}

int main()
{
    int fails = 0;
    // "word": voiced buzz (140 Hz saw-ish + formant noise), 0.7 s
    juce::AudioBuffer<float> src (2, (int) (sr * 0.7)); juce::Random rng (9); double ph = 0; float lp = 0;
    for (int i = 0; i < src.getNumSamples(); ++i)
    {
        ph += 140.0 / sr; if (ph >= 1) ph -= 1; const float w = rng.nextFloat() * 2 - 1; lp += (w - lp) * 0.2f;
        const float env = juce::jmin (1.0f, i / 200.0f) * std::exp (-(float) i / (float) (sr * 0.5));
        const float v = ((float) (2 * ph - 1) * 0.4f + lp * 0.3f + 0.2f * (float) std::sin (2 * juce::MathConstants<double>::pi * 60.0 * i / sr)) * env;
        src.setSample (0, i, v); src.setSample (1, i, v * 0.9f);
    }

    for (float bars : { 0.25f, 0.5f, 1.0f, 2.0f })
    {
        SogParams p; p.bpm = 110.0; p.bars = bars;
        auto o = SogRenderer::render (src, sr, p);
        const int n = o.getNumSamples(), want = (int) std::round (bars * 4 * 60.0 / 110.0 * sr);
        int peakAt = 0; float pk = 0; bool bad = false;
        for (int i = 0; i < n; ++i) for (int ch = 0; ch < 2; ++ch) { const float v = o.getSample (ch, i); if (! std::isfinite (v)) bad = true; if (std::abs (v) > pk) { pk = std::abs (v); peakAt = i; } }
        const float rise = rmsDb (o, n * 3 / 4, n) - rmsDb (o, 0, n / 4);
        std::printf ("%4.2f bar: %d samples (want %d)  rise %.1f dB  peak %.2f at %.0f %% of the length\n", bars, n, want, rise, pk, 100.0 * peakAt / n);
        if (n != want) { ++fails; std::printf ("  FAIL: wrong length\n"); }
        if (bad || pk > 0.891f) { ++fails; std::printf ("  FAIL: not finite / too hot\n"); }
        if (rise < 12.0f) { ++fails; std::printf ("  FAIL: does not rise\n"); }
        if (peakAt < n * 0.8) { ++fails; std::printf ("  FAIL: peak not at the end\n"); }
    }

    SogParams base; base.bpm = 140.0; base.bars = 1.0f;
    {
        auto small = base, big = base; small.size = 0.0f; big.size = 1.0f;
        auto a = SogRenderer::render (src, sr, small), b = SogRenderer::render (src, sr, big); const int n = a.getNumSamples();
        const float ma = rmsDb (a, n / 4, n / 2) - rmsDb (a, n * 7 / 8, n), mb = rmsDb (b, n / 4, n / 2) - rmsDb (b, n * 7 / 8, n);
        std::printf ("size: second quarter vs. end  small %.1f dB, big %.1f dB\n", ma, mb);
        if (! (mb > ma + 6.0f)) { ++fails; std::printf ("  FAIL: Size does not change the shape\n"); }
    }
    {
        auto dark = base, bright = base; dark.tone = 0.0f; bright.tone = 1.0f;
        auto a = SogRenderer::render (src, sr, dark), b = SogRenderer::render (src, sr, bright);
        const float da = bandDb (a, 5000, 12000) - bandDb (a, 300, 1500), db = bandDb (b, 5000, 12000) - bandDb (b, 300, 1500);
        std::printf ("tone: top vs. mid  dark %.1f dB, bright %.1f dB\n", da, db);
        if (! (db > da + 6.0f)) { ++fails; std::printf ("  FAIL: Tone does nothing\n"); }
    }
    {
        auto mono = base, wide = base; mono.width = 0.0f; wide.width = 1.0f;
        auto a = SogRenderer::render (src, sr, mono), b = SogRenderer::render (src, sr, wide);
        auto side = [] (const juce::AudioBuffer<float>& x) { double s = 0, m = 0; for (int i = 0; i < x.getNumSamples(); ++i) { const double l = x.getSample (0, i), r = x.getSample (1, i); s += (l - r) * (l - r); m += (l + r) * (l + r); } return 10.0 * std::log10 (s / (m + 1e-12) + 1e-12); };
        std::printf ("width: side vs. mid  0 %% %.1f dB, 100 %% %.1f dB\n", side (a), side (b));
        if (! (side (a) < -60.0 && side (b) > -12.0)) { ++fails; std::printf ("  FAIL: Width\n"); }
    }
    {
        auto on = base, off = base; on.lowCut = true; off.lowCut = false;
        auto a = SogRenderer::render (src, sr, on), b = SogRenderer::render (src, sr, off);
        std::printf ("low cut: 40-100 Hz vs. mid  on %.1f dB, off %.1f dB\n", bandDb (a, 40, 100) - bandDb (a, 300, 1500), bandDb (b, 40, 100) - bandDb (b, 300, 1500));
        if (! (bandDb (a, 40, 100) - bandDb (a, 300, 1500) < bandDb (b, 40, 100) - bandDb (b, 300, 1500) - 8.0f)) { ++fails; std::printf ("  FAIL: Low Cut\n"); }
    }
    {
        auto t = base; t.tremolo = 1.0f; t.tremDiv = 2; t.bars = 2.0f; t.size = 1.0f;      // 1/16 on 2 bars: no length lock
        auto o = SogRenderer::render (src, sr, t); const int n = o.getNumSamples(); const double step = 60.0 / 140.0 * sr * 0.25;
        // loud at multiples of a 16th counted from the end, quiet half way between
        double onP = 0, offP = 0; int cnt = 0;
        for (int k = 1; k < 12; ++k) { const int c = n - 1 - (int) (k * step), h = n - 1 - (int) ((k - 0.5) * step); if (h - 60 < 0) break; onP += std::pow (10.0, rmsDb (o, c - 60, c + 60) / 10.0) / std::exp (-2.0 * 1.5 * (double) (n - c) / n); offP += std::pow (10.0, rmsDb (o, h - 60, h + 60) / 10.0) / std::exp (-2.0 * 1.5 * (double) (n - h) / n); ++cnt; }
        std::printf ("tremolo 1/16 on 2 bars: on-beat vs. between %.1f dB (%d pulses checked)\n", 10 * std::log10 (onP / offP), cnt);
        if (10 * std::log10 (onP / offP) < 10.0) { ++fails; std::printf ("  FAIL: tremolo not on the grid\n"); }
    }
    {
        auto q = base; q.level = 0.5f; auto o = SogRenderer::render (src, sr, q);
        const float pk = juce::jmax (o.getMagnitude (0, 0, o.getNumSamples()), o.getMagnitude (1, 0, o.getNumSamples()));
        std::printf ("level 50 %%: peak %.3f (want %.3f)\n", pk, 0.89f * 0.5f);
        if (std::abs (pk - 0.445f) > 0.01f) { ++fails; std::printf ("  FAIL: level\n"); }
        juce::AudioBuffer<float> silent (2, 4000); silent.clear(); auto z = SogRenderer::render (silent, sr, base);
        if (z.getMagnitude (0, 0, z.getNumSamples()) != 0.0f) { ++fails; std::printf ("  FAIL: silence in, noise out\n"); }
    }
    std::printf (fails == 0 ? "\nALL OK\n" : "\n%d FAILED\n", fails);
    return fails;
}
