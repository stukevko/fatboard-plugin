#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>

// ============================================================================
//  Smash – the drum bus that doesn't turn to mud
//
//  Chain (stereo, zero latency):
//    HPF 35 Hz  →  transient shaper (level-aware)  →  dynamic low-mid cut (200–500)
//    →  glue compressor (auto threshold, slow attack / fast release)
//    →  hi tamer (6–9 kHz, dynamic)  →  tape-ish saturation  →  FIT  →  trim
//
//  FIT (needs the sample on the sidechain): a 3-band dynamic EQ that opens a
//  window in the drums exactly where the sample is loud right now – the drums
//  stay audible without getting brighter. No sidechain → FIT does nothing.
//
//  Style presets shift the balance:  Tight (controlled, less sat),
//  Punch (max transients, moderate glue), Dirty (more sat + glue, softer top).
//
//  One knob (Smash) scales the whole chain; Fit scales the sample window.
//  All TUNE values below.
// ============================================================================

class SmashEngine
{
public:
    enum class Style { Tight = 0, Punch = 1, Dirty = 2 };

    struct StyleParams { float transientDb; float glueGrDb; float glueAttackMs; float glueReleaseMs; float satDrive; float hiTamerDb; float lowMidDb; };

    struct Settings
    {
        float hpfHz = 35.0f;              // TUNE – keeps stray sub off the bus
        // transient shaper
        float transientFastMs = 0.5f;     // TUNE
        float transientSlowMs = 40.0f;    // TUNE
        float transientHoldMs = 6.0f;     // TUNE
        // dynamic low-mid (mud)
        float mudLoHz = 200.0f, mudHiHz = 500.0f;   // TUNE
        float mudThresholdDb = 2.0f;      // TUNE – how far mud must stick out of the rest
        // glue
        float glueRatio = 3.0f;           // TUNE
        float glueKneeDb = 6.0f;          // TUNE
        float autoThreshSec = 1.2f;       // TUNE – how fast the auto threshold follows level
        // hi tamer
        float hiHz = 7500.0f; float hiQ = 1.2f;  // TUNE
        float hiThresholdDb = 5.0f;       // TUNE – vs. the mid band
        // saturation
        float satMix = 0.6f;              // TUNE
        // fit (sample window)
        float fitBandsHz[3] = { 250.0f, 1200.0f, 4000.0f };   // TUNE
        float fitMaxDb = 5.0f;            // TUNE – never carve more than this
        float fitAttackMs = 30.0f, fitReleaseMs = 200.0f;      // TUNE
        float gateDb = -60.0f;
    };
    Settings settings;

    static const StyleParams& style (Style s)
    {
        //                               trans  gr   att   rel   sat   hiTam  lowMid
        static const StyleParams tight { 3.0f, 2.0f, 20.0f, 80.0f, 0.15f, 4.0f, 4.0f };    // TUNE
        static const StyleParams punch { 8.0f, 3.0f, 30.0f, 60.0f, 0.30f, 3.0f, 3.5f };    // TUNE
        static const StyleParams dirty { 4.0f, 4.0f, 15.0f, 100.0f, 0.60f, 5.0f, 3.0f };   // TUNE
        switch (s) { case Style::Tight: return tight; case Style::Dirty: return dirty; default: return punch; }
    }

    void prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        sr = sampleRate; channels = juce::jlimit (1, 2, numChannels);
        juce::ignoreUnused (maxBlockSize);
        using C = juce::dsp::IIR::Coefficients<float>;
        for (int ch = 0; ch < 2; ++ch)
        {
            hpf[ch].coefficients = C::makeHighPass (sr, settings.hpfHz, 0.707f); hpf[ch].reset();
            mudCut[ch].coefficients = C::makePeakFilter (sr, 330.0f, 1.0f, 1.0f); mudCut[ch].reset();
            hiCut[ch].coefficients  = C::makePeakFilter (sr, settings.hiHz, settings.hiQ, 1.0f); hiCut[ch].reset();
            for (int b = 0; b < 3; ++b) { fit[ch][b].coefficients = C::makePeakFilter (sr, settings.fitBandsHz[b], 1.0f, 1.0f); fit[ch][b].reset(); }
        }
        mudDet.coefficients = C::makeBandPass (sr, 330.0f, 0.7f); mudDet.reset();
        restDet.coefficients = C::makeBandPass (sr, 1500.0f, 0.35f); restDet.reset();
        hiDet.coefficients = C::makeBandPass (sr, settings.hiHz, settings.hiQ); hiDet.reset();
        midDet.coefficients = C::makeBandPass (sr, 1500.0f, 0.7f); midDet.reset();
        for (int b = 0; b < 3; ++b) { scDet[b].coefficients = C::makeBandPass (sr, settings.fitBandsHz[b], 1.0f); scDet[b].reset(); }
        fastEnv = slowEnv = 0.0f; mudDb = hiDb = 0.0f; glueEnvDb = -100.0f; glueGain = 1.0f; autoThreshDb = -20.0f;
        for (auto& f : fitDb) f = 0.0f;
        inPow = outPow = 0.0f; autoGain = 1.0f;
        smashSmoothed.reset (sr, 0.05); smashSmoothed.setCurrentAndTargetValue (smash);
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch) { hpf[ch].reset(); mudCut[ch].reset(); hiCut[ch].reset(); for (auto& f : fit[ch]) f.reset(); }
        mudDet.reset(); restDet.reset(); hiDet.reset(); midDet.reset(); for (auto& d : scDet) d.reset();
        glueGain = 1.0f; glueEnvDb = -100.0f;
    }

    void setParams (Style s, float smashIn, float fitIn, bool sidechain)
    {
        st = s; smash = juce::jlimit (0.0f, 1.0f, smashIn); smashSmoothed.setTargetValue (smash);
        fitAmount = juce::jlimit (0.0f, 1.0f, fitIn); hasSidechain = sidechain;
    }

    void process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>* sample)
    {
        const int n = buffer.getNumSamples();
        if (n == 0) return;
        const int nCh = juce::jmin (channels, buffer.getNumChannels());
        const float blockMs = 1000.0f * (float) n / (float) sr;
        const auto& p = style (st);
        const float a = smashSmoothed.getNextValue(); smashSmoothed.skip (n - 1);

        // ---- block analysis on mono ------------------------------------------
        float mudE = 0, restE = 0, hiE = 0, midE = 0, tot = 0;
        for (int i = 0; i < n; ++i)
        {
            float m = buffer.getReadPointer (0)[i]; if (nCh > 1) m = 0.5f * (m + buffer.getReadPointer (1)[i]);
            const float md = mudDet.processSample (m), rs = restDet.processSample (m), hd = hiDet.processSample (m), mi = midDet.processSample (m);
            mudE += md * md; restE += rs * rs; hiE += hd * hd; midE += mi * mi; tot += m * m;
        }
        const float totDb = dB (tot, n);
        const bool active = totDb > settings.gateDb;
        inPow += (tot / (float) n - inPow) * coef (500.0f, blockMs);       // mean power, always running

        // dynamic mud cut: mud band vs. the 1–2 kHz "rest"
        float wantMud = 0.0f;
        if (active) { const float ex = (dB (mudE, n) - dB (restE, n)) - settings.mudThresholdDb; if (ex > 0) wantMud = juce::jmin (p.lowMidDb, ex * 0.7f); }
        wantMud *= a;
        mudDb += (wantMud - mudDb) * coef (wantMud > mudDb ? 8.0f : 150.0f, blockMs);

        // hi tamer: 7.5k vs mids
        float wantHi = 0.0f;
        if (active) { const float ex = (dB (hiE, n) - dB (midE, n)) + 6.0f - settings.hiThresholdDb; if (ex > 0) wantHi = juce::jmin (p.hiTamerDb, ex * 0.8f); }
        wantHi *= a;
        hiDb += (wantHi - hiDb) * coef (wantHi > hiDb ? 3.0f : 120.0f, blockMs);

        // FIT: where is the sample loud?
        if (hasSidechain && sample != nullptr && sample->getNumChannels() > 0 && sample->getNumSamples() >= n)
        {
            float e[3] = { 0, 0, 0 }; float scTot = 0;
            for (int i = 0; i < n; ++i)
            {
                float m = sample->getReadPointer (0)[i]; if (sample->getNumChannels() > 1) m = 0.5f * (m + sample->getReadPointer (1)[i]);
                for (int b = 0; b < 3; ++b) { const float v = scDet[b].processSample (m); e[b] += v * v; }
                scTot += m * m;
            }
            const float scDb = dB (scTot, n);
            for (int b = 0; b < 3; ++b)
            {
                float want = 0.0f;
                if (scDb > settings.gateDb)
                {
                    // band energy relative to the sample's total: a band that carries the sample gets carved out of the drums
                    const float rel = dB (e[b], n) - scDb + 8.0f;             // ~0 when the band holds its "fair share"
                    if (rel > 0) want = juce::jmin (settings.fitMaxDb, rel * 0.6f);
                }
                want *= fitAmount * a;
                fitDb[b] += (want - fitDb[b]) * coef (want > fitDb[b] ? settings.fitAttackMs : settings.fitReleaseMs, blockMs);
            }
        }
        else for (auto& f : fitDb) f += (0.0f - f) * coef (settings.fitReleaseMs, blockMs);

        // glue: auto threshold follows the long-term level so GR lands near the style's target
        glueEnvDb += (totDb - glueEnvDb) * (totDb > glueEnvDb ? coef (p.glueAttackMs, blockMs) : coef (p.glueReleaseMs, blockMs));
        if (active) autoThreshDb += ((glueEnvDb - p.glueGrDb * (settings.glueRatio / (settings.glueRatio - 1.0f))) - autoThreshDb) * coef (settings.autoThreshSec * 1000.0f, blockMs);
        float gr = 0.0f;
        {
            const float over = glueEnvDb - autoThreshDb;
            if (over > -settings.glueKneeDb * 0.5f)
            {
                const float k = settings.glueKneeDb;
                const float x = over + k * 0.5f;
                const float compressed = over <= k * 0.5f ? (1.0f / settings.glueRatio - 1.0f) * x * x / (2.0f * k) : over - over / settings.glueRatio;
                gr = -compressed;
            }
        }
        gr = juce::jlimit (-12.0f, 0.0f, gr) * a;
        const float wantGlue = juce::Decibels::decibelsToGain (gr);
        glueGain += (wantGlue - glueGain) * coef (wantGlue < glueGain ? p.glueAttackMs : p.glueReleaseMs, blockMs);
        const float makeup = juce::Decibels::decibelsToGain (p.glueGrDb * 0.6f * a);
        currentGrDb = gr;

        // update filters
        using C = juce::dsp::IIR::Coefficients<float>;
        for (int ch = 0; ch < 2; ++ch)
        {
            *mudCut[ch].coefficients = *C::makePeakFilter (sr, 330.0f, 1.0f, juce::Decibels::decibelsToGain (-mudDb));
            *hiCut[ch].coefficients  = *C::makePeakFilter (sr, settings.hiHz, settings.hiQ, juce::Decibels::decibelsToGain (-hiDb));
            for (int b = 0; b < 3; ++b) *fit[ch][b].coefficients = *C::makePeakFilter (sr, settings.fitBandsHz[b], 1.0f, juce::Decibels::decibelsToGain (-fitDb[b]));
        }

        // ---- per-sample processing --------------------------------------------
        float outAcc = 0.0f;
        const float cf = 1.0f - std::exp (-1.0f / (settings.transientFastMs * 0.001f * (float) sr));
        const float cs = 1.0f - std::exp (-1.0f / (settings.transientSlowMs * 0.001f * (float) sr));
        const float transDb = p.transientDb * a;
        const float drv = 1.0f + p.satDrive * a * 5.0f;
        const float satMix = settings.satMix * juce::jmin (1.0f, p.satDrive * a * 2.0f);
        for (int i = 0; i < n; ++i)
        {
            // transient detector on mono (shared gain for both channels → stereo image stays put)
            float m = buffer.getReadPointer (0)[i]; if (nCh > 1) m = 0.5f * (m + buffer.getReadPointer (1)[i]);
            const float am = std::abs (m);
            fastEnv += (am - fastEnv) * (am > fastEnv ? cf : cs);
            slowEnv += (am - slowEnv) * cs;
            const float ratio = (fastEnv + 1.0e-6f) / (slowEnv + 1.0e-6f);
            // level-aware: quiet hits get a bit more help than loud ones
            const float levelScale = juce::jlimit (0.6f, 1.4f, 1.0f - (juce::Decibels::gainToDecibels (slowEnv + 1.0e-6f) + 18.0f) / 30.0f);
            const float transGainDb = juce::jlimit (0.0f, transDb * 1.5f, (ratio - 1.0f) * transDb * levelScale * 0.5f);
            const float tg = juce::Decibels::decibelsToGain (transGainDb);

            for (int ch = 0; ch < nCh; ++ch)
            {
                float x = buffer.getReadPointer (ch)[i];
                x = hpf[ch].processSample (x);
                x *= tg;
                x = mudCut[ch].processSample (x);
                x *= glueGain * makeup;
                x = hiCut[ch].processSample (x);
                const float sat = std::tanh (x * drv) / drv * (1.0f + 0.15f * p.satDrive * a);   // tape-ish: soft, slight level lift
                x = x + (sat - x) * satMix;
                for (int b = 0; b < 3; ++b) x = fit[ch][b].processSample (x);
                if (ch == 0) outAcc += x * x;             // pre-gain loudness
                x *= autoGain;
                buffer.getWritePointer (ch)[i] = x;
            }
        }
        // level-neutral: match output loudness to input loudness, slowly (a bus tool must not win by being louder)
        outPow += (outAcc / (float) n - outPow) * coef (500.0f, blockMs);
        if (inPow > 1.0e-7f && outPow > 1.0e-7f)
        {
            const float wantDb = juce::jlimit (-12.0f, 6.0f, 10.0f * std::log10 (inPow / outPow));
            autoGain += (juce::Decibels::decibelsToGain (wantDb) - autoGain) * coef (300.0f, blockMs);
        }
    }

    // ---- UI ---------------------------------------------------------------
    float getGrDb() const { return currentGrDb; }
    float getMudDb() const { return mudDb; }
    float getHiDb() const { return hiDb; }
    float getFitDb (int b) const { return fitDb[(size_t) juce::jlimit (0, 2, b)]; }
    bool  usingSidechain() const { return hasSidechain; }
    float getActivity() const { return juce::jlimit (0.0f, 1.0f, (-currentGrDb + mudDb + hiDb) / 8.0f); }

private:
    static float coef (float ms, float blockMs) { return 1.0f - std::exp (-blockMs / ms); }
    static float dB (float e, int n) { return juce::Decibels::gainToDecibels (std::sqrt (e / (float) n) + 1.0e-9f); }

    double sr = 48000.0; int channels = 2;
    Style st = Style::Punch; float smash = 0.5f, fitAmount = 0.5f; bool hasSidechain = false;
    juce::SmoothedValue<float> smashSmoothed { 0.5f };

    juce::dsp::IIR::Filter<float> hpf[2], mudCut[2], hiCut[2], fit[2][3];
    juce::dsp::IIR::Filter<float> mudDet, restDet, hiDet, midDet, scDet[3];
    float fastEnv = 0, slowEnv = 0, mudDb = 0, hiDb = 0, glueEnvDb = -100, glueGain = 1, autoThreshDb = -20, currentGrDb = 0;
    std::array<float, 3> fitDb { 0, 0, 0 };
    float inPow = 0.0f, outPow = 0.0f, autoGain = 1.0f;
};
