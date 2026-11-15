#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>
#include <algorithm>

// ============================================================================
//  Dig – turning a region of the song into a clip at the project tempo
//
//    Original  the region as it is (only the sample rate is matched). Length follows the song's tempo.
//    Repitch   played faster or slower until it fits: pitch moves with the tempo, like a record or an MPC.
//    Stretch   fits the project tempo and keeps the pitch (WSOLA: overlapping grains, each placed
//              where it lines up best with the one before). Fine for a few percent up to about ±25 %;
//              beyond that drums start to flam. Pitch knob shifts in semitones on top.
//
//  Chops: the clip cut into 8 or 16 equal parts, one per beat, or at its hits.
// ============================================================================

namespace Dig
{
    enum Mode { Original = 0, Repitch, Stretch };
    enum Chops { Eight = 0, Sixteen, Beats, Hits };

    struct Clip
    {
        juce::AudioBuffer<float> audio;       // stereo, host sample rate
        std::vector<int> slices;              // start of each chop; the last one ends at the clip end
        int sliceEnd (int i) const { return i + 1 < (int) slices.size() ? slices[(size_t) (i + 1)] : audio.getNumSamples(); }
    };

    /** step = input samples per output sample. Low-passes first when it reads faster than 1:1. */
    inline juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& in, double step, int outLen, double sr)
    {
        juce::AudioBuffer<float> src; src.makeCopyOf (in);
        const int n = src.getNumSamples();
        if (step > 1.02 && n > 8)
        {
            using C = juce::dsp::IIR::Coefficients<float>;
            const float hz = (float) juce::jmin (sr * 0.45, sr * 0.45 / step);
            for (int ch = 0; ch < src.getNumChannels(); ++ch)
            {
                juce::dsp::IIR::Filter<float> f[3]; for (auto& x : f) x.coefficients = C::makeLowPass (sr, hz, 0.707f);
                float* d = src.getWritePointer (ch);
                for (int i = 0; i < n; ++i) d[i] = f[2].processSample (f[1].processSample (f[0].processSample (d[i])));
            }
        }
        juce::AudioBuffer<float> out (src.getNumChannels(), juce::jmax (1, outLen)); out.clear();
        for (int ch = 0; ch < src.getNumChannels(); ++ch)
        {
            const float* s = src.getReadPointer (ch); float* o = out.getWritePointer (ch);
            for (int i = 0; i < outLen; ++i)
            {
                const double pos = i * step; const int k = (int) pos; if (k >= n - 1) break;
                const float t = (float) (pos - k);
                const float y0 = s[juce::jmax (0, k - 1)], y1 = s[k], y2 = s[k + 1], y3 = s[juce::jmin (n - 1, k + 2)];
                const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);       // Hermite
                o[i] = ((c3 * t + c2) * t + c1) * t + y1;
            }
        }
        return out;
    }

    /** Changes the length, keeps the pitch. */
    inline juce::AudioBuffer<float> wsola (const juce::AudioBuffer<float>& in, int outLen, double sr)
    {
        const int n = in.getNumSamples(), nCh = in.getNumChannels();
        juce::AudioBuffer<float> out (nCh, juce::jmax (1, outLen)); out.clear();
        if (n < 64 || outLen < 64) return out;
        if (std::abs (n - outLen) < 8) { for (int ch = 0; ch < nCh; ++ch) out.copyFrom (ch, 0, in, ch, 0, juce::jmin (n, outLen)); return out; }
        const int frame = juce::jmax (256, ((int) (sr * 0.046)) & ~1), hs = frame / 2, tol = frame / 4;      // TUNE: 46 ms grains, search ±11 ms
        const double speed = (double) n / (double) outLen;
        std::vector<float> mono ((size_t) n), w ((size_t) frame), norm ((size_t) outLen + (size_t) frame, 0.0f);
        for (int i = 0; i < n; ++i) { float s = 0.0f; for (int ch = 0; ch < nCh; ++ch) s += in.getSample (ch, i); mono[(size_t) i] = s; }
        for (int i = 0; i < frame; ++i) w[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) frame);
        juce::AudioBuffer<float> acc (nCh, outLen + frame); acc.clear();
        int prevPos = 0;
        for (int k = 0;; ++k)
        {
            const int outPos = k * hs; if (outPos >= outLen) break;
            const int ideal = (int) std::round (outPos * speed);
            int pos = ideal;
            if (k > 0)
            {
                // the grain that would follow the previous one naturally starts at prevPos + hs: find the spot near `ideal` that looks most like it
                const int target = prevPos + hs; double best = -1.0e30; pos = juce::jlimit (0, juce::jmax (0, n - frame), ideal);
                const int lo = juce::jmax (0, ideal - tol), hi = juce::jmin (n - frame, ideal + tol);
                if (target + frame <= n)
                    for (int c = lo; c <= hi; c += 2)
                    {
                        double xy = 0.0, yy = 1.0e-9;
                        for (int i = 0; i < hs; i += 2) { const double u = mono[(size_t) (target + i)], v = mono[(size_t) (c + i)]; xy += u * v; yy += v * v; }
                        const double s = xy / std::sqrt (yy);
                        if (s > best) { best = s; pos = c; }
                    }
            }
            pos = juce::jlimit (0, juce::jmax (0, n - frame), pos);
            for (int ch = 0; ch < nCh; ++ch)
            {
                const float* s = in.getReadPointer (ch); float* o = acc.getWritePointer (ch);
                for (int i = 0; i < frame && pos + i < n; ++i) o[outPos + i] += s[pos + i] * w[(size_t) i];
            }
            for (int i = 0; i < frame; ++i) norm[(size_t) (outPos + i)] += w[(size_t) i];
            prevPos = pos;
        }
        for (int ch = 0; ch < nCh; ++ch)
        {
            const float* a = acc.getReadPointer (ch); float* o = out.getWritePointer (ch);
            for (int i = 0; i < outLen; ++i) o[i] = a[i] / juce::jmax (0.25f, norm[(size_t) i]);
        }
        return out;
    }

    struct RenderSettings
    {
        double startSec = 0.0, songBpm = 120.0, hostBpm = 120.0, hostSr = 48000.0;
        int beats = 8;                 // length of the region in beats of the song
        int mode = Stretch, chops = Sixteen;
        int semitones = 0;
        float gainDb = 0.0f;
    };

    inline Clip render (const juce::AudioBuffer<float>& song, double songSr, const RenderSettings& rs)
    {
        Clip clip;
        const double beatSec = 60.0 / juce::jlimit (30.0, 300.0, rs.songBpm);
        const int a = (int) std::round (rs.startSec * songSr), len = juce::jmax (64, (int) std::round (rs.beats * beatSec * songSr));
        juce::AudioBuffer<float> region (2, len); region.clear();
        for (int ch = 0; ch < 2; ++ch)
        {
            const int sc = juce::jmin (ch, song.getNumChannels() - 1);
            const int from = juce::jmax (0, a), to = juce::jmin (song.getNumSamples(), a + len);
            if (to > from) region.copyFrom (ch, from - a, song, sc, from, to - from);
        }
        const double srStep = songSr / rs.hostSr, pitch = std::pow (2.0, rs.semitones / 12.0);
        const int fitLen = juce::jmax (64, (int) std::round (rs.beats * 60.0 / juce::jlimit (30.0, 300.0, rs.hostBpm) * rs.hostSr));
        if (rs.mode == Original)
        {
            const double step = srStep * pitch;
            clip.audio = resample (region, step, (int) std::round (len / step), songSr);
        }
        else if (rs.mode == Repitch)
            clip.audio = resample (region, (double) len / (double) fitLen, fitLen, songSr);
        else
        {
            const double step = srStep * pitch;
            auto pitched = resample (region, step, (int) std::round (len / step), songSr);
            clip.audio = wsola (pitched, fitLen, rs.hostSr);
        }
        const int n = clip.audio.getNumSamples(), fade = juce::jmin (n / 4, (int) (0.002 * rs.hostSr));
        const float g = juce::Decibels::decibelsToGain (rs.gainDb);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* d = clip.audio.getWritePointer (ch);
            for (int i = 0; i < n; ++i) { float f = g; if (i < fade) f *= (float) i / (float) fade; if (i >= n - fade) f *= (float) (n - 1 - i) / (float) fade; d[i] *= f; }
        }

        // ---- chops ----
        auto equal = [&] (int parts) { for (int i = 0; i < parts; ++i) clip.slices.push_back ((int) ((juce::int64) i * n / parts)); };
        if (rs.chops == Eight) equal (8);
        else if (rs.chops == Sixteen) equal (16);
        else if (rs.chops == Beats) equal (juce::jlimit (1, 32, rs.beats));
        else
        {
            // hits: energy jumps, the 15 strongest that are at least a 16th of the clip's beat apart
            const int hop = juce::jmax (16, (int) (rs.hostSr * 0.004)), frames = n / hop;
            std::vector<float> e ((size_t) juce::jmax (1, frames), 0.0f), d ((size_t) juce::jmax (1, frames), 0.0f);
            for (int f = 0; f < frames; ++f) { double s = 0.0; for (int ch = 0; ch < 2; ++ch) { const float* x = clip.audio.getReadPointer (ch) + (size_t) f * (size_t) hop; for (int i = 0; i < hop; ++i) s += (double) x[i] * x[i]; } e[(size_t) f] = (float) std::log1p (2000.0 * s / hop); }
            // 24 ms of level after a point against the 24 ms before it (long enough that a low kick's own wave is not taken for hits)
            const int w = 6;
            for (int f = w; f + w <= frames; ++f) { float after = 0.0f, before = 0.0f; for (int i = 0; i < w; ++i) { after += e[(size_t) (f + i)]; before += e[(size_t) (f - 1 - i)]; } d[(size_t) f] = juce::jmax (0.0f, (after - before) / (float) w); }
            const int minGap = juce::jmax (4, (int) ((double) n / hop / juce::jmax (1, rs.beats) / 4.0));
            std::vector<std::pair<float, int>> peaks;
            for (int f = w; f < frames - 1; ++f) if (d[(size_t) f] > d[(size_t) (f - 1)] && d[(size_t) f] >= d[(size_t) (f + 1)] && d[(size_t) f] > 0.25f) peaks.push_back ({ d[(size_t) f], f });
            std::sort (peaks.begin(), peaks.end(), [] (const auto& l, const auto& r) { return l.first > r.first; });
            std::vector<int> chosen { 0 };
            for (const auto& p : peaks)
            {
                bool ok = p.second > minGap; for (int c : chosen) if (std::abs (c - p.second) < minGap) ok = false;
                if (ok) chosen.push_back (p.second);
                if (chosen.size() >= 16) break;
            }
            std::sort (chosen.begin(), chosen.end());
            for (int c : chosen) clip.slices.push_back (juce::jmax (0, c * hop - (c > 0 ? hop : 0)));       // a touch early so the attack is in
        }
        return clip;
    }
}
