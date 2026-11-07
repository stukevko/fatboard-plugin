#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>

// ============================================================================
//  Harun – pitch tracker (YIN)
//
//  Monophonic f0 estimation, block based. Every `hop` samples it looks at the
//  last `window` samples and runs the YIN difference function with cumulative
//  mean normalisation + parabolic interpolation. Output: f0 (Hz) and a
//  voiced flag. Cheap enough for one vocal track; not meant for polyphony.
//
//  De Cheveigné & Kawahara, "YIN, a fundamental frequency estimator for
//  speech and music" (2002). Implemented from the paper, no external code.
// ============================================================================

class PitchTracker
{
public:
    struct Settings
    {
        float minHz = 70.0f;        // TUNE – lowest vocal note we care about
        float maxHz = 1100.0f;      // TUNE
        float threshold = 0.15f;    // TUNE – YIN aperiodicity threshold (lower = stricter)
        float gateDb = -50.0f;      // TUNE – below this we report "unvoiced"
        float windowMs = 32.0f;     // analysis window
        float hopMs = 8.0f;         // update rate
    };
    Settings settings;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        window = nextPow2 ((int) (settings.windowMs * 0.001 * sr));
        hop = juce::jmax (64, (int) (settings.hopMs * 0.001 * sr));
        tauMin = juce::jmax (2, (int) (sr / settings.maxHz));
        tauMax = juce::jmin (window / 2, (int) (sr / settings.minHz));
        ring.assign ((size_t) window * 2, 0.0f);
        frame.assign ((size_t) window, 0.0f);
        diff.assign ((size_t) tauMax + 1, 0.0f);
        writePos = 0; sinceHop = 0;
        f0 = 0.0f; voiced = false; conf = 0.0f;
    }

    void reset() { std::fill (ring.begin(), ring.end(), 0.0f); f0 = 0.0f; voiced = false; }

    /** Push mono samples; returns true when a new estimate was computed in this call. */
    bool push (const float* x, int n)
    {
        bool updated = false;
        for (int i = 0; i < n; ++i)
        {
            ring[(size_t) writePos] = x[i];
            ring[(size_t) (writePos + window)] = x[i];      // mirrored write → contiguous reads
            writePos = (writePos + 1) % window;
            if (++sinceHop >= hop) { sinceHop = 0; analyse(); updated = true; }
        }
        return updated;
    }

    float getF0() const      { return f0; }
    bool  isVoiced() const   { return voiced; }
    float getConfidence() const { return conf; }
    int   getLatencySamples() const { return window / 2; }

private:
    static int nextPow2 (int v) { int p = 64; while (p < v) p <<= 1; return p; }

    void analyse()
    {
        // contiguous copy of the last `window` samples
        const float* src = ring.data() + writePos;        // thanks to the mirror, [writePos, writePos+window) is valid
        float energy = 0.0f;
        for (int i = 0; i < window; ++i) { frame[(size_t) i] = src[i]; energy += src[i] * src[i]; }
        const float rmsDb = juce::Decibels::gainToDecibels (std::sqrt (energy / (float) window) + 1.0e-9f);
        if (rmsDb < settings.gateDb) { voiced = false; conf = 0.0f; return; }

        // YIN difference function over the first half of the window
        const int half = window / 2;
        const float* f = frame.data();
        for (int tau = tauMin; tau <= tauMax; ++tau)
        {
            float acc = 0.0f;
            const float* a = f;
            const float* b = f + tau;
            for (int i = 0; i < half; ++i) { const float d = a[i] - b[i]; acc += d * d; }
            diff[(size_t) tau] = acc;
        }
        // cumulative mean normalised difference
        float running = 0.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            const float d = tau < tauMin ? 0.0f : diff[(size_t) tau];
            running += d;
            diff[(size_t) tau] = (tau < tauMin) ? 1.0f : (running > 0.0f ? d * (float) tau / running : 1.0f);
        }
        // first dip below threshold, then local minimum
        int best = -1;
        for (int tau = tauMin; tau <= tauMax; ++tau)
        {
            if (diff[(size_t) tau] < settings.threshold)
            {
                while (tau + 1 <= tauMax && diff[(size_t) tau + 1] < diff[(size_t) tau]) ++tau;
                best = tau; break;
            }
        }
        if (best < 0)
        {
            // no clear period: take the global minimum if it's at least somewhat periodic
            float m = 1.0f; int mi = -1;
            for (int tau = tauMin; tau <= tauMax; ++tau) if (diff[(size_t) tau] < m) { m = diff[(size_t) tau]; mi = tau; }
            if (mi < 0 || m > 0.4f) { voiced = false; conf = 1.0f - m; return; }
            best = mi;
        }
        // parabolic interpolation around best
        float tauF = (float) best;
        if (best > tauMin && best < tauMax)
        {
            const float s0 = diff[(size_t) best - 1], s1 = diff[(size_t) best], s2 = diff[(size_t) best + 1];
            const float denom = 2.0f * (s0 - 2.0f * s1 + s2);
            if (std::abs (denom) > 1.0e-9f) tauF += (s0 - s2) / denom;
        }
        conf = 1.0f - diff[(size_t) best];
        f0 = (float) sr / tauF;
        voiced = f0 >= settings.minHz && f0 <= settings.maxHz;
    }

    double sr = 48000.0;
    int window = 1024, hop = 384, tauMin = 40, tauMax = 600;
    std::vector<float> ring, frame, diff;
    int writePos = 0, sinceHop = 0;
    float f0 = 0.0f, conf = 0.0f;
    bool voiced = false;
};
