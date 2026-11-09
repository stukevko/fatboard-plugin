#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>

// ============================================================================
//  Scrub engine (Fatboard)
//
//  "Thin in. Fat out." – one knob.
//
//  What it does, in plain words:
//   1. Listens to the signal in 24 narrow bands (90 Hz .. 9 kHz).
//   2. For every band it asks: "is this band sticking out of its neighbours?"
//      If yes, that is a resonance (boom, honk, ring). It gets pulled down,
//      only as much as it sticks out, and only while it does.
//   3. Separately it looks at the overall balance between the mids and the
//      top end. Dull sources get a little air, harsh ones get calmed.
//   4. A fixed sub-rumble filter at 22 Hz keeps the bottom clean.
//
//  No machine learning, no analysis of the "spectrum vs. a curve". Just
//  envelope followers and dynamic peaking filters. Everything runs per block
//  on the user's machine.
//
//  Tuning knobs for the ear (marked TUNE) are all in one place: Settings.
// ============================================================================

class CleanupEngine
{
public:
    static constexpr int numBands = 24;

    struct Settings
    {
        // Frequency range of the resonance bands (log spaced)
        float lowestBandHz   = 90.0f;      // TUNE
        float highestBandHz  = 9000.0f;    // TUNE

        // How far a band must stick out of its neighbours before we act
        float thresholdDb        = 4.5f;   // TUNE – above ~300 Hz
        float thresholdLowDb     = 3.5f;   // TUNE – below ~300 Hz. v0.1 had 2.5: ate the body of the voice
        float ratio              = 0.8f;   // TUNE – fraction of the excess we remove
        float maxCutDb           = 8.0f;   // TUNE – ceiling per band
        float maxCutLowDb        = 9.0f;   // TUNE – ceiling in the boom region (was 14: too deep, voice lost weight)
        float bandQ              = 2.6f;   // TUNE – wide enough to actually catch a boom, narrow enough to leave the body
        float analysisQ          = 4.5f;   // TUNE – narrowness of the "listening" bands
        int   neighbourRadius    = 3;      // TUNE – how many bands each side define "normal"

        // Time constants (ms)
        float envAttackMs   = 6.0f;        // TUNE
        float envReleaseMs  = 220.0f;      // TUNE – was 90: followed the vibrato and flattened it
        float gainAttackMs  = 4.0f;        // TUNE
        float gainReleaseMs = 350.0f;      // TUNE – cuts let go slowly, no pumping on phrases

        // Air / harshness balance (dynamic high shelf)
        float shelfHz          = 5000.0f;  // TUNE
        float shelfTargetDb    = -8.0f;    // TUNE – wanted level of 4k-9k vs 400-2k (pink-ish)
        float shelfMaxDb       = 3.0f;     // TUNE – never more than this up or down
        float shelfStrength    = 0.5f;     // TUNE – how hard we chase the target
        float shelfSmoothMs    = 700.0f;   // TUNE – slow, it's a tone decision not a transient

        // Gate: below this input level we do nothing (avoid pumping on silence)
        float gateDb = -60.0f;
    };

    Settings settings;

    void prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        sr = sampleRate;
        channels = juce::jlimit (1, 2, numChannels);
        mono.setSize (1, maxBlockSize);

        const float ratioStep = std::pow (settings.highestBandHz / settings.lowestBandHz,
                                          1.0f / float (numBands - 1));
        for (int i = 0; i < numBands; ++i)
        {
            bandHz[i] = settings.lowestBandHz * std::pow (ratioStep, float (i));

            analysis[i].coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, bandHz[i], settings.analysisQ);
            analysis[i].reset();

            peakCoeffs[i] = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, bandHz[i], settings.bandQ, 1.0f);
            for (int ch = 0; ch < 2; ++ch)
            {
                peak[i][ch].coefficients = peakCoeffs[i];
                peak[i][ch].reset();
            }
            envDb[i] = -100.0f;
            reductionDb[i] = 0.0f;
            targetReductionDb[i] = 0.0f;
        }

        shelfCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, settings.shelfHz, 0.7f, 1.0f);
        rumbleCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 22.0f, 0.707f);
        for (int ch = 0; ch < 2; ++ch)
        {
            shelf[ch].coefficients = shelfCoeffs;   shelf[ch].reset();
            rumble[ch].coefficients = rumbleCoeffs; rumble[ch].reset();
        }
        shelfDb = 0.0f;
        amountSmoothed.reset (sr, 0.05);
        amountSmoothed.setCurrentAndTargetValue (amountTarget);
    }

    void reset()
    {
        for (int i = 0; i < numBands; ++i)
        {
            analysis[i].reset();
            for (int ch = 0; ch < 2; ++ch) peak[i][ch].reset();
            envDb[i] = -100.0f; reductionDb[i] = 0.0f; targetReductionDb[i] = 0.0f;
        }
        for (int ch = 0; ch < 2; ++ch) { shelf[ch].reset(); rumble[ch].reset(); }
        shelfDb = 0.0f;
    }

    /** 0..1 */
    void setAmount (float a) { amountTarget = juce::jlimit (0.0f, 1.0f, a); amountSmoothed.setTargetValue (amountTarget); }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        if (numSamples == 0) return;
        const int nCh = juce::jmin (channels, buffer.getNumChannels());

        // --- 1. mono analysis signal --------------------------------------
        auto* m = mono.getWritePointer (0);
        juce::FloatVectorOperations::copy (m, buffer.getReadPointer (0), numSamples);
        if (nCh > 1)
        {
            juce::FloatVectorOperations::add (m, buffer.getReadPointer (1), numSamples);
            juce::FloatVectorOperations::multiply (m, 0.5f, numSamples);
        }

        const float blockMs = 1000.0f * float (numSamples) / float (sr);
        const float envA = coefForMs (settings.envAttackMs,  blockMs);
        const float envR = coefForMs (settings.envReleaseMs, blockMs);
        const float gA   = coefForMs (settings.gainAttackMs,  blockMs);
        const float gR   = coefForMs (settings.gainReleaseMs, blockMs);
        const float shS  = coefForMs (settings.shelfSmoothMs, blockMs);

        // overall level for the gate
        float broadband = 0.0f;
        for (int n = 0; n < numSamples; ++n) broadband += m[n] * m[n];
        const float broadbandDb = juce::Decibels::gainToDecibels (std::sqrt (broadband / float (numSamples)) + 1.0e-9f);
        const bool  active = broadbandDb > settings.gateDb;

        // --- 2. per-band envelopes ----------------------------------------
        for (int i = 0; i < numBands; ++i)
        {
            float acc = 0.0f;
            auto& f = analysis[i];
            for (int n = 0; n < numSamples; ++n)
            {
                const float y = f.processSample (m[n]);
                acc += y * y;
            }
            const float rmsDb = juce::Decibels::gainToDecibels (std::sqrt (acc / float (numSamples)) + 1.0e-9f);
            const float c = rmsDb > envDb[i] ? envA : envR;
            envDb[i] += (rmsDb - envDb[i]) * c;
        }

        // --- 3. compare each band to the smooth spectral envelope -----------
        // The envelope is a wide (±6 band) median-ish average with the band itself and its
        // direct neighbours excluded, so a broad boom still "sticks out" while the natural
        // slope of a voice does not.
        const float amount = amountSmoothed.getNextValue();
        amountSmoothed.skip (numSamples - 1);

        for (int i = 0; i < numBands; ++i)
        {
            float sum = 0.0f; int cnt = 0;
            for (int k = -6; k <= 6; ++k)
            {
                if (std::abs (k) <= 1) continue;                     // exclude self + direct neighbours
                const int j = i + k;
                if (j < 0 || j >= numBands) continue;
                sum += envDb[j]; ++cnt;
            }
            const float local = cnt > 0 ? sum / float (cnt) : envDb[i];
            // a resonance also has to stick out of at least one direct neighbour (avoids cutting plateaus)
            const float nb = juce::jmin (i > 1 ? envDb[i - 2] : 1.0e9f, i < numBands - 2 ? envDb[i + 2] : 1.0e9f);
            const float peakiness = envDb[i] - nb;

            const bool  lowRegion = bandHz[i] < 300.0f;
            float thr = lowRegion ? settings.thresholdLowDb : settings.thresholdDb;
            float cap = lowRegion ? settings.maxCutLowDb    : settings.maxCutDb;
            // 90–180 Hz is where a voice has its weight: be extra careful there
            if (bandHz[i] < 180.0f) { thr += 1.5f; cap *= 0.75f; }

            float want = 0.0f;
            if (active)
            {
                const float excess = envDb[i] - local - thr;
                if (excess > 0.0f && peakiness > 0.5f)
                    want = juce::jmin (cap, excess * settings.ratio);
            }
            want *= amount;
            targetReductionDb[i] = want;

            const float c = want > reductionDb[i] ? gA : gR;
            reductionDb[i] += (want - reductionDb[i]) * c;
        }

        // --- 4. air / harshness balance -----------------------------------
        {
            float mid = 0.0f, hi = 0.0f; int nm = 0, nh = 0;
            for (int i = 0; i < numBands; ++i)
            {
                if (bandHz[i] >= 400.0f && bandHz[i] <= 2000.0f) { mid += envDb[i]; ++nm; }
                if (bandHz[i] >= 4000.0f)                        { hi  += envDb[i]; ++nh; }
            }
            float wantShelf = 0.0f;
            if (active && nm > 0 && nh > 0)
            {
                const float tilt = (hi / float (nh)) - (mid / float (nm));
                wantShelf = juce::jlimit (-settings.shelfMaxDb, settings.shelfMaxDb,
                                          (settings.shelfTargetDb - tilt) * settings.shelfStrength);
            }
            wantShelf *= amount;
            shelfDb += (wantShelf - shelfDb) * shS;
        }

        // --- 5. update filters and process ---------------------------------
        for (int i = 0; i < numBands; ++i)
        {
            const bool wasActive = bandActive[i];
            bandActive[i] = reductionDb[i] > 0.05f;
            if (bandActive[i])
            {
                *peakCoeffs[i] = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (
                                      sr, bandHz[i], settings.bandQ, juce::Decibels::decibelsToGain (-reductionDb[i]));
            }
            else if (wasActive)
            {
                for (int ch = 0; ch < 2; ++ch) peak[i][ch].reset();
            }
        }
        *shelfCoeffs = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, settings.shelfHz, 0.7f,
                                                                           juce::Decibels::decibelsToGain (shelfDb));

        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int n = 0; n < numSamples; ++n)
            {
                float x = rumble[ch].processSample (d[n]);
                for (int i = 0; i < numBands; ++i)
                    if (bandActive[i]) x = peak[i][ch].processSample (x);
                x = shelf[ch].processSample (x);
                d[n] = x;
            }
        }
    }

    // ---- for the UI --------------------------------------------------------
    float getBandReductionDb (int i) const { return reductionDb[juce::jlimit (0, numBands - 1, i)]; }
    float getBandHz (int i) const          { return bandHz[juce::jlimit (0, numBands - 1, i)]; }
    float getShelfDb() const               { return shelfDb; }
    float getTotalActivity() const
    {
        float s = 0.0f;
        for (int i = 0; i < numBands; ++i) s += reductionDb[i];
        return s;
    }

private:
    static float coefForMs (float ms, float blockMs)
    {
        if (ms <= 0.0f) return 1.0f;
        return 1.0f - std::exp (-blockMs / ms);
    }

    double sr = 48000.0;
    int channels = 2;
    juce::AudioBuffer<float> mono;

    std::array<float, numBands> bandHz {};
    std::array<juce::dsp::IIR::Filter<float>, numBands> analysis;
    std::array<juce::dsp::IIR::Coefficients<float>::Ptr, numBands> peakCoeffs;
    juce::dsp::IIR::Filter<float> peak[numBands][2];
    std::array<bool, numBands> bandActive {};

    std::array<float, numBands> envDb {};
    std::array<float, numBands> reductionDb {};
    std::array<float, numBands> targetReductionDb {};

    juce::dsp::IIR::Coefficients<float>::Ptr shelfCoeffs, rumbleCoeffs;
    juce::dsp::IIR::Filter<float> shelf[2], rumble[2];
    float shelfDb = 0.0f;

    float amountTarget = 0.5f;
    juce::SmoothedValue<float> amountSmoothed { 0.5f };
};
