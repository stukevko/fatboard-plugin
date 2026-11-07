#pragma once
#include <juce_dsp/juce_dsp.h>
#include "signalsmith-stretch/signalsmith-stretch.h"
#include "PitchTracker.h"
#include "HarmonyLogic.h"
#include <array>
#include <vector>
#include <random>

// ============================================================================
//  Harun engine – "sing once"
//
//  Input: one vocal. Output: the vocal plus one or two extra voices that sound
//  like someone sang them again – a harmony in the key, or a double.
//
//  Why it doesn't sound like copy/paste:
//    pitch     : the harmony interval (diatonic) + slow random drift
//    timing    : each voice sits 15–30 ms late and the offset wanders
//    formants  : voice colour is nudged, pitch-compensated (no chipmunk)
//    tone      : darker, slightly scooped, a few dB under the lead
//    stereo    : two voices go left / right, one voice sits a bit wide
//
//  The lead is delayed by the pitch-shifter latency so everything lines up;
//  the host gets the latency reported for PDC.
//
//  All TUNE values are in Settings.
// ============================================================================

class HarunEngine
{
public:
    enum class Mode  { Harmony = 0, Double = 1 };
    enum class Voice { Low = 0, High = 1, Both = 2 };

    struct Settings
    {
        // levels
        float harmonyLevelDb = -6.0f;     // TUNE – harmony voice vs. lead at Blend = 100 %
        float doubleLevelDb  = -4.0f;     // TUNE
        // timing (ms)
        float baseDelayMsA = 18.0f;       // TUNE – first voice
        float baseDelayMsB = 27.0f;       // TUNE – second voice
        float delayWobbleMs = 5.0f;       // TUNE – × Human
        float wobbleHz = 0.23f;           // TUNE
        // pitch (cents)
        float driftCents = 9.0f;          // TUNE – × Human, slow random drift
        float doubleDetuneCents = 7.0f;   // TUNE – fixed ± detune in Double mode, × Human
        // formants (semitones), × Human
        float formantHigh = -0.8f;        // TUNE – a harmony above gets a slightly bigger voice
        float formantLow  =  0.8f;        // TUNE
        float formantDouble = 0.5f;       // TUNE – ± per voice
        // tone
        float hpfHz = 140.0f;             // TUNE
        float scoopHz = 2800.0f;          // TUNE
        float scoopDb = -2.5f;            // TUNE
        float shelfHz = 7000.0f;          // TUNE
        float shelfDb = -3.0f;            // TUNE
        // glide between harmony notes (ms) – short, it's a note change not a slide
        float shiftGlideMs = 12.0f;       // TUNE
        // pitch-shifter quality (seconds): bigger = smoother, more latency
        float stretchBlockSec = 0.10f;    // TUNE
        float stretchIntervalSec = 0.025f;// TUNE
        float tonalityLimitHz = 8000.0f;  // TUNE – keeps formants below this when transposing
        float gateDb = -55.0f;            // silence gate for the generated voices
    };
    Settings settings;

    void prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        sr = sampleRate; channels = juce::jlimit (1, 2, numChannels);
        maxBlock = maxBlockSize;
        mono.setSize (1, maxBlockSize);
        tracker.prepare (sr);

        for (auto& v : voices)
        {
            v.stretch.configure (1, (int) (sr * settings.stretchBlockSec), (int) (sr * settings.stretchIntervalSec), false);
            v.stretch.reset();
            v.out.assign ((size_t) maxBlockSize, 0.0f);
            const int maxDelay = (int) (sr * 0.06);
            v.delay.assign ((size_t) maxDelay, 0.0f);
            v.delayWrite = 0;
            v.hpf.coefficients   = juce::dsp::IIR::Coefficients<float>::makeHighPass  (sr, settings.hpfHz, 0.707f);
            v.scoop.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, settings.scoopHz, 1.0f, juce::Decibels::decibelsToGain (settings.scoopDb));
            v.shelf.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, settings.shelfHz, 0.7f, juce::Decibels::decibelsToGain (settings.shelfDb));
            v.hpf.reset(); v.scoop.reset(); v.shelf.reset();
            v.shift = 0.0f; v.gain = 0.0f;
            v.lfoPhase = uni (rng) * juce::MathConstants<float>::twoPi;
            v.driftPhaseA = uni (rng) * juce::MathConstants<float>::twoPi;
            v.driftPhaseB = uni (rng) * juce::MathConstants<float>::twoPi;
        }

        latency = voices[0].stretch.inputLatency() + voices[0].stretch.outputLatency();
        dryDelay.setSize (2, latency + maxBlockSize + 8);
        dryDelay.clear(); dryWrite = 0;

        // ring of pitch estimates so the shift decision lines up with the shifter's processing time
        midiHistory.assign ((size_t) juce::jmax (1, (latency + maxBlockSize) / 64 + 4), 0.0f);
        midiHistWrite = 0;
        midiSmoothed = 60.0f; inputEnvDb = -100.0f; lastVoicedMidi = 60.0f;
    }

    void reset()
    {
        tracker.reset();
        for (auto& v : voices) { v.stretch.reset(); std::fill (v.delay.begin(), v.delay.end(), 0.0f); v.hpf.reset(); v.scoop.reset(); v.shelf.reset(); }
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

        // ---- mono analysis signal --------------------------------------
        auto* m = mono.getWritePointer (0);
        juce::FloatVectorOperations::copy (m, buffer.getReadPointer (0), n);
        if (nCh > 1) { juce::FloatVectorOperations::add (m, buffer.getReadPointer (1), n); juce::FloatVectorOperations::multiply (m, 0.5f, n); }

        // input envelope (for the silence gate)
        float e = 0.0f; for (int i = 0; i < n; ++i) e += m[i] * m[i];
        const float blockMs = 1000.0f * (float) n / (float) sr;
        const float inDb = juce::Decibels::gainToDecibels (std::sqrt (e / (float) n) + 1.0e-9f);
        inputEnvDb += (inDb - inputEnvDb) * (inDb > inputEnvDb ? coef (5.0f, blockMs) : coef (120.0f, blockMs));

        // ---- pitch tracking -----------------------------------------------
        tracker.push (m, n);
        if (tracker.isVoiced())
        {
            const float midi = Harmony::hzToMidi (tracker.getF0());
            // octave-jump guard: ignore sudden ±12 leaps for one update
            if (std::abs (midi - lastVoicedMidi) > 10.0f && std::abs (std::abs (midi - lastVoicedMidi) - 12.0f) < 1.0f) {}
            else lastVoicedMidi = midi;
            midiSmoothed += (lastVoicedMidi - midiSmoothed) * coef (10.0f, blockMs);
        }
        // store for the delayed read
        midiHistory[(size_t) midiHistWrite] = midiSmoothed;
        midiHistWrite = (midiHistWrite + 1) % (int) midiHistory.size();
        // read back the estimate from ≈ (inputLatency - trackerLatency) samples ago
        const int lagSamples = juce::jmax (0, voices[0].stretch.inputLatency() - tracker.getLatencySamples());
        const int lagBlocks  = juce::jmin ((int) midiHistory.size() - 1, lagSamples / juce::jmax (1, n));
        const float midiForShift = midiHistory[(size_t) (((midiHistWrite - 1 - lagBlocks) % (int) midiHistory.size() + (int) midiHistory.size()) % (int) midiHistory.size())];
        currentNoteMidi = midiSmoothed;

        // ---- which voices are on ------------------------------------------
        const bool useA = true;
        const bool useB = (voice == Voice::Both);
        const float silence = inputEnvDb < settings.gateDb ? 0.0f : 1.0f;

        // ---- generate voices ----------------------------------------------
        for (int vi = 0; vi < 2; ++vi)
        {
            auto& v = voices[(size_t) vi];
            const bool on = (vi == 0 ? useA : useB);
            const float targetGain = on ? silence * blend * juce::Decibels::decibelsToGain (mode == Mode::Harmony ? settings.harmonyLevelDb : settings.doubleLevelDb) : 0.0f;
            v.gain += (targetGain - v.gain) * coef (30.0f, blockMs);
            if (! on && v.gain < 1.0e-4f) { v.active = false; continue; }
            v.active = true;

            // --- target shift (semitones) ---
            float targetShift = 0.0f;
            float formantSemis = 0.0f;
            if (mode == Mode::Harmony)
            {
                Harmony::Interval iv;
                if (voice == Voice::Both) iv = (vi == 0) ? Harmony::Interval::ThirdAbove : Harmony::Interval::ThirdBelow;
                else                      iv = (voice == Voice::High) ? Harmony::Interval::ThirdAbove : Harmony::Interval::ThirdBelow;
                targetShift = Harmony::shiftFor (midiForShift, key, scale, iv);
                formantSemis = human * (targetShift > 0 ? settings.formantHigh : settings.formantLow);
            }
            else
            {
                targetShift = (vi == 0 ? -1.0f : 1.0f) * settings.doubleDetuneCents * 0.01f * human;
                formantSemis = human * (vi == 0 ? settings.formantDouble : -settings.formantDouble);
            }
            v.shift += (targetShift - v.shift) * coef (settings.shiftGlideMs, blockMs);

            // --- slow random drift (two incommensurate sines) ---
            v.driftPhaseA += juce::MathConstants<float>::twoPi * 0.17f * blockMs * 0.001f;
            v.driftPhaseB += juce::MathConstants<float>::twoPi * 0.41f * blockMs * 0.001f;
            const float drift = (0.6f * std::sin (v.driftPhaseA) + 0.4f * std::sin (v.driftPhaseB)) * settings.driftCents * 0.01f * human;

            v.stretch.setTransposeSemitones (v.shift + drift, settings.tonalityLimitHz / (float) sr);
            v.stretch.setFormantSemitones (formantSemis, true);
            if (tracker.isVoiced()) v.stretch.setFormantBase (tracker.getF0() / (float) sr);

            // --- shift ---
            const float* in[1] = { m };
            float* out[1] = { v.out.data() };
            v.stretch.process (in, n, out, n);

            // --- timing wobble via modulated delay ---
            v.lfoPhase += juce::MathConstants<float>::twoPi * settings.wobbleHz * blockMs * 0.001f;
            if (v.lfoPhase > juce::MathConstants<float>::twoPi) v.lfoPhase -= juce::MathConstants<float>::twoPi;
            const float baseMs = (vi == 0 ? settings.baseDelayMsA : settings.baseDelayMsB);
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
                y = v.scoop.processSample (y);
                y = v.shelf.processSample (y);
                v.out[(size_t) i] = y * v.gain;
                v.delayWrite = (v.delayWrite + 1) % dlen;
            }
        }

        // ---- delay the lead by the shifter latency, then mix ---------------
        const int dl = dryDelay.getNumSamples();
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            auto* ring = dryDelay.getWritePointer (ch);
            int w = dryWrite;
            for (int i = 0; i < n; ++i)
            {
                ring[w] = d[i];
                int r = w - latency; if (r < 0) r += dl;
                float y = ring[r];
                // pan: two voices L/R, single voice slightly wide
                if (voices[0].active) y += voices[0].out[(size_t) i] * panGain (0, ch, nCh);
                if (voices[1].active) y += voices[1].out[(size_t) i] * panGain (1, ch, nCh);
                d[i] = y;
                w = (w + 1) % dl;
            }
        }
        dryWrite = (dryWrite + n) % dl;
    }

    // ---- UI ---------------------------------------------------------------
    bool  isVoiced() const         { return tracker.isVoiced(); }
    float getNoteMidi() const      { return currentNoteMidi; }
    float getVoiceShift (int i) const { return voices[(size_t) juce::jlimit (0, 1, i)].shift; }
    float getActivity() const      { return juce::jlimit (0.0f, 1.0f, (voices[0].gain + voices[1].gain) * 1.5f); }

private:
    static float coef (float ms, float blockMs) { return 1.0f - std::exp (-blockMs / ms); }

    float panGain (int vi, int ch, int nCh) const
    {
        if (nCh < 2) return 1.0f;
        const bool two = voices[1].active;
        if (! two) return 0.8f;                                          // single voice: a touch wide via both sides
        const float l = (vi == 0) ? 0.9f : 0.35f;
        const float r = (vi == 0) ? 0.35f : 0.9f;
        return ch == 0 ? l : r;
    }

    struct VoiceState
    {
        signalsmith::stretch::SignalsmithStretch<float> stretch { 1234 };
        std::vector<float> out, delay;
        int delayWrite = 0;
        juce::dsp::IIR::Filter<float> hpf, scoop, shelf;
        float shift = 0.0f, gain = 0.0f;
        float lfoPhase = 0.0f, driftPhaseA = 0.0f, driftPhaseB = 0.0f;
        bool active = false;
    };

    double sr = 48000.0; int channels = 2; int maxBlock = 512; int latency = 0;
    juce::AudioBuffer<float> mono, dryDelay; int dryWrite = 0;
    PitchTracker tracker;
    std::array<VoiceState, 2> voices;
    std::vector<float> midiHistory; int midiHistWrite = 0;
    float midiSmoothed = 60.0f, lastVoicedMidi = 60.0f, currentNoteMidi = 60.0f, inputEnvDb = -100.0f;

    int key = 0; Harmony::Scale scale = Harmony::Scale::Minor; Voice voice = Voice::High; Mode mode = Mode::Harmony;
    float blend = 0.6f, human = 0.6f;

    std::mt19937 rng { 42 };
    std::uniform_real_distribution<float> uni { 0.0f, 1.0f };
};
