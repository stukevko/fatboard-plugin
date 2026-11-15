// Dig offline check: tempo, grid, key, loop finder, repitch, stretch, chops.
#include "DigAnalysis.h"
#include "DigRender.h"
#include <cstdio>

static const double sr = 44100.0;
static const double PI2 = juce::MathConstants<double>::twoPi;

// a little "record": kick on 1 and 3, snare on 2 and 4, hats on 8ths, bass + chord per bar
static juce::AudioBuffer<float> makeSong (double bpm, const int* roots, const bool* minor, int bars, double lead, int swingless = 1)
{
    juce::ignoreUnused (swingless);
    const double beat = 60.0 / bpm; const int n = (int) ((lead + bars * 4 * beat + 1.0) * sr);
    juce::AudioBuffer<float> b (2, n); b.clear(); juce::Random rng (3);
    auto add = [&] (double t, double len, auto fn) { const int s = (int) (t * sr), l = (int) (len * sr); for (int i = 0; i < l && s + i < n; ++i) { const float v = fn (i / sr); b.addSample (0, s + i, v); b.addSample (1, s + i, v * 0.92f); } };
    for (int bar = 0; bar < bars; ++bar)
    {
        const double t0 = lead + bar * 4 * beat; const int root = roots[bar % 4]; const bool mn = minor[bar % 4];
        const double f0 = 110.0 * std::pow (2.0, (root - 9) / 12.0);                       // root in the A2 octave
        for (int q = 0; q < 4; ++q)
        {
            const double t = t0 + q * beat;
            if (q % 2 == 0) add (t, 0.18, [] (double x) { return (float) (0.8 * std::sin (PI2 * (50.0 * x + 30.0 * (1 - std::exp (-x * 40)) / 40.0)) * std::exp (-x * 18)); });
            else            add (t, 0.12, [&] (double x) { return (float) ((rng.nextFloat() * 2 - 1) * 0.5 * std::exp (-x * 30)); });
            for (int h = 0; h < 2; ++h) add (t + h * beat * 0.5, 0.03, [&] (double x) { return (float) ((rng.nextFloat() * 2 - 1) * 0.15 * std::exp (-x * 150)); });
        }
        const int third = mn ? 3 : 4;
        for (int st : { 0, 12, 12 + third, 19, 24 })
            add (t0, 4 * beat * 0.97, [&, st] (double x) { const double f = f0 * std::pow (2.0, st / 12.0); return (float) (0.07 * (std::sin (PI2 * f * x) + 0.4 * std::sin (PI2 * 2 * f * x) + 0.2 * std::sin (PI2 * 3 * f * x)) * juce::jmin (1.0, x * 200) * std::exp (-x * 0.6)); });
    }
    return b;
}
static double zcRate (const juce::AudioBuffer<float>& b) { int c = 0; const float* d = b.getReadPointer (0); for (int i = 1; i < b.getNumSamples(); ++i) if ((d[i - 1] < 0) != (d[i] < 0)) ++c; return (double) c / b.getNumSamples(); }

int main (int argc, char**)
{
    int fails = 0;
    if (argc > 1)
    {
        const int r[4] = { 4, 0, 7, 2 }; const bool m[4] = { true, false, false, false };
        const auto song = makeSong (141.0, r, m, 24, 0.1); const auto a = Dig::analyse (song, sr); const int fr = (int) a.env.size();
        auto ac = [&] (double L) { double t = 0; int c = 0; for (int i = 0; i + L + 1 < fr; ++i) { t += a.env[(size_t) i] * Dig::detail::at (a.env, i + L); ++c; } return t / c; };
        for (double bpm : { 141.0, 94.0, 70.5, 188.0 }) { const double L = a.envRate * 60 / bpm; std::printf ("%.1f:", bpm); double t = 0; for (int k = 1; k <= 8; ++k) { const double v = ac (k * L); t += v; std::printf (" %.3f", v); } std::printf ("  sum %.3f\n", t); }
        return 0;
    }
    struct Case { double bpm; int roots[4]; bool minor[4]; int wantKey; const char* name; double lead; };
    const Case cases[] = {
        { 93.0,  { 9, 5, 0, 7 },  { true, false, false, false }, 12 + 9, "93 bpm, Am F C G (A minor / C major)", 0.37 },
        { 128.0, { 0, 5, 7, 0 },  { false, false, false, false }, 0,     "128 bpm, C F G C (C major)", 0.0 },
        { 74.5,  { 2, 10, 5, 0 }, { true, false, false, false }, 12 + 2, "74.5 bpm, Dm Bb F C (D minor / F major)", 1.2 },
        { 141.0, { 4, 0, 7, 2 },  { true, false, false, false }, 12 + 4, "141 bpm, Em C G D (E minor / G major)", 0.1 },
    };
    for (const auto& c : cases)
    {
        const auto song = makeSong (c.bpm, c.roots, c.minor, 24, c.lead);
        const auto a = Dig::analyse (song, sr);
        const double beat = 60.0 / a.bpm; double ph = std::fmod (a.firstBeat - c.lead, 60.0 / c.bpm); if (ph > 30.0 / c.bpm) ph -= 60.0 / c.bpm; if (ph < -30.0 / c.bpm) ph += 60.0 / c.bpm;
        const int rel = c.wantKey >= 12 ? (c.wantKey - 12 + 3) % 12 : 12 + (c.wantKey + 9) % 12;      // relative major / minor shares the notes
        std::printf ("%-42s -> %.2f bpm, grid off by %+.0f ms, key %s (%.2f)\n", c.name, a.bpm, ph * 1000.0, Dig::keyName (a.key).toRawUTF8(), a.keyStrength);
        if (std::abs (a.bpm - c.bpm) > 0.25) { ++fails; std::printf ("  FAIL: tempo\n"); }
        if (std::abs (ph) > 0.02) { ++fails; std::printf ("  FAIL: grid\n"); }
        if (a.key != c.wantKey && a.key != rel) { ++fails; std::printf ("  FAIL: key (not even the relative)\n"); }
        juce::ignoreUnused (beat);
    }

    // ---- loop finder + render ----
    const int roots[4] = { 9, 5, 0, 7 }; const bool mn[4] = { true, false, false, false };
    const double songBpm = 93.0, lead = 2.0;
    auto song = makeSong (songBpm, roots, mn, 16, lead);
    for (int i = 0; i < (int) (lead * sr * 0.9); ++i) { song.setSample (0, i, 0.0f); song.setSample (1, i, 0.0f); }
    const auto a = Dig::analyse (song, sr);
    const auto loops = Dig::findLoops (a, a.bpm, a.firstBeat, 8);
    std::printf ("loop finder (2 bars): %d candidates, best starts at beat %d = %.2f s\n", (int) loops.size(), loops.empty() ? -1 : loops[0], loops.empty() ? 0.0 : a.firstBeat + loops[0] * 60.0 / a.bpm);
    if (loops.empty()) { ++fails; std::printf ("  FAIL: no loop\n"); }
    else
    {
        const double t = a.firstBeat + loops[0] * 60.0 / a.bpm, inBar = std::fmod (t - lead + 100 * 4 * 60.0 / songBpm, 4 * 60.0 / songBpm);
        const double offBeat = std::fmod (inBar + 0.02, 60.0 / songBpm) - 0.02;
        std::printf ("  sits %.0f ms after a beat, %.2f beats into the bar\n", offBeat * 1000.0, inBar / (60.0 / songBpm));
        if (std::abs (offBeat) > 0.03 || t < lead - 0.05) { ++fails; std::printf ("  FAIL: loop not on a beat / in the silence\n"); }
    }

    Dig::RenderSettings rs; rs.startSec = lead; rs.songBpm = songBpm; rs.hostBpm = 110.0; rs.hostSr = 48000.0; rs.beats = 8;
    const int want = (int) std::round (8 * 60.0 / 110.0 * 48000.0);
    // a pure tone region to measure pitch
    juce::AudioBuffer<float> tone (2, (int) (sr * 20)); for (int i = 0; i < tone.getNumSamples(); ++i) { const float v = 0.5f * (float) std::sin (PI2 * 440.0 * i / sr); tone.setSample (0, i, v); tone.setSample (1, i, v); }
    for (int mode = 0; mode < 3; ++mode)
    {
        rs.mode = mode; rs.semitones = 0;
        const auto c = Dig::render (tone, sr, rs); const int n = c.audio.getNumSamples();
        const double hz = zcRate (c.audio) * 48000.0 / 2.0; float pk = c.audio.getMagnitude (0, n);
        static const char* mnm[3] = { "original", "repitch", "stretch" };
        const double wantHz = mode == Dig::Repitch ? 440.0 * 110.0 / 93.0 : 440.0;
        const int wantLen = mode == Dig::Original ? (int) std::round (8 * 60.0 / 93.0 * 48000.0) : want;
        std::printf ("%-8s: %d samples (want %d), tone %.1f Hz (want %.1f), peak %.2f, %d chops\n", mnm[mode], n, wantLen, hz, wantHz, pk, (int) c.slices.size());
        if (std::abs (n - wantLen) > 2) { ++fails; std::printf ("  FAIL: length\n"); }
        if (std::abs (hz - wantHz) > wantHz * 0.01) { ++fails; std::printf ("  FAIL: pitch\n"); }
        if (pk > 0.62f || pk < 0.4f || c.slices.size() != 16) { ++fails; std::printf ("  FAIL: level or chops\n"); }
    }
    {
        rs.mode = Dig::Stretch; rs.semitones = 3; const auto c = Dig::render (tone, sr, rs);
        const double hz = zcRate (c.audio) * 48000.0 / 2.0;
        std::printf ("stretch +3 st: tone %.1f Hz (want %.1f), %d samples\n", hz, 440.0 * std::pow (2.0, 0.25), c.audio.getNumSamples());
        if (std::abs (hz - 440.0 * std::pow (2.0, 0.25)) > 6.0 || std::abs (c.audio.getNumSamples() - want) > 2) { ++fails; std::printf ("  FAIL\n"); }
        // stretch must not leave holes or bumps: the level of a steady tone stays steady
        rs.semitones = 0; const auto s = Dig::render (tone, sr, rs); float lo = 9, hi = 0;
        for (int i = 4800; i + 2400 < s.audio.getNumSamples() - 4800; i += 2400) { const float r = s.audio.getRMSLevel (0, i, 2400); lo = juce::jmin (lo, r); hi = juce::jmax (hi, r); }
        std::printf ("stretch steadiness on a tone: rms between %.3f and %.3f\n", lo, hi);
        if (hi / lo > 1.12f) { ++fails; std::printf ("  FAIL: stretch wobbles\n"); }
    }
    {
        // chops on the real region: beats = 8 parts, hits land near the drum hits
        rs.semitones = 0; rs.mode = Dig::Repitch; rs.chops = Dig::Beats; auto c = Dig::render (song, sr, rs);
        std::printf ("chops per beat: %d\n", (int) c.slices.size()); if (c.slices.size() != 8) { ++fails; std::printf ("  FAIL\n"); }
        rs.chops = Dig::Hits; c = Dig::render (song, sr, rs); const double beatS = 60.0 / 110.0 * 48000.0; int onGrid = 0;
        for (int s : c.slices) { const double r = std::fmod ((double) s + 600.0, beatS * 0.5) - 600.0; if (std::abs (r) < 0.02 * 48000.0) ++onGrid; }
        std::printf ("chops at hits: %d, of those on an 8th of the grid: %d\n", (int) c.slices.size(), onGrid);
        if (c.slices.size() < 6 || onGrid < (int) c.slices.size() * 3 / 4 || c.slices[0] != 0) { ++fails; std::printf ("  FAIL: hits\n"); }
        bool sorted = true; for (size_t i = 1; i < c.slices.size(); ++i) if (c.slices[i] <= c.slices[i - 1]) sorted = false;
        if (! sorted) { ++fails; std::printf ("  FAIL: chops out of order\n"); }
    }
    std::printf (fails == 0 ? "\nALL OK\n" : "\n%d FAILED\n", fails);
    return fails;
}
