// Stress test: tries to make Dafocus blow up. Full-scale square bursts, DC, silence,
// sudden space/echo switches, odd block sizes, 44.1/48/96 kHz. Fails on NaN/inf or > 0 dBFS wet.
#include <juce_dsp/juce_dsp.h>
#include "DafocusEngine.h"
#include <cstdio>
#include <random>

int main()
{
    std::mt19937 rng (7); std::uniform_real_distribution<float> uni (-1.0f, 1.0f);
    int failures = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        DafocusEngine e; e.prepare (sr, 2048, 2);
        const int total = (int) (sr * 20);
        juce::AudioBuffer<float> buf (2, 2048);
        float maxOut = 0.0f, maxWet = 0.0f;
        int pos = 0, blockIdx = 0;
        while (pos < total)
        {
            const int n = 1 + (blockIdx * 37) % 2047;                    // odd block sizes
            const double t = pos / sr;
            const int phase = (int) (t / 2.5) % 8;
            // param churn
            e.setParams ((DafocusEngine::Space) (blockIdx % 3), (DafocusEngine::Echo) (blockIdx % 4),
                         phase == 6 ? 1.0f : 0.6f, 0.8f, phase == 5 ? 30.0 : 140.0);
            for (int i = 0; i < n; ++i)
            {
                float x = 0.0f;
                switch (phase)
                {
                    case 0: x = ((i / 50) % 2) ? 1.0f : -1.0f; break;         // full-scale square
                    case 1: x = 1.0f; break;                                   // DC
                    case 2: x = 0.0f; break;                                   // silence (denormals)
                    case 3: x = uni (rng); break;                              // white noise 0 dBFS
                    case 4: x = ((pos + i) % 4800 == 0) ? 1.0f : 0.0f; break;  // clicks
                    case 5: x = uni (rng) * 0.5f; break;
                    case 6: x = 0.3f * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (pos + i) / sr); break;
                    default: x = 1.0e-30f; break;                              // sub-denormal trickle
                }
                buf.setSample (0, i, x); buf.setSample (1, i, x);
            }
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, 0, n);
            juce::AudioBuffer<float> dry; dry.makeCopyOf (view);
            e.process (view);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i)
            {
                const float y = view.getSample (ch, i);
                if (! std::isfinite (y)) { ++failures; std::printf ("NaN/inf at sr=%.0f t=%.2fs\n", sr, (pos + i) / sr); goto next; }
                maxOut = juce::jmax (maxOut, std::abs (y));
                maxWet = juce::jmax (maxWet, std::abs (y - dry.getSample (ch, i)));
            }
            next:
            pos += n; ++blockIdx;
        }
        std::printf ("sr %.0f: max out %.2f dBFS (dry hits 0 dBFS)   max wet %.2f dBFS   %s\n", sr,
                     juce::Decibels::gainToDecibels (maxOut), juce::Decibels::gainToDecibels (maxWet),
                     maxWet < 1.0f ? "OK" : "WET TOO HOT");
        if (maxWet >= 1.0f) ++failures;
    }
    std::printf (failures ? "FAILED (%d)\n" : "ALL STABLE\n", failures);
    return failures ? 1 : 0;
}
