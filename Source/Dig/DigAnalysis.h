#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>
#include <algorithm>

// ============================================================================
//  Dig – analysis of a whole song (private sampling tool, v0.1)
//
//    tempo   onset envelope (spectral flux) → autocorrelation, summed over 1..4 beats,
//            with a mild preference for 75–150 bpm. Old records drift: the number is a
//            good average, the BPM field in the plugin lets you correct it.
//    grid    the beat phase that collects the most onset energy.
//    key     chroma (pitch classes, 55 Hz–1.76 kHz) against the Krumhansl major/minor profiles.
//            Assumes A = 440 Hz; a record that runs fast or slow can read a semitone off.
//    loops   starts on a beat, ranked by how well the bar after the loop resembles its start
//            (then the seam is natural) and how strong the hit on the first beat is.
//
//  Pure functions, no state.
// ============================================================================

namespace Dig
{
    struct Analysis
    {
        double bpm = 0.0;            // 0 = not found
        double firstBeat = 0.0;      // seconds: position of one beat (the grid is firstBeat + k * 60/bpm)
        int key = -1;                // 0..11 = C..B major, 12..23 = C..B minor
        float keyStrength = 0.0f;    // correlation with the profile, 0..1
        double seconds = 0.0;
        std::vector<float> env;      // onset envelope
        double envRate = 0.0;        // envelope frames per second
    };

    inline juce::String keyName (int key)
    {
        static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        if (key < 0 || key > 23) return "?";
        // the relative key uses the same notes and the two are easy to confuse, so both are shown
        if (key < 12) return juce::String (n[key]) + " major / " + n[(key + 9) % 12] + " minor";
        return juce::String (n[key % 12]) + " minor / " + n[(key % 12 + 3) % 12] + " major";
    }

    namespace detail
    {
        inline std::vector<float> monoDecimated (const juce::AudioBuffer<float>& b, int factor)
        {
            const int n = b.getNumSamples() / factor, nCh = b.getNumChannels();
            std::vector<float> out ((size_t) juce::jmax (0, n));
            for (int i = 0; i < n; ++i)
            {
                float s = 0.0f;
                for (int ch = 0; ch < nCh; ++ch) { const float* d = b.getReadPointer (ch) + (size_t) i * (size_t) factor; for (int k = 0; k < factor; ++k) s += d[k]; }
                out[(size_t) i] = s / (float) (factor * nCh);
            }
            return out;
        }

        inline float at (const std::vector<float>& v, double pos)
        {
            if (pos < 0.0 || pos >= (double) v.size() - 1.0) return 0.0f;
            const size_t i = (size_t) pos; const float f = (float) (pos - (double) i);
            return v[i] + (v[i + 1] - v[i]) * f;
        }
    }

    inline Analysis analyse (const juce::AudioBuffer<float>& song, double sr)
    {
        Analysis a; a.seconds = song.getNumSamples() / sr;
        if (song.getNumSamples() < (int) (sr * 2.0) || song.getNumChannels() < 1) return a;
        const int factor = juce::jmax (1, (int) std::round (sr / 11025.0));
        const double dsRate = sr / factor;
        const auto x = detail::monoDecimated (song, factor);

        // ---- onset envelope: positive change of the log spectrum ----
        const int order = 9, N = 1 << order, hop = 64;
        juce::dsp::FFT fft (order);
        juce::dsp::WindowingFunction<float> win ((size_t) N, juce::dsp::WindowingFunction<float>::hann);
        std::vector<float> buf ((size_t) N * 2), prev ((size_t) N / 2, 0.0f);
        const int frames = ((int) x.size() - N) / hop;
        a.envRate = dsRate / hop; a.env.assign ((size_t) juce::jmax (0, frames), 0.0f);
        for (int f = 0; f < frames; ++f)
        {
            std::copy (x.begin() + (size_t) f * hop, x.begin() + (size_t) f * hop + N, buf.begin());
            std::fill (buf.begin() + N, buf.end(), 0.0f);
            win.multiplyWithWindowingTable (buf.data(), (size_t) N);
            fft.performFrequencyOnlyForwardTransform (buf.data());
            float flux = 0.0f;
            for (int k = 1; k < N / 2; ++k) { const float m = std::log1p (60.0f * buf[(size_t) k]); const float d = m - prev[(size_t) k]; if (d > 0.0f) flux += d; prev[(size_t) k] = m; }
            a.env[(size_t) f] = flux;
        }
        if (frames < 200) return a;
        {   // remove the slow part, keep the hits
            const int w = (int) (a.envRate * 0.4); std::vector<float> e (a.env.size()); double run = 0.0;
            for (int i = 0; i < frames; ++i) { run += a.env[(size_t) i]; if (i >= w) run -= a.env[(size_t) (i - w)]; e[(size_t) i] = juce::jmax (0.0f, a.env[(size_t) i] - (float) (run / juce::jmin (i + 1, w))); }
            a.env.swap (e);
        }
        {   // widen the hits a little: a tempo whose beat falls between two frames must not lose against one that lands on a frame
            std::vector<float> e (a.env.size(), 0.0f);
            for (int i = 3; i < frames - 3; ++i) e[(size_t) i] = (a.env[(size_t) (i - 3)] + 2 * a.env[(size_t) (i - 2)] + 3 * a.env[(size_t) (i - 1)] + 4 * a.env[(size_t) i] + 3 * a.env[(size_t) (i + 1)] + 2 * a.env[(size_t) (i + 2)] + a.env[(size_t) (i + 3)]) / 16.0f;
            a.env.swap (e);
        }

        // ---- tempo ----
        const int useFrames = juce::jmin (frames, (int) (a.envRate * 300.0));                       // five minutes are plenty
        const int maxLag = juce::jmin (useFrames - 1, (int) (a.envRate * 60.0 / 55.0 * 8.0) + 4);
        std::vector<float> ac ((size_t) maxLag + 2, 0.0f);
        for (int L = 1; L <= maxLag; ++L) { double s = 0.0; for (int i = 0; i + L < useFrames; ++i) s += (double) a.env[(size_t) i] * a.env[(size_t) (i + L)]; ac[(size_t) L] = (float) (s / (useFrames - L)); }
        // a real beat repeats at 1, 2, 3 ... 8 beats. A tempo that is off by 2/3 or 3/2 only hits every other one.
        auto raw = [&] (double bpm)
        {
            const double L = a.envRate * 60.0 / bpm;
            if (bpm < 55.0 || bpm > 190.0 || 8.0 * L >= maxLag) return 0.0;
            double s = 0.0; for (int k = 1; k <= 8; ++k) s += detail::at (ac, k * L);
            return s;
        };
        auto rawNear = [&] (double bpm, double& at) { double b = 0.0; at = bpm; for (double x = bpm * 0.99; x <= bpm * 1.01; x += bpm * 0.0005) { const double v = raw (x); if (v > b) { b = v; at = x; } } return b; };
        double best = -1.0, bestBpm = 0.0;
        for (double bpm = 55.0; bpm <= 190.0; bpm += 0.05) { const double v = raw (bpm); if (v > best) { best = v; bestBpm = bpm; } }
        // half and double time fit a steady beat about as well: between those, take the one nearer to 105 bpm
        if (bestBpm > 0.0)
        {
            double pick = bestBpm, pickScore = -1.0;
            for (double mul : { 1.0, 2.0, 0.5 })
            {
                double at = 0.0; const double v = rawNear (bestBpm * mul, at);
                if (v < 0.9 * best) continue;
                const double o = std::log2 (at / 105.0), sc = v * std::exp (-0.5 * (o / 0.75) * (o / 0.75));
                if (sc > pickScore) { pickScore = sc; pick = at; }
            }
            bestBpm = pick;
        }
        // fine tune: the tempo (within half a percent) whose grid collects the most hits over the whole song
        if (bestBpm > 0.0)
        {
            double fineBest = -1.0, fineBpm = bestBpm;
            for (double bpm = bestBpm * 0.995; bpm <= bestBpm * 1.005; bpm += bestBpm * 0.0001)
            {
                const double beat = a.envRate * 60.0 / bpm;
                for (double off = 0.0; off < beat; off += 1.0)
                {
                    double s = 0.0; for (double t = off; t < useFrames - 1; t += beat) s += detail::at (a.env, t);
                    if (s > fineBest) { fineBest = s; fineBpm = bpm; }
                }
            }
            bestBpm = fineBpm;
        }
        a.bpm = bestBpm;

        // ---- grid phase ----
        if (a.bpm > 0.0)
        {
            const double beat = a.envRate * 60.0 / a.bpm; double bestS = -1.0, bestOff = 0.0;
            for (double off = 0.0; off < beat; off += 0.25)
            {
                double s = 0.0; for (double t = off; t < frames - 1; t += beat) s += detail::at (a.env, t);
                if (s > bestS) { bestS = s; bestOff = off; }
            }
            // the envelope jumps as soon as a hit enters the analysis window from the right, so the hit sits near the window's end
            a.firstBeat = juce::jmax (0.0, (bestOff * hop + 0.8 * N) / dsRate);            // TUNE: measured on sharp hits
        }

        // ---- key ----
        {
            const int ko = 12, KN = 1 << ko, khop = KN / 2;
            juce::dsp::FFT kfft (ko);
            juce::dsp::WindowingFunction<float> kwin ((size_t) KN, juce::dsp::WindowingFunction<float>::hann);
            std::vector<float> kb ((size_t) KN * 2); double chroma[12] = {};
            for (int s = 0; s + KN <= (int) x.size(); s += khop)
            {
                std::copy (x.begin() + s, x.begin() + s + KN, kb.begin()); std::fill (kb.begin() + KN, kb.end(), 0.0f);
                kwin.multiplyWithWindowingTable (kb.data(), (size_t) KN);
                kfft.performFrequencyOnlyForwardTransform (kb.data());
                double frame[12] = {}, tot = 0.0;
                for (int k = 1; k < KN / 2; ++k)
                {
                    const double hz = k * dsRate / KN; if (hz < 55.0 || hz > 1760.0) continue;
                    const int pc = ((int) std::lround (12.0 * std::log2 (hz / 440.0)) % 12 + 12 + 9) % 12;      // A = 9 when C = 0
                    frame[pc] += kb[(size_t) k]; tot += kb[(size_t) k];
                }
                if (tot > 1.0e-6) for (int i = 0; i < 12; ++i) chroma[i] += frame[i] / tot;                      // every moment counts the same, loud or quiet
            }
            static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
            static const double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
            auto corr = [&] (const double* prof, int root)
            {
                double mx = 0, my = 0; for (int i = 0; i < 12; ++i) { mx += chroma[i]; my += prof[i]; } mx /= 12; my /= 12;
                double xy = 0, xx = 0, yy = 0;
                for (int i = 0; i < 12; ++i) { const double u = chroma[(i + root) % 12] - mx, v = prof[i] - my; xy += u * v; xx += u * u; yy += v * v; }
                return xy / std::sqrt (xx * yy + 1.0e-20);
            };
            double bk = -2.0;
            for (int r = 0; r < 12; ++r) { const double c1 = corr (maj, r), c2 = corr (mnr, r); if (c1 > bk) { bk = c1; a.key = r; } if (c2 > bk) { bk = c2; a.key = 12 + r; } }
            a.keyStrength = (float) juce::jlimit (0.0, 1.0, bk);
        }
        return a;
    }

    /** Beat indices (0 = firstBeat) where a loop of `beats` beats could start, best first. */
    inline std::vector<int> findLoops (const Analysis& a, double bpm, double firstBeat, int beats, int maxResults = 8)
    {
        std::vector<int> out;
        if (bpm <= 0.0 || a.env.size() < 100) return out;
        const double beat = a.envRate * 60.0 / bpm, off = firstBeat * a.envRate;
        const int count = (int) (((double) a.env.size() - off) / beat) - beats - 1;
        if (count < 1) return out;
        float mean = 0.0f; for (float v : a.env) mean += v; mean /= (float) a.env.size(); mean = juce::jmax (mean, 1.0e-6f);
        auto hit = [&] (double t) { float m = 0.0f; for (double d = -2.0; d <= 2.0; d += 1.0) m = juce::jmax (m, detail::at (a.env, t + d)); return m; };
        std::vector<std::pair<double, int>> scored;
        for (int j = 0; j < count; ++j)
        {
            const double s = off + j * beat, e = s + beats * beat;
            // seam: the bar after the end against the first bar
            const int len = (int) juce::jmin (4.0 * beat, (double) a.env.size() - e - 1.0);
            if (len < 8) continue;
            double xy = 0, xx = 0, yy = 0, energy = 0;
            for (int i = 0; i < len; ++i) { const double u = detail::at (a.env, s + i), v = detail::at (a.env, e + i); xy += u * v; xx += u * u; yy += v * v; }
            for (double t = s; t < e; t += 1.0) energy += detail::at (a.env, t);
            const double seam = xy / std::sqrt (xx * yy + 1.0e-12);
            const double down = juce::jmin (3.0, (double) hit (s) / (4.0 * mean));
            const double busy = juce::jmin (1.0, energy / ((e - s) * mean));                    // skip the silent intro
            scored.push_back ({ seam + 0.25 * down + 0.3 * busy, j });
        }
        std::sort (scored.begin(), scored.end(), [] (const auto& l, const auto& r) { return l.first > r.first; });
        for (const auto& sc : scored)
        {
            bool near = false; for (int o : out) if (std::abs (o - sc.second) < juce::jmax (2, beats / 2)) near = true;      // not the same spot twice
            if (! near) out.push_back (sc.second);
            if ((int) out.size() >= maxResults) break;
        }
        return out;
    }
}
