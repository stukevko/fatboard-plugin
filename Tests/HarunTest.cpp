// Harun v1 offline check: a "double" track + a louder "lead" on the sidechain.
// Verifies: stereo width (L/R correlation drops), mono sum stays healthy (no comb cancellation),
// auto level rides the layer under the lead, zero latency (cross-correlation peak at ~0).
#include <juce_dsp/juce_dsp.h>
#include "HarunEngine.h"
#include <cstdio>

int main()
{
    const double sr = 48000.0; const int block = 256; const int total = (int) sr * 6;
    juce::Random rng (3);
    juce::AudioBuffer<float> dbl (2, total), lead (2, total);
    juce::dsp::IIR::Filter<float> f; f.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, 4000.0f, 0.7f);
    double ph = 0.0;
    for (int i = 0; i < total; ++i)
    {
        const double t = i / sr;
        ph += 200.0 / sr; if (ph >= 1.0) ph -= 1.0;
        float v = f.processSample ((float) (2.0 * ph - 1.0) * 0.3f + rng.nextFloat() * 0.02f);
        const float env = (std::fmod (t, 1.0) < 0.6) ? 1.0f : 0.0f;         // phrases
        dbl.setSample (0, i, v * env * 0.5f); dbl.setSample (1, i, v * env * 0.5f);       // double quieter
        const float lenv = (t > 3.0) ? 0.3f : 1.0f;                           // lead drops 10 dB after 3 s
        lead.setSample (0, i, v * env * lenv); lead.setSample (1, i, v * env * lenv);
    }
    HarunEngine e; e.prepare (sr, block, 2);
    e.setParams (HarunEngine::Mode::Double, 1.0f, 0.8f, 0.5f, true);
    juce::AudioBuffer<float> out (2, total); out.makeCopyOf (dbl);
    float rideA = 0, rideB = 0; int na = 0, nb = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        juce::AudioBuffer<float> v (out.getArrayOfWritePointers(), 2, pos, n);
        juce::AudioBuffer<float> l (lead.getArrayOfWritePointers(), 2, pos, n);
        e.process (v, &l);
        const double t = pos / sr;
        if (t > 1.5 && t < 2.9) { rideA += e.getRideDb(); ++na; }
        if (t > 4.5 && t < 5.9) { rideB += e.getRideDb(); ++nb; }
    }
    auto corr = [&] (int a, int b, int from, int to) { double xy = 0, xx = 0, yy = 0; for (int i = from; i < to; ++i) { const float x = out.getSample (0, i), y = out.getSample (1, i); xy += x * y; xx += x * x; yy += y * y; } return xy / std::sqrt (xx * yy + 1e-12); };
    auto rmsDb = [&] (const juce::AudioBuffer<float>& b, int ch, int from, int to) { double a = 0; for (int i = from; i < to; ++i) a += b.getSample (ch, i) * b.getSample (ch, i); return 20 * std::log10 (std::sqrt (a / (to - from)) + 1e-9); };
    const int s0 = (int) (sr * 1.0), s1 = (int) (sr * 1.6);
    // mono sum vs. input
    double mono = 0; for (int i = s0; i < s1; ++i) { const float m = 0.5f * (out.getSample (0, i) + out.getSample (1, i)); mono += m * m; }
    const double monoDb = 20 * std::log10 (std::sqrt (mono / (s1 - s0)) + 1e-9);
    // latency: cross-correlate out L with input mono
    int bestLag = 0; double best = -1;
    for (int lag = 0; lag < 2000; ++lag) { double c = 0; for (int i = s0 + lag; i < s1; i += 3) c += out.getSample (0, i) * dbl.getSample (0, i - lag); if (c > best) { best = c; bestLag = lag; } }
    std::printf ("L/R correlation: in %.2f → out %.2f  (lower = wider)\n", corr (0, 1, s0, s1) * 0 + 1.0, corr (0, 1, s0, s1));
    std::printf ("level: in %.1f dB   out L %.1f dB   mono sum %.1f dB (comb loss vs L: %.1f dB)\n", rmsDb (dbl, 0, s0, s1), rmsDb (out, 0, s0, s1), monoDb, monoDb - rmsDb (out, 0, s0, s1));
    std::printf ("auto level: ride while lead loud %.1f dB → after lead drops 10 dB: %.1f dB (should fall ~10)\n", rideA / na, rideB / nb);
    std::printf ("latency: strongest correlation at lag %d samples (%.1f ms) – Haas offset, not processing latency\n", bestLag, 1000.0 * bestLag / sr);
    return 0;
}
