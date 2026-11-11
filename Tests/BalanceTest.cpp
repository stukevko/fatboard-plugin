// Auto Balance + Leveler test.
//  A) dark, boomy "vocal" with loud and quiet phrases and pauses
//     → low-mid down, presence/air up, phrases closer together, overall loudness kept.
//  B) a voice that already sits on the target → left alone (tone moves < 1 dB).
//  C) steady 1 kHz tone → no pumping (level ripple after settling < 0.1 dB).
//  D) pause: tone corrections hold still while nothing is playing.
//  E) amount 0 → bit-identical.  F) no NaN / runaway with hot or silent input.
#include <juce_dsp/juce_dsp.h>
#include "AutoBalance.h"
#include <cstdio>
#include <vector>

static const double sr = 48000.0;
static const float edges[5] = { 150.0f, 450.0f, 1800.0f, 5000.0f, 12000.0f };

static float bandDb (const juce::AudioBuffer<float>& b, int band, int from, int to)
{
    using C = juce::dsp::IIR::Coefficients<float>;
    juce::dsp::IIR::Filter<float> hp[4], lp[4];
    for (int k = 0; k < 4; ++k) { hp[k].coefficients = C::makeHighPass (sr, edges[band], 0.707f); lp[k].coefficients = C::makeLowPass (sr, edges[band + 1], 0.707f); }
    double a = 0; int cnt = 0;
    for (int i = juce::jmax (0, from - 4000); i < to; ++i) { float y = b.getSample (0, i); for (int k = 0; k < 4; ++k) y = lp[k].processSample (hp[k].processSample (y)); if (i >= from) { a += y * y; ++cnt; } }
    return 10.0f * (float) std::log10 (a / cnt + 1e-12);
}
static float rmsDb (const juce::AudioBuffer<float>& b, int from, int to)
{
    double a = 0; for (int i = from; i < to; ++i) a += (double) b.getSample (0, i) * b.getSample (0, i);
    return 10.0f * (float) std::log10 (a / (to - from) + 1e-12);
}

// noise voice: four bands with given levels relative to mid (dB), syllables at 4.5 Hz,
// phrase pattern: 2 s loud, 0.5 s pause, 2 s quiet (-quietDb), 0.5 s pause ...
static juce::AudioBuffer<float> makeVoice (float lm, float pr, float ar, float quietDb, int seconds, int seed)
{
    using C = juce::dsp::IIR::Coefficients<float>;
    const int total = (int) sr * seconds;
    juce::AudioBuffer<float> out (2, total);
    juce::Random rng (seed);
    juce::dsp::IIR::Filter<float> hp[4][4], lp[4][4];
    for (int b = 0; b < 4; ++b) for (int k = 0; k < 4; ++k) { hp[b][k].coefficients = C::makeHighPass (sr, edges[b], 0.707f); lp[b][k].coefficients = C::makeLowPass (sr, edges[b + 1], 0.707f); }
    const float g[4] = { juce::Decibels::decibelsToGain (lm), 1.0f, juce::Decibels::decibelsToGain (pr), juce::Decibels::decibelsToGain (ar) };
    // calibrate: run white noise through each band once so all four start at equal power
    float norm[4];
    {
        juce::Random cal (99);
        double pw[4] = { 0, 0, 0, 0 };
        for (int i = 0; i < 96000; ++i)
        {
            const float w = cal.nextFloat() * 2 - 1;
            for (int b = 0; b < 4; ++b) { float y = w; for (int k = 0; k < 4; ++k) y = lp[b][k].processSample (hp[b][k].processSample (y)); pw[b] += (double) y * y; }
        }
        for (int b = 0; b < 4; ++b) { norm[b] = 0.02f / (float) std::sqrt (pw[b] / 96000.0); for (int k = 0; k < 4; ++k) { hp[b][k].reset(); lp[b][k].reset(); } }
    }
    for (int i = 0; i < total; ++i)
    {
        const double t = i / sr;
        const float w = rng.nextFloat() * 2 - 1;
        float s = 0; for (int b = 0; b < 4; ++b) { float y = w; for (int k = 0; k < 4; ++k) y = lp[b][k].processSample (hp[b][k].processSample (y)); s += y * g[b] * norm[b] * 40.0f; }
        const double ph = std::fmod (t, 5.0);
        float env = 0.0f;
        if (ph < 2.0) env = 1.0f; else if (ph >= 2.5 && ph < 4.5) env = juce::Decibels::decibelsToGain (-quietDb);
        const float syl = 0.55f + 0.45f * (float) std::sin (2 * juce::MathConstants<double>::pi * 4.5 * t);
        s *= env * syl * 0.25f;
        out.setSample (0, i, s); out.setSample (1, i, s);
    }
    return out;
}

static void run (AutoBalance& ab, juce::AudioBuffer<float>& buf, float amount, int block = 256)
{
    for (int pos = 0; pos < buf.getNumSamples(); pos += block)
    {
        const int n = juce::jmin (block, buf.getNumSamples() - pos);
        juce::AudioBuffer<float> v (buf.getArrayOfWritePointers(), 2, pos, n);
        ab.process (v, amount);
    }
}

#include "CleanupEngine.h"
#include "VocalModes.h"
// optional: BalanceTest in.f32 out.f32  → run a raw 48k stereo float file through the whole Scrub Pro vocal path
static int runFile (const char* inPath, const char* outPath)
{
    FILE* f = std::fopen (inPath, "rb"); if (! f) return 1;
    std::fseek (f, 0, SEEK_END); const long bytes = std::ftell (f); std::fseek (f, 0, SEEK_SET);
    const int total = (int) (bytes / 8);
    std::vector<float> raw ((size_t) total * 2); if (std::fread (raw.data(), 4, raw.size(), f) != raw.size()) return 1; std::fclose (f);
    juce::AudioBuffer<float> buf (2, total);
    for (int i = 0; i < total; ++i) { buf.setSample (0, i, raw[(size_t) i * 2]); buf.setSample (1, i, raw[(size_t) i * 2 + 1]); }
    CleanupEngine eng; eng.prepare (sr, 512, 2); eng.setAmount (0.5f);
    AutoBalance ab; ab.prepare (sr, 2); ab.setMode (1);
    VocalChain chain; chain.prepare (sr, 512, 2); chain.setMode (1, true); chain.setAmount (0.5f);
    for (int pos = 0; pos < total; pos += 512)
    {
        const int n = juce::jmin (512, total - pos);
        juce::AudioBuffer<float> v (buf.getArrayOfWritePointers(), 2, pos, n);
        eng.process (v); ab.processTone (v, 0.5f); chain.process (v); ab.processLevel (v, 0.5f);
        if ((pos / 512) % 94 == 0) std::printf ("%5.1fs  low-mid %+4.1f  presence %+4.1f  air %+4.1f  leveler %4.1f  makeup %+4.1f\n", pos / sr, ab.getLowMidDb(), ab.getPresenceDb(), ab.getAirDb(), ab.getLevelDb(), ab.getMakeupDb());
    }
    for (int i = 0; i < total; ++i) { raw[(size_t) i * 2] = buf.getSample (0, i); raw[(size_t) i * 2 + 1] = buf.getSample (1, i); }
    f = std::fopen (outPath, "wb"); std::fwrite (raw.data(), 4, raw.size(), f); std::fclose (f);
    return 0;
}

int main (int argc, char** argv)
{
    if (argc == 3) return runFile (argv[1], argv[2]);
    int fails = 0;
    const auto& tgt = FatboardBalance::Clear;

    // ---------- A: dark + boomy + uneven ----------
    {
        auto in = makeVoice (9.0f, -16.0f, -24.0f, 9.0f, 20, 3);
        juce::AudioBuffer<float> out; out.makeCopyOf (in);
        AutoBalance ab; ab.prepare (sr, 2); ab.setMode (1);
        run (ab, out, 1.0f);
        const int a0 = (int) sr * 10, a1 = (int) sr * 20;
        auto rel = [&] (const juce::AudioBuffer<float>& b, int band) { return bandDb (b, band, a0, a1) - bandDb (b, 1, a0, a1); };
        std::printf ("A  target        low-mid %+5.1f  presence %+5.1f  air %+5.1f   (dB rel. mid)\n", tgt.lowMid, tgt.presence, tgt.air);
        std::printf ("A  in            low-mid %+5.1f  presence %+5.1f  air %+5.1f\n", rel (in, 0), rel (in, 2), rel (in, 3));
        std::printf ("A  out           low-mid %+5.1f  presence %+5.1f  air %+5.1f\n", rel (out, 0), rel (out, 2), rel (out, 3));
        std::printf ("A  filters       low-mid %+5.1f  presence %+5.1f  air %+5.1f   leveler %.1f dB, makeup %+.1f dB\n",
                     ab.getLowMidDb(), ab.getPresenceDb(), ab.getAirDb(), ab.getLevelDb(), ab.getMakeupDb());
        if (! (rel (out, 0) < rel (in, 0) - 3.0f)) { ++fails; std::printf ("  FAIL: low-mid not reduced\n"); }
        if (! (rel (out, 2) > rel (in, 2) + 4.0f)) { ++fails; std::printf ("  FAIL: presence not lifted\n"); }
        if (! (rel (out, 3) > rel (in, 3) + 4.0f)) { ++fails; std::printf ("  FAIL: air not lifted\n"); }
        // phrases: loud 10..12 s, quiet 12.5..14.5 s
        const int l0 = (int) (sr * 10.2), l1 = (int) (sr * 11.9), q0 = (int) (sr * 12.7), q1 = (int) (sr * 14.4);
        const float dIn = rmsDb (in, l0, l1) - rmsDb (in, q0, q1), dOut = rmsDb (out, l0, l1) - rmsDb (out, q0, q1);
        std::printf ("A  loud vs quiet phrase: in %.1f dB apart, out %.1f dB apart\n", dIn, dOut);
        if (! (dOut < dIn - 2.0f)) { ++fails; std::printf ("  FAIL: leveler does not even out phrases\n"); }
        // loudness: compare the 450–1800 band (tone change excluded)
        const float loud = bandDb (out, 1, a0, a1) - bandDb (in, 1, a0, a1);
        std::printf ("A  mid-band loudness change %+.1f dB\n", loud);
        if (! (std::abs (loud) < 2.5f)) { ++fails; std::printf ("  FAIL: not level-neutral\n"); }
    }

    // ---------- B: already right ----------
    {
        // the generator's bands overlap a little: measure once, correct the request, generate again
        auto probe = makeVoice (tgt.lowMid, tgt.presence, tgt.air, 0.0f, 12, 5);
        const int n = probe.getNumSamples();
        const float m = bandDb (probe, 1, 0, n);
        auto in = makeVoice (2 * tgt.lowMid - (bandDb (probe, 0, 0, n) - m), 2 * tgt.presence - (bandDb (probe, 2, 0, n) - m), 2 * tgt.air - (bandDb (probe, 3, 0, n) - m), 0.0f, 12, 5);
        juce::AudioBuffer<float> out; out.makeCopyOf (in);
        AutoBalance ab; ab.prepare (sr, 2); ab.setMode (1);
        run (ab, out, 1.0f);
        std::printf ("B  balanced voice: filters low-mid %+.2f  presence %+.2f  air %+.2f\n", ab.getLowMidDb(), ab.getPresenceDb(), ab.getAirDb());
        if (std::abs (ab.getLowMidDb()) > 1.0f || std::abs (ab.getPresenceDb()) > 1.0f || std::abs (ab.getAirDb()) > 1.0f)
        { ++fails; std::printf ("  FAIL: touches a voice that is already balanced\n"); }
    }

    // ---------- C: no pumping on a steady tone ----------
    {
        const int total = (int) sr * 8;
        juce::AudioBuffer<float> out (2, total);
        for (int i = 0; i < total; ++i) { const float s = 0.3f * (float) std::sin (2 * juce::MathConstants<double>::pi * 1000.0 * i / sr); out.setSample (0, i, s); out.setSample (1, i, s); }
        AutoBalance ab; ab.prepare (sr, 2); ab.setMode (1);
        run (ab, out, 1.0f);
        float mn = 100, mx = -100;
        for (int w = (int) sr * 6; w + 2400 <= total; w += 2400) { const float d = rmsDb (out, w, w + 2400); mn = juce::jmin (mn, d); mx = juce::jmax (mx, d); }
        std::printf ("C  steady tone: level ripple after settling %.3f dB\n", mx - mn);
        if (! (mx - mn < 0.1f)) { ++fails; std::printf ("  FAIL: pumping\n"); }
    }

    // ---------- D: holds still in a pause ----------
    {
        auto in = makeVoice (9.0f, -16.0f, -24.0f, 0.0f, 12, 7);
        for (int i = (int) sr * 7; i < in.getNumSamples(); ++i) { const float bleed = in.getSample (0, i) * 0.003f; in.setSample (0, i, bleed); in.setSample (1, i, bleed); } // -50 dB bleed
        AutoBalance ab; ab.prepare (sr, 2); ab.setMode (1);
        juce::AudioBuffer<float> a (in.getArrayOfWritePointers(), 2, 0, (int) sr * 7);
        run (ab, a, 1.0f);
        const float p0 = ab.getPresenceDb(), l0 = ab.getLowMidDb();
        juce::AudioBuffer<float> b (in.getArrayOfWritePointers(), 2, (int) sr * 7, (int) sr * 5);
        run (ab, b, 1.0f);
        std::printf ("D  5 s pause with bleed: presence %+.2f -> %+.2f, low-mid %+.2f -> %+.2f\n", p0, ab.getPresenceDb(), l0, ab.getLowMidDb());
        if (std::abs (ab.getPresenceDb() - p0) > 0.3f || std::abs (ab.getLowMidDb() - l0) > 0.3f) { ++fails; std::printf ("  FAIL: drifts in pauses\n"); }
    }

    // ---------- E: amount 0 = untouched ----------
    {
        auto in = makeVoice (9.0f, -16.0f, -24.0f, 9.0f, 6, 11);
        juce::AudioBuffer<float> out; out.makeCopyOf (in);
        AutoBalance ab; ab.prepare (sr, 2); ab.setMode (1);
        run (ab, out, 0.0f);
        float md = 0; for (int i = 0; i < in.getNumSamples(); ++i) md = juce::jmax (md, std::abs (out.getSample (0, i) - in.getSample (0, i)));
        std::printf ("E  amount 0: max difference %.2g\n", md);
        if (md != 0.0f) { ++fails; std::printf ("  FAIL: not transparent at 0\n"); }
    }

    // ---------- F: abuse ----------
    {
        AutoBalance ab; ab.prepare (sr, 2); ab.setMode (0);
        juce::Random rng (1); float peak = 0; bool bad = false;
        for (int rep = 0; rep < 600; ++rep)
        {
            const int n = 1 + rng.nextInt (700);
            juce::AudioBuffer<float> b (2, n);
            const int kind = (rep / 60) % 4;
            for (int i = 0; i < n; ++i)
            {
                float s = 0; if (kind == 0) s = (rng.nextFloat() * 2 - 1) * 4.0f; else if (kind == 1) s = 0.0f; else if (kind == 2) s = (i % 97 == 0) ? 1.0f : 0.0f; else s = (rng.nextFloat() * 2 - 1) * 1.0e-5f;
                b.setSample (0, i, s); b.setSample (1, i, -s * 0.5f);
            }
            ab.process (b, rng.nextFloat());
            for (int i = 0; i < n; ++i) { const float v = b.getSample (0, i); if (! std::isfinite (v)) bad = true; peak = juce::jmax (peak, std::abs (v)); }
        }
        std::printf ("F  abuse: peak %.1f (input peak 4.0), finite: %s\n", peak, bad ? "NO" : "yes");
        if (bad || peak > 12.0f) { ++fails; std::printf ("  FAIL: unstable\n"); }
    }

    std::printf (fails == 0 ? "\nALL OK\n" : "\n%d FAILED\n", fails);
    return fails;
}
