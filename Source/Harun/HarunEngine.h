#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include <cmath>
#include "VoiceCurve.h"
#include "HarunSpace.h"

// ============================================================================
//  Harun v2 – the double / harmony track, finished
//
//  You recorded the double (or the harmony). Put Harun on THAT track and pick
//  a template. No sidechain, no routing.
//
//    in ─┬─► L path: short delay a ──────────────┐
//        └─► R path: short delay b, all-pass ────┤
//                                                ▼
//            EQ from the Scrub family  →  presence dip  →  saturation
//                                                ▼
//                          width  →  level  →  + hall / echo (ducked)
//
//  Why it fits a lead that went through Scrub:
//   - the layer gets the SAME voice curve (same mud and harsh cuts), so lead and
//     layer are one tonal family …
//   - … but cut higher at the bottom, lower at the top, and with a wide dip at
//     3 kHz: the lead keeps the body, the presence and the air for itself
//   - two arrival times + an all-pass on one side → wide, mono stays clean
//   - a space that is already filtered, de-essed and ducked
//
//  No pitch shifting, zero latency.
//  All TUNE values are in the templates below.
// ============================================================================

struct HarunTemplate
{
    const char* name;
    // tone: the Scrub voice curve, shaped for a layer
    float lowCutHz, mud, harsh, topHz;
    float presenceDb;                 // wide dip at 3 kHz – the lead owns this
    float drive;                      // 0..1 saturation
    // what the template writes into the knobs
    float width;                      // 0..1
    float levelDb;                    // layer level
    float space;                      // 0..1
    int   spaceType;                  // 0 Hall, 1 Echo, 2 Hall + Echo
    // fixed character of the space
    HarunSpace::Space hall;
    HarunSpace::Echo  echo;
};

namespace HarunTemplates
{
    static constexpr int count = 4;
    // TUNE                                    name       lowCut  mud   harsh  top      pres   drive  width  level  space  type  hall                      echo
    static constexpr HarunTemplate all[count] = {
        { "Double",  150.0f, 1.0f, 1.0f, 9000.0f, -2.5f, 0.25f, 0.75f,  -6.0f, 0.12f, 0, HarunSpace::Space::Tight, HarunSpace::Echo::Eighth  },
        { "Wide",    170.0f, 1.2f, 1.0f, 8500.0f, -3.5f, 0.30f, 1.00f,  -7.0f, 0.18f, 0, HarunSpace::Space::Room,  HarunSpace::Echo::Eighth  },
        { "Harmony", 220.0f, 1.3f, 0.8f, 9500.0f, -3.0f, 0.15f, 0.85f,  -8.0f, 0.28f, 0, HarunSpace::Space::Room,  HarunSpace::Echo::Quarter },
        { "Far",     250.0f, 1.4f, 1.0f, 7000.0f, -4.5f, 0.20f, 1.00f, -10.0f, 0.40f, 2, HarunSpace::Space::Far,   HarunSpace::Echo::Quarter },
    };
    inline const HarunTemplate& get (int i) { return all[juce::jlimit (0, count - 1, i)]; }

    static constexpr float presenceHz = 3000.0f, presenceQ = 0.7f;

    inline FatboardCurve::Shape shapeOf (const HarunTemplate& t)
    {
        FatboardCurve::Shape s; s.lowCutHz = t.lowCutHz; s.mud = t.mud; s.harsh = t.harsh; s.topHz = t.topHz; return s;
    }

    /** The layer EQ at one frequency (dB). For the display. */
    inline float responseDb (const HarunTemplate& t, float hz)
    {
        return VoiceCurve::responseDb (hz, 1.0f, shapeOf (t)) + VoiceCurve::bellDb (hz, presenceHz, presenceQ, t.presenceDb);
    }
}

class HarunEngine
{
public:
    struct Settings
    {
        // timing (ms) – Haas offsets. Small! 20+ ms starts to sound like a slap.
        float delayLMs = 8.5f;              // TUNE
        float delayRMs = 13.0f;             // TUNE
        float wobbleMs = 0.35f;             // TUNE – tiny, slow movement so it isn't static
        float wobbleHz = 0.05f;             // TUNE
        // decorrelation: all-pass chain on R (frequency-dependent phase, flat magnitude)
        float allpassHz[3] = { 420.0f, 1600.0f, 5200.0f };         // TUNE
        float spaceDuck = 0.6f;             // TUNE – how far the space is pushed down while the layer sings
    };
    Settings settings;

    void prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        sr = sampleRate; channels = juce::jlimit (1, 2, numChannels);
        mono.setSize (1, maxBlockSize);
        layer.setSize (2, maxBlockSize);
        const int maxDelay = (int) (sr * 0.04);
        for (int i = 0; i < 2; ++i)
        {
            auto& p = path[(size_t) i];
            p.delay.assign ((size_t) maxDelay, 0.0f); p.w = 0;
            for (int k = 0; k < 3; ++k)
            {
                p.ap[k].coefficients = juce::dsp::IIR::Coefficients<float>::makeAllPass (sr, settings.allpassHz[k], 0.6f);
                p.ap[k].reset();
            }
            p.pres.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, HarunTemplates::presenceHz, HarunTemplates::presenceQ, 1.0f);
            p.pres.reset();
            p.lfoPhase = (float) i * 1.7f;
        }
        curve.prepare (sr, 2);
        space.prepare (sr, maxBlockSize, channels);
        applyTemplate (templateIndex);
        gain = juce::Decibels::decibelsToGain (levelDb);
        levelNow = -100.0f;
    }

    void reset()
    {
        for (auto& p : path) { std::fill (p.delay.begin(), p.delay.end(), 0.0f); p.pres.reset(); for (auto& a : p.ap) a.reset(); }
        curve.reset(); space.reset();
    }

    /** spaceType: 0 Hall, 1 Echo, 2 Hall + Echo. */
    void setParams (int templateIdx, float widthIn, float levelDbIn, float spaceIn, int spaceType, double bpm)
    {
        templateIdx = juce::jlimit (0, HarunTemplates::count - 1, templateIdx);
        if (templateIdx != templateIndex) applyTemplate (templateIdx);
        width   = juce::jlimit (0.0f, 1.0f, widthIn);
        levelDb = juce::jlimit (-24.0f, 6.0f, levelDbIn);
        const auto& t = HarunTemplates::get (templateIndex);
        space.setParams (t.hall, spaceType != 1, spaceType == 0 ? HarunSpace::Echo::Off : t.echo,
                         juce::jlimit (0.0f, 1.0f, spaceIn), settings.spaceDuck, bpm);
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int n = buffer.getNumSamples();
        if (n == 0) return;
        const int nCh = juce::jmin (channels, buffer.getNumChannels());
        const float blockMs = 1000.0f * (float) n / (float) sr;
        const auto& t = HarunTemplates::get (templateIndex);

        // ---- mono source of the layer ----------------------------------------
        auto* m = mono.getWritePointer (0);
        juce::FloatVectorOperations::copy (m, buffer.getReadPointer (0), n);
        if (nCh > 1) { juce::FloatVectorOperations::add (m, buffer.getReadPointer (1), n); juce::FloatVectorOperations::multiply (m, 0.5f, n); }

        // ---- two arrival times, one side coloured ------------------------------
        for (int side = 0; side < 2; ++side)
        {
            auto& p = path[(size_t) side];
            p.lfoPhase += juce::MathConstants<float>::twoPi * settings.wobbleHz * blockMs * 0.001f;
            const float baseMs = side == 0 ? settings.delayLMs : settings.delayRMs;
            const float dMs = baseMs + std::sin (p.lfoPhase) * settings.wobbleMs;
            const float dS = juce::jlimit (1.0f, (float) p.delay.size() - 2.0f, dMs * 0.001f * (float) sr);
            const int len = (int) p.delay.size();
            auto* out = layer.getWritePointer (side);
            for (int i = 0; i < n; ++i)
            {
                p.delay[(size_t) p.w] = m[i];
                float rp = (float) p.w - dS; if (rp < 0) rp += (float) len;
                const int i0 = (int) rp; const float fr = rp - (float) i0; const int i1 = (i0 + 1) % len;
                float y = p.delay[(size_t) i0] + (p.delay[(size_t) i1] - p.delay[(size_t) i0]) * fr;
                p.w = (p.w + 1) % len;
                if (side == 1) for (auto& a : p.ap) y = a.processSample (y);
                out[i] = y;
            }
        }

        // ---- tone: Scrub family curve + presence dip + a hair of saturation ------
        juce::AudioBuffer<float> view (layer.getArrayOfWritePointers(), 2, n);
        curve.setAmount (1.0f, n);
        curve.processPre (view);
        const float drv = 1.0f + t.drive * 4.0f, wet = t.drive;
        for (int side = 0; side < 2; ++side)
        {
            auto* d = view.getWritePointer (side);
            auto& pres = path[(size_t) side].pres;
            for (int i = 0; i < n; ++i)
            {
                float y = pres.processSample (d[i]);
                const float sat = std::tanh (y * drv) / drv;
                d[i] = y + (sat - y) * wet;
            }
        }
        curve.processLowPass (view);

        // ---- width + level → replaces the track ----------------------------------
        const float wantGain = juce::Decibels::decibelsToGain (levelDb);
        const float same = 0.5f + 0.5f * width, other = 0.5f - 0.5f * width;
        const float* L = view.getReadPointer (0); const float* R = view.getReadPointer (1);
        double acc = 0.0;
        for (int i = 0; i < n; ++i)
        {
            gain += (wantGain - gain) * 0.002f;
            const float l = L[i] * gain, r = R[i] * gain;
            if (nCh > 1)
            {
                buffer.getWritePointer (0)[i] = l * same + r * other;
                buffer.getWritePointer (1)[i] = r * same + l * other;
            }
            else buffer.getWritePointer (0)[i] = 0.5f * (l + r);
            acc += 0.5 * ((double) l * l + (double) r * r);
        }
        const float db = juce::Decibels::gainToDecibels ((float) std::sqrt (acc / n) + 1.0e-9f);
        levelNow += (db - levelNow) * (db > levelNow ? 0.5f : 0.08f);

        // ---- hall / echo on top ---------------------------------------------------
        space.process (buffer);
    }

    // ---- UI ---------------------------------------------------------------
    float getActivity() const { return juce::jlimit (0.0f, 1.0f, (levelNow + 60.0f) / 50.0f); }
    float getSpaceMeter() const { return space.getWetMeter(); }
    int   getTemplate() const { return templateIndex; }

private:
    void applyTemplate (int idx)
    {
        templateIndex = idx;
        const auto& t = HarunTemplates::get (idx);
        curve.setShape (HarunTemplates::shapeOf (t));
        auto c = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, HarunTemplates::presenceHz, HarunTemplates::presenceQ,
                                                                      juce::Decibels::decibelsToGain (t.presenceDb));
        for (auto& p : path) *p.pres.coefficients = *c;
    }

    struct Path
    {
        std::vector<float> delay; int w = 0;
        juce::dsp::IIR::Filter<float> pres, ap[3];
        float lfoPhase = 0.0f;
    };

    double sr = 48000.0; int channels = 2;
    juce::AudioBuffer<float> mono, layer;
    std::array<Path, 2> path;
    VoiceCurve curve;
    HarunSpace space;
    int templateIndex = 0;
    float width = 0.75f, levelDb = -6.0f, gain = 0.5f, levelNow = -100.0f;
};
