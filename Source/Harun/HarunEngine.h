#pragma once
#include <juce_dsp/juce_dsp.h>
#include "signalsmith-stretch/signalsmith-stretch.h"
#include "PitchTracker.h"
#include "HarmonyLogic.h"
#include <array>
#include <vector>
#include <random>

// ============================================================================
//  Harun engine – "sing once"  (v0.2 – the "sounds like a take" pass)
//
//  Harun is ALWAYS a left/right doubler: two voices, one hard-ish left, one
//  hard-ish right. In Harmony mode both sing the chosen interval (or, with
//  "Both", the third above left and the third below right). In Double mode
//  they sing unison, slightly detuned. The lead stays in the middle.
//
//  What changed from v0.1 after listening feedback ("too pitched, angular"):
//   - timing wobble was a chorus in disguise (±5 ms @0.23 Hz = ±12 cents of
//     pitch modulation) → now ±1.2 ms, very slow. Drift 9 → 4 cents.
//   - the harmony interval could flip between +3/+4 semitones while the
//     singer slides → degree changes now need to persist (hysteresis) and
//     jump instead of gliding (a real singer jumps, glides sound synthetic)
//   - formant base is no longer fed from the tracker every block (warble);
//     formants are preserved by the shifter, colour nudge is tiny
//   - less low end in the voices (HPF 200 Hz + low shelf), a touch brighter
//   - default a few dB quieter, both voices panned wide
//
//  All TUNE values live in Settings.
// ============================================================================

class HarunEngine
{
public:
    enum class Mode  { Harmony = 0, Double = 1 };
    enum class Voice { Low = 0, High = 1, Both = 2 };

    struct Settings
    {
        // levels (vs. lead, at Blend = 100 %)
        float harmonyLevelDb = -8.0f;     // TUNE
        float doubleLevelDb  = -5.0f;     // TUNE
        // stereo: 1.0 = hard L/R, 0 = centre
        float width = 0.85f;              // TUNE
        // timing (ms)
        float baseDelayMsL = 14.0f;       // TUNE
        float baseDelayMsR = 21.0f;       // TUNE
        float delayWobbleMs = 1.2f;       // TUNE – × Human. Keep tiny: this is pitch modulation!
        float wobbleHz = 0.07f;           // TUNE
        // pitch (cents)
        float driftCents = 4.0f;          // TUNE – × Human
        float doubleDetuneCents = 6.0f;   // TUNE – ± per side in Double mode, × Human
        // formants (semitones) × Human – tiny colour difference per side
        float formantNudge = 0.35f;       // TUNE
        // tone of the generated voices
        float hpfHz = 200.0f;             // TUNE
        float lowShelfHz = 320.0f;        // TUNE
        float lowShelfDb = -3.0f;         // TUNE
        float scoopHz = 2600.0f;          // TUNE
        float scoopDb = -1.5f;            // TUNE
        float shelfHz = 8000.0f;          // TUNE
        float shelfDb = -1.5f;            // TUNE
        // note changes: how long a new degree must persist before we jump (ms)
        float degreeHoldMs = 45.0f;       // TUNE
        float shiftGlideMs = 3.0f;        // TUNE – near-instant; smoothing only against zipper
        // pitch shifter
        float stretchBlockSec = 0.10f;    // TUNE – smaller = tighter transients, more latency-friendly
        float stretchIntervalSec = 0.025f;// TUNE
        float tonalityLimitHz = 8000.0f;  // TUNE – formant preservation ceiling
        float gateDb = -55.0f;
    };
    Settings settings;

    void prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        sr = sampleRate; channels = juce::jlimit (1, 2, numChannels);
        mono.setSize (1, maxBlockSize);
        tracker.prepare (sr);

        for (int i = 0; i < 2; ++i)
        {
            auto& v = voices[(size_t) i];
            v.stretch.configure (1, (int) (sr * settings.stretchBlockSec), (int) (sr * settings.stretchIntervalSec), false);
            v.stretch.reset();
            v.stretch.setFormantBase (0.0f);                 // auto-detect; no per-block jitter from the tracker
            v.out.assign ((size_t) maxBlockSize, 0.0f);
            v.delay.assign ((size_t) (sr * 0.06), 0.0f);
            v.delayWrite = 0;
            v.hpf.coefficients   = juce::dsp::IIR::Coefficients<float>::makeHighPass  (sr, settings.hpfHz, 0.707f);
            v.low.coefficients   = juce::dsp::IIR::Coefficients<float>::makeLowShelf  (sr, settings.lowShelfHz, 0.7f, juce::Decibels::decibelsToGain (settings.lowShelfDb));
            v.scoop.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, settings.scoopHz, 1.0f, juce::Decibels::decibelsToGain (settings.scoopDb));
            v.shelf.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, settings.shelfHz, 0.7f, juce::Decibels::decibelsToGain (settings.shelfDb));
            v.hpf.reset(); v.low.reset(); v.scoop.reset(); v.shelf.reset();
            v.shift = 0.0f; v.targetShift = 0.0f; v.candidateShift = 0.0f; v.candidateMs = 0.0f; v.gain = 0.0f;
            v.lfoPhase = uni (rng) * juce::MathConstants<float>::twoPi;
            v.driftPhaseA = uni (rng) * juce::MathConstants<float>::twoPi;
            v.driftPhaseB = uni (rng) * juce::MathConstants<float>::twoPi;
        }

        latency = voices[0].stretch.inputLatency() + voices[0].stretch.outputLatency();
        dryDelay.setSize (2, latency + maxBlockSize + 8);
        dryDelay.clear(); dryWrite = 0;

        midiHistory.assign ((size_t) juce::jmax (1, (latency + maxBlockSize) / 64 + 4), 0.0f);
        midiHistWrite = 0;
        midiSmoothed = 60.0f; inputEnvDb = -100.0f; lastVoicedMidi = 60.0f;
    }

    void reset()
    {
        tracker.reset();
        for (auto& v : voices) { v.stretch.reset(); std::fill (v.delay.begin(), v.delay.end(), 0.0f); v.hpf.reset(); v.low.reset(); v.scoop.reset(); v.shelf.reset(); }
        dryDelay.clear();
    }

    int getLatencySamples() const { return latency; }

    void setParams (int keyIn, Harmony::Scale scaleIn, Voice voiceIn, Mode modeIn, float blendIn, float humanIn)
    {
        key = keyIn; scale = scaleIn; voice = voiceIn; mode = modeIn;
        blend = juce::jlimit (0.0f, 1.0f, blendIn); human = juce::jlimit (0.0f, 1.0f, humanIn);
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int n = buffer.getNumSamples();
        if (n == 0) return;
        const int nCh = juce::jmin (channels, buffer.getNumChannels());
        const float blockMs = 1000.0f * (float) n / (float) sr;

        // ---- mono analysis signal --------------------------------------
        auto* m = mono.getWritePointer (0);
        juce::FloatVectorOperations::copy (m, buffer.getReadPointer (0), n);
        if (nCh > 1) { juce::FloatVectorOperations::add (m, buffer.getReadPointer (1), n); juce::FloatVectorOperations::multiply (m, 0.5f, n); }

        float e = 0.0f; for (int i = 0; i < n; ++i) e += m[i] * m[i];
        const float inDb = juce::Decibels::gainToDecibels (std::sqrt (e / (float) n) + 1.0e-9f);
        inputEnvDb += (inDb - inputEnvDb) * (inDb > inputEnvDb ? coef (5.0f, blockMs) : coef (120.0f, blockMs));

        // ---- pitch tracking -----------------------------------------------
        tracker.push (m, n);
        if (tracker.isVoiced())
        {
            const float midi = Harmony::hzToMidi (tracker.getF0());
            const bool octaveGlitch = std::abs (std::abs (midi - lastVoicedMidi) - 12.0f) < 0.7f;
            if (! octaveGlitch) lastVoicedMidi = midi;
            midiSmoothed += (lastVoicedMidi - midiSmoothed) * coef (8.0f, blockMs);
        }
        midiHistory[(size_t) midiHistWrite] = midiSmoothed;
        midiHistWrite = (midiHistWrite + 1) % (int) midiHistory.size();
        const int lagSamples = juce::jmax (0, voices[0].stretch.inputLatency() - tracker.getLatencySamples());
        const int lagBlocks  = juce::jmin ((int) midiHistory.size() - 1, lagSamples / juce::jmax (1, n));
        const float midiForShift = midiHistory[(size_t) (((midiHistWrite - 1 - lagBlocks) % (int) midiHistory.size() + (int) midiHistory.size()) % (int) midiHistory.size())];
        currentNoteMidi = midiSmoothed;

        const float silence = inputEnvDb < settings.gateDb ? 0.0f : 1.0f;
        const float levelDb = mode == Mode::Harmony ? settings.harmonyLevelDb : settings.doubleLevelDb;

        // ---- two voices, always: L and R -----------------------------------
        for (int vi = 0; vi < 2; ++vi)
        {
            auto& v = voices[(size_t) vi];
            const float targetGain = silence * blend * juce::Decibels::decibelsToGain (levelDb);
            v.gain += (targetGain - v.gain) * coef (30.0f, blockMs);
            v.active = v.gain > 1.0e-4f || targetGain > 0.0f;
            if (! v.active) continue;

            // --- what should this side sing? ---
            float wanted = 0.0f;
            float formantSemis = (vi == 0 ? 1.0f : -1.0f) * settings.formantNudge * human;
            if (mode == Mode::Harmony)
            {
                Harmony::Interval iv;
                if (voice == Voice::Both) iv = (vi == 0) ? Harmony::Interval::ThirdAbove : Harmony::Interval::ThirdBelow;
                else                      iv = (voice == Voice::High) ? Harmony::Interval::ThirdAbove : Harmony::Interval::ThirdBelow;
                wanted = Harmony::shiftFor (midiForShift, key, scale, iv);
            }
            else
            {
                wanted = (vi == 0 ? -1.0f : 1.0f) * settings.doubleDetuneCents * 0.01f * human;
            }

            // --- hysteresis: a new interval must persist before we jump to it ---
            if (std::abs (wanted - v.candidateShift) > 0.01f) { v.candidateShift = wanted; v.candidateMs = 0.0f; }
            else v.candidateMs += blockMs;
            const bool firstNote = (mode == Mode::Harmony && v.targetShift == 0.0f);   // nothing sung yet: take it right away
            if (std::abs (v.candidateShift - v.targetShift) > 0.01f && (v.candidateMs >= settings.degreeHoldMs || firstNote))
                v.targetShift = v.candidateShift;
            v.shift += (v.targetShift - v.shift) * coef (settings.shiftGlideMs, blockMs);

            // --- slow drift ---
            v.driftPhaseA += juce::MathConstants<float>::twoPi * 0.11f * blockMs * 0.001f;
            v.driftPhaseB += juce::MathConstants<float>::twoPi * 0.29f * blockMs * 0.001f;
            const float drift = (0.6f * std::sin (v.driftPhaseA) + 0.4f * std::sin (v.driftPhaseB)) * settings.driftCents * 0.01f * human;

            v.stretch.setTransposeSemitones (v.shift + drift, settings.tonalityLimitHz / (float) sr);
            v.stretch.setFormantSemitones (formantSemis, true);

            const float* in[1] = { m };
            float* out[1] = { v.out.data() };
            v.stretch.process (in, n, out, n);

            // --- timing: a fixed offset per side + a very slow, tiny wander ---
            v.lfoPhase += juce::MathConstants<float>::twoPi * settings.wobbleHz * blockMs * 0.001f;
            if (v.lfoPhase > juce::MathConstants<float>::twoPi) v.lfoPhase -= juce::MathConstants<float>::twoPi;
            const float baseMs = (vi == 0 ? settings.baseDelayMsL : settings.baseDelayMsR);
            const float delayMs = baseMs + std::sin (v.lfoPhase) * settings.delayWobbleMs * human;
            const float delaySamples = juce::jlimit (1.0f, (float) v.delay.size() - 2.0f, delayMs * 0.001f * (float) sr);
            const int dlen = (int) v.delay.size();
            for (int i = 0; i < n; ++i)
            {
                v.delay[(size_t) v.delayWrite] = v.out[(size_t) i];
                float rp = (float) v.delayWrite - delaySamples;
                if (rp < 0) rp += (float) dlen;
                const int i0 = (int) rp; const float fr = rp - (float) i0;
                const int i1 = (i0 + 1) % dlen;
                float y = v.delay[(size_t) i0] + (v.delay[(size_t) i1] - v.delay[(size_t) i0]) * fr;
                y = v.hpf.processSample (y);
                y = v.low.processSample (y);
                y = v.scoop.processSample (y);
                y = v.shelf.processSample (y);
                v.out[(size_t) i] = y * v.gain;
                v.delayWrite = (v.delayWrite + 1) % dlen;
            }
        }

        // ---- lead delayed by shifter latency, voices panned L / R -----------
        const int dl = dryDelay.getNumSamples();
        const float w = settings.width;
        const float gSame = 0.5f + 0.5f * w, gOther = 0.5f - 0.5f * w;   // equal-power-ish pan
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            auto* ring = dryDelay.getWritePointer (ch);
            int wr = dryWrite;
            const float gL = nCh < 2 ? 0.7f : (ch == 0 ? gSame : gOther);
            const float gR = nCh < 2 ? 0.7f : (ch == 0 ? gOther : gSame);
            for (int i = 0; i < n; ++i)
            {
                ring[wr] = d[i];
                int r = wr - latency; if (r < 0) r += dl;
                float y = ring[r];
                if (voices[0].active) y += voices[0].out[(size_t) i] * gL;
                if (voices[1].active) y += voices[1].out[(size_t) i] * gR;
                d[i] = y;
                wr = (wr + 1) % dl;
            }
        }
        dryWrite = (dryWrite + n) % dl;
    }

    // ---- UI ---------------------------------------------------------------
    bool  isVoiced() const         { return tracker.isVoiced(); }
    float getNoteMidi() const      { return currentNoteMidi; }
    float getVoiceShift (int i) const { return voices[(size_t) juce::jlimit (0, 1, i)].targetShift; }
    float getActivity() const      { return juce::jlimit (0.0f, 1.0f, (voices[0].gain + voices[1].gain) * 1.5f); }

private:
    static float coef (float ms, float blockMs) { return 1.0f - std::exp (-blockMs / ms); }

    struct VoiceState
    {
        signalsmith::stretch::SignalsmithStretch<float> stretch { 1234 };
        std::vector<float> out, delay;
        int delayWrite = 0;
        juce::dsp::IIR::Filter<float> hpf, low, scoop, shelf;
        float shift = 0.0f, targetShift = 0.0f, candidateShift = 0.0f, candidateMs = 0.0f, gain = 0.0f;
        float lfoPhase = 0.0f, driftPhaseA = 0.0f, driftPhaseB = 0.0f;
        bool active = false;
    };

    double sr = 48000.0; int channels = 2; int latency = 0;
    juce::AudioBuffer<float> mono, dryDelay; int dryWrite = 0;
    PitchTracker tracker;
    std::array<VoiceState, 2> voices;
    std::vector<float> midiHistory; int midiHistWrite = 0;
    float midiSmoothed = 60.0f, lastVoicedMidi = 60.0f, currentNoteMidi = 60.0f, inputEnvDb = -100.0f;

    int key = 0; Harmony::Scale scale = Harmony::Scale::Minor; Voice voice = Voice::High; Mode mode = Mode::Harmony;
    float blend = 0.5f, human = 0.5f;

    std::mt19937 rng { 42 };
    std::uniform_real_distribution<float> uni { 0.0f, 1.0f };
};
