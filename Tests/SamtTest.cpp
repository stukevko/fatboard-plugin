// Samt offline check: transparency at zero, static curve, attack, two-part release, memory, link, warmth, mix.
#include "SamtEngine.h"
#include <cstdio>
#include <vector>
static const double sr = 48000.0;
static const double PI2 = juce::MathConstants<double>::twoPi;
static juce::AudioBuffer<float> tone (double hz, float ampL, float ampR, double secs) { const int n = (int) (secs * sr); juce::AudioBuffer<float> b (2, n); for (int i = 0; i < n; ++i) { const float s = (float) std::sin (PI2 * hz * i / sr); b.setSample (0, i, s * ampL); b.setSample (1, i, s * ampR); } return b; }
static void run (SamtEngine& e, juce::AudioBuffer<float>& b, std::vector<float>* grTrace = nullptr)
{
    const int blk = 64;
    for (int s = 0; s < b.getNumSamples(); s += blk) { const int n = juce::jmin (blk, b.getNumSamples() - s); juce::AudioBuffer<float> v (b.getArrayOfWritePointers(), 2, s, n); e.process (v); if (grTrace) grTrace->push_back (e.getReductionDb()); }
}
static float rmsDb (const juce::AudioBuffer<float>& b, int ch, double a, double z) { return juce::Decibels::gainToDecibels (b.getRMSLevel (ch, (int) (a * sr), (int) ((z - a) * sr)), -140.0f); }

int main()
{
    int fails = 0; auto fail = [&] (const char* m) { ++fails; std::printf ("  FAIL: %s\n", m); };

    {   // nothing set = nothing changed
        SamtEngine e; e.prepare (sr, 64, 2); e.setParams (0.0f, 0.0f, SamtEngine::Compress, 0.0f, 1.0f);
        auto b = tone (220.0, 0.7f, 0.5f, 1.0); const auto ref = b; run (e, b);
        float diff = 0; for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < b.getNumSamples(); ++i) diff = juce::jmax (diff, std::abs (b.getSample (ch, i) - ref.getSample (ch, i)));
        std::printf ("all at zero: biggest difference to the input %.2e\n", diff); if (diff != 0.0f) fail ("not bit-identical at zero");
    }
    {   // steady tone: reduction matches the curve, Limit holds harder than Compress
        float got[2];
        for (int m = 0; m < 2; ++m)
        {
            SamtEngine e; e.prepare (sr, 64, 2); e.setParams (0.6f, 0.0f, (SamtEngine::Mode) m, 0.0f, 1.0f);
            auto b = tone (440.0, 0.5f, 0.5f, 4.0); run (e, b);
            got[m] = (juce::Decibels::gainToDecibels (0.5f) - 3.01f) - rmsDb (b, 0, 3.0, 4.0);
            const float want = SamtEngine::curveDb (juce::Decibels::gainToDecibels (0.5f), 0.6f, (SamtEngine::Mode) m);
            std::printf ("%s, tone at -6 dBFS, Peak Reduction 60 %%: %.1f dB down (curve says %.1f)\n", m ? "Limit   " : "Compress", got[m], want);
            // the curve is for the peak of the wave; between the peaks the cell recovers a little, so the average sits 1-2 dB under it
            if (got[m] > want + 0.2f || got[m] < want - 2.2f) fail ("steady reduction off the curve");
        }
        if (! (got[1] > got[0] + 3.0f)) fail ("Limit does not hold harder than Compress");
    }
    {   // attack and the two-part release, after a short and after a long stretch of compression
        auto measure = [&] (double holdSecs, float& attackMs, float& after100, float& after1s, float& peakGr)
        {
            SamtEngine e; e.prepare (sr, 64, 2); e.setParams (0.7f, 0.0f, SamtEngine::Compress, 0.0f, 1.0f);
            const double pre = 0.5, post = 6.0; const int n = (int) ((pre + holdSecs + post) * sr);
            juce::AudioBuffer<float> b (2, n);
            for (int i = 0; i < n; ++i) { const double t = i / sr; const float a = (t >= pre && t < pre + holdSecs) ? 0.7f : 0.004f; const float s = a * (float) std::sin (PI2 * 500.0 * t); b.setSample (0, i, s); b.setSample (1, i, s); }
            std::vector<float> gr; run (e, b, &gr);
            auto at = [&] (double t) { return gr[(size_t) juce::jlimit (0, (int) gr.size() - 1, (int) (t * sr / 64))]; };
            peakGr = at (pre + holdSecs - 0.01);
            attackMs = 0; for (double t = pre; t < pre + 0.3; t += 64 / sr) if (at (t) >= 0.63f * peakGr) { attackMs = (float) ((t - pre) * 1000.0); break; }
            after100 = at (pre + holdSecs + 0.1) / peakGr; after1s = at (pre + holdSecs + 1.0) / peakGr;
        };
        float a1, r100a, r1a, pk1, a2, r100b, r1b, pk2;
        measure (0.08, a1, r100a, r1a, pk1); measure (6.0, a2, r100b, r1b, pk2);
        std::printf ("short hit (80 ms):  %.1f dB down, of that left after 100 ms %.0f %%, after 1 s %.0f %%\n", pk1, r100a * 100, r1a * 100);
        std::printf ("long note (6 s):    %.1f dB down, reached 63 %% after %.0f ms, left after 100 ms %.0f %%, after 1 s %.0f %%\n", pk2, a2, r100b * 100, r1b * 100);
        if (a2 < 4.0f || a2 > 40.0f) fail ("attack not around 10-30 ms");
        if (! (r100b > 0.35f && r100b < 0.7f)) fail ("after a long note about half should still be held 100 ms later");
        if (! (r1b > 0.2f)) fail ("after a long note the slow half should still hold after 1 s");
        if (! (r100a < r100b - 0.1f && r1a < 0.1f)) fail ("a short hit should let go much faster than a long note");
    }
    {   // linked: louder left channel, both channels get the same gain, so the balance stays
        SamtEngine e; e.prepare (sr, 64, 2); e.setParams (0.7f, 0.0f, SamtEngine::Compress, 0.0f, 1.0f);
        auto b = tone (300.0, 0.8f, 0.2f, 3.0); run (e, b);
        const float bal = rmsDb (b, 0, 2.0, 3.0) - rmsDb (b, 1, 2.0, 3.0);
        std::printf ("stereo link: left-right balance %.2f dB (input 12.04)\n", bal); if (std::abs (bal - 12.04f) > 0.05f) fail ("stereo image moved");
    }
    {   // rumble below the voice does not duck it
        SamtEngine e; e.prepare (sr, 64, 2); e.setParams (0.5f, 0.0f, SamtEngine::Compress, 0.0f, 1.0f);
        auto b = tone (35.0, 0.7f, 0.7f, 2.0); std::vector<float> gr; run (e, b, &gr);
        SamtEngine e2; e2.prepare (sr, 64, 2); e2.setParams (0.5f, 0.0f, SamtEngine::Compress, 0.0f, 1.0f);
        auto c = tone (800.0, 0.7f, 0.7f, 2.0); std::vector<float> gr2; run (e2, c, &gr2);
        std::printf ("35 Hz rumble at -3 dBFS: %.1f dB down   (800 Hz at the same level: %.1f dB)\n", gr.back(), gr2.back());
        if (! (gr.back() < gr2.back() * 0.5f)) fail ("rumble ducks the signal");
    }
    {   // warmth: adds a little 2nd harmonic, level stays, no DC
        auto h2 = [&] (float warmth, float& dc, float& level)
        {
            SamtEngine e; e.prepare (sr, 64, 2); e.setParams (0.0f, 0.0f, SamtEngine::Compress, warmth, 1.0f);
            auto b = tone (1000.0, 0.25f, 0.25f, 2.0); run (e, b);
            double re1 = 0, im1 = 0, re2 = 0, im2 = 0, sum = 0; const int a = (int) sr, n = (int) sr;
            for (int i = 0; i < n; ++i) { const double x = b.getSample (0, a + i), ph = PI2 * 1000.0 * (a + i) / sr; re1 += x * std::cos (ph); im1 += x * std::sin (ph); re2 += x * std::cos (2 * ph); im2 += x * std::sin (2 * ph); sum += x; }
            dc = (float) std::abs (sum / n); level = juce::Decibels::gainToDecibels ((float) (2.0 * std::hypot (re1, im1) / n)) - juce::Decibels::gainToDecibels (0.25f);
            return juce::Decibels::gainToDecibels ((float) (std::hypot (re2, im2) / std::hypot (re1, im1)), -160.0f);
        };
        float dc0, l0, dc1, l1, dc2, l2; const float a = h2 (0.0f, dc0, l0), b = h2 (0.3f, dc1, l1), c = h2 (1.0f, dc2, l2);
        std::printf ("warmth, 1 kHz at -12 dBFS: 2nd harmonic %.0f dB / %.0f dB / %.0f dB at 0 / 30 / 100 %%, level change %.2f / %.2f dB, DC %.1e\n", a, b, c, l1, l2, dc2);
        if (a > -120.0f) fail ("clean setting is not clean");
        if (! (b > -70.0f && b < -35.0f && c > b && c < -20.0f)) fail ("warmth out of range");
        if (std::abs (l1) > 0.3f || std::abs (l2) > 1.0f || dc2 > 1.0e-3f) fail ("warmth changes level or leaves DC");
    }
    {   // mix 0 = dry, gain is gain, silence stays silence, no NaN on hot input
        SamtEngine e; e.prepare (sr, 64, 2); e.setParams (1.0f, 12.0f, SamtEngine::Limit, 1.0f, 0.0f);
        auto b = tone (500.0, 0.6f, 0.6f, 1.0); const auto ref = b; run (e, b);
        float diff = 0; for (int i = 0; i < b.getNumSamples(); ++i) diff = juce::jmax (diff, std::abs (b.getSample (0, i) - ref.getSample (0, i)));
        std::printf ("mix 0: difference to dry %.1e\n", diff); if (diff > 1.0e-6f) fail ("mix 0 is not dry");
        SamtEngine g; g.prepare (sr, 64, 2); g.setParams (0.0f, 6.0f, SamtEngine::Compress, 0.0f, 1.0f);
        auto c = tone (500.0, 0.2f, 0.2f, 1.0); run (g, c); const float up = rmsDb (c, 0, 0.5, 1.0) - (juce::Decibels::gainToDecibels (0.2f) - 3.01f);
        std::printf ("gain +6 dB: %.2f dB\n", up); if (std::abs (up - 6.0f) > 0.05f) fail ("gain");
        SamtEngine h; h.prepare (sr, 64, 2); h.setParams (1.0f, 24.0f, SamtEngine::Limit, 1.0f, 1.0f);
        juce::AudioBuffer<float> z (2, 48000); z.clear(); run (h, z); if (z.getMagnitude (0, 48000) > 1.0e-6f) fail ("silence in, noise out");
        auto hot = tone (90.0, 4.0f, 4.0f, 1.0); run (h, hot); bool bad = false; for (int i = 0; i < hot.getNumSamples(); ++i) if (! std::isfinite (hot.getSample (0, i))) bad = true;
        if (bad) fail ("NaN on hot input");
    }
    std::printf (fails == 0 ? "\nALL OK\n" : "\n%d FAILED\n", fails);
    return fails;
}
