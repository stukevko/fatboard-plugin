#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>

// ============================================================================
//  Scrub – the voice curve   (v0.6)
//
//  Kevin's standard vocal EQ (read out of his FL "Parametric EQ 2" preset),
//  built in as the fixed base of Scrub and Scrub Pro:
//
//      high-pass 131 Hz
//      192 Hz  -1.8 dB  (wide)
//      313 Hz  -1.7 dB
//      634 Hz  -1.6 dB
//      1.35 kHz -2.0 dB (narrow)
//      3.13 kHz -3.4 dB (very narrow)
//      low-pass 10 kHz
//
//  One amount (0..1) dials the whole curve in: gains scale, the high-pass
//  travels up from 20 Hz, the low-pass comes down from 20 kHz. At 1 it IS the
//  EQ above. At 0 the signal is bit-identical.
//
//  The automatic cleanup runs between the two halves:
//      processPre  (high-pass + 5 cuts)  →  cleanup (→ air)  →  processLowPass
//
//  TUNE: frequencies and gains are exact from the preset. The Q values are an
//  estimate of FL's "width" (25412 / 9828 / 6552 / 3276 / 0 of 65536); the
//  filter slope of band 1 and 7 is FL's default, taken as 12 dB/oct here.
// ============================================================================

namespace FatboardCurve
{
    struct Band { float hz, db, q; };

    static constexpr float hpHz = 131.0f;
    static constexpr float lpHz = 10000.0f;
    static constexpr int   numBands = 5;
    static constexpr Band  bands[numBands] = {
        {  192.0f, -1.8f, 0.9f },
        {  313.0f, -1.7f, 2.0f },
        {  634.0f, -1.6f, 3.0f },
        { 1354.0f, -2.0f, 5.0f },
        { 3130.0f, -3.4f, 8.0f },
    };

    // the knob reaches the full curve here, so normal settings already give "the EQ"
    static constexpr float fullAt = 0.6f;
    inline float fromKnob (float knob) { return juce::jlimit (0.0f, 1.0f, knob / fullAt); }
}

class VoiceCurve
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;
        channels = juce::jlimit (1, 2, numChannels);
        using C = juce::dsp::IIR::Coefficients<float>;
        for (int ch = 0; ch < 2; ++ch)
        {
            hp[ch].coefficients = C::makeHighPass (sr, 20.0f, 0.707f);
            lp[ch].coefficients = C::makeLowPass  (sr, juce::jmin (20000.0f, (float) sr * 0.45f), 0.707f);
            for (int b = 0; b < FatboardCurve::numBands; ++b)
                pk[ch][b].coefficients = C::makePeakFilter (sr, FatboardCurve::bands[b].hz, FatboardCurve::bands[b].q, 1.0f);
        }
        reset();
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch) { hp[ch].reset(); lp[ch].reset(); for (auto& f : pk[ch]) f.reset(); }
        current = -1.0f; preOn = lpOn = false;
    }

    /** Call once per block before processPre. amount 0..1. */
    void setAmount (float amount, int blockSamples)
    {
        amount = juce::jlimit (0.0f, 1.0f, amount);
        if (current < 0.0f) current = amount;
        // glide (40 ms) so turning the knob never zips
        const float c = 1.0f - std::exp (-(float) blockSamples / (0.040f * (float) sr));
        float next = current + (amount - current) * c;
        if (std::abs (next - amount) < 0.0005f) next = amount;
        if (next != applied) { current = next; update(); } else current = next;
    }

    void processPre (juce::AudioBuffer<float>& buffer)
    {
        if (applied <= 0.0f) { preOn = false; return; }                  // bit-identical
        const int nCh = juce::jmin (channels, buffer.getNumChannels());
        if (! preOn) { for (int ch = 0; ch < 2; ++ch) { hp[ch].reset(); for (auto& f : pk[ch]) f.reset(); } preOn = true; }
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int n = 0; n < buffer.getNumSamples(); ++n)
            {
                float x = hp[ch].processSample (d[n]);
                for (int b = 0; b < FatboardCurve::numBands; ++b) x = pk[ch][b].processSample (x);
                d[n] = x;
            }
        }
    }

    void processLowPass (juce::AudioBuffer<float>& buffer)
    {
        if (applied <= 0.0f) { lpOn = false; return; }
        const int nCh = juce::jmin (channels, buffer.getNumChannels());
        if (! lpOn) { for (int ch = 0; ch < 2; ++ch) lp[ch].reset(); lpOn = true; }
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int n = 0; n < buffer.getNumSamples(); ++n) d[n] = lp[ch].processSample (d[n]);
        }
    }

    float getAmount() const { return applied; }

private:
    void update()
    {
        using C = juce::dsp::IIR::Coefficients<float>;
        applied = current;
        const float a = applied;
        const float hpF = 20.0f * std::pow (FatboardCurve::hpHz / 20.0f, a);
        const float lpF = juce::jmin (20000.0f * std::pow (FatboardCurve::lpHz / 20000.0f, a), (float) sr * 0.45f);
        auto h = C::makeHighPass (sr, hpF, 0.707f);
        auto l = C::makeLowPass  (sr, lpF, 0.707f);
        for (int ch = 0; ch < 2; ++ch) { *hp[ch].coefficients = *h; *lp[ch].coefficients = *l; }
        for (int b = 0; b < FatboardCurve::numBands; ++b)
        {
            const auto& bd = FatboardCurve::bands[b];
            auto p = C::makePeakFilter (sr, bd.hz, bd.q, juce::Decibels::decibelsToGain (bd.db * a));
            for (int ch = 0; ch < 2; ++ch) *pk[ch][b].coefficients = *p;
        }
    }

    double sr = 48000.0;
    int channels = 2;
    float current = -1.0f, applied = 0.0f;
    bool preOn = false, lpOn = false;
    juce::dsp::IIR::Filter<float> hp[2], lp[2], pk[2][FatboardCurve::numBands];
};
