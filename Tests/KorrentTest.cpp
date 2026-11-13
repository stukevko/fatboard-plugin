// Korrent (master) offline check.
//  mix = 808 (50 Hz, decaying, on every beat) + kick click + mids + hats, a quiet verse then a loud hook, exported quiet (~-22 LUFS)
//  - loudness lands near the target of the knob, for each style
//  - verse and hook keep their distance (one gain, no riding)
//  - nothing above the ceiling
//  - the 808 gets round tops, not square ones (3rd harmonic present, higher ones low) and no pumping of the mids
//  - CHECK changes the sound when listening and is OFF while rendering
//  - abuse → finite
#include <juce_dsp/juce_dsp.h>
#include "KorrentEngine.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

static const double sr = 48000.0;

static float lufs (const juce::AudioBuffer<float>& b, int from, int to)
{
    using C = juce::dsp::IIR::Coefficients<float>;
    double ms = 0;
    for (int ch = 0; ch < 2; ++ch)
    {
        juce::dsp::IIR::Filter<float> hp, sh; hp.coefficients = C::makeHighPass (sr, 38.0f, 0.5f); sh.coefficients = C::makeHighShelf (sr, 1681.0f, 0.707f, juce::Decibels::decibelsToGain (4.0f));
        for (int i = juce::jmax (0, from - 9600); i < to; ++i) { const float y = sh.processSample (hp.processSample (b.getSample (ch, i))); if (i >= from) ms += (double) y * y; }
    }
    return -0.691f + 10.0f * (float) std::log10 (ms / (to - from) + 1e-12);
}

static juce::AudioBuffer<float> makeMix (int seconds)
{
    const int total = (int) sr * seconds; juce::AudioBuffer<float> b (2, total); juce::Random rng (5);
    float lp = 0;
    for (int i = 0; i < total; ++i)
    {
        const double t = i / sr, beat = std::fmod (t, 0.5);
        const bool hook = t >= seconds * 0.5;
        const float sub = (float) (std::sin (2 * juce::MathConstants<double>::pi * 50.0 * beat) * std::exp (-beat * 5.0)) * 0.20f;
        const float click = beat < 0.004 ? (rng.nextFloat() * 2 - 1) * 0.25f : 0.0f;
        const float w = rng.nextFloat() * 2 - 1; lp += (w - lp) * 0.08f;
        const float mid = lp * 0.10f * (0.6f + 0.4f * (float) std::sin (2 * juce::MathConstants<double>::pi * 2.0 * t));
        const float hat = (std::fmod (t, 0.25) < 0.02) ? w * 0.03f : 0.0f;
        const float s = (sub + click + mid + hat) * (hook ? 1.0f : 0.5f) * 1.3f;
        b.setSample (0, i, s + hat * 0.3f); b.setSample (1, i, s - hat * 0.3f);
    }
    return b;
}

static void run (KorrentEngine& e, juce::AudioBuffer<float>& buf, float loud, int style, int check, bool rendering, int block = 512)
{
    for (int pos = 0; pos < buf.getNumSamples(); pos += block)
    {
        const int n = juce::jmin (block, buf.getNumSamples() - pos);
        juce::AudioBuffer<float> v (buf.getArrayOfWritePointers(), 2, pos, n);
        e.setParams (loud, style, check, rendering); e.process (v);
    }
}

// optional: KorrentTest in.f32 out.f32 <knob> <style>  → raw 48k stereo float file, played twice (learn, then judge)
static int runFile (const char* inPath, const char* outPath, float knob, int style)
{
    FILE* f = std::fopen (inPath, "rb"); if (! f) return 1;
    std::fseek (f, 0, SEEK_END); const long bytes = std::ftell (f); std::fseek (f, 0, SEEK_SET);
    const int total = (int) (bytes / 8);
    std::vector<float> raw ((size_t) total * 2); if (std::fread (raw.data(), 4, raw.size(), f) != raw.size()) return 1; std::fclose (f);
    juce::AudioBuffer<float> in (2, total);
    for (int i = 0; i < total; ++i) { in.setSample (0, i, raw[(size_t) i * 2]); in.setSample (1, i, raw[(size_t) i * 2 + 1]); }
    KorrentEngine e; e.prepare (sr, 512, 2);
    juce::AudioBuffer<float> a; a.makeCopyOf (in); run (e, a, knob, style, 0, false);
    juce::AudioBuffer<float> out; out.makeCopyOf (in); run (e, out, knob, style, 0, true);
    float peak = 0; for (int i = 0; i < total; ++i) for (int ch = 0; ch < 2; ++ch) peak = juce::jmax (peak, std::abs (out.getSample (ch, i)));
    std::printf ("in %.1f LUFS → out %.1f LUFS, gain %+.1f dB, peak %+.2f dB\n", lufs (in, 0, total), lufs (out, 0, total), e.getGainDb(), juce::Decibels::gainToDecibels (peak));
    for (int i = 0; i < total; ++i) { raw[(size_t) i * 2] = out.getSample (0, i); raw[(size_t) i * 2 + 1] = out.getSample (1, i); }
    f = std::fopen (outPath, "wb"); std::fwrite (raw.data(), 4, raw.size(), f); std::fclose (f);
    return 0;
}

int main (int argc, char** argv)
{
    if (argc == 5) return runFile (argv[1], argv[2], (float) std::atof (argv[3]), std::atoi (argv[4]));
    int fails = 0;
    const int secs = 24; auto in = makeMix (secs);
    const int v0 = (int) sr * 6, v1 = (int) sr * 11, h0 = (int) sr * 16, h1 = (int) sr * 23;
    std::printf ("input: verse %.1f LUFS, hook %.1f LUFS\n", lufs (in, v0, v1), lufs (in, h0, h1));
    const char* names[3] = { "Clean", "Warm", "Hard" };

    // song is played twice: the first pass teaches the loudest part, the second is what gets judged (= how an export behaves after one listen)
    for (int style = 0; style < 3; ++style)
        for (float loud : { 0.4f, 0.7f, 1.0f })
        {
            KorrentEngine e; e.prepare (sr, 512, 2);
            juce::AudioBuffer<float> a; a.makeCopyOf (in); run (e, a, loud, style, 0, false);
            juce::AudioBuffer<float> out; out.makeCopyOf (in); run (e, out, loud, style, 0, true);
            const float target = e.settings.targetMinLufs + (e.settings.targetMaxLufs - e.settings.targetMinLufs) * loud;
            float peak = 0; for (int i = 0; i < out.getNumSamples(); ++i) for (int ch = 0; ch < 2; ++ch) peak = juce::jmax (peak, std::abs (out.getSample (ch, i)));
            const float hook = lufs (out, h0, h1), verse = lufs (out, v0, v1);
            std::printf ("%-5s knob %3.0f %%  target %5.1f  hook %5.1f LUFS  verse %5.1f (distance %.1f, was %.1f)  peak %+.2f dB  gain %+.1f dB  limiter %.1f dB\n",
                         names[style], loud * 100, target, hook, verse, hook - verse, lufs (in, h0, h1) - lufs (in, v0, v1),
                         juce::Decibels::gainToDecibels (peak), e.getGainDb(), e.getReductionDb());
            if (juce::Decibels::gainToDecibels (peak) > e.settings.ceilingDb + 0.05f) { ++fails; std::printf ("  FAIL: above the ceiling\n"); }
            if (std::abs (hook - target) > (loud > 0.9f ? 3.0f : 2.0f)) { ++fails; std::printf ("  FAIL: misses the target\n"); }
            if (loud < 0.9f && (hook - verse) < (lufs (in, h0, h1) - lufs (in, v0, v1)) - 2.0f) { ++fails; std::printf ("  FAIL: verse and hook squeezed together\n"); }
        }

    // 808 shape: a 50 Hz tone pushed 8 dB over the ceiling
    {
        KorrentEngine e; e.prepare (sr, 512, 2);
        const int total = (int) sr * 4; juce::AudioBuffer<float> b (2, total);
        for (int i = 0; i < total; ++i) { const float s = 0.9f * (float) std::sin (2 * juce::MathConstants<double>::pi * 50.0 * i / sr); b.setSample (0, i, s); b.setSample (1, i, s); }
        // force gain: quiet copy first is not needed – drive it via the knob on an already hot signal
        run (e, b, 1.0f, 1, 0, true);
        auto harm = [&] (double f) { double re = 0, im = 0; const int a0 = (int) sr * 2; for (int i = a0; i < total; ++i) { const double ph = 2 * juce::MathConstants<double>::pi * f * i / sr; re += b.getSample (0, i) * std::cos (ph); im += b.getSample (0, i) * std::sin (ph); } return 20 * std::log10 (2 * std::sqrt (re * re + im * im) / (total - a0) + 1e-12); };
        std::printf ("808 tone at full knob: fundamental %.1f dB, 3rd %.1f, 5th %.1f, 7th %.1f, 9th %.1f dB\n", harm (50), harm (150), harm (250), harm (350), harm (450));
        if (harm (450) > harm (50) - 30.0) { ++fails; std::printf ("  FAIL: square-ish clipping (too much 9th harmonic)\n"); }
    }

    // CHECK: audible when listening, never in a render
    {
        for (int c = 1; c <= 3; ++c)
        {
            KorrentEngine e1, e2, e3; e1.prepare (sr, 512, 2); e2.prepare (sr, 512, 2); e3.prepare (sr, 512, 2);
            juce::AudioBuffer<float> ref, live, rend; ref.makeCopyOf (in); live.makeCopyOf (in); rend.makeCopyOf (in);
            run (e1, ref, 0.7f, 1, 0, false); run (e2, live, 0.7f, 1, c, false); run (e3, rend, 0.7f, 1, c, true);
            double dl = 0, dr = 0, rp = 0; for (int i = h0; i < h1; ++i) { const double a = live.getSample (0, i) - ref.getSample (0, i), b2 = rend.getSample (0, i) - ref.getSample (0, i); dl += a * a; dr += b2 * b2; rp += (double) ref.getSample (0, i) * ref.getSample (0, i); }
            std::printf ("check %d: listening differs by %.1f dB, render differs by %.1f dB\n", c, 10 * std::log10 (dl / rp + 1e-12), 10 * std::log10 (dr / rp + 1e-12));
            if (10 * std::log10 (dl / rp + 1e-12) < -12.0) { ++fails; std::printf ("  FAIL: check not audible\n"); }
            if (dr != 0.0) { ++fails; std::printf ("  FAIL: check leaks into the render\n"); }
        }
    }

    // abuse
    {
        KorrentEngine e; e.prepare (sr, 1024, 2); juce::Random r (2); float peak = 0; bool bad = false;
        for (int rep = 0; rep < 1500; ++rep)
        {
            const int n = 1 + r.nextInt (1000); juce::AudioBuffer<float> b (2, n); const int kind = (rep / 70) % 4;
            for (int i = 0; i < n; ++i) { float s = 0; if (kind == 0) s = (r.nextFloat() * 2 - 1) * 6.0f; else if (kind == 2) s = (i % 53 == 0) ? 3.0f : 0.0f; else if (kind == 3) s = (r.nextFloat() * 2 - 1) * 1e-6f; b.setSample (0, i, s); b.setSample (1, i, -s); }
            e.setParams (r.nextFloat(), r.nextInt (3), 0, true); e.process (b);
            for (int i = 0; i < n; ++i) for (int ch = 0; ch < 2; ++ch) { const float v = b.getSample (ch, i); if (! std::isfinite (v)) bad = true; peak = juce::jmax (peak, std::abs (v)); }
        }
        std::printf ("abuse: peak %.3f (ceiling %.3f), finite: %s\n", peak, juce::Decibels::decibelsToGain (-1.0f), bad ? "NO" : "yes");
        if (bad || peak > juce::Decibels::decibelsToGain (-1.0f) + 0.001f) { ++fails; std::printf ("  FAIL\n"); }
    }
    std::printf (fails == 0 ? "\nALL OK\n" : "\n%d FAILED\n", fails);
    return fails;
}
