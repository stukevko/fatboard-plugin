#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include <cmath>

// ============================================================================
//  Spit engine – beatbox in, drum hits out
//
//  1. ONSETS  – spectral flux on a 512-point STFT (hop 128 → 2.7 ms @48k),
//               adaptive threshold (median of the recent flux), 60 ms dead time.
//  2. FEATURES – for the 40 ms after an onset:
//               low ratio (energy < 200 Hz / total), centroid, high ratio
//               (> 5 kHz / total), decay length, peak level.
//  3. CLASS   – nearest prototype of three: Kick / Snare / Hat. Prototypes
//               start at sensible defaults for mouth sounds and can be LEARNED
//               from the user (3 hits per class → mean + spread).
//
//  Output: a Hit {class, velocity, time} per detected onset, delivered with a
//  fixed latency of `latencySamples()` so classification happens before the
//  note is emitted (the host gets PDC).
// ============================================================================

class SpitEngine
{
public:
    enum Drum { Kick = 0, Snare = 1, Hat = 2, None = 3 };

    struct Features { float low, centroid, high, decay, level, mid; };

    struct Hit { Drum drum; float velocity; int sampleOffset; float confidence; Features features; };

    struct Settings
    {
        float sensitivity = 0.5f;       // 0..1 – user knob
        float deadTimeMs = 70.0f;       // TUNE – min distance between hits (1/32 @ 200 BPM = 75 ms)
        float analysisMs = 40.0f;       // TUNE – window after onset used for the class
        float gateDb = -45.0f;          // TUNE – quieter onsets are ignored
    };
    Settings settings;

    void prepare (double sampleRate, int maxBlockSize)
    {
        sr = sampleRate;
        fftOrder = 9; fftSize = 1 << fftOrder; hop = fftSize / 4;
        fft = std::make_unique<juce::dsp::FFT> (fftOrder);
        window.resize ((size_t) fftSize);
        for (int i = 0; i < fftSize; ++i) window[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize));
        ring.assign ((size_t) fftSize * 2, 0.0f); ringW = 0; sinceHop = 0;
        fftBuf.assign ((size_t) fftSize * 2, 0.0f);
        prevMag.assign ((size_t) fftSize / 2, 0.0f);
        fluxHist.assign (43, 0.0f); fluxW = 0;                  // ~0.1 s of flux history
        analysisSamples = (int) (settings.analysisMs * 0.001 * sr);
        deadSamples = (int) (settings.deadTimeMs * 0.001 * sr);
        pending.clear(); samplesSinceOnset = deadSamples;
        latency = analysisSamples + hop;
        // feature accumulation buffers
        featBuf.assign ((size_t) analysisSamples, 0.0f);
        juce::ignoreUnused (maxBlockSize);
        resetPrototypes();
    }

    void reset() { std::fill (ring.begin(), ring.end(), 0.0f); std::fill (prevMag.begin(), prevMag.end(), 0.0f); pending.clear(); }

    int latencySamples() const { return latency; }

    /** Feed a mono block. Hits whose analysis finished land in `out` with sampleOffset relative to this block (may be negative → emit at 0). */
    void process (const float* x, int n, std::vector<Hit>& out)
    {
        out.clear();
        for (int i = 0; i < n; ++i)
        {
            ring[(size_t) ringW] = x[i]; ring[(size_t) (ringW + fftSize)] = x[i];
            ringW = (ringW + 1) % fftSize;
            ++samplesSinceOnset;

            // collect post-onset samples for the pending hit
            if (! pending.empty())
            {
                auto& p = pending.back();
                if (p.collected < analysisSamples) featBuf[(size_t) p.collected++] = x[i];
                if (p.collected >= analysisSamples && ! p.done)
                {
                    p.done = true;
                    const Features f = extract();
                    float conf = 0.0f;
                    const Drum d = classify (f, conf);
                    if (d != None)
                        out.push_back ({ d, juce::jlimit (0.15f, 1.0f, (f.level + 40.0f) / 36.0f), i - analysisSamples + latency, conf, f });
                    lastFeatures = f; lastDrum = d;
                }
            }

            if (++sinceHop >= hop)
            {
                sinceHop = 0;
                if (detectOnset() && samplesSinceOnset >= deadSamples)
                {
                    samplesSinceOnset = 0;
                    pending.clear();
                    pending.push_back ({ 0, false });
                }
            }
        }
    }

    // ---- learning -----------------------------------------------------------
    void startLearn (Drum d) { learning = d; learnCount = 0; learnAcc = {}; }
    bool isLearning() const { return learning != None; }
    Drum learningDrum() const { return learning; }
    int  learnProgress() const { return learnCount; }
    /** Call with each classified hit's features while learning. */
    void feedLearn (const Features& f)
    {
        if (learning == None) return;
        learnAcc.low += f.low; learnAcc.centroid += f.centroid; learnAcc.high += f.high; learnAcc.decay += f.decay; learnAcc.mid += f.mid;
        if (++learnCount >= 3)
        {
            auto& p = proto[(size_t) learning];
            p.low = learnAcc.low / 3; p.centroid = learnAcc.centroid / 3; p.high = learnAcc.high / 3; p.decay = learnAcc.decay / 3; p.mid = learnAcc.mid / 3;
            learning = None;
        }
    }
    void resetPrototypes()
    {
        // TUNE – defaults for typical mouth sounds (low ratio, centroid Hz, high ratio, decay ms)
        proto[Kick]  = { 0.80f,  250.0f, 0.02f, 45.0f, 0.0f, 0.10f };
        proto[Snare] = { 0.05f, 4500.0f, 0.40f, 45.0f, 0.0f, 0.45f };
        proto[Hat]   = { 0.00f, 12000.0f, 0.95f, 25.0f, 0.0f, 0.04f };
    }
    const Features& prototype (Drum d) const { return proto[(size_t) d]; }
    Features lastFeatures {}; Drum lastDrum = None;

private:
    struct Pending { int collected; bool done; };

    bool detectOnset()
    {
        // 1) spectral flux (great for snares/hats)
        const float* src = ring.data() + ringW;
        for (int i = 0; i < fftSize; ++i) { fftBuf[(size_t) i] = src[i] * window[(size_t) i]; }
        std::fill (fftBuf.begin() + fftSize, fftBuf.end(), 0.0f);
        fft->performFrequencyOnlyForwardTransform (fftBuf.data());
        float flux = 0.0f, energy = 0.0f;
        for (int k = 1; k < fftSize / 2; ++k)
        {
            const float m = fftBuf[(size_t) k];
            const float d = m - prevMag[(size_t) k];
            if (d > 0.0f) flux += d;
            prevMag[(size_t) k] = m;
            energy += m * m;
        }
        float mean = 0.0f; for (float v : fluxHist) mean += v; mean /= (float) fluxHist.size();
        fluxHist[(size_t) fluxW] = flux; fluxW = (fluxW + 1) % (int) fluxHist.size();
        const float factor = 1.6f + (1.0f - settings.sensitivity) * 3.0f;
        const bool fluxHit = flux > mean * factor && flux > 0.02f * (float) fftSize;

        // 2) energy jump on the last hop (catches dull kicks the flux misses, and is EARLY)
        float hopE = 0.0f;
        for (int i = fftSize - hop; i < fftSize; ++i) hopE += src[i] * src[i];
        const float hopDb = juce::Decibels::gainToDecibels (std::sqrt (hopE / (float) hop) + 1.0e-9f);
        const float jumpDb = hopDb - envDb;                               // vs. slow envelope
        const float jumpThr = 9.0f + (1.0f - settings.sensitivity) * 9.0f; // 9 .. 18 dB
        const bool energyHit = jumpDb > jumpThr && hopDb > settings.gateDb;
        envDb += (hopDb - envDb) * (hopDb > envDb ? 0.35f : 0.06f);       // fast up, slow down

        // a flux-only onset shortly after a hit is usually the tail of that hit (kick sweeps), not a new one
        const bool recent = samplesSinceOnset < (int) (0.15 * sr);
        const bool above = (energyHit || (fluxHit && ! recent)) && hopDb > settings.gateDb;
        if (! above) { if (flux < mean * 1.1f && jumpDb < 3.0f) armed = true; return false; }
        if (! armed) return false;
        armed = false;
        return true;
    }

    Features extract()
    {
        // energy bands over the analysis window via a second FFT of its first part
        const int N = juce::jmin (fftSize, analysisSamples);
        std::vector<float> buf ((size_t) fftSize * 2, 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = featBuf[(size_t) i] * window[(size_t) (i * fftSize / N)];
        fft->performFrequencyOnlyForwardTransform (buf.data());
        float total = 0, low = 0, high = 0, mid = 0, cw = 0;
        for (int k = 1; k < fftSize / 2; ++k)
        {
            const float hz = (float) k * (float) sr / (float) fftSize;
            const float e = buf[(size_t) k] * buf[(size_t) k];
            total += e; cw += e * hz;
            if (hz < 200.0f) low += e;
            if (hz >= 800.0f && hz < 4000.0f) mid += e;
            if (hz > 5000.0f) high += e;
        }
        total = juce::jmax (total, 1.0e-12f);
        // decay: how long until the envelope falls 12 dB below its peak
        float peak = 0.0f; for (int i = 0; i < analysisSamples; ++i) peak = juce::jmax (peak, std::abs (featBuf[(size_t) i]));
        const float thr = peak * 0.25f; int lastAbove = 0;
        for (int i = 0; i < analysisSamples; ++i) if (std::abs (featBuf[(size_t) i]) > thr) lastAbove = i;
        return { low / total, cw / total, high / total, 1000.0f * (float) lastAbove / (float) sr, juce::Decibels::gainToDecibels (peak + 1.0e-9f), mid / total };
    }

    Drum classify (const Features& f, float& confidence) const
    {
        float best = 1.0e9f, second = 1.0e9f; int bi = None;
        for (int d = 0; d < 3; ++d)
        {
            const auto& p = proto[(size_t) d];
            // normalised distance: low & high ratios 0..1, centroid in octaves, decay in 50 ms units
            const float dl = (f.low - p.low) * 3.0f;
            const float dh = (f.high - p.high) * 2.5f;
            const float dm = (f.mid - p.mid) * 3.0f;
            const float dc = std::log2 (juce::jmax (50.0f, f.centroid) / juce::jmax (50.0f, p.centroid)) * 0.6f;
            const float dist = dl * dl + dh * dh + dm * dm + dc * dc;
            if (dist < best) { second = best; best = dist; bi = d; }
            else if (dist < second) second = dist;
        }
        confidence = second > 0.0f ? juce::jlimit (0.0f, 1.0f, 1.0f - best / second) : 1.0f;
        return (Drum) bi;
    }

    double sr = 48000.0;
    int fftOrder = 9, fftSize = 512, hop = 128;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, ring, fftBuf, prevMag, fluxHist, featBuf;
    int ringW = 0, sinceHop = 0, fluxW = 0;
    int analysisSamples = 1920, deadSamples = 2880, samplesSinceOnset = 0, latency = 0;
    std::vector<Pending> pending;
    std::array<Features, 3> proto {};
    Drum learning = None; int learnCount = 0; Features learnAcc {};
    bool armed = true; float envDb = -60.0f;
};
