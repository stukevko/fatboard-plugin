#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>

// ============================================================================
//  Scrub Pro – Auto Balance + Leveler   (v0.5)
//
//  Cleanup removes what sticks out. This fixes what is simply wrong with the
//  recording as a whole:
//
//   AUTO BALANCE  listens to the long-term tone of the voice in four broad
//                 bands and pulls it towards a target curve for the mode:
//                 too much 150–450 Hz comes down, missing presence and air
//                 come up. As much as THIS recording needs, not a fixed EQ.
//
//   LEVELER       evens out loud and quiet words. The threshold follows the
//                 voice's own average level, so it behaves the same no matter
//                 how hot the track was recorded. Makeup is automatic.
//
//  "Clean" rules, all enforced here:
//    - feed-forward only: it measures the input, never its own output
//    - three very wide filters (Q 0.7 / shelf), never narrow ones
//    - tone moves over seconds, not syllables → no pumping, no colour shifts
//    - measurement freezes in pauses, so breaths and bleed don't steer it
//    - every correction is hard-limited (see TUNE)
//    - amount 0 → bit-identical
// ============================================================================

struct BalanceTarget
{
    // band power relative to the 450–1800 Hz band, in dB
    float lowMid;      // 150–450 Hz
    float presence;    // 1.8–5 kHz
    float air;         // 5–12 kHz
};

namespace FatboardBalance
{
    // TUNE: the static mode EQ adds its flavour on top of these, so the targets sit
    // at "a good, natural recording", not at the finished sound.
    static constexpr BalanceTarget Crisp {  1.0f,  -7.0f, -12.0f };
    static constexpr BalanceTarget Clear {  2.0f,  -8.0f, -13.5f };
    static constexpr BalanceTarget Soft  {  3.0f,  -9.5f, -15.5f };

    inline const BalanceTarget& get (int mode)
    {
        switch (mode) { case 1: return Clear; case 2: return Soft; default: return Crisp; }
    }

    // TUNE: limits of what Auto Balance may ever do (dB)
    static constexpr float lowMidMin = -6.0f, lowMidMax = 1.5f;
    static constexpr float presMin   = -3.0f, presMax   = 6.0f;
    static constexpr float airMin    = -3.0f, airMax    = 6.0f;

    // TUNE: leveler
    static constexpr float levRatioSlope = 0.62f;   // 1 - 1/ratio at full amount (≈ 2.6:1)
    static constexpr float levKneeDb     = 8.0f;
    static constexpr float levThreshRel  = -6.0f;   // threshold relative to the voice's own average level
    static constexpr float levMaxDb      = 10.0f;
    static constexpr float levAttackMs   = 6.0f;
    static constexpr float levReleaseMs  = 140.0f;

    // TUNE: peak catcher (what still jumps out after the leveler)
    static constexpr float pkThreshRel   = 9.0f;    // peak level above the voice's average RMS
    static constexpr float pkRatioSlope  = 0.75f;   // 4:1 at full amount
    static constexpr float pkKneeDb      = 4.0f;
    static constexpr float pkMaxDb       = 8.0f;
    static constexpr float pkAttackMs    = 0.4f;
    static constexpr float pkReleaseMs   = 70.0f;
}

class AutoBalance
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;
        channels = juce::jlimit (1, 2, numChannels);
        using C = juce::dsp::IIR::Coefficients<float>;
        static constexpr float edges[5] = { 150.0f, 450.0f, 1800.0f, 5000.0f, 12000.0f };
        for (int b = 0; b < 4; ++b)
        {
            const float hi = juce::jmin (edges[b + 1], (float) sr * 0.45f);
            // 8th-order edges: the bands must not leak into each other, or a boomy
            // voice would look "mid heavy" and the presence reading would be wrong
            for (int k = 0; k < 4; ++k)
            {
                bandHp[b][k].coefficients = C::makeHighPass (sr, edges[b], 0.707f);
                bandLp[b][k].coefficients = C::makeLowPass  (sr, hi,       0.707f);
            }
        }
        for (int ch = 0; ch < 2; ++ch)
        {
            lowMidEq[ch].coefficients = C::makePeakFilter (sr, lowMidHz, 0.7f, 1.0f);
            presEq[ch].coefficients   = C::makePeakFilter (sr, presHz,   0.7f, 1.0f);
            airEq[ch].coefficients    = C::makeHighShelf  (sr, airHz,    0.7f, 1.0f);
        }
        levAtt = 1.0f - std::exp (-1.0f / (0.001f * FatboardBalance::levAttackMs  * (float) sr));
        levRel = 1.0f - std::exp (-1.0f / (0.001f * FatboardBalance::levReleaseMs * (float) sr));
        pkAtt = 1.0f - std::exp (-1.0f / (0.001f * FatboardBalance::pkAttackMs  * (float) sr));
        pkRel = 1.0f - std::exp (-1.0f / (0.001f * FatboardBalance::pkReleaseMs * (float) sr));
        pkEnvRel = 1.0f - std::exp (-1.0f / (0.004f * (float) sr));    // 4 ms hold-ish release of the peak detector
        rmsCoef = 1.0f - std::exp (-1.0f / (0.012f * (float) sr));     // 12 ms power window
        reset();
    }

    void reset()
    {
        for (auto& b : bandHp) for (auto& f : b) f.reset();
        for (auto& b : bandLp) for (auto& f : b) f.reset();
        for (int ch = 0; ch < 2; ++ch) { lowMidEq[ch].reset(); presEq[ch].reset(); airEq[ch].reset(); }
        for (auto& p : bandPow) p = 0.0;
        heardSec = 0.0f; longPow = 0.0; peakDb = -100.0f;
        corr[0] = corr[1] = corr[2] = 0.0f;
        levPow = 0.0f; levGrDb = 0.0f; avgGrDb = 0.0f; makeupDb = 0.0f; pkEnv = 0.0f; pkGrDb = 0.0f; levHeardSec = 0.0f; levPeakDb = -100.0f; shownGrDb = 0.0f;
        wasBypassed = true;
    }

    void setMode (int m) { mode = juce::jlimit (0, 2, m); }

    /** Both stages in one go. amount 0..1 (the Mode knob). 0 = bit-identical. */
    void process (juce::AudioBuffer<float>& buffer, float amount)
    {
        processTone (buffer, amount);
        processLevel (buffer, amount);
    }

    /** Stage 1, BEFORE the mode chain: tone. */
    void processTone (juce::AudioBuffer<float>& buffer, float amount)
    {
        const int numSamples = buffer.getNumSamples();
        const int nCh = juce::jmin (channels, buffer.getNumChannels());
        if (numSamples == 0 || nCh == 0) return;

        // the knob reaches full correction at ~60 % so the default already does the job
        const float strength = juce::jlimit (0.0f, 1.0f, amount * 1.65f);
        const float blockSec = (float) numSamples / (float) sr;

        // ---------- measure the INPUT (mono) ----------
        double bp[4] = { 0, 0, 0, 0 }, broad = 0.0;
        const float* in0 = buffer.getReadPointer (0);
        const float* in1 = nCh > 1 ? buffer.getReadPointer (1) : nullptr;
        for (int n = 0; n < numSamples; ++n)
        {
            const float m = in1 ? 0.5f * (in0[n] + in1[n]) : in0[n];
            broad += (double) m * m;
            for (int b = 0; b < 4; ++b)
            {
                float y = m;
                for (int k = 0; k < 4; ++k) y = bandLp[b][k].processSample (bandHp[b][k].processSample (y));
                bp[b] += (double) y * y;
            }
        }
        const double blockPow = broad / numSamples;
        const float blockDb = 10.0f * (float) std::log10 (blockPow + 1.0e-12);

        // gate: only "the voice is really there" counts. Relative to its own recent peak.
        peakDb = juce::jmax (blockDb, peakDb - 3.0f * blockSec);             // peak falls 3 dB/s
        const bool active = blockDb > -58.0f && blockDb > peakDb - 22.0f;

        if (active)
        {
            // quick first impression (0.35 s), then settle into a slow 4 s average
            const float tau = juce::jlimit (0.35f, 4.0f, heardSec * 0.8f);
            const float c = 1.0f - std::exp (-blockSec / tau);
            for (int b = 0; b < 4; ++b) bandPow[b] += (bp[b] / numSamples - bandPow[b]) * c;
            heardSec += blockSec;
        }

        // ---------- tone: where should the three filters be? ----------
        float want[3] = { 0.0f, 0.0f, 0.0f };
        if (heardSec > 0.15f && strength > 0.0f)
        {
            const auto& t = FatboardBalance::get (mode);
            const float mid = 10.0f * (float) std::log10 (bandPow[1] + 1.0e-12);
            const float lm  = 10.0f * (float) std::log10 (bandPow[0] + 1.0e-12) - mid;
            const float pr  = 10.0f * (float) std::log10 (bandPow[2] + 1.0e-12) - mid;
            const float ar  = 10.0f * (float) std::log10 (bandPow[3] + 1.0e-12) - mid;
            using namespace FatboardBalance;
            // 0.85: a wide filter also moves its neighbours, stay a touch under the full difference
            want[0] = juce::jlimit (lowMidMin, lowMidMax, (t.lowMid   - lm) * 0.85f) * strength;
            want[1] = juce::jlimit (presMin,   presMax,   (t.presence - pr) * 0.85f) * strength;
            want[2] = juce::jlimit (airMin,    airMax,    (t.air      - ar) * 0.85f) * strength;
            // 1 dB dead zone: a recording that is already right stays untouched
            for (auto& w : want) w = (std::abs (w) < 1.0f) ? 0.0f : w - (w > 0.0f ? 1.0f : -1.0f) * (1.0f - juce::jmin (1.0f, (std::abs (w) - 1.0f) / 2.0f));
        }
        const float move = 1.0f - std::exp (-blockSec / (heardSec < 1.5f ? 0.25f : 1.2f));
        bool anyEq = false;
        for (int i = 0; i < 3; ++i)
        {
            corr[i] += (want[i] - corr[i]) * move;
            if (std::abs (corr[i]) < 0.02f && want[i] == 0.0f) corr[i] = 0.0f;
            anyEq = anyEq || corr[i] != 0.0f;
        }

        if (! anyEq)
        {
            wasBypassed = true;                    // nothing to do: leave the buffer bit-identical
            return;
        }
        if (wasBypassed)
        {
            for (int ch = 0; ch < 2; ++ch) { lowMidEq[ch].reset(); presEq[ch].reset(); airEq[ch].reset(); }
            wasBypassed = false;
        }

        using C = juce::dsp::IIR::Coefficients<float>;
        *lowMidEq[0].coefficients = *C::makePeakFilter (sr, lowMidHz, 0.7f, juce::Decibels::decibelsToGain (corr[0]));
        *presEq[0].coefficients   = *C::makePeakFilter (sr, presHz,   0.7f, juce::Decibels::decibelsToGain (corr[1]));
        *airEq[0].coefficients    = *C::makeHighShelf  (sr, airHz,    0.7f, juce::Decibels::decibelsToGain (corr[2]));
        lowMidEq[1].coefficients = lowMidEq[0].coefficients;
        presEq[1].coefficients   = presEq[0].coefficients;
        airEq[1].coefficients    = airEq[0].coefficients;

        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int n = 0; n < numSamples; ++n)
                d[n] = airEq[ch].processSample (presEq[ch].processSample (lowMidEq[ch].processSample (d[n])));
        }
    }

    /** Stage 2, AFTER the mode chain: leveler + peak catcher, linked stereo.
        Measures its own input, so whatever the chain did before is already counted. */
    void processLevel (juce::AudioBuffer<float>& buffer, float amount)
    {
        using namespace FatboardBalance;
        const int numSamples = buffer.getNumSamples();
        const int nCh = juce::jmin (channels, buffer.getNumChannels());
        if (numSamples == 0 || nCh == 0) return;
        const float strength = juce::jlimit (0.0f, 1.0f, amount * 1.65f);
        const float blockSec = (float) numSamples / (float) sr;
        auto* l = buffer.getWritePointer (0);
        auto* r = nCh > 1 ? buffer.getWritePointer (1) : nullptr;

        double broad = 0.0;
        for (int n = 0; n < numSamples; ++n) { const float m = r ? 0.5f * (l[n] + r[n]) : l[n]; broad += (double) m * m; }
        const double blockPow = broad / numSamples;
        const float blockDb = 10.0f * (float) std::log10 (blockPow + 1.0e-12);
        levPeakDb = juce::jmax (blockDb, levPeakDb - 3.0f * blockSec);
        const bool active = blockDb > -58.0f && blockDb > levPeakDb - 22.0f;
        if (active)
        {
            const float tau = juce::jlimit (0.35f, 4.0f, levHeardSec * 0.8f);
            longPow += (blockPow - longPow) * (1.0f - std::exp (-blockSec / tau));
            levHeardSec += blockSec;
        }

        const bool levReady = levHeardSec > 0.3f && strength > 0.0f;
        if (! levReady && levGrDb < 0.01f && pkGrDb < 0.01f && std::abs (makeupDb) < 0.01f)
        {
            levGrDb = pkGrDb = makeupDb = 0.0f; shownGrDb = 0.0f;
            return;                                // bit-identical
        }

        const float voiceDb = 10.0f * (float) std::log10 (longPow + 1.0e-12);
        const float thresh  = voiceDb + levThreshRel;
        const float pkThr   = voiceDb + pkThreshRel;
        const float slope   = levRatioSlope * strength;
        const float pkSl    = pkRatioSlope * strength;
        double grSum = 0.0;
        const float startMakeup = makeupDb;
        // makeup follows the average reduction slowly (gated) → level-neutral, never pumps
        if (active) avgGrDb += (shownGrDb - avgGrDb) * (1.0f - std::exp (-blockSec / juce::jlimit (0.25f, 2.5f, levHeardSec * 0.4f)));
        const float endMakeup = levReady ? avgGrDb : makeupDb * std::exp (-blockSec / 0.3f);
        for (int n = 0; n < numSamples; ++n)
        {
            const float m = r ? 0.5f * (l[n] + r[n]) : l[n];
            levPow += (m * m - levPow) * rmsCoef;
            float want_ = 0.0f;
            if (levReady)
            {
                const float over = 10.0f * std::log10 (levPow + 1.0e-12f) - thresh;
                float o = 0.0f;                                   // soft knee
                if (over > levKneeDb * 0.5f)        o = over;
                else if (over > -levKneeDb * 0.5f)  { const float k = over + levKneeDb * 0.5f; o = k * k / (2.0f * levKneeDb); }
                want_ = juce::jmin (levMaxDb, o * slope);
            }
            levGrDb += (want_ - levGrDb) * (want_ > levGrDb ? levAtt : levRel);
            const float mk = startMakeup + (endMakeup - startMakeup) * ((float) n / (float) numSamples);

            // peak catcher: looks at the sample AFTER leveler + makeup, grabs only what still jumps out
            const float g1 = fastDbToGain (mk - levGrDb);
            const float pk = std::abs (m * g1);
            pkEnv = pk > pkEnv ? pk : pkEnv + (pk - pkEnv) * pkEnvRel;
            float wantPk = 0.0f;
            if (levReady)
            {
                const float over = 20.0f * std::log10 (pkEnv + 1.0e-9f) - pkThr;
                float o = 0.0f;
                if (over > pkKneeDb * 0.5f)        o = over;
                else if (over > -pkKneeDb * 0.5f)  { const float k = over + pkKneeDb * 0.5f; o = k * k / (2.0f * pkKneeDb); }
                wantPk = juce::jmin (pkMaxDb, o * pkSl);
            }
            pkGrDb += (wantPk - pkGrDb) * (wantPk > pkGrDb ? pkAtt : pkRel);

            grSum += levGrDb + pkGrDb;
            const float g = g1 * fastDbToGain (-pkGrDb);
            l[n] *= g; if (r) r[n] *= g;
        }
        makeupDb = endMakeup;
        shownGrDb = (float) (grSum / numSamples);
    }

    float getLowMidDb()   const { return corr[0]; }
    float getPresenceDb() const { return corr[1]; }
    float getAirDb()      const { return corr[2]; }
    float getLevelDb()    const { return shownGrDb; }
    float getMakeupDb()   const { return makeupDb; }
    bool  hasHeard()      const { return heardSec > 0.3f; }

private:
    static float fastDbToGain (float db) { return std::exp (db * 0.11512925f); }

    static constexpr float lowMidHz = 270.0f, presHz = 3200.0f, airHz = 7500.0f;

    double sr = 48000.0;
    int channels = 2, mode = 1;

    juce::dsp::IIR::Filter<float> bandHp[4][4], bandLp[4][4];
    juce::dsp::IIR::Filter<float> lowMidEq[2], presEq[2], airEq[2];
    double bandPow[4] = { 0, 0, 0, 0 }, longPow = 0.0;
    float heardSec = 0.0f, peakDb = -100.0f;
    float corr[3] = { 0, 0, 0 };

    float levAtt = 0.01f, levRel = 0.001f, rmsCoef = 0.01f;
    float levPow = 0.0f, levGrDb = 0.0f, avgGrDb = 0.0f, makeupDb = 0.0f, shownGrDb = 0.0f;
    float pkAtt = 0.05f, pkRel = 0.001f, pkEnvRel = 0.01f, pkEnv = 0.0f, pkGrDb = 0.0f, levHeardSec = 0.0f, levPeakDb = -100.0f;
    bool wasBypassed = true;
};
