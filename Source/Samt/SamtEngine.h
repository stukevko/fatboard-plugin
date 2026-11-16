#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <cmath>

// ============================================================================
//  Samt – optical levelling for vocals (stereo, linked)          v0.1
//
//  Our own take on the classic electro-optical leveller. Not a copy of any product:
//  it follows how a light cell behaves, with our own numbers.
//
//    Two knobs do the work, like on the hardware that made the sound famous:
//      Peak Reduction  how deep the voice is pushed into the cell
//      Gain            brings the level back up
//
//    What makes it "optical":
//      - slow-ish attack (about 10 ms): consonants get through, the body is held
//      - release in two parts: about half is back in 60 ms, the rest takes
//        0.5 to 5 seconds, and the longer and harder it worked, the longer it takes.
//        That is why it sounds like a hand on the fader and not like a pump.
//      - a soft knee: there is no point where it "kicks in"
//
//    Compress  about 3:1, wide knee: everyday vocal levelling
//    Limit     about 12:1, tighter knee: holds a wild take in place
//
//    Warmth    a touch of tube colour after the cell (even harmonics). 0 = clean.
//    Mix       parallel blend. No latency, so dry and wet stay in phase.
//
//    The detector listens to both channels together (linked), so the stereo picture does not move,
//    and it ignores everything below 90 Hz, so pops and rumble do not duck the voice.
//
//  Peak Reduction 0 + Warmth 0 + Gain 0 + Mix 100 = bit-identical to the input.
//  All TUNE values are constants here.
// ============================================================================
class SamtEngine
{
public:
    enum Mode { Compress = 0, Limit };

    // TUNE
    static constexpr float thresholdPerUnit = -46.0f;   // Peak Reduction 100 % puts the threshold here (dBFS)
    static constexpr float ratioCompress = 3.2f, kneeCompress = 14.0f;
    static constexpr float ratioLimit = 12.0f,  kneeLimit = 7.0f;
    static constexpr float attackMs = 10.0f;
    static constexpr float fastReleaseMs = 60.0f;       // the quick half
    static constexpr float slowAttackMs = 70.0f;        // the cell needs light for a while before the slow half builds
    static constexpr float slowReleaseMin = 0.5f, slowReleaseMax = 5.0f;   // seconds
    static constexpr float memorySeconds = 4.0f;        // how long the cell "remembers" being lit
    static constexpr float slowShare = 0.5f;            // part of the reduction that releases slowly
    static constexpr float detectorHighPassHz = 90.0f;
    static constexpr float maxReductionDb = 30.0f;

    void prepare (double sampleRate, int /*blockSize*/, int channels)
    {
        sr = sampleRate; nCh = juce::jlimit (1, 2, channels);
        const auto c = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, detectorHighPassHz, 0.707f);
        for (auto& f : scHp) { f.coefficients = c; f.reset(); }
        aAtt = coef (attackMs * 0.001f); aFastRel = coef (fastReleaseMs * 0.001f); aSlowAtt = coef (slowAttackMs * 0.001f);
        aMem = coef (memorySeconds);
        reset();
    }
    void reset()
    {
        fast = slow = memory = 0.0f; for (auto& f : scHp) f.reset(); dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
        grNow.store (0.0f); inPeak.store (0.0f); outPeak.store (0.0f);
    }
    void setParams (float peakReduction, float gainDb, Mode m, float warmthAmount, float mixAmount)
    {
        pr = juce::jlimit (0.0f, 1.0f, peakReduction); makeup = juce::Decibels::decibelsToGain (gainDb);
        mode = m; warmth = juce::jlimit (0.0f, 1.0f, warmthAmount); mix = juce::jlimit (0.0f, 1.0f, mixAmount);
    }

    /** Static curve: reduction in dB (positive) for a detector level in dBFS. */
    static float curveDb (float levelDb, float peakReduction, Mode m)
    {
        if (peakReduction <= 0.0001f) return 0.0f;
        const float thr = thresholdPerUnit * peakReduction, ratio = m == Limit ? ratioLimit : ratioCompress, knee = m == Limit ? kneeLimit : kneeCompress;
        const float over = levelDb - thr, slope = 1.0f - 1.0f / ratio;
        float g;
        if (over <= -knee * 0.5f) g = 0.0f;
        else if (over >= knee * 0.5f) g = slope * over;
        else { const float x = over + knee * 0.5f; g = slope * x * x / (2.0f * knee); }
        return juce::jmin (maxReductionDb, g);
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int n = buffer.getNumSamples(), chs = juce::jmin (nCh, buffer.getNumChannels());
        if (chs < 1 || n < 1) return;
        const bool clean = pr <= 0.0001f && warmth <= 0.0001f && std::abs (makeup - 1.0f) < 1.0e-6f;
        float pkIn = 0.0f, pkOut = 0.0f, grMax = 0.0f;
        // tube stage: a bent curve around a small offset gives even harmonics; scaled so quiet signals pass at unity
        const float drive = 1.0f + 1.6f * warmth, bias = 0.22f * warmth, tb = std::tanh (bias), unity = 1.0f / (drive * (1.0f - tb * tb));
        float* d[2] = { buffer.getWritePointer (0), chs > 1 ? buffer.getWritePointer (1) : nullptr };
        for (int i = 0; i < n; ++i)
        {
            float det = 0.0f;
            for (int ch = 0; ch < chs; ++ch) { pkIn = juce::jmax (pkIn, std::abs (d[ch][i])); det = juce::jmax (det, std::abs (scHp[ch].processSample (d[ch][i]))); }
            const float target = curveDb (juce::Decibels::gainToDecibels (det, -120.0f), pr, mode);

            // the slow half: charges only while there is light for a while, lets go depending on the cell's memory
            const float slowTarget = target * slowShare;
            if (slowTarget > slow) slow += (slowTarget - slow) * aSlowAtt;
            else                   slow += (slowTarget - slow) * coef (slowReleaseMin + (slowReleaseMax - slowReleaseMin) * memory);
            // the fast half takes whatever the slow one has not got yet
            const float fastTarget = juce::jmax (0.0f, target - slow);
            fast += (fastTarget - fast) * (fastTarget > fast ? aAtt : aFastRel);
            const float gr = fast + slow;
            memory += (juce::jlimit (0.0f, 1.0f, gr / 10.0f) - memory) * aMem;
            grMax = juce::jmax (grMax, gr);
            if (clean) { for (int ch = 0; ch < chs; ++ch) pkOut = juce::jmax (pkOut, std::abs (d[ch][i])); continue; }      // nothing to do: leave the samples alone

            const float g = juce::Decibels::decibelsToGain (-gr);
            for (int ch = 0; ch < chs; ++ch)
            {
                const float dry = d[ch][i];
                float y = dry * g;
                if (warmth > 0.0001f)
                {
                    y = (std::tanh (y * drive + bias) - tb) * unity;
                    const float out = y - dcX[ch] + 0.9995f * dcY[ch]; dcX[ch] = y; dcY[ch] = out; y = out;                  // the offset must not reach the output
                }
                y *= makeup;
                y = dry + (y - dry) * mix;
                d[ch][i] = y; pkOut = juce::jmax (pkOut, std::abs (y));
            }
        }
        grNow.store (grMax); inPeak.store (pkIn); outPeak.store (pkOut);
    }

    float getReductionDb() const { return grNow.load(); }      // positive dB, biggest value of the last block
    float getInPeak() const  { return inPeak.load(); }
    float getOutPeak() const { return outPeak.load(); }

private:
    float coef (float seconds) const { return 1.0f - std::exp (-1.0f / (juce::jmax (1.0e-4f, seconds) * (float) sr)); }
    double sr = 48000.0; int nCh = 2;
    float pr = 0.0f, makeup = 1.0f, warmth = 0.0f, mix = 1.0f; Mode mode = Compress;
    float aAtt = 0, aFastRel = 0, aSlowAtt = 0, aMem = 0;
    float fast = 0, slow = 0, memory = 0, dcX[2] {}, dcY[2] {};
    juce::dsp::IIR::Filter<float> scHp[2];
    std::atomic<float> grNow { 0.0f }, inPeak { 0.0f }, outPeak { 0.0f };
};
