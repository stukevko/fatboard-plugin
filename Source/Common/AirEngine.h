#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>

// ============================================================================
//  Scrub Pro – Air   (v0.6)
//
//  One knob that opens the voice up: a wide "mid air" shelf for presence and
//  a "high air" shelf for shine. Not a static treble boost: a guard listens
//  to the sibilance band and takes the lift away for the few milliseconds an
//  S or T would otherwise turn sharp. So the knob can go far without ice pick.
//
//  Sits after the cleanup and before the curve's 10 kHz low-pass.
//  air 0 → bit-identical.
// ============================================================================

namespace FatboardAir
{
    // TUNE
    static constexpr float midHz = 3500.0f, midMaxDb = 5.0f, midQ = 0.6f;
    static constexpr float highHz = 8000.0f, highMaxDb = 6.5f, highQ = 0.7f;
    static constexpr float guardHz = 7000.0f, guardQ = 1.6f;
    static constexpr float guardThresholdDb = 4.0f;       // how far sibilance may stick out before the guard reacts
    static constexpr float guardFadeDb = 4.0f;            // this much excess takes the air lift away completely ...
    static constexpr float guardCutDb  = 3.0f;            // ... and beyond that the guard cuts up to this (light de-ess)
}

class AirEngine
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;
        channels = juce::jlimit (1, 2, numChannels);
        using C = juce::dsp::IIR::Coefficients<float>;
        for (int ch = 0; ch < 2; ++ch)
        {
            mid[ch].coefficients   = C::makeHighShelf  (sr, FatboardAir::midHz,  FatboardAir::midQ,  1.0f);
            high[ch].coefficients  = C::makeHighShelf  (sr, FatboardAir::highHz, FatboardAir::highQ, 1.0f);
            guard[ch].coefficients = C::makePeakFilter (sr, FatboardAir::guardHz, FatboardAir::guardQ, 1.0f);
        }
        detect.coefficients = C::makeBandPass (sr, FatboardAir::guardHz, 2.0f);
        reset();
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch) { mid[ch].reset(); high[ch].reset(); guard[ch].reset(); }
        detect.reset();
        sibEnvDb = broadEnvDb = -100.0f; guardDb = 0.0f; smoothed = -1.0f; on = false;
    }

    void process (juce::AudioBuffer<float>& buffer, float air)
    {
        using namespace FatboardAir;
        const int numSamples = buffer.getNumSamples();
        const int nCh = juce::jmin (channels, buffer.getNumChannels());
        if (numSamples == 0 || nCh == 0) return;
        air = juce::jlimit (0.0f, 1.0f, air);
        const float blockMs = 1000.0f * (float) numSamples / (float) sr;
        if (smoothed < 0.0f) smoothed = air;
        smoothed += (air - smoothed) * coef (40.0f, blockMs);
        if (std::abs (smoothed - air) < 0.0005f) smoothed = air;

        if (smoothed <= 0.0f && guardDb < 0.01f) { on = false; guardDb = 0.0f; return; }     // bit-identical
        if (! on) { for (int ch = 0; ch < 2; ++ch) { mid[ch].reset(); high[ch].reset(); guard[ch].reset(); } on = true; }

        // --- guard detection on the INPUT (mono) ---
        double sib = 0.0, broad = 0.0;
        const float* in0 = buffer.getReadPointer (0);
        const float* in1 = nCh > 1 ? buffer.getReadPointer (1) : nullptr;
        for (int n = 0; n < numSamples; ++n)
        {
            const float m = in1 ? 0.5f * (in0[n] + in1[n]) : in0[n];
            const float s = detect.processSample (m);
            sib += (double) s * s; broad += (double) m * m;
        }
        const float sibDb   = 10.0f * (float) std::log10 (sib   / numSamples + 1.0e-12);
        const float broadDb = 10.0f * (float) std::log10 (broad / numSamples + 1.0e-12);
        sibEnvDb   += (sibDb   - sibEnvDb)   * (sibDb   > sibEnvDb   ? coef (2.0f, blockMs)  : coef (60.0f, blockMs));
        broadEnvDb += (broadDb - broadEnvDb) * (broadDb > broadEnvDb ? coef (10.0f, blockMs) : coef (200.0f, blockMs));

        float want = 0.0f;
        if (broadEnvDb > -60.0f && smoothed > 0.0f)
        {
            // the 7 kHz band normally sits ~12 dB under the broadband level of a voice
            const float excess = (sibEnvDb - broadEnvDb) + 12.0f - guardThresholdDb;
            if (excess > 0.0f) want = juce::jmin (guardFadeDb + guardCutDb, excess);
        }
        guardDb += (want - guardDb) * (want > guardDb ? coef (1.5f, blockMs) : coef (80.0f, blockMs));

        using C = juce::dsp::IIR::Coefficients<float>;
        // the guard first takes the lift itself away (the shelves are what would make an S sharp),
        // only what is left after that becomes a small cut
        const float lift = smoothed * (1.0f - juce::jlimit (0.0f, 1.0f, guardDb / guardFadeDb));
        const float cut  = juce::jlimit (0.0f, guardCutDb, guardDb - guardFadeDb);
        *mid[0].coefficients   = *C::makeHighShelf  (sr, midHz,  midQ,  juce::Decibels::decibelsToGain (midMaxDb  * lift));
        *high[0].coefficients  = *C::makeHighShelf  (sr, highHz, highQ, juce::Decibels::decibelsToGain (highMaxDb * lift));
        *guard[0].coefficients = *C::makePeakFilter (sr, guardHz, guardQ, juce::Decibels::decibelsToGain (-cut));
        mid[1].coefficients = mid[0].coefficients; high[1].coefficients = high[0].coefficients; guard[1].coefficients = guard[0].coefficients;

        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int n = 0; n < numSamples; ++n)
                d[n] = guard[ch].processSample (high[ch].processSample (mid[ch].processSample (d[n])));
        }
    }

    float getGuardDb() const { return guardDb; }

private:
    static float coef (float ms, float blockMs) { return 1.0f - std::exp (-blockMs / ms); }

    double sr = 48000.0;
    int channels = 2;
    juce::dsp::IIR::Filter<float> mid[2], high[2], guard[2], detect;
    float sibEnvDb = -100.0f, broadEnvDb = -100.0f, guardDb = 0.0f, smoothed = -1.0f;
    bool on = false;
};
