#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>

// ============================================================================
//  Scrub Pro – vocal modes
//
//  Crisp / Clear / Soft. Each one is a whole chain, not an EQ preset:
//     high-pass → static tone shaping → de-esser (dynamic top band)
//     → saturation → output trim
//  Cleanup runs BEFORE all of this (in the processor), so the mode only has
//  to do tone, never repair.
//
//  One "Amount" knob (0..1) scales every dB value and the saturation drive,
//  so half way is a tasteful version and full is the loud version.
//
//  All TUNE values live in the three ModeParams below.
// ============================================================================

struct ModeParams
{
    const char* name;

    float hpfHz;

    float lowShelfHz,  lowShelfDb;
    float lowMidHz,    lowMidDb,    lowMidQ;
    float boxHz,       boxDb,       boxQ;
    float presenceHz,  presenceDb,  presenceQ;
    float hiShelfHz,   hiShelfDb;

    float deEssHz,     deEssMaxDb,  deEssThresholdDb;   // dynamic cut around sibilance
    float drive;                                          // 0..1 saturation
    float outTrimDb;
};

namespace FatboardModes
{
    // TUNE
    static constexpr ModeParams Crisp {
        "Crisp",
        90.0f,
        180.0f, 0.5f,
        300.0f, -1.5f, 1.0f,
        600.0f, -1.0f, 1.4f,
        3600.0f, 3.0f, 1.0f,
        9000.0f, 3.0f,
        7200.0f, 6.0f, 4.0f,
        0.25f,
        -0.5f
    };

    // TUNE
    static constexpr ModeParams Clear {
        "Clear",
        85.0f,
        180.0f, 0.5f,
        250.0f, -2.0f, 1.2f,
        500.0f, -1.0f, 1.6f,
        2600.0f, 2.0f, 1.1f,
        8000.0f, 1.5f,
        7000.0f, 5.0f, 4.5f,
        0.15f,
        0.0f
    };

    // TUNE
    static constexpr ModeParams Soft {
        "Soft",
        70.0f,
        180.0f, 1.5f,
        280.0f, -1.0f, 1.0f,
        0.0f,   0.0f,  1.0f,
        3000.0f, -1.5f, 1.0f,
        7000.0f, -2.0f,
        6500.0f, 7.0f, 3.0f,
        0.35f,
        0.0f
    };

    inline const ModeParams& get (int index)
    {
        switch (index) { case 1: return Clear; case 2: return Soft; default: return Crisp; }
    }
}

class VocalChain
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        sr = sampleRate;
        channels = juce::jlimit (1, 2, numChannels);
        juce::ignoreUnused (maxBlockSize);
        for (int ch = 0; ch < 2; ++ch)
        {
            hpf[ch].coefficients      = juce::dsp::IIR::Coefficients<float>::makeHighPass  (sr, 100.0f, 0.707f);
            lowShelf[ch].coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf  (sr, 180.0f, 0.7f, 1.0f);
            lowMid[ch].coefficients   = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, 250.0f, 1.0f, 1.0f);
            box[ch].coefficients      = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, 500.0f, 1.0f, 1.0f);
            presence[ch].coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, 3000.0f, 1.0f, 1.0f);
            hiShelf[ch].coefficients  = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, 8000.0f, 0.7f, 1.0f);
            deEss[ch].coefficients    = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, 7000.0f, 2.0f, 1.0f);
        }
        deEssDetect.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, 7000.0f, 2.0f);
        reset();
        setMode (modeIndex, true);
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            hpf[ch].reset(); lowShelf[ch].reset(); lowMid[ch].reset(); box[ch].reset();
            presence[ch].reset(); hiShelf[ch].reset(); deEss[ch].reset();
        }
        deEssDetect.reset();
        sibEnvDb = -100.0f; broadEnvDb = -100.0f; deEssDb = 0.0f;
    }

    void setMode (int index, bool force = false)
    {
        index = juce::jlimit (0, 2, index);
        if (index != modeIndex || force)
        {
            modeIndex = index;
            dirty = true;
        }
    }

    void setAmount (float a) { amount = juce::jlimit (0.0f, 1.0f, a); dirty = true; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        if (numSamples == 0) return;
        const int nCh = juce::jmin (channels, buffer.getNumChannels());
        const auto& p = FatboardModes::get (modeIndex);

        if (dirty) { updateStaticFilters (p); dirty = false; }

        // --- de-esser detection on a mono mix ------------------------------
        float sib = 0.0f, broad = 0.0f;
        for (int n = 0; n < numSamples; ++n)
        {
            float m = buffer.getReadPointer (0)[n];
            if (nCh > 1) m = 0.5f * (m + buffer.getReadPointer (1)[n]);
            const float s = deEssDetect.processSample (m);
            sib += s * s; broad += m * m;
        }
        const float blockMs = 1000.0f * float (numSamples) / float (sr);
        const float sibDb   = juce::Decibels::gainToDecibels (std::sqrt (sib   / float (numSamples)) + 1.0e-9f);
        const float broadDb = juce::Decibels::gainToDecibels (std::sqrt (broad / float (numSamples)) + 1.0e-9f);
        sibEnvDb   += (sibDb   - sibEnvDb)   * (sibDb   > sibEnvDb   ? coef (2.0f, blockMs)  : coef (60.0f, blockMs));
        broadEnvDb += (broadDb - broadEnvDb) * (broadDb > broadEnvDb ? coef (10.0f, blockMs) : coef (200.0f, blockMs));

        float wantDeEss = 0.0f;
        if (broadEnvDb > -60.0f)
        {
            // sibilance sticks out of the broadband level by more than the threshold → cut
            const float excess = (sibEnvDb - broadEnvDb) + 12.0f - p.deEssThresholdDb; // +12: 7k band is naturally ~12 dB under broadband
            if (excess > 0.0f) wantDeEss = juce::jmin (p.deEssMaxDb, excess) * amount;
        }
        deEssDb += (wantDeEss - deEssDb) * (wantDeEss > deEssDb ? coef (1.5f, blockMs) : coef (80.0f, blockMs));
        *deEss[0].coefficients = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, p.deEssHz, 2.0f,
                                                                                       juce::Decibels::decibelsToGain (-deEssDb));
        deEss[1].coefficients = deEss[0].coefficients;

        // --- run the chain -------------------------------------------------
        const float drive  = 1.0f + p.drive * amount * 6.0f;          // 1 .. 7
        const float makeup = 1.0f / std::tanh (drive * 0.7f) * 0.7f;   // keep loudness roughly even
        const float wet    = p.drive * amount;
        const float trim   = juce::Decibels::decibelsToGain (p.outTrimDb * amount);
        const bool  useBox = p.boxHz > 0.0f;

        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int n = 0; n < numSamples; ++n)
            {
                float x = d[n];
                x = hpf[ch].processSample (x);
                x = lowShelf[ch].processSample (x);
                x = lowMid[ch].processSample (x);
                if (useBox) x = box[ch].processSample (x);
                x = presence[ch].processSample (x);
                x = hiShelf[ch].processSample (x);
                x = deEss[ch].processSample (x);

                // saturation: tanh soft clip, blended
                const float sat = std::tanh (x * drive) * makeup;
                x = x + (sat - x) * wet;

                d[n] = x * trim;
            }
        }
    }

    float getDeEssDb() const { return deEssDb; }
    int   getMode()   const { return modeIndex; }

private:
    static float coef (float ms, float blockMs) { return 1.0f - std::exp (-blockMs / ms); }

    void updateStaticFilters (const ModeParams& p)
    {
        using C = juce::dsp::IIR::Coefficients<float>;
        const float a = amount;
        auto db = [a] (float v) { return juce::Decibels::decibelsToGain (v * a); };

        auto hp = C::makeHighPass   (sr, p.hpfHz, 0.707f);
        auto ls = C::makeLowShelf   (sr, p.lowShelfHz, 0.7f, db (p.lowShelfDb));
        auto lm = C::makePeakFilter (sr, p.lowMidHz, p.lowMidQ, db (p.lowMidDb));
        auto bx = C::makePeakFilter (sr, p.boxHz > 0.0f ? p.boxHz : 500.0f, p.boxQ, db (p.boxDb));
        auto pr = C::makePeakFilter (sr, p.presenceHz, p.presenceQ, db (p.presenceDb));
        auto hs = C::makeHighShelf  (sr, p.hiShelfHz, 0.7f, db (p.hiShelfDb));
        for (int ch = 0; ch < 2; ++ch)
        {
            *hpf[ch].coefficients = *hp;  *lowShelf[ch].coefficients = *ls;
            *lowMid[ch].coefficients = *lm; *box[ch].coefficients = *bx;
            *presence[ch].coefficients = *pr; *hiShelf[ch].coefficients = *hs;
        }
        *deEssDetect.coefficients = *C::makeBandPass (sr, p.deEssHz, 2.0f);
    }

    double sr = 48000.0;
    int channels = 2;
    int modeIndex = 0;
    float amount = 0.5f;
    bool dirty = true;

    juce::dsp::IIR::Filter<float> hpf[2], lowShelf[2], lowMid[2], box[2], presence[2], hiShelf[2], deEss[2];
    juce::dsp::IIR::Filter<float> deEssDetect;
    float sibEnvDb = -100.0f, broadEnvDb = -100.0f, deEssDb = 0.0f;
};
