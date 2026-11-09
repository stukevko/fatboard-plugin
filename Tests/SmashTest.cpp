// Smash offline: synthetic drums (kick/snare/hat, muddy snare body at 330 Hz, sharp hats) + a "sample"
// (pad with energy at 250 Hz and 1.2 kHz) on the sidechain. Checks: stable, GR in range, mud reduced,
// hats tamed, transients kept, fit carves where the sample lives, no sidechain → fit zero.
#include <juce_dsp/juce_dsp.h>
#include "SmashEngine.h"
#include <cstdio>
int main()
{
    const double sr = 48000.0; const int block = 256; const int total = (int) sr * 8;
    juce::Random rng (5);
    juce::AudioBuffer<float> drums (2, total), sample (2, total); drums.clear(); sample.clear();
    juce::dsp::IIR::Filter<float> body; body.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 330.0f, 1.5f, 6.0f);
    juce::dsp::IIR::Filter<float> hatF; hatF.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 6000.0f, 0.7f);
    const double beat = 60.0 / 140.0;
    for (int i = 0; i < total; ++i)
    {
        const double t = i / sr; const double inBeat = std::fmod (t, beat); const int bn = (int) (t / beat);
        float v = 0.0f;
        if (bn % 2 == 0 && inBeat < 0.15) { const double f = 55 + 90 * std::exp (-inBeat * 40); v += (float) std::sin (2 * juce::MathConstants<double>::pi * f * inBeat) * (float) std::exp (-inBeat * 20) * 0.9f; }
        if (bn % 2 == 1 && inBeat < 0.2) v += body.processSample ((rng.nextFloat() - 0.5f) * 1.2f) * (float) std::exp (-inBeat * 18);
        if (std::fmod (t, beat / 2) < 0.04) v += hatF.processSample (rng.nextFloat() - 0.5f) * (float) std::exp (-std::fmod (t, beat / 2) * 90) * 0.7f;
        drums.setSample (0, i, v); drums.setSample (1, i, v);
        const float s = 0.25f * (float) (std::sin (2 * juce::MathConstants<double>::pi * 250 * t) + 0.7 * std::sin (2 * juce::MathConstants<double>::pi * 1200 * t));
        sample.setSample (0, i, s); sample.setSample (1, i, s);
    }
    auto bandDb = [&] (const juce::AudioBuffer<float>& b, float hz, float q) { juce::dsp::IIR::Filter<float> f; f.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, hz, q); double a = 0; for (int i = total / 4; i < total; ++i) { const float y = f.processSample (b.getSample (0, i)); a += y * y; } return 20 * std::log10 (std::sqrt (a / (total * 0.75)) + 1e-9); };
    auto crest = [&] (const juce::AudioBuffer<float>& b) { double pk = 0, e = 0; for (int i = total / 4; i < total; ++i) { const float y = std::abs (b.getSample (0, i)); pk = std::max (pk, (double) y); e += y * y; } return 20 * std::log10 (pk / (std::sqrt (e / (total * 0.75)) + 1e-9)); };

    for (int sc = 0; sc < 2; ++sc)
    {
        SmashEngine e; e.prepare (sr, block, 2); e.setParams (SmashEngine::Style::Punch, 0.7f, 0.8f, sc == 1);
        juce::AudioBuffer<float> out (2, total); out.makeCopyOf (drums);
        bool bad = false; float peak = 0; float grSum = 0; int gn = 0;
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = juce::jmin (block, total - pos);
            juce::AudioBuffer<float> v (out.getArrayOfWritePointers(), 2, pos, n), s (sample.getArrayOfWritePointers(), 2, pos, n);
            e.process (v, sc ? &s : nullptr);
            for (int i = 0; i < n; ++i) { const float y = v.getSample (0, i); if (! std::isfinite (y)) bad = true; peak = std::max (peak, std::abs (y)); }
            if (pos > total / 4) { grSum += e.getGrDb(); ++gn; }
        }
        std::printf ("\n== %s ==  finite:%s  peak %.1f dBFS  avg GR %.1f dB  mud cut %.1f dB  hi tame %.1f dB  fit [%.1f %.1f %.1f] dB\n",
                     sc ? "with sample on sidechain" : "no sidechain", bad ? "NO" : "yes", 20 * std::log10 (peak), grSum / gn, e.getMudDb(), e.getHiDb(), e.getFitDb (0), e.getFitDb (1), e.getFitDb (2));
        std::printf ("  330 Hz (mud):  %.1f → %.1f dB   7.5k (hats): %.1f → %.1f dB   250 Hz: %.1f → %.1f   1.2k: %.1f → %.1f   4k: %.1f → %.1f\n",
                     bandDb (drums, 330, 2), bandDb (out, 330, 2), bandDb (drums, 7500, 2), bandDb (out, 7500, 2),
                     bandDb (drums, 250, 2), bandDb (out, 250, 2), bandDb (drums, 1200, 2), bandDb (out, 1200, 2), bandDb (drums, 4000, 2), bandDb (out, 4000, 2));
        std::printf ("  crest factor (transients): %.1f dB → %.1f dB  (higher = punchier)\n", crest (drums), crest (out));
        auto rmsLast = [&] (const juce::AudioBuffer<float>& b) { double a = 0; for (int i = total - (int) sr * 2; i < total; ++i) a += b.getSample (0, i) * b.getSample (0, i); return 20 * std::log10 (std::sqrt (a / (sr * 2)) + 1e-9); };
        std::printf ("  loudness last 2 s: in %.1f dB → out %.1f dB (level-neutral target: ±1 dB)\n", rmsLast (drums), rmsLast (out));
    }
    return 0;
}
