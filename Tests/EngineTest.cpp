// Quick offline sanity test for the Scrub engines.
// Feeds pink-ish noise with a strong 180 Hz resonance and a dull top end,
// prints what the engine does. No audio hardware needed.
#include <juce_dsp/juce_dsp.h>
#include "CleanupEngine.h"
#include "VocalModes.h"
#include <cstdio>

static float bandDb (const juce::AudioBuffer<float>& b, double sr, float hz, float q)
{
    juce::dsp::IIR::Filter<float> f;
    f.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, hz, q);
    double acc = 0.0; const int n = b.getNumSamples();
    const float* x = b.getReadPointer (0);
    for (int i = 0; i < n; ++i) { const float y = f.processSample (x[i]); if (i > n / 4) acc += y * y; }
    return juce::Decibels::gainToDecibels ((float) std::sqrt (acc / (n * 0.75)) + 1e-12f);
}

int main()
{
    const double sr = 48000.0; const int block = 256; const int seconds = 4;
    const int total = (int) sr * seconds;
    juce::Random rng (1234);

    // source: pink-ish noise (one-pole lowpassed white) + resonance at 180 Hz
    juce::AudioBuffer<float> src (2, total);
    juce::dsp::IIR::Filter<float> res; res.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, 180.0f, 8.0f);
    juce::dsp::IIR::Filter<float> dull; dull.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, 2500.0f, 0.7f);
    float lp = 0.0f;
    for (int i = 0; i < total; ++i)
    {
        const float w = rng.nextFloat() * 2.0f - 1.0f;
        lp += (w - lp) * 0.08f;                         // pink-ish
        float s = dull.processSample (lp * 0.5f);         // dull top
        s += res.processSample (w) * 2.2f;                // boomy resonance
        src.setSample (0, i, s); src.setSample (1, i, s);
    }

    auto run = [&] (const char* name, auto&& processor)
    {
        juce::AudioBuffer<float> out (2, total);
        out.makeCopyOf (src);
        bool bad = false; float peak = 0.0f;
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = juce::jmin (block, total - pos);
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, n);
            processor (view);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                {
                    const float v = view.getSample (ch, i);
                    if (! std::isfinite (v)) bad = true;
                    peak = juce::jmax (peak, std::abs (v));
                }
        }
        std::printf ("\n== %s ==  finite:%s  peak:%.2f dBFS\n", name, bad ? "NO!" : "yes", juce::Decibels::gainToDecibels (peak));
        const float probes[] = { 90.0f, 180.0f, 360.0f, 1000.0f, 2500.0f, 6000.0f, 9000.0f };
        for (float hz : probes)
            std::printf ("  %6.0f Hz   in %7.2f dB   out %7.2f dB   delta %+6.2f\n", hz,
                         bandDb (src, sr, hz, 4.0f), bandDb (out, sr, hz, 4.0f),
                         bandDb (out, sr, hz, 4.0f) - bandDb (src, sr, hz, 4.0f));
    };

    for (float amount : { 0.0f, 0.5f, 1.0f })
    {
        CleanupEngine e; e.prepare (sr, block, 2); e.setAmount (amount);
        char name[64]; std::snprintf (name, sizeof (name), "Cleanup amount %.1f", amount);
        run (name, [&] (juce::AudioBuffer<float>& b) { e.process (b); });
        std::printf ("  shelf %+.2f dB, max band cut %.2f dB at %.0f Hz\n", e.getShelfDb(),
                     [&] { float m = 0; for (int i = 0; i < CleanupEngine::numBands; ++i) m = juce::jmax (m, e.getBandReductionDb (i)); return m; }(),
                     [&] { float m = 0; int idx = 0; for (int i = 0; i < CleanupEngine::numBands; ++i) if (e.getBandReductionDb (i) > m) { m = e.getBandReductionDb (i); idx = i; } return e.getBandHz (idx); }());
    }

    for (int mode = 0; mode < 3; ++mode)
    {
        CleanupEngine e; e.prepare (sr, block, 2); e.setAmount (0.5f);
        VocalChain c; c.prepare (sr, block, 2); c.setMode (mode, true); c.setAmount (0.5f);
        char name[64]; std::snprintf (name, sizeof (name), "Pro mode %s (0.5 / 0.5)", FatboardModes::get (mode).name);
        run (name, [&] (juce::AudioBuffer<float>& b) { e.process (b); c.process (b); });
        std::printf ("  de-ess %.2f dB\n", c.getDeEssDb());
    }
    return 0;
}
