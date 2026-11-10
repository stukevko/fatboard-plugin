#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>

// ============================================================================
//  Room – makes the BEAT step aside for the vocal
//
//  Put Scrub Pro on the beat / sample bus, pick "Room", send the vocal to the
//  sidechain. Six bands of the beat are pulled down only while the vocal is
//  there, and only in the bands the vocal is actually using right now. Between
//  lines the beat is untouched and full.
//
//  A plugin on the vocal can only make the voice louder or brighter. This is
//  the other half: the beat gives way, so the voice does not have to shout.
// ============================================================================

class RoomCarver
{
public:
    static constexpr int numBands = 6;

    struct Settings
    {
        float bandHz[numBands]  = { 300.0f, 600.0f, 1200.0f, 2400.0f, 4000.0f, 6500.0f };   // TUNE
        float weight[numBands]  = { 0.35f,  0.6f,   0.9f,    1.0f,    0.9f,    0.5f   };    // TUNE – speech lives in 1–4 kHz
        float bandQ = 1.4f;              // TUNE
        float maxDb = 4.0f;              // TUNE – per band at Amount = 100 % (neighbours overlap: ~6 dB in the speech range)
        float attackMs = 12.0f;          // TUNE – beat steps aside quickly …
        float releaseMs = 160.0f;        // TUNE – … and comes back smoothly
        float presentDb = -48.0f;        // vocal quieter than this = nobody is singing
        float shareDb = 16.0f;           // TUNE – a band counts when it is within this many dB of the vocal's level
    };
    Settings settings;

    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate; channels = juce::jlimit (1, 2, numChannels);
        using C = juce::dsp::IIR::Coefficients<float>;
        for (int b = 0; b < numBands; ++b)
        {
            det[b].coefficients = C::makeBandPass (sr, settings.bandHz[b], settings.bandQ); det[b].reset();
            for (int ch = 0; ch < 2; ++ch) { cut[ch][b].coefficients = C::makePeakFilter (sr, settings.bandHz[b], settings.bandQ, 1.0f); cut[ch][b].reset(); }
            cutDb[(size_t) b] = 0.0f;
        }
        vocalDb = -100.0f;
    }

    void reset()
    {
        for (int b = 0; b < numBands; ++b) { det[b].reset(); for (int ch = 0; ch < 2; ++ch) cut[ch][b].reset(); cutDb[(size_t) b] = 0.0f; }
    }

    /** `vocal` = sidechain (may be null → nothing happens, the beat passes untouched). */
    void process (juce::AudioBuffer<float>& beat, const juce::AudioBuffer<float>* vocal, float amount)
    {
        const int n = beat.getNumSamples();
        if (n == 0) return;
        const int nCh = juce::jmin (channels, beat.getNumChannels());
        const float blockMs = 1000.0f * (float) n / (float) sr;
        const float aCoef = 1.0f - std::exp (-blockMs / settings.attackMs);
        const float rCoef = 1.0f - std::exp (-blockMs / settings.releaseMs);

        float want[numBands] = { 0, 0, 0, 0, 0, 0 };
        linked = vocal != nullptr && vocal->getNumChannels() > 0 && vocal->getNumSamples() >= n;
        if (linked)
        {
            float e[numBands] = { 0, 0, 0, 0, 0, 0 }; float tot = 0.0f;
            const int vc = vocal->getNumChannels();
            for (int i = 0; i < n; ++i)
            {
                float m = vocal->getReadPointer (0)[i]; if (vc > 1) m = 0.5f * (m + vocal->getReadPointer (1)[i]);
                tot += m * m;
                for (int b = 0; b < numBands; ++b) { const float v = det[b].processSample (m); e[b] += v * v; }
            }
            vocalDb = dB (tot, n);
            if (vocalDb > settings.presentDb)
            {
                // how loud is the vocal overall? quiet words need less room than a shouted line
                const float presence = juce::jlimit (0.0f, 1.0f, (vocalDb - settings.presentDb) / 24.0f);
                for (int b = 0; b < numBands; ++b)
                {
                    const float rel = dB (e[b], n) - (vocalDb - settings.shareDb);      // > 0 → the vocal uses this band
                    if (rel > 0.0f)
                        want[b] = juce::jmin (settings.maxDb, rel * 0.55f) * settings.weight[b] * presence * amount;
                }
            }
        }
        else vocalDb = -100.0f;

        using C = juce::dsp::IIR::Coefficients<float>;
        bool any = false;
        for (int b = 0; b < numBands; ++b)
        {
            auto& c = cutDb[(size_t) b];
            c += (want[b] - c) * (want[b] > c ? aCoef : rCoef);
            if (c > 0.05f) any = true;
            *cut[0][b].coefficients = *C::makePeakFilter (sr, settings.bandHz[b], settings.bandQ, juce::Decibels::decibelsToGain (-c));
            cut[1][b].coefficients = cut[0][b].coefficients;
        }
        if (! any && ! wasActive) return;          // nothing to do: bit-transparent
        wasActive = any;

        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* d = beat.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                float x = d[i];
                for (int b = 0; b < numBands; ++b) x = cut[ch][b].processSample (x);
                d[i] = x;
            }
        }
    }

    bool  isLinked() const { return linked; }
    bool  vocalPresent() const { return vocalDb > settings.presentDb; }
    float getCutDb (int b) const { return cutDb[(size_t) juce::jlimit (0, numBands - 1, b)]; }
    float getTotalCutDb() const { float s = 0; for (float c : cutDb) s += c; return s; }

private:
    static float dB (float e, int n) { return juce::Decibels::gainToDecibels (std::sqrt (e / (float) n) + 1.0e-9f); }

    double sr = 48000.0; int channels = 2;
    juce::dsp::IIR::Filter<float> det[numBands], cut[2][numBands];
    std::array<float, numBands> cutDb {};
    float vocalDb = -100.0f; bool linked = false, wasActive = false;
};
