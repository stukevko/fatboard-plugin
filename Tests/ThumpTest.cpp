// Thump (kick + 808) offline check.
//  - every built-in tone plays the note it is asked for (after the pitch drop)
//  - kick is there in the first milliseconds, and the 808 makes room for it
//  - Clip rounds off: louder body, no hard edges, never above the ceiling
//  - a new note cuts the old one without a click
//  - own sample: root is detected, another key plays in tune
//  - abuse → finite
#include <juce_dsp/juce_dsp.h>
#include "ThumpEngine.h"
#include <cstdio>

static const double sr = 48000.0;

static float f0 (const std::vector<float>& x, int from, int win)
{
    const int minLag = (int) (sr / 400.0), maxLag = (int) (sr / 25.0); double best = -1; int bl = 0;
    std::vector<double> ac ((size_t) maxLag + 2, 0.0);
    for (int lag = minLag; lag <= maxLag; ++lag) { double s = 0, e0 = 0, e1 = 0; for (int i = 0; i < win; ++i) { const double a = x[(size_t) (from + i)], b = x[(size_t) (from + i + lag)]; s += a * b; e0 += a * a; e1 += b * b; } ac[(size_t) lag] = s / std::sqrt (e0 * e1 + 1e-12); if (ac[(size_t) lag] > best) { best = ac[(size_t) lag]; bl = lag; } }
    for (int lag = minLag + 1; lag < bl; ++lag) if (ac[(size_t) lag] > best * 0.92 && ac[(size_t) lag] >= ac[(size_t) lag - 1] && ac[(size_t) lag] >= ac[(size_t) lag + 1]) { bl = lag; break; }
    return (float) (sr / bl);
}
static float rms (const std::vector<float>& x, int a, int b) { double s = 0; for (int i = a; i < b; ++i) s += (double) x[(size_t) i] * x[(size_t) i]; return 10.0f * (float) std::log10 (s / (b - a) + 1e-12); }
static float hiRms (const std::vector<float>& x, int a, int b, float hz)
{
    juce::dsp::IIR::Filter<float> hp; hp.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, hz, 0.707f);
    double s = 0; for (int i = juce::jmax (0, a - 2000); i < b; ++i) { const float y = hp.processSample (x[(size_t) i]); if (i >= a) s += (double) y * y; }
    return 10.0f * (float) std::log10 (s / (b - a) + 1e-12);
}
static std::vector<float> play (ThumpEngine& e, int note, double seconds)
{
    std::vector<float> out ((size_t) (sr * seconds)); e.noteOn (note, 1.0f);
    for (size_t pos = 0; pos < out.size(); pos += 256) e.render (out.data() + pos, (int) juce::jmin ((size_t) 256, out.size() - pos));
    return out;
}

int main()
{
    int fails = 0;
    const int note = 36; const float want = 65.41f;        // C2

    for (int t = 0; t < ThumpTones::count; ++t)
    {
        ThumpEngine e; e.prepare (sr); e.setParams (t, 0.8f, 0.0f, 0.67f, 0.0f);
        auto x = play (e, note, 2.0);
        const float f = f0 (x, (int) (sr * 0.25), 4096);
        float peak = 0; for (float v : x) peak = juce::jmax (peak, std::abs (v));
        std::printf ("%-6s plays %.1f Hz (asked %.1f)   level after 0.1 s %.1f dB, after 1 s %.1f dB   peak %.2f\n", ThumpTones::all[t].name, f, want, rms (x, 4800, 9600), rms (x, 48000, 52800), peak);
        if (std::abs (12.0f * std::log2 (f / want)) > 0.35f) { ++fails; std::printf ("  FAIL: out of tune\n"); }
        if (peak > 0.891f) { ++fails; std::printf ("  FAIL: above the ceiling\n"); }
    }

    // kick + ducking
    {
        ThumpEngine a, b; a.prepare (sr); b.prepare (sr);
        a.setParams (0, 0.8f, 0.0f, 0.67f, 0.0f); b.setParams (0, 0.8f, 0.8f, 0.67f, 0.0f);
        auto x = play (a, note, 0.5), y = play (b, note, 0.5);
        std::printf ("kick: above 1 kHz in the first 5 ms %.1f dB (without kick %.1f)   body 100-ms level %.1f vs %.1f dB\n", hiRms (y, 0, 240, 1000), hiRms (x, 0, 240, 1000), rms (y, 0, 4800), rms (x, 0, 4800));
        if (hiRms (y, 0, 240, 1000) < hiRms (x, 0, 240, 1000) + 10.0f) { ++fails; std::printf ("  FAIL: no kick click\n"); }
        // kick only (808 level 0) vs both: the sum must not be much hotter than the kick alone in the first 20 ms (= the 808 ducks)
        ThumpEngine c; c.prepare (sr); c.setParams (0, 0.0f, 0.8f, 0.67f, 0.0f); auto z = play (c, note, 0.5);
        std::printf ("first 20 ms: kick alone %.1f dB, kick + 808 %.1f dB\n", rms (z, 0, 960), rms (y, 0, 960));
        if (rms (y, 0, 960) > rms (z, 0, 960) + 4.0f) { ++fails; std::printf ("  FAIL: 808 does not make room\n"); }
    }

    // clip
    {
        ThumpEngine a, b; a.prepare (sr); b.prepare (sr);
        a.setParams (0, 0.8f, 0.6f, 0.67f, 0.0f); b.setParams (0, 0.8f, 0.6f, 0.67f, 1.0f);
        auto x = play (a, note, 1.0), y = play (b, note, 1.0);
        float pk = 0, jump = 0; for (size_t i = 1; i < y.size(); ++i) { pk = juce::jmax (pk, std::abs (y[i])); if (i > 960) jump = juce::jmax (jump, std::abs (y[i] - y[i - 1])); }
        std::printf ("clip 100 %%: body %.1f → %.1f dB, peak %.2f, harmonics above 600 Hz %.1f → %.1f dB\n", rms (x, 9600, 28800), rms (y, 9600, 28800), pk, hiRms (x, 9600, 28800, 600), hiRms (y, 9600, 28800, 600));
        if (pk > 0.891f) { ++fails; std::printf ("  FAIL: above the ceiling\n"); }
        if (rms (y, 9600, 28800) < rms (x, 9600, 28800) + 1.0f) { ++fails; std::printf ("  FAIL: clip does not thicken\n"); }
        if (hiRms (y, 9600, 28800, 600) > rms (y, 9600, 28800) - 18.0f) { ++fails; std::printf ("  FAIL: clip sounds like a square (too much top)\n"); }
    }

    // choke without click
    {
        ThumpEngine e; e.prepare (sr); e.setParams (3, 0.8f, 0.0f, 0.67f, 0.0f);
        std::vector<float> out ((size_t) sr); e.noteOn (36, 1.0f);
        for (size_t pos = 0; pos < out.size(); pos += 100) { if (pos == 24000) e.noteOn (43, 1.0f); e.render (out.data() + pos, 100); }
        float jump = 0; for (size_t i = 23900; i < 24600; ++i) jump = juce::jmax (jump, std::abs (out[i] - out[i - 1]));
        std::printf ("new note cuts the old one: largest step %.3f\n", jump);
        if (jump > 0.12f) { ++fails; std::printf ("  FAIL: click on note change\n"); }
    }

    // own sample: 55 Hz (A1) "808" with a pitch drop and harmonics, recorded at 44.1 k
    {
        auto s = std::make_shared<ThumpSample>(); s->sampleRate = 44100.0; s->data.resize (44100 * 2); double ph = 0;
        for (size_t i = 0; i < s->data.size(); ++i) { const double t = i / 44100.0, f = 55.0 * std::pow (2.0, 1.0 * std::exp (-t / 0.012)); ph += f / 44100.0; s->data[i] = (float) (std::tanh (2.0 * std::sin (2 * juce::MathConstants<double>::pi * ph)) * std::exp (-t * 1.5) * 0.8); }
        s->detectRoot();
        ThumpEngine e; e.prepare (sr); e.setSample (s); e.setParams (4, 0.8f, 0.0f, 1.0f, 0.0f);
        auto x = play (e, 36, 1.5);
        const float f = f0 (x, (int) (sr * 0.2), 4096);
        std::printf ("sample: root detected %.1f Hz (is 55.0, %s)   key C2 plays %.1f Hz (asked 65.4)\n", s->rootHz, s->rootDetected ? "found" : "NOT found", f);
        if (! s->rootDetected || std::abs (s->rootHz - 55.0f) > 1.0f) { ++fails; std::printf ("  FAIL: root detection\n"); }
        if (std::abs (12.0f * std::log2 (f / 65.41f)) > 0.4f) { ++fails; std::printf ("  FAIL: sample out of tune\n"); }
    }

    // abuse
    {
        ThumpEngine e; e.prepare (sr); juce::Random r (3); bool bad = false; float pk = 0; std::vector<float> buf (1024);
        for (int rep = 0; rep < 3000; ++rep)
        {
            if (r.nextInt (4) == 0) e.noteOn (12 + r.nextInt (80), r.nextFloat());
            e.setParams (r.nextInt (5), r.nextFloat(), r.nextFloat(), r.nextFloat(), r.nextFloat());
            const int n = 1 + r.nextInt (1000); e.render (buf.data(), n);
            for (int i = 0; i < n; ++i) { if (! std::isfinite (buf[(size_t) i])) bad = true; pk = juce::jmax (pk, std::abs (buf[(size_t) i])); }
        }
        std::printf ("abuse: peak %.3f, finite: %s\n", pk, bad ? "NO" : "yes");
        if (bad || pk > 0.891f) { ++fails; std::printf ("  FAIL\n"); }
    }
    std::printf (fails == 0 ? "\nALL OK\n" : "\n%d FAILED\n", fails);
    return fails;
}
