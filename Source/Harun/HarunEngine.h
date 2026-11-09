#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include <cmath>

// ============================================================================
//  Harun v1 – the double/harmony track, finished
//
//  You recorded the double (or the harmony). Put Harun on THAT track. It turns
//  the single take into the wide, tucked-in L/R layer a mixer would build by
//  hand – without the hand:
//
//    in ─┬─► L path: short delay a, tone, saturation ─► pan L
//        └─► R path: short delay b, all-pass colour,  ─► pan R
//                    tone, saturation
//                                     × auto level (vs. the lead on the sidechain,
//                                       or vs. the track's own loudness)
//
//  What makes it sound "mixed":
//   - two slightly different arrival times (Haas) → width without phase smear
//   - an all-pass "colour" on one side → the sides decorrelate, mono stays clean
//   - the low end is taken out and the top rolled a touch → layer sits behind
//   - a hair of saturation glues it to the lead
//   - AUTO LEVEL: the layer follows the lead's loudness (sidechain) a few dB
//     under it, so it never pokes out and never vanishes
//
//  No pitch shifting, no detection, zero latency. One knob (Amount), one
//  Width, and a Tuck knob for how far under the lead it sits.
//
//  All TUNE values in Settings.
// ============================================================================

class HarunEngine
{
public:
    enum class Mode { Double = 0, Harmony = 1 };     // Harmony: a bit darker, further back, less wide

    struct Settings
    {
        // timing (ms) – Haas offsets. Small! 20+ ms starts to sound like a slap.
        float delayLMs = 8.5f;              // TUNE
        float delayRMs = 13.0f;             // TUNE
        float wobbleMs = 0.35f;             // TUNE – tiny, slow movement so it isn't static
        float wobbleHz = 0.05f;             // TUNE
        // tone of the layer
        float hpfHz = 160.0f;               // TUNE – Double
        float hpfHzHarmony = 220.0f;        // TUNE – Harmony sits further back
        float lowShelfHz = 300.0f;  float lowShelfDb = -2.0f;      // TUNE
        float presenceHz = 3200.0f; float presenceDb = -1.5f;      // TUNE – the lead owns the presence
        float airHz = 9000.0f;      float airDb = -2.0f;           // TUNE
        float airDbHarmony = -3.5f;                                // TUNE
        // decorrelation: all-pass chain on R (frequency-dependent phase, flat magnitude)
        float allpassHz[3] = { 420.0f, 1600.0f, 5200.0f };         // TUNE
        // saturation (0..1 drive at Amount = 1)
        float drive = 0.25f;                // TUNE
        // level
        float layerDbDouble  = -6.0f;       // TUNE – under the lead at Tuck = 50 %
        float layerDbHarmony = -8.0f;       // TUNE
        float tuckRangeDb    = 10.0f;       // TUNE – Tuck knob range (0 % = +5 dB louder than default, 100 % = -5 dB)
        float followAttackMs = 25.0f;       // TUNE – auto level follows the lead this fast
        float followReleaseMs = 180.0f;     // TUNE
        float followMaxDb = 12.0f;          // TUNE – never ride more than this
        float gateDb = -60.0f;
    };
    Settings settings;

    void prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        sr = sampleRate; channels = juce::jlimit (1, 2, numChannels);
        mono.setSize (1, maxBlockSize);
        const int maxDelay = (int) (sr * 0.04);
        for (int i = 0; i < 2; ++i)
        {
            auto& p = path[(size_t) i];
            p.delay.assign ((size_t) maxDelay, 0.0f); p.w = 0;
            p.hpf.coefficients  = juce::dsp::IIR::Coefficients<float>::makeHighPass  (sr, settings.hpfHz, 0.707f);
            p.low.coefficients  = juce::dsp::IIR::Coefficients<float>::makeLowShelf  (sr, settings.lowShelfHz, 0.7f, juce::Decibels::decibelsToGain (settings.lowShelfDb));
            p.pres.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, settings.presenceHz, 1.0f, juce::Decibels::decibelsToGain (settings.presenceDb));
            p.air.coefficients  = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, settings.airHz, 0.7f, juce::Decibels::decibelsToGain (settings.airDb));
            for (int k = 0; k < 3; ++k)
            {
                p.ap[k].coefficients = juce::dsp::IIR::Coefficients<float>::makeAllPass (sr, settings.allpassHz[k], 0.6f);
                p.ap[k].reset();
            }
            p.hpf.reset(); p.low.reset(); p.pres.reset(); p.air.reset();
            p.lfoPhase = (float) i * 1.7f;
        }
        applyMode (mode, true);
        layerEnvDb = -100.0f; leadEnvDb = -100.0f; followGain = 1.0f; outGain = 0.0f;
    }

    void reset()
    {
        for (auto& p : path) { std::fill (p.delay.begin(), p.delay.end(), 0.0f); p.hpf.reset(); p.low.reset(); p.pres.reset(); p.air.reset(); for (auto& a : p.ap) a.reset(); }
        layerEnvDb = -100.0f; leadEnvDb = -100.0f; followGain = 1.0f;
    }

    void setParams (Mode m, float amountIn, float widthIn, float tuckIn, bool sidechainPresent)
    {
        if (m != mode) applyMode (m, false);
        amount = juce::jlimit (0.0f, 1.0f, amountIn);
        width  = juce::jlimit (0.0f, 1.0f, widthIn);
        tuck   = juce::jlimit (0.0f, 1.0f, tuckIn);
        hasSidechain = sidechainPresent;
    }

    /** `lead` may be null (no sidechain): the layer then follows its own loudness. */
    void process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>* lead)
    {
        const int n = buffer.getNumSamples();
        if (n == 0) return;
        const int nCh = juce::jmin (channels, buffer.getNumChannels());
        const float blockMs = 1000.0f * (float) n / (float) sr;

        // ---- mono source of the layer ----------------------------------------
        auto* m = mono.getWritePointer (0);
        juce::FloatVectorOperations::copy (m, buffer.getReadPointer (0), n);
        if (nCh > 1) { juce::FloatVectorOperations::add (m, buffer.getReadPointer (1), n); juce::FloatVectorOperations::multiply (m, 0.5f, n); }

        // ---- envelopes ---------------------------------------------------------
        const float layerDb = rmsDb (m, n);
        layerEnvDb += (layerDb - layerEnvDb) * (layerDb > layerEnvDb ? coef (settings.followAttackMs, blockMs) : coef (settings.followReleaseMs, blockMs));
        float leadDb = -100.0f;
        if (lead != nullptr && lead->getNumChannels() > 0 && lead->getNumSamples() >= n)
        {
            double acc = 0.0; const int lc = lead->getNumChannels();
            for (int ch = 0; ch < lc; ++ch) { const float* l = lead->getReadPointer (ch); for (int i = 0; i < n; ++i) acc += l[i] * l[i]; }
            leadDb = juce::Decibels::gainToDecibels ((float) std::sqrt (acc / (double) (n * lc)) + 1.0e-9f);
            leadEnvDb += (leadDb - leadEnvDb) * (leadDb > leadEnvDb ? coef (settings.followAttackMs, blockMs) : coef (settings.followReleaseMs, blockMs));
        }

        // ---- auto level ----------------------------------------------------------
        // target: layer sits `layerDb + tuck offset` under the lead. Without a sidechain the
        // layer just gets a gentle levelling against its own long-term loudness.
        const float tuckDb = (0.5f - tuck) * settings.tuckRangeDb;                 // +5 .. -5
        const float targetUnderDb = (mode == Mode::Double ? settings.layerDbDouble : settings.layerDbHarmony) + tuckDb;
        float ride = 0.0f;
        if (hasSidechain && leadEnvDb > settings.gateDb && layerEnvDb > settings.gateDb)
            ride = (leadEnvDb + targetUnderDb) - layerEnvDb;                        // dB we need to add (or remove)
        else if (layerEnvDb > settings.gateDb)
            ride = (longTermDb + targetUnderDb + 6.0f) - layerEnvDb;                 // mild self-levelling
        ride = juce::jlimit (-settings.followMaxDb, settings.followMaxDb, ride);
        const float wantFollow = juce::Decibels::decibelsToGain (ride);
        followGain += (wantFollow - followGain) * coef (wantFollow < followGain ? settings.followAttackMs : settings.followReleaseMs, blockMs);
        if (layerEnvDb > settings.gateDb) longTermDb += (layerEnvDb - longTermDb) * coef (3000.0f, blockMs);

        const float wantOut = amount;
        outGain += (wantOut - outGain) * coef (20.0f, blockMs);

        // ---- two paths ---------------------------------------------------------
        const float drv = 1.0f + settings.drive * amount * 4.0f;
        const float wet = settings.drive * amount;
        for (int side = 0; side < 2; ++side)
        {
            auto& p = path[(size_t) side];
            p.lfoPhase += juce::MathConstants<float>::twoPi * settings.wobbleHz * blockMs * 0.001f;
            const float baseMs = side == 0 ? settings.delayLMs : settings.delayRMs;
            const float dMs = baseMs + std::sin (p.lfoPhase) * settings.wobbleMs;
            const float dS = juce::jlimit (1.0f, (float) p.delay.size() - 2.0f, dMs * 0.001f * (float) sr);
            const int len = (int) p.delay.size();
            auto* out = side == 0 ? outL.data() : outR.data();
            if ((int) outL.size() < n) { outL.resize ((size_t) n); outR.resize ((size_t) n); out = side == 0 ? outL.data() : outR.data(); }
            for (int i = 0; i < n; ++i)
            {
                p.delay[(size_t) p.w] = m[i];
                float rp = (float) p.w - dS; if (rp < 0) rp += (float) len;
                const int i0 = (int) rp; const float fr = rp - (float) i0; const int i1 = (i0 + 1) % len;
                float y = p.delay[(size_t) i0] + (p.delay[(size_t) i1] - p.delay[(size_t) i0]) * fr;
                p.w = (p.w + 1) % len;
                if (side == 1) for (auto& a : p.ap) y = a.processSample (y);      // colour one side → decorrelated
                y = p.hpf.processSample (y);
                y = p.low.processSample (y);
                y = p.pres.processSample (y);
                y = p.air.processSample (y);
                const float sat = std::tanh (y * drv) / drv;
                y = y + (sat - y) * wet;
                out[i] = y;
            }
        }

        // ---- replace the track with the finished layer ---------------------------
        const float g = followGain * outGain;
        const float same = 0.5f + 0.5f * width, other = 0.5f - 0.5f * width;
        for (int i = 0; i < n; ++i)
        {
            const float l = outL[(size_t) i] * g, r = outR[(size_t) i] * g;
            const float dryL = buffer.getReadPointer (0)[i];
            const float dryR = nCh > 1 ? buffer.getReadPointer (1)[i] : dryL;
            // Amount blends from the untouched track (0) to the finished layer (1)
            if (nCh > 1)
            {
                buffer.getWritePointer (0)[i] = dryL * (1.0f - outGain) + (l * same + r * other);
                buffer.getWritePointer (1)[i] = dryR * (1.0f - outGain) + (r * same + l * other);
            }
            else buffer.getWritePointer (0)[i] = dryL * (1.0f - outGain) + 0.5f * (l + r);
        }
    }

    // ---- UI ---------------------------------------------------------------
    float getRideDb() const { return juce::Decibels::gainToDecibels (followGain); }
    bool  usingSidechain() const { return hasSidechain && leadEnvDb > settings.gateDb; }
    float getActivity() const { return juce::jlimit (0.0f, 1.0f, outGain * (layerEnvDb > settings.gateDb ? 1.0f : 0.2f)); }

private:
    static float coef (float ms, float blockMs) { return 1.0f - std::exp (-blockMs / ms); }
    static float rmsDb (const float* x, int n) { double a = 0; for (int i = 0; i < n; ++i) a += x[i] * x[i]; return juce::Decibels::gainToDecibels ((float) std::sqrt (a / (double) n) + 1.0e-9f); }

    void applyMode (Mode m, bool)
    {
        mode = m;
        const bool h = (m == Mode::Harmony);
        for (auto& p : path)
        {
            *p.hpf.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, h ? settings.hpfHzHarmony : settings.hpfHz, 0.707f);
            *p.air.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, settings.airHz, 0.7f, juce::Decibels::decibelsToGain (h ? settings.airDbHarmony : settings.airDb));
        }
    }

    struct Path
    {
        std::vector<float> delay; int w = 0;
        juce::dsp::IIR::Filter<float> hpf, low, pres, air, ap[3];
        float lfoPhase = 0.0f;
    };

    double sr = 48000.0; int channels = 2;
    juce::AudioBuffer<float> mono;
    std::vector<float> outL, outR;
    std::array<Path, 2> path;
    Mode mode = Mode::Double;
    float amount = 1.0f, width = 0.8f, tuck = 0.5f;
    bool hasSidechain = false;
    float layerEnvDb = -100.0f, leadEnvDb = -100.0f, longTermDb = -20.0f, followGain = 1.0f, outGain = 0.0f;
};
