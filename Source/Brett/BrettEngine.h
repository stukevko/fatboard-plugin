#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>

// ============================================================================
//  Brett – the master   (working title, v0.1)
//
//  One knob that makes the song as loud as a release, without the 808 folding.
//
//    in → loudness meter (how loud is the loudest part of this song?)
//       → gain up to the target the knob asks for
//       → low band (< 110 Hz): soft clipper – the 808 gets round tops, not a pumping limiter
//       → full band: soft clip (style) → look-ahead limiter, ceiling -1 dB
//       → CHECK: phone / car / earbuds simulation, monitoring only
//
//  Rules:
//   - the gain is ONE value for the whole song (set by its loudest part), so verse
//     and hook keep their relation. It never rides the level.
//   - nothing leaves above the ceiling.
//   - CHECK is switched off automatically while the host renders, so a simulation
//     can never end up in the exported file.
//
//  All TUNE values are in Settings.
// ============================================================================

class BrettEngine
{
public:
    enum Style { Clean = 0, Warm, Hard };
    enum Check { Off = 0, Phone, Car, Buds };

    struct Settings
    {
        float targetMinLufs = -20.0f;     // TUNE: knob at 0
        float targetMaxLufs = -7.0f;      // TUNE: knob at 100 %  (default 70 % ≈ -11)
        float maxGainDb     = 24.0f;      // TUNE
        float ceilingDb     = -1.0f;      // TUNE
        float lookaheadMs   = 3.0f;       // TUNE (= latency)
        float crossoverHz   = 110.0f;     // TUNE: below this the clipper takes care of the 808
        float lowClipDb     = -3.0f;      // TUNE: where the low band starts to round off (relative to ceiling)
        float bias          = 1.0f;       // TUNE: clipper + limiter eat a little loudness, aim this much higher
        // per style:                       Clean   Warm    Hard
        float softClipKneeDb[3] = { -2.0f, -4.0f,  -7.0f };   // TUNE: where the full band starts to round off (transient tips)
        float releaseMs[3]      = { 120.0f, 90.0f,  60.0f };  // TUNE
    };
    Settings settings;

    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        sr = sampleRate; channels = juce::jlimit (1, 2, numChannels);
        juce::ignoreUnused (maxBlock);
        using C = juce::dsp::IIR::Coefficients<float>;
        la = juce::jmax (8, (int) std::round (settings.lookaheadMs * 0.001 * sr));
        const int len = la + 1;
        for (int ch = 0; ch < 2; ++ch)
        {
            delay[ch].assign ((size_t) len, 0.0f);
            kHp[ch].coefficients    = C::makeHighPass  (sr, 38.0f, 0.5f);
            kShelf[ch].coefficients = C::makeHighShelf (sr, 1681.0f, 0.707f, juce::Decibels::decibelsToGain (4.0f));
            oHp[ch].coefficients = kHp[ch].coefficients; oShelf[ch].coefficients = kShelf[ch].coefficients;
            for (int k = 0; k < 2; ++k) xLp[ch][k].coefficients = C::makeLowPass (sr, settings.crossoverHz, 0.707f);
            // CHECK filters
            phone[ch][0].coefficients = C::makeHighPass (sr, 420.0f, 0.707f);
            phone[ch][1].coefficients = C::makeHighPass (sr, 420.0f, 0.707f);
            phone[ch][2].coefficients = C::makeLowPass  (sr, 7500.0f, 0.707f);
            phone[ch][3].coefficients = C::makePeakFilter (sr, 2600.0f, 1.0f, juce::Decibels::decibelsToGain (4.0f));
            car[ch][0].coefficients = C::makePeakFilter (sr, 70.0f, 1.0f, juce::Decibels::decibelsToGain (5.0f));
            car[ch][1].coefficients = C::makePeakFilter (sr, 420.0f, 0.8f, juce::Decibels::decibelsToGain (-3.0f));
            car[ch][2].coefficients = C::makeHighShelf  (sr, 8000.0f, 0.7f, juce::Decibels::decibelsToGain (-2.5f));
            car[ch][3].coefficients = C::makeHighPass   (sr, 35.0f, 0.707f);
            buds[ch][0].coefficients = C::makeHighPass  (sr, 65.0f, 0.707f);
            buds[ch][1].coefficients = C::makePeakFilter (sr, 120.0f, 0.9f, juce::Decibels::decibelsToGain (3.0f));
            buds[ch][2].coefficients = C::makePeakFilter (sr, 3000.0f, 1.2f, juce::Decibels::decibelsToGain (3.0f));
            buds[ch][3].coefficients = C::makeLowPass   (sr, juce::jmin (11000.0f, (float) sr * 0.45f), 0.707f);
        }
        minBuf.assign ((size_t) len, 1.0f);
        reset();
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill (delay[ch].begin(), delay[ch].end(), 0.0f);
            kHp[ch].reset(); kShelf[ch].reset(); oHp[ch].reset(); oShelf[ch].reset();
            for (auto& f : xLp[ch]) f.reset();
            for (auto& f : phone[ch]) f.reset(); for (auto& f : car[ch]) f.reset(); for (auto& f : buds[ch]) f.reset();
        }
        std::fill (minBuf.begin(), minBuf.end(), 1.0f);
        w = 0; shortPow = 0.0; heardSec = 0.0f; maxShortDb = -100.0f; gainDb = 0.0f; limGain = 1.0f;
        outPow = 0.0; outDb = -100.0f; redDb = 0.0f; checkMix = 0.0f; shownCheck = Off;
    }

    int getLatencySamples() const { return la; }

    void setParams (float loudIn, int styleIn, int checkIn, bool rendering)
    {
        loud = juce::jlimit (0.0f, 1.0f, loudIn);
        style = juce::jlimit (0, 2, styleIn);
        check = rendering ? (int) Off : juce::jlimit (0, 3, checkIn);
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int n = buffer.getNumSamples();
        const int nCh = juce::jmin (channels, buffer.getNumChannels());
        if (n == 0 || nCh == 0) return;
        const float blockSec = (float) n / (float) sr;
        auto* L = buffer.getWritePointer (0);
        auto* R = nCh > 1 ? buffer.getWritePointer (1) : nullptr;

        // ---- 1. how loud is the input? (K-weighted, ~3 s) ----
        double ms = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float a = kShelf[0].processSample (kHp[0].processSample (L[i]));
            ms += (double) a * a;
            if (R) { const float b = kShelf[1].processSample (kHp[1].processSample (R[i])); ms += (double) b * b; }
        }
        ms /= n;
        const float blockDb = -0.691f + 10.0f * (float) std::log10 (ms + 1.0e-12);
        if (blockDb > -55.0f)
        {
            const float tau = juce::jlimit (0.3f, 3.0f, heardSec * 0.7f);
            shortPow += (ms - shortPow) * (1.0f - std::exp (-blockSec / tau));
            heardSec += blockSec;
            const float sDb = -0.691f + 10.0f * (float) std::log10 (shortPow + 1.0e-12);
            // the loudest part of the song decides. It only forgets very slowly (~2 dB a minute).
            maxShortDb = juce::jmax (sDb, maxShortDb - 0.03f * blockSec);
            if (heardSec < 1.0f) maxShortDb = sDb;           // first second: follow, there is nothing to remember yet
        }

        // ---- 2. one gain for the whole song ----
        const float target = settings.targetMinLufs + (settings.targetMaxLufs - settings.targetMinLufs) * loud + settings.bias;
        const float want = heardSec > 0.05f ? juce::jlimit (0.0f, settings.maxGainDb, target - maxShortDb) : 0.0f;
        const float gStart = gainDb;
        gainDb += (want - gainDb) * (1.0f - std::exp (-blockSec / (heardSec < 1.5f ? 0.15f : 0.8f)));

        const float ceil = juce::Decibels::decibelsToGain (settings.ceilingDb);
        const float lowT = ceil * juce::Decibels::decibelsToGain (settings.lowClipDb);
        const float kneeDb = settings.softClipKneeDb[style];
        const float clipT = ceil * juce::Decibels::decibelsToGain (kneeDb);
        const float rel = 1.0f - std::exp (-1.0f / (0.001f * settings.releaseMs[style] * (float) sr));
        const float att = 1.0f - std::exp (-1.0f / (0.25f * (float) la));
        const int len = la + 1;
        double redSum = 0.0, oms = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const float g = juce::Decibels::decibelsToGain (gStart + (gainDb - gStart) * ((float) i / (float) n));
            float x[2] = { L[i] * g, R ? R[i] * g : 0.0f };

            // ---- 3. low band gets round tops ----
            for (int ch = 0; ch < nCh; ++ch)
            {
                const float low = xLp[ch][1].processSample (xLp[ch][0].processSample (x[ch]));
                const float high = x[ch] - low;
                x[ch] = soft (low, lowT) + high;
                // ---- 4a. full-band soft clip (style) ----
                if (kneeDb < 0.0f) x[ch] = softAbove (x[ch], clipT, ceil * 1.25f);
            }

            // ---- 4b. look-ahead limiter ----
            const float peak = juce::jmax (std::abs (x[0]), std::abs (x[1]));
            const float need = peak > ceil ? ceil / peak : 1.0f;
            minBuf[(size_t) w] = need;
            float mn = 1.0f; for (float v : minBuf) mn = juce::jmin (mn, v);       // smallest gain needed in the look-ahead window
            limGain += (mn - limGain) * (mn < limGain ? att : rel);

            for (int ch = 0; ch < nCh; ++ch)
            {
                const int rp = (w + 1) % len;                 // oldest sample = la samples ago
                const float d = delay[ch][(size_t) rp];
                delay[ch][(size_t) w] = x[ch];
                float y = d * juce::jmin (limGain, 1.0f);
                y = juce::jlimit (-ceil, ceil, y);            // safety net, rarely touched
                const float k = oShelf[ch].processSample (oHp[ch].processSample (y));
                oms += (double) k * k;
                (ch == 0 ? L : R)[i] = y;
            }
            w = (w + 1) % len;
            redSum += -juce::Decibels::gainToDecibels (juce::jmin (limGain, 1.0f));
        }

        oms /= n;
        outPow += (oms - outPow) * (1.0f - std::exp (-blockSec / 1.0f));
        outDb = -0.691f + 10.0f * (float) std::log10 (outPow + 1.0e-12);
        redDb += ((float) (redSum / n) - redDb) * 0.3f;

        // ---- 5. CHECK: how does it sound out there? (monitoring only) ----
        const float wantMix = check != Off ? 1.0f : 0.0f;
        if (check != Off && check != shownCheck) { shownCheck = check; resetCheck(); }
        if (wantMix > 0.0f || checkMix > 0.001f)
        {
            for (int i = 0; i < n; ++i)
            {
                checkMix += (wantMix - checkMix) * 0.002f;
                float l = L[i], r = R ? R[i] : L[i];
                float sl = l, sr_ = r;
                auto run = [&] (juce::dsp::IIR::Filter<float> (*f)[4]) { for (int k = 0; k < 4; ++k) { sl = f[0][k].processSample (sl); sr_ = f[1][k].processSample (sr_); } };
                if (shownCheck == Phone)     { const float m = 0.5f * (l + r); sl = sr_ = m; run (phone); sl = sr_ = std::tanh (sl * 1.4f) * 0.7f; }   // one tiny speaker, mono, a bit strained
                else if (shownCheck == Car)  { run (car);  const float m = 0.5f * (sl + sr_), s = 0.5f * (sl - sr_) * 0.7f; sl = m + s; sr_ = m - s; sl *= 0.75f; sr_ *= 0.75f; }
                else if (shownCheck == Buds) { run (buds); sl *= 0.8f; sr_ *= 0.8f; }
                L[i] = l + (sl - l) * checkMix;
                if (R) R[i] = r + (sr_ - r) * checkMix;
            }
        }
        if (wantMix == 0.0f && checkMix <= 0.001f) { checkMix = 0.0f; shownCheck = Off; }
    }

    // ---- UI ----
    float getGainDb() const       { return gainDb; }
    float getReductionDb() const  { return redDb; }
    float getOutputLufs() const   { return outDb; }
    bool  hasHeard() const        { return heardSec > 0.5f; }
    bool  checkActive() const     { return check != Off; }

private:
    // smooth saturation that never exceeds t
    static float soft (float x, float t)
    {
        const float a = x / t, a3 = std::abs (a) * a * a;
        return t * a / std::cbrt (1.0f + std::abs (a3));
    }
    // linear below knee, rounds off towards `top`
    static float softAbove (float x, float knee, float top)
    {
        const float ax = std::abs (x);
        if (ax <= knee) return x;
        const float range = top - knee, o = (ax - knee) / range;
        return (x < 0.0f ? -1.0f : 1.0f) * (knee + range * std::tanh (o));
    }
    void resetCheck() { for (int ch = 0; ch < 2; ++ch) { for (auto& f : phone[ch]) f.reset(); for (auto& f : car[ch]) f.reset(); for (auto& f : buds[ch]) f.reset(); } }

    double sr = 48000.0; int channels = 2, la = 144, w = 0;
    float loud = 0.7f; int style = Warm, check = Off, shownCheck = Off;
    std::vector<float> delay[2], minBuf;
    juce::dsp::IIR::Filter<float> kHp[2], kShelf[2], oHp[2], oShelf[2], xLp[2][2];
    juce::dsp::IIR::Filter<float> phone[2][4], car[2][4], buds[2][4];
    double shortPow = 0.0, outPow = 0.0;
    float heardSec = 0.0f, maxShortDb = -100.0f, gainDb = 0.0f, limGain = 1.0f, outDb = -100.0f, redDb = 0.0f, checkMix = 0.0f;
};
