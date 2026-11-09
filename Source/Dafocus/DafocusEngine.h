#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include <cmath>

// ============================================================================
//  Dafocus engine – vocal space that stays out of the way
//
//  Signal flow (the lead passes through untouched):
//
//    lead ─┬──────────────────────────────────────────────────────► out
//          └► send: de-ess → HPF 250 → LPF 7k ─┬► tempo delay (filtered fb) ─┐
//                                               └► pre-delay → FDN reverb ───┤
//                                                                             ▼
//                                                  × duck (lead envelope) ── + ──► out
//
//  Why it sits in the beat:
//   - the wet signal never has low end or fizz (fixed HPF/LPF on the send)
//   - S-sounds are tamed before they hit the reverb
//   - delay follows the host tempo, each repeat gets darker (tape-like)
//   - the reverb is an 8-line feedback delay network with a little
//     modulation: dense, no metallic ring, no "JUCE reverb" wash
//   - DUCKING: while the singer sings, reverb+delay are pushed down; when the
//     line ends, they bloom. Threshold adapts to the vocal's own level.
//
//  All TUNE values in Settings / Space presets.
// ============================================================================

class DafocusEngine
{
public:
    enum class Space { Tight = 0, Room = 1, Far = 2 };
    enum class Echo  { Off = 0, Eighth = 1, Quarter = 2, DottedEighth = 3 };

    struct SpacePreset { float rt60Sec; float sizeScale; float preDelayMs; float dampHz; float width; };
    struct Settings
    {
        // send conditioning
        float sendHpfHz = 250.0f;          // TUNE – nothing muddy goes into the space
        float sendLpfHz = 7000.0f;         // TUNE – no fizz in the tail
        float deEssHz = 7000.0f;           // TUNE
        float deEssMaxDb = 8.0f;           // TUNE
        float deEssThresholdDb = 4.0f;     // TUNE
        // delay
        float delayFeedback = 0.38f;       // TUNE – per repeat
        float delayFbLpfHz = 4200.0f;      // TUNE – each repeat darker
        float delayFbHpfHz = 300.0f;       // TUNE
        float delayLevelDb = -3.0f;        // TUNE – vs reverb at same Amount
        float delayWidthMs = 9.0f;         // TUNE – R side slightly later → width without ping-pong clutter
        float delayToReverb = 0.35f;       // TUNE – how much of the echo also feeds the reverb (glue)
        // reverb
        float reverbLevelDb = 0.0f;        // TUNE
        float modDepthSamples = 3.0f;      // TUNE – anti-metallic
        float modHz = 0.35f;               // TUNE
        // ducking
        float duckRangeDb = 14.0f;         // TUNE – max attenuation at Duck = 100 %
        float duckAttackMs = 12.0f;        // TUNE
        float duckReleaseMs = 260.0f;      // TUNE – how fast the space blooms after a line
        float duckRelativeDb = 16.0f;      // TUNE – "singing" = within this many dB of the recent peak
        float peakDecaySec = 2.5f;         // TUNE – memory of the recent peak
        // amount curve
        float maxWetDb = 0.0f;             // TUNE – wet level at Amount = 100 % (0 dB = as loud as lead; mixed in parallel)
    };
    Settings settings;

    static const SpacePreset& preset (Space s)
    {
        //                                   rt60  size  pre   damp    width
        static const SpacePreset tight { 0.75f, 0.55f, 12.0f, 5200.0f, 0.8f };    // TUNE
        static const SpacePreset room  { 1.50f, 0.85f, 24.0f, 4600.0f, 0.95f };   // TUNE
        static const SpacePreset far   { 2.70f, 1.15f, 38.0f, 3800.0f, 1.0f };    // TUNE
        switch (s) { case Space::Tight: return tight; case Space::Far: return far; default: return room; }
    }

    void prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        sr = sampleRate; channels = juce::jlimit (1, 2, numChannels);
        send.setSize (1, maxBlockSize);
        wet.setSize (2, maxBlockSize);

        // send filters
        sendHpf.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, settings.sendHpfHz, 0.707f);
        sendLpf.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass  (sr, settings.sendLpfHz, 0.707f);
        deEssDetect.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, settings.deEssHz, 2.0f);
        deEssCoeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, settings.deEssHz, 2.0f, 1.0f);
        deEss.coefficients = deEssCoeffs;
        sendHpf.reset(); sendLpf.reset(); deEssDetect.reset(); deEss.reset();

        // delay lines (max 2 s)
        const int maxDelay = (int) (sr * 2.2);
        for (int ch = 0; ch < 2; ++ch)
        {
            dly[ch].buf.assign ((size_t) maxDelay, 0.0f); dly[ch].w = 0;
            dly[ch].fbLpf.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass  (sr, settings.delayFbLpfHz, 0.707f);
            dly[ch].fbHpf.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, settings.delayFbHpfHz, 0.707f);
            dly[ch].fbLpf.reset(); dly[ch].fbHpf.reset();
        }
        delaySamplesSmoothed = 0.0f;

        // reverb FDN
        preDelay.assign ((size_t) (sr * 0.12), 0.0f); preW = 0;
        static const float baseMs[numLines] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.1f, 73.3f };   // prime-ish, TUNE
        for (int i = 0; i < numLines; ++i)
        {
            fdn[i].base = baseMs[i] * 0.001f * (float) sr;
            fdn[i].buf.assign ((size_t) (fdn[i].base * 1.6f + 256), 0.0f);   // room for Far (1.15×) + modulation
            fdn[i].w = 0; fdn[i].lp = 0.0f;
            fdn[i].modPhase = juce::MathConstants<float>::twoPi * (float) i / (float) numLines;
        }
        applySpace (space, true);

        // duck
        envDb = -100.0f; peakDb = -100.0f; duckGain = 1.0f;
        amountSmoothed.reset (sr, 0.05); amountSmoothed.setCurrentAndTargetValue (amount);
    }

    void reset()
    {
        for (auto& d : dly) { std::fill (d.buf.begin(), d.buf.end(), 0.0f); d.fbLpf.reset(); d.fbHpf.reset(); }
        for (auto& l : fdn) { std::fill (l.buf.begin(), l.buf.end(), 0.0f); l.lp = 0.0f; }
        std::fill (preDelay.begin(), preDelay.end(), 0.0f);
        sendHpf.reset(); sendLpf.reset(); deEss.reset(); deEssDetect.reset();
        envDb = -100.0f; peakDb = -100.0f; duckGain = 1.0f;
    }

    void setParams (Space s, Echo e, float amountIn, float duckIn, double bpmIn)
    {
        if (s != space) applySpace (s, false);
        echo = e;
        amount = juce::jlimit (0.0f, 1.0f, amountIn); amountSmoothed.setTargetValue (amount);
        duck = juce::jlimit (0.0f, 1.0f, duckIn);
        bpm = (bpmIn > 20.0 && bpmIn < 400.0) ? bpmIn : 120.0;
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int n = buffer.getNumSamples();
        if (n == 0) return;
        const int nCh = juce::jmin (channels, buffer.getNumChannels());
        const float blockMs = 1000.0f * (float) n / (float) sr;

        // ---- mono send --------------------------------------------------
        auto* s = send.getWritePointer (0);
        juce::FloatVectorOperations::copy (s, buffer.getReadPointer (0), n);
        if (nCh > 1) { juce::FloatVectorOperations::add (s, buffer.getReadPointer (1), n); juce::FloatVectorOperations::multiply (s, 0.5f, n); }

        // ---- lead envelope → ducking (adaptive threshold) ---------------
        float acc = 0.0f, sib = 0.0f;
        for (int i = 0; i < n; ++i) { acc += s[i] * s[i]; const float d = deEssDetect.processSample (s[i]); sib += d * d; }
        const float rmsDb = juce::Decibels::gainToDecibels (std::sqrt (acc / (float) n) + 1.0e-9f);
        const float sibDb = juce::Decibels::gainToDecibels (std::sqrt (sib / (float) n) + 1.0e-9f);
        envDb += (rmsDb - envDb) * (rmsDb > envDb ? coef (settings.duckAttackMs, blockMs) : coef (settings.duckReleaseMs, blockMs));
        if (rmsDb > peakDb) peakDb = rmsDb; else peakDb -= blockMs * 0.001f * (20.0f / settings.peakDecaySec);  // ~20 dB per peakDecaySec
        peakDb = juce::jmax (peakDb, -60.0f);
        // 0 = silence, 1 = singing at full level
        const float singing = juce::jlimit (0.0f, 1.0f, (envDb - (peakDb - settings.duckRelativeDb)) / settings.duckRelativeDb + 0.0f);
        const float targetDuckDb = -settings.duckRangeDb * duck * singing;
        const float targetDuck = juce::Decibels::decibelsToGain (targetDuckDb);
        duckGain += (targetDuck - duckGain) * (targetDuck < duckGain ? coef (settings.duckAttackMs, blockMs) : coef (settings.duckReleaseMs, blockMs));

        // ---- de-ess on the send -------------------------------------------
        float wantDeEss = 0.0f;
        if (rmsDb > -60.0f)
        {
            const float excess = (sibDb - rmsDb) + 12.0f - settings.deEssThresholdDb;
            if (excess > 0.0f) wantDeEss = juce::jmin (settings.deEssMaxDb, excess);
        }
        deEssDb += (wantDeEss - deEssDb) * (wantDeEss > deEssDb ? coef (1.5f, blockMs) : coef (80.0f, blockMs));
        *deEssCoeffs = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, settings.deEssHz, 2.0f, juce::Decibels::decibelsToGain (-deEssDb));

        for (int i = 0; i < n; ++i)
            s[i] = sendLpf.processSample (sendHpf.processSample (deEss.processSample (s[i])));

        // ---- tempo delay ----------------------------------------------------
        const float beatSec = (float) (60.0 / bpm);
        float delaySec = 0.0f;
        switch (echo) { case Echo::Eighth: delaySec = beatSec * 0.5f; break; case Echo::Quarter: delaySec = beatSec; break;
                        case Echo::DottedEighth: delaySec = beatSec * 0.75f; break; default: break; }
        const bool echoOn = delaySec > 0.0f;
        const float targetDelaySamples = juce::jlimit (1.0f, (float) dly[0].buf.size() - 2.0f, delaySec * (float) sr);
        if (delaySamplesSmoothed <= 0.0f) delaySamplesSmoothed = targetDelaySamples;

        auto* wl = wet.getWritePointer (0);
        auto* wr = wet.getWritePointer (1);
        const float dlyGain = echoOn ? juce::Decibels::decibelsToGain (settings.delayLevelDb) : 0.0f;
        const float widthSamples = settings.delayWidthMs * 0.001f * (float) sr;

        for (int i = 0; i < n; ++i)
        {
            delaySamplesSmoothed += (targetDelaySamples - delaySamplesSmoothed) * 0.0005f;   // glide, no clicks on tempo change
            float echoL = 0.0f, echoR = 0.0f;
            if (echoOn)
            {
                echoL = readDelay (dly[0], delaySamplesSmoothed);
                echoR = readDelay (dly[1], delaySamplesSmoothed + widthSamples);
                const float fbL = flushDenormal (dly[0].fbHpf.processSample (dly[0].fbLpf.processSample (echoL))) * settings.delayFeedback;
                const float fbR = flushDenormal (dly[1].fbHpf.processSample (dly[1].fbLpf.processSample (echoR))) * settings.delayFeedback;
                writeDelay (dly[0], s[i] + fbL);
                writeDelay (dly[1], s[i] + fbR);
            }
            else { writeDelay (dly[0], 0.0f); writeDelay (dly[1], 0.0f); }

            // ---- reverb: pre-delay then FDN ----
            const float revIn = s[i] + (echoL + echoR) * 0.5f * settings.delayToReverb * dlyGain;
            preDelay[(size_t) preW] = revIn;
            int pr = preW - preDelaySamples; if (pr < 0) pr += (int) preDelay.size();
            const float x = preDelay[(size_t) pr];
            preW = (preW + 1) % (int) preDelay.size();

            float outL = 0.0f, outR = 0.0f;
            fdnTick (x, outL, outR);

            wl[i] = limit (outL * revGain + echoL * dlyGain);
            wr[i] = limit (outR * revGain + echoR * dlyGain);
        }

        // ---- mix: lead untouched + wet × amount × duck ---------------------
        const float wetMax = juce::Decibels::decibelsToGain (settings.maxWetDb);
        float lastWet = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float a = amountSmoothed.getNextValue() * wetMax * duckGain;
            float gl = wl[i] * a, gr = wr[i] * a;
            if (! std::isfinite (gl) || ! std::isfinite (gr)) { gl = gr = 0.0f; panic(); }
            buffer.getWritePointer (0)[i] += nCh > 1 ? gl : 0.5f * (gl + gr);
            if (nCh > 1) buffer.getWritePointer (1)[i] += gr;
            lastWet = juce::jmax (lastWet, std::abs (gl));
        }
        wetMeter += (lastWet - wetMeter) * 0.3f;
    }

    // ---- UI -------------------------------------------------------------
    float getDuckGain() const { return duckGain; }
    float getWetMeter() const { return wetMeter; }
    double getBpm() const { return bpm; }

private:
    static constexpr int numLines = 8;
    void panic() { reset(); }
    static float coef (float ms, float blockMs) { return 1.0f - std::exp (-blockMs / ms); }

    struct DelayLine { std::vector<float> buf; int w = 0; juce::dsp::IIR::Filter<float> fbLpf, fbHpf; };
    struct FdnLine   { std::vector<float> buf; int w = 0; float base = 0.0f; float len = 0.0f; float lp = 0.0f; float modPhase = 0.0f; float g = 0.0f; };

    static float readDelay (DelayLine& d, float samples)
    {
        const int len = (int) d.buf.size();
        float rp = (float) d.w - samples; if (rp < 0) rp += (float) len;
        const int i0 = (int) rp; const float fr = rp - (float) i0; const int i1 = (i0 + 1) % len;
        return d.buf[(size_t) i0] + (d.buf[(size_t) i1] - d.buf[(size_t) i0]) * fr;
    }
    static void writeDelay (DelayLine& d, float v) { d.buf[(size_t) d.w] = v; d.w = (d.w + 1) % (int) d.buf.size(); }

    void applySpace (Space s, bool force)
    {
        const auto& p = preset (s);
        space = s;
        preDelaySamples = juce::jlimit (1, juce::jmax (1, (int) preDelay.size() - 1), (int) (p.preDelayMs * 0.001f * (float) sr));
        dampCoef = std::exp (-juce::MathConstants<float>::twoPi * p.dampHz / (float) sr);
        width = p.width;
        for (int i = 0; i < numLines; ++i)
        {
            const float len = juce::jlimit (16.0f, (float) fdn[i].buf.size() - 8.0f, fdn[i].base * p.sizeScale);
            fdn[i].len = len;
            fdn[i].g = juce::jmin (0.985f, std::pow (10.0f, -3.0f * len / (p.rt60Sec * (float) sr)));   // per-pass gain for RT60, never ~1
            if (force) { std::fill (fdn[i].buf.begin(), fdn[i].buf.end(), 0.0f); fdn[i].lp = 0.0f; }
        }
        revGain = juce::Decibels::decibelsToGain (settings.reverbLevelDb) * 0.35f;   // FDN is hot; normalise
    }

    void fdnTick (float x, float& outL, float& outR)
    {
        // Stability: Householder matrix is orthonormal (energy preserving), every line is
        // multiplied by g < 1 AND low-passed (one-pole, |H| <= 1) → loop gain is strictly < 1
        // for every frequency, whatever the modulation does. Values are clamped as a last resort.
        float v[numLines];
        for (int i = 0; i < numLines; ++i)
        {
            auto& l = fdn[i];
            l.modPhase += juce::MathConstants<float>::twoPi * settings.modHz * (1.0f + 0.07f * (float) i) / (float) sr;
            if (l.modPhase > juce::MathConstants<float>::twoPi) l.modPhase -= juce::MathConstants<float>::twoPi;
            const int len = (int) l.buf.size();
            const float d = juce::jlimit (2.0f, (float) len - 3.0f, l.len + std::sin (l.modPhase) * settings.modDepthSamples);
            float rp = (float) l.w - d; if (rp < 0) rp += (float) len;
            const int i0 = (int) rp; const float fr = rp - (float) i0; const int i1 = (i0 + 1) % len;
            float y = l.buf[(size_t) i0] + (l.buf[(size_t) i1] - l.buf[(size_t) i0]) * fr;
            l.lp = y + dampCoef * (l.lp - y);                 // one-pole LP, gain <= 1 at every frequency
            l.lp = flushDenormal (l.lp);
            v[i] = l.lp * l.g;                                // g < 1 by construction
        }
        float sum = 0.0f; for (int i = 0; i < numLines; ++i) sum += v[i];
        const float k = (2.0f / (float) numLines) * sum;
        for (int i = 0; i < numLines; ++i)
        {
            auto& l = fdn[i];
            float w = x + (v[i] - k);
            w = juce::jlimit (-4.0f, 4.0f, w);                // last-resort clamp: can never run away
            l.buf[(size_t) l.w] = w;
            l.w = (l.w + 1) % (int) l.buf.size();
        }
        const float a = v[0] + v[2] + v[4] + v[6];
        const float b = v[1] + v[3] + v[5] + v[7];
        const float mid = (a + b) * 0.5f, side = (a - b) * 0.5f * width;
        outL = mid + side; outR = mid - side;
    }

    static float flushDenormal (float v) { return std::abs (v) < 1.0e-20f ? 0.0f : v; }

    /** Simple wet-path safety limiter: never lets the space exceed ~-1 dBFS even if everything stacks up. */
    float limit (float v)
    {
        const float a = std::abs (v);
        if (a > limEnv) limEnv = a; else limEnv += (a - limEnv) * 0.0005f;
        const float ceiling = 0.89f;
        const float g = limEnv > ceiling ? ceiling / limEnv : 1.0f;
        return v * g;
    }
    float limEnv = 0.0f;

    double sr = 48000.0; int channels = 2; double bpm = 120.0;
    juce::AudioBuffer<float> send, wet;

    juce::dsp::IIR::Filter<float> sendHpf, sendLpf, deEss, deEssDetect;
    juce::dsp::IIR::Coefficients<float>::Ptr deEssCoeffs;
    float deEssDb = 0.0f;

    DelayLine dly[2]; float delaySamplesSmoothed = 0.0f;
    std::vector<float> preDelay; int preW = 0; int preDelaySamples = 1;
    std::array<FdnLine, numLines> fdn;
    float dampCoef = 0.5f, width = 1.0f, revGain = 0.3f;

    Space space = Space::Room; Echo echo = Echo::Eighth;
    float amount = 0.4f, duck = 0.6f;
    juce::SmoothedValue<float> amountSmoothed { 0.4f };
    float envDb = -100.0f, peakDb = -100.0f, duckGain = 1.0f, wetMeter = 0.0f;
};
