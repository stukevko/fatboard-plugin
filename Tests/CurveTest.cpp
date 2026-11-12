// Voice curve + Air test.
//  A) VoiceCurve at amount 1 must be Kevin's Parametric EQ 2: response table against the preset values.
//  B) amount 0 → bit-identical. Half way → roughly half the cut.
//  C) Air: lifts the top of a steady voice-like signal, holds back on a sibilant burst, 0 → bit-identical.
//  D) abuse: random block sizes, hot / silent input, knob jumps → finite, no runaway.
#include <juce_dsp/juce_dsp.h>
#include "VoiceCurve.h"
#include "AirEngine.h"
#include "CleanupEngine.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

static const double sr = 48000.0;

template <typename Fn>
static float gainAt (float hz, Fn&& process)
{
    const int total = (int) sr; juce::AudioBuffer<float> b (2, total);
    for (int i = 0; i < total; ++i) { const float s = 0.25f * (float) std::sin (2 * juce::MathConstants<double>::pi * hz * i / sr); b.setSample (0, i, s); b.setSample (1, i, s); }
    for (int pos = 0; pos < total; pos += 256) { juce::AudioBuffer<float> v (b.getArrayOfWritePointers(), 2, pos, juce::jmin (256, total - pos)); process (v); }
    double a = 0; for (int i = total / 2; i < total; ++i) a += (double) b.getSample (0, i) * b.getSample (0, i);
    return 10.0f * (float) std::log10 (a / (total / 2) / (0.25 * 0.25 * 0.5) + 1e-12);
}

// optional: CurveTest in.f32 out.f32 <scrub 0..1> <air 0..1>  → raw 48k stereo float file through the Scrub Pro voice path
static int runFile (const char* inPath, const char* outPath, float knob, float airAmt)
{
    FILE* f = std::fopen (inPath, "rb"); if (! f) return 1;
    std::fseek (f, 0, SEEK_END); const long bytes = std::ftell (f); std::fseek (f, 0, SEEK_SET);
    const int total = (int) (bytes / 8);
    std::vector<float> raw ((size_t) total * 2); if (std::fread (raw.data(), 4, raw.size(), f) != raw.size()) return 1; std::fclose (f);
    juce::AudioBuffer<float> buf (2, total);
    for (int i = 0; i < total; ++i) { buf.setSample (0, i, raw[(size_t) i * 2]); buf.setSample (1, i, raw[(size_t) i * 2 + 1]); }
    CleanupEngine eng; eng.prepare (sr, 512, 2); VoiceCurve c; c.prepare (sr, 2); AirEngine a; a.prepare (sr, 2);
    double guardSum = 0; int blocks = 0, guarded = 0;
    for (int pos = 0; pos < total; pos += 512)
    {
        const int n = juce::jmin (512, total - pos);
        juce::AudioBuffer<float> v (buf.getArrayOfWritePointers(), 2, pos, n);
        c.setAmount (FatboardCurve::fromKnob (knob), n); c.processPre (v); eng.setAmount (knob); eng.process (v); a.process (v, airAmt); c.processLowPass (v);
        guardSum += a.getGuardDb(); ++blocks; if (a.getGuardDb() > 1.0f) ++guarded;
    }
    std::printf ("guard: average %.2f dB, active in %.0f %% of the time\n", guardSum / blocks, 100.0 * guarded / blocks);
    for (int i = 0; i < total; ++i) { raw[(size_t) i * 2] = buf.getSample (0, i); raw[(size_t) i * 2 + 1] = buf.getSample (1, i); }
    f = std::fopen (outPath, "wb"); std::fwrite (raw.data(), 4, raw.size(), f); std::fclose (f);
    return 0;
}

int main (int argc, char** argv)
{
    if (argc == 5) return runFile (argv[1], argv[2], (float) std::atof (argv[3]), (float) std::atof (argv[4]));
    int fails = 0;

    // ---------- A ----------
    std::printf ("A  voice curve at 100 %% (= the Parametric EQ 2 preset)\n");
    auto curveGain = [] (float hz, float amount)
    {
        VoiceCurve c; c.prepare (sr, 2);
        return gainAt (hz, [&] (juce::AudioBuffer<float>& v) { c.setAmount (amount, v.getNumSamples()); c.processPre (v); c.processLowPass (v); });
    };
    for (float hz : { 40.0f, 80.0f, 131.0f, 192.0f, 313.0f, 634.0f, 1000.0f, 1354.0f, 2200.0f, 3130.0f, 5000.0f, 10000.0f, 15000.0f })
        std::printf ("   %7.0f Hz  %+6.1f dB\n", hz, curveGain (hz, 1.0f));
    // -3 dB from the high-pass plus ~1.3 dB from the wide 192 Hz cut next to it
    if (std::abs (curveGain (131.0f, 1.0f) - (-4.3f)) > 0.8f)  { ++fails; std::printf ("  FAIL: high-pass corner not at 131 Hz\n"); }
    if (std::abs (curveGain (10000.0f, 1.0f) - (-3.0f)) > 1.0f) { ++fails; std::printf ("  FAIL: low-pass corner not at 10 kHz\n"); }
    if (std::abs (curveGain (3130.0f, 1.0f) - (-3.4f)) > 0.6f)  { ++fails; std::printf ("  FAIL: 3.13 kHz cut wrong\n"); }
    if (std::abs (curveGain (1354.0f, 1.0f) - (-2.0f)) > 0.6f)  { ++fails; std::printf ("  FAIL: 1.35 kHz cut wrong\n"); }
    if (std::abs (curveGain (634.0f, 1.0f) - (-1.6f)) > 0.6f)   { ++fails; std::printf ("  FAIL: 634 Hz cut wrong\n"); }
    if (curveGain (5000.0f, 1.0f) < -1.0f)                      { ++fails; std::printf ("  FAIL: 5 kHz should be nearly untouched\n"); }

    // ---------- B ----------
    {
        juce::Random rng (4); juce::AudioBuffer<float> in (2, 48000), out;
        for (int i = 0; i < 48000; ++i) { const float s = rng.nextFloat() - 0.5f; in.setSample (0, i, s); in.setSample (1, i, -s); }
        out.makeCopyOf (in);
        VoiceCurve c; c.prepare (sr, 2);
        for (int pos = 0; pos < 48000; pos += 300) { juce::AudioBuffer<float> v (out.getArrayOfWritePointers(), 2, pos, juce::jmin (300, 48000 - pos)); c.setAmount (0.0f, v.getNumSamples()); c.processPre (v); c.processLowPass (v); }
        float md = 0; for (int i = 0; i < 48000; ++i) md = juce::jmax (md, std::abs (out.getSample (0, i) - in.getSample (0, i)));
        std::printf ("B  amount 0: max difference %.2g     3.13 kHz at 50 %%: %+.1f dB\n", md, curveGain (3130.0f, 0.5f));
        if (md != 0.0f) { ++fails; std::printf ("  FAIL: not transparent at 0\n"); }
        if (std::abs (curveGain (3130.0f, 0.5f) - (-1.7f)) > 0.5f) { ++fails; std::printf ("  FAIL: half amount is not half the cut\n"); }
    }

    // ---------- C ----------
    {
        auto airGain = [] (float hz, float air) { AirEngine a; a.prepare (sr, 2); return gainAt (hz, [&] (juce::AudioBuffer<float>& v) { a.process (v, air); }); };
        std::printf ("C  air 100 %% on a steady tone:  1 kHz %+.1f   3.5 kHz %+.1f   6 kHz %+.1f   9 kHz %+.1f dB (a lone 6 kHz tone looks like sibilance to the guard)\n",
                     airGain (1000, 1), airGain (3500, 1), airGain (6000, 1), airGain (9000, 1));
        std::printf ("   air 50 %%:                    1 kHz %+.1f   3.5 kHz %+.1f\n", airGain (1000, 0.5f), airGain (3500, 0.5f));
        if (std::abs (airGain (1000, 1)) > 1.2f) { ++fails; std::printf ("  FAIL: air touches 1 kHz\n"); }
        if (airGain (3500, 1) < 1.5f)            { ++fails; std::printf ("  FAIL: no mid air\n"); }

        // voice-like: 200 Hz saw (vowel) + a sibilant burst (7 kHz noise) every 0.5 s
        const int total = (int) sr * 4; juce::AudioBuffer<float> in (2, total), out; juce::Random rng (2);
        juce::dsp::IIR::Filter<float> sf; sf.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, 7000.0f, 1.5f);
        double ph = 0;
        for (int i = 0; i < total; ++i)
        {
            const double t = i / sr; ph += 200.0 / sr; if (ph >= 1) ph -= 1;
            const bool ess = std::fmod (t, 0.5) > 0.35;
            const float x = ess ? sf.processSample (rng.nextFloat() * 2 - 1) * 0.6f : (float) (2 * ph - 1) * 0.25f + sf.processSample (0.0f);
            in.setSample (0, i, x); in.setSample (1, i, x);
        }
        out.makeCopyOf (in);
        AirEngine a; a.prepare (sr, 2); float maxGuard = 0;
        for (int pos = 0; pos < total; pos += 256) { juce::AudioBuffer<float> v (out.getArrayOfWritePointers(), 2, pos, juce::jmin (256, total - pos)); a.process (v, 1.0f); maxGuard = juce::jmax (maxGuard, a.getGuardDb()); }
        auto rms = [&] (const juce::AudioBuffer<float>& b, double t0, double t1) { double s = 0; const int a0 = (int) (t0 * sr), a1 = (int) (t1 * sr); for (int i = a0; i < a1; ++i) s += (double) b.getSample (0, i) * b.getSample (0, i); return 10.0f * (float) std::log10 (s / (a1 - a0) + 1e-12); };
        const float essLift = rms (out, 2.40, 2.49) - rms (in, 2.40, 2.49);
        std::printf ("   sibilant burst with air 100 %%: %+.1f dB (the same burst without guard: about +8), guard at %.1f dB\n", essLift, maxGuard);
        if (! (essLift < 1.5f)) { ++fails; std::printf ("  FAIL: guard does not hold sibilance back\n"); }

        juce::AudioBuffer<float> z; z.makeCopyOf (in); AirEngine b; b.prepare (sr, 2);
        for (int pos = 0; pos < total; pos += 256) { juce::AudioBuffer<float> v (z.getArrayOfWritePointers(), 2, pos, juce::jmin (256, total - pos)); b.process (v, 0.0f); }
        float md = 0; for (int i = 0; i < total; ++i) md = juce::jmax (md, std::abs (z.getSample (0, i) - in.getSample (0, i)));
        std::printf ("   air 0: max difference %.2g\n", md);
        if (md != 0.0f) { ++fails; std::printf ("  FAIL: air not transparent at 0\n"); }
    }

    // ---------- D ----------
    {
        VoiceCurve c; c.prepare (sr, 2); AirEngine a; a.prepare (sr, 2);
        juce::Random rng (1); float peak = 0; bool bad = false;
        for (int rep = 0; rep < 800; ++rep)
        {
            const int n = 1 + rng.nextInt (700); juce::AudioBuffer<float> b (2, n); const int kind = (rep / 50) % 4;
            for (int i = 0; i < n; ++i) { float s = 0; if (kind == 0) s = (rng.nextFloat() * 2 - 1) * 4.0f; else if (kind == 2) s = (i % 97 == 0) ? 1.0f : 0.0f; else if (kind == 3) s = (rng.nextFloat() * 2 - 1) * 1e-5f; b.setSample (0, i, s); b.setSample (1, i, -s); }
            c.setAmount (rng.nextFloat(), n); c.processPre (b); a.process (b, rng.nextFloat()); c.processLowPass (b);
            for (int i = 0; i < n; ++i) { const float v = b.getSample (0, i); if (! std::isfinite (v)) bad = true; peak = juce::jmax (peak, std::abs (v)); }
        }
        std::printf ("D  abuse: peak %.1f (input peak 4.0), finite: %s\n", peak, bad ? "NO" : "yes");
        if (bad || peak > 20.0f) { ++fails; std::printf ("  FAIL: unstable\n"); }
    }

    std::printf (fails == 0 ? "\nALL OK\n" : "\n%d FAILED\n", fails);
    return fails;
}
