#pragma once
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>
#include <cmath>

// ============================================================================
//  Thump – kick + 808 in one   (working title, v0.1)
//
//  An instrument. Play a note: the 808 plays that note, the kick hits with it,
//  and both go through one clipper that rounds the tops off.
//
//    note ─┬─► 808: built-in tone (synthesised) or your own sample, pitched to the note
//          └─► kick: click + pitched thump ──► ducks the 808 for a few ms so it punches through
//                         808 + kick ──► CLIP (drive into a soft clipper, level compensated) ──► out
//
//   - monophonic like an 808 should be: a new note cuts the old one (5 ms cross-fade, no click)
//   - built-in tones are generated here, nothing sampled from anyone
//   - your own sample: the root note is detected, then every key plays in tune
//
//  All TUNE values are in the tone table and Settings.
// ============================================================================

struct ThumpTone
{
    const char* name;
    float dropSemis;      // pitch starts this far above the note …
    float dropMs;         // … and falls in this time
    float decaySec;       // time to -60 dB at Decay = 50 %
    float drive;          // character saturation inside the tone (0..1)
    float asym;           // 0 = symmetric (odd harmonics), >0 adds even ones (growl)
    float second;         // a little 2nd harmonic so small speakers hear the note
};

namespace ThumpTones
{
    static constexpr int count = 4;             // index 4 = user sample
    // TUNE                                name     drop  ms    decay  drive asym  2nd
    static constexpr ThumpTone all[count] = {
        { "Round",  12.0f, 22.0f, 2.6f, 0.10f, 0.00f, 0.06f },
        { "Punch",  24.0f, 16.0f, 1.3f, 0.30f, 0.05f, 0.10f },
        { "Grit",   12.0f, 28.0f, 2.2f, 0.85f, 0.35f, 0.14f },
        { "Long",    7.0f, 40.0f, 5.0f, 0.20f, 0.00f, 0.08f },
    };
    inline const ThumpTone& get (int i) { return all[juce::jlimit (0, count - 1, i)]; }
}

/** A loaded 808 sample (mono) with its detected root. */
struct ThumpSample
{
    std::vector<float> data;
    double sampleRate = 48000.0;
    float rootHz = 65.41f;          // fallback: C2
    bool rootDetected = false;
    juce::String name;

    /** Finds the fundamental in 28–130 Hz by autocorrelation on the sustained part. */
    void detectRoot()
    {
        const int n = (int) data.size();
        const int minLag = (int) (sampleRate / 130.0), maxLag = (int) (sampleRate / 28.0);
        const int win = juce::jmin (n - maxLag - 1, (int) (sampleRate * 0.35));
        if (win < maxLag) return;
        const int start = juce::jlimit (0, n - win - maxLag - 1, (int) (n * 0.12));
        double e0 = 0; for (int i = 0; i < win; ++i) e0 += (double) data[(size_t) (start + i)] * data[(size_t) (start + i)];
        if (e0 < 1.0e-6) return;
        double best = 0.0; int bestLag = 0;
        std::vector<double> ac ((size_t) maxLag + 2, 0.0);
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double s = 0, e1 = 0;
            for (int i = 0; i < win; i += 2) { const double a = data[(size_t) (start + i)], b = data[(size_t) (start + i + lag)]; s += a * b; e1 += b * b; }
            const double c = s / std::sqrt ((e0 * 0.5) * e1 + 1.0e-12);
            ac[(size_t) lag] = c;
            if (c > best) { best = c; bestLag = lag; }
        }
        if (best < 0.6 || bestLag == 0) return;
        // the first strong peak wins (a lag twice as long correlates just as well)
        for (int lag = minLag + 1; lag < bestLag; ++lag)
            if (ac[(size_t) lag] > best * 0.92 && ac[(size_t) lag] >= ac[(size_t) lag - 1] && ac[(size_t) lag] >= ac[(size_t) lag + 1]) { bestLag = lag; break; }
        // parabolic refinement
        const double a = ac[(size_t) bestLag - 1], b = ac[(size_t) bestLag], c = ac[(size_t) bestLag + 1];
        const double off = (std::abs (a - 2 * b + c) > 1.0e-9) ? 0.5 * (a - c) / (a - 2 * b + c) : 0.0;
        rootHz = (float) (sampleRate / ((double) bestLag + off));
        rootDetected = true;
    }
};

class ThumpEngine
{
public:
    struct Settings
    {
        float kickStartHz = 190.0f, kickEndHz = 48.0f, kickSweepMs = 24.0f, kickDecayMs = 190.0f;   // TUNE
        float kickClick   = 0.35f;        // TUNE
        float duckDepth   = 0.75f;        // TUNE: how far the 808 gets out of the kick's way
        float duckMs      = 55.0f;        // TUNE
        float clipMaxDb   = 18.0f;        // TUNE: drive at Clip = 100 %
        float ceiling     = 0.89f;        // -1 dB
        float chokeMs     = 5.0f;         // TUNE
    };
    Settings settings;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        clickHp.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 2500.0f, 0.707f);
        dcBlock.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 18.0f, 0.707f);
        reset();
    }
    void reset() { for (auto& v : voice) v = Voice(); kick = Kick(); clickHp.reset(); dcBlock.reset(); meter = 0.0f; }

    /** tone 0..3 built-in, 4 = sample (falls back to tone 0 without one). All knobs 0..1. */
    void setParams (int toneIn, float subLevel, float kickLevel, float decayIn, float clipIn)
    {
        tone = juce::jlimit (0, ThumpTones::count, toneIn);
        sub = subLevel; kickLvl = kickLevel; decay = decayIn; clip = clipIn;
    }
    void setSample (std::shared_ptr<const ThumpSample> s) { sample = std::move (s); }
    bool hasSample() const { return sample != nullptr && ! sample->data.empty(); }

    void noteOn (int midiNote, float velocity)
    {
        // old note fades out in a few ms, the new one starts in the other slot
        cur ^= 1;
        voice[cur ^ 1].choke = true;
        auto& v = voice[cur]; v = Voice();
        v.active = true; v.hz = 440.0f * std::pow (2.0f, ((float) midiNote - 69.0f) / 12.0f); v.vel = 0.5f + 0.5f * velocity;
        v.useSample = (tone == ThumpTones::count) && hasSample();
        v.toneIdx = juce::jmin (tone, ThumpTones::count - 1);
        kick = Kick(); kick.active = true; kick.vel = v.vel;
    }
    void allOff() { for (auto& v : voice) v.choke = true; }

    /** Renders `n` mono samples into out (overwrites). */
    void render (float* out, int n)
    {
        const float dt = 1.0f / (float) sr;
        const float chokeStep = 1.0f / (settings.chokeMs * 0.001f * (float) sr);
        const float driveDb = settings.clipMaxDb * clip, drive = juce::Decibels::decibelsToGain (driveDb);
        const float comp = juce::Decibels::decibelsToGain (-driveDb * 0.15f);          // thicker, but the peak stays about where it was
        const float decayMul = 0.25f * std::pow (8.0f, decay);                         // 0.25× … 2×, 1× at ~67 %
        float peak = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            // ---- kick ----
            float k = 0.0f, duck = 0.0f;
            if (kick.active)
            {
                const float tMs = kick.t * 1000.0f;
                const float f = settings.kickEndHz + (settings.kickStartHz - settings.kickEndHz) * std::exp (-tMs / settings.kickSweepMs);
                kick.phase += f * dt; if (kick.phase >= 1.0f) kick.phase -= 1.0f;
                const float env = std::exp (-tMs / (settings.kickDecayMs * 0.22f)) * juce::jmin (1.0f, tMs / 0.4f);
                float click = 0.0f;
                if (tMs < 6.0f) { noise = noise * 1664525u + 1013904223u; click = clickHp.processSample (((float) (noise >> 8) / 8388608.0f - 1.0f)) * std::exp (-tMs / 1.6f) * settings.kickClick; }
                k = (std::sin (juce::MathConstants<float>::twoPi * kick.phase) * env + click) * kick.vel;
                duck = std::exp (-tMs / (settings.duckMs * 0.4f));
                kick.t += dt; if (tMs > settings.kickDecayMs * 2.0f) kick.active = false;
            }

            // ---- 808 (two slots: the playing note + the one being cut) ----
            float s = 0.0f;
            for (auto& v : voice)
            {
                if (! v.active) continue;
                float y = 0.0f; const float tMs = v.t * 1000.0f;
                if (v.useSample && sample != nullptr)
                {
                    const auto& d = sample->data;
                    const int i0 = (int) v.pos;
                    if (i0 + 1 >= (int) d.size()) { v.active = false; continue; }
                    const float fr = (float) (v.pos - (double) i0);
                    y = d[(size_t) i0] + (d[(size_t) i0 + 1] - d[(size_t) i0]) * fr;
                    v.pos += (double) (v.hz / sample->rootHz) * (sample->sampleRate / sr);
                    // Decay shortens (or keeps) the sample: full length at 100 %
                    const float len = (float) d.size() / (float) sample->sampleRate * (sample->rootHz / v.hz);
                    y *= std::exp (-6.9f * v.t / juce::jmax (0.05f, len * (0.15f + 2.0f * decay)));
                }
                else
                {
                    const auto& tn = ThumpTones::get (v.toneIdx);
                    const float f = v.hz * std::pow (2.0f, tn.dropSemis / 12.0f * std::exp (-tMs / (tn.dropMs * 0.4f)));
                    v.phase += f * dt; if (v.phase >= 1.0f) v.phase -= 1.0f;
                    const float ph = juce::MathConstants<float>::twoPi * v.phase;
                    float o = std::sin (ph) + tn.second * std::sin (2.0f * ph);
                    // character: drive into tanh, optional asymmetry for the growl
                    const float dg = 1.0f + tn.drive * 6.0f;
                    o = std::tanh ((o + tn.asym * 0.5f) * dg) / std::tanh (dg) - std::tanh (tn.asym * 0.5f * dg) / std::tanh (dg);
                    const float env = std::exp (-6.9f * v.t / (tn.decaySec * decayMul)) * juce::jmin (1.0f, tMs / 1.5f);
                    y = o * env;
                    if (env < 0.0008f && tMs > 20.0f) v.active = false;
                }
                if (v.choke) { v.fade -= chokeStep; if (v.fade <= 0.0f) { v.active = false; continue; } }
                s += y * v.fade * v.vel;
                v.t += dt;
            }

            // ---- together, then the clipper ----
            float x = s * sub * (1.0f - settings.duckDepth * duck * juce::jmin (1.0f, kickLvl * 1.5f)) * 0.8f + k * kickLvl * 0.9f;
            x = dcBlock.processSample (x);
            x = softClip (x * drive) * comp;
            x = juce::jlimit (-settings.ceiling, settings.ceiling, x);
            out[i] = x;
            peak = juce::jmax (peak, std::abs (x));
        }
        meter += (peak - meter) * (peak > meter ? 0.6f : 0.1f);
    }

    float getMeter() const { return meter; }

private:
    // rounds off towards ±0.89, never above
    static float softClip (float x)
    {
        const float t = 0.89f, a = x / t, a3 = std::abs (a) * a * a;
        return t * a / std::cbrt (1.0f + std::abs (a3));
    }

    struct Voice { bool active = false, choke = false, useSample = false; int toneIdx = 0; float hz = 55.0f, vel = 1.0f, phase = 0.0f, t = 0.0f, fade = 1.0f; double pos = 0.0; };
    struct Kick  { bool active = false; float phase = 0.0f, t = 0.0f, vel = 1.0f; };

    double sr = 48000.0;
    Voice voice[2]; int cur = 0; Kick kick;
    int tone = 0; float sub = 0.8f, kickLvl = 0.7f, decay = 0.67f, clip = 0.3f, meter = 0.0f;
    std::shared_ptr<const ThumpSample> sample;
    juce::dsp::IIR::Filter<float> clickHp, dcBlock;
    uint32_t noise = 22222u;
};
