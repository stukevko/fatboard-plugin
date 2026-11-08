// Offline test for Dafocus: synthetic vocal phrases with gaps at 90 BPM.
// Checks stability, that the space ducks while "singing" and blooms in gaps,
// that the echo lands on the 1/8 grid, and writes a WAV to listen to.
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "DafocusEngine.h"
#include <cstdio>

int main (int argc, char** argv)
{
    const double sr = 48000.0; const int block = 256; const double bpm = 90.0;
    const double beat = 60.0 / bpm;
    const int total = (int) (sr * beat * 16);   // 4 bars

    // "vocal": formant-filtered saw bursts, 2 beats on, 2 beats off
    juce::AudioBuffer<float> src (2, total); src.clear();
    juce::dsp::IIR::Filter<float> f1, f2;
    f1.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 800.0f, 2.0f, 4.0f);
    f2.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, 5000.0f, 0.7f);
    double phase = 0.0;
    for (int i = 0; i < total; ++i)
    {
        const double t = i / sr;
        const double inBar = std::fmod (t, 4 * beat);
        const bool on = inBar < 2 * beat;
        const double hz = 220.0 * (1.0 + 0.004 * std::sin (2 * juce::MathConstants<double>::pi * 5.0 * t));
        phase += hz / sr; if (phase >= 1.0) phase -= 1.0;
        float s = (float) (2.0 * phase - 1.0) * 0.4f;
        s = f2.processSample (f1.processSample (s));
        float env = 0.0f;
        if (on) env = (float) juce::jmin (1.0, inBar / 0.02) * (float) juce::jmin (1.0, (2 * beat - inBar) / 0.05);
        s *= env;
        src.setSample (0, i, s); src.setSample (1, i, s);
    }

    DafocusEngine e; e.prepare (sr, block, 2);
    e.setParams (DafocusEngine::Space::Room, DafocusEngine::Echo::Eighth, 0.5f, 0.7f, bpm);

    juce::AudioBuffer<float> out (2, total); out.makeCopyOf (src);
    bool bad = false; float peak = 0.0f;
    float duckDuring = 0.0f, duckGap = 0.0f; int nd = 0, ng = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, n);
        e.process (view);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i) { const float v = view.getSample (ch, i); if (! std::isfinite (v)) bad = true; peak = juce::jmax (peak, std::abs (v)); }
        const double t = pos / sr; const double inBar = std::fmod (t, 4 * beat);
        if (inBar > 0.5 * beat && inBar < 1.8 * beat) { duckDuring += e.getDuckGain(); ++nd; }
        if (inBar > 2.6 * beat && inBar < 3.8 * beat) { duckGap += e.getDuckGain(); ++ng; }
    }
    std::printf ("finite:%s peak:%.2f dBFS\n", bad ? "NO!" : "yes", juce::Decibels::gainToDecibels (peak));
    std::printf ("duck gain while singing: %.2f (%.1f dB)   in the gap: %.2f (%.1f dB)\n",
                 duckDuring / nd, juce::Decibels::gainToDecibels (duckDuring / nd), duckGap / ng, juce::Decibels::gainToDecibels (duckGap / ng));

    // wet-only energy (out - dry) in the gap: tail present?
    double gapE = 0.0, singE = 0.0; int gn = 0, sn = 0;
    for (int i = 0; i < total; ++i)
    {
        const double inBar = std::fmod (i / sr, 4 * beat);
        const float w = out.getSample (0, i) - src.getSample (0, i);
        if (inBar > 2.1 * beat && inBar < 3.0 * beat) { gapE += w * w; ++gn; }
        if (inBar > 0.5 * beat && inBar < 1.8 * beat) { singE += w * w; ++sn; }
    }
    std::printf ("wet level in gap: %.1f dBFS   while singing: %.1f dBFS\n",
                 juce::Decibels::gainToDecibels ((float) std::sqrt (gapE / gn) + 1e-9f), juce::Decibels::gainToDecibels ((float) std::sqrt (singE / sn) + 1e-9f));

    // echo timing: cross-correlate wet with dry around 1/8 note
    {
        const int lag8 = (int) (beat * 0.5 * sr);
        auto corrAt = [&] (int lag) { double c = 0; for (int i = lag; i < total; i += 7) c += (out.getSample (0, i) - src.getSample (0, i)) * src.getSample (0, i - lag); return c; };
        const double c8 = corrAt (lag8), cOff = corrAt (lag8 + (int) (0.1 * sr));
        std::printf ("echo correlation at 1/8: %.3g   off-grid: %.3g  → %s\n", c8, cOff, std::abs (c8) > std::abs (cOff) * 3 ? "on the grid" : "?");
    }

    if (argc > 1)
    {
        juce::File f (argv[1]); f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 16, {}, 0));
        if (w) { w->writeFromAudioSampleBuffer (out, 0, total); std::printf ("wrote %s\n", argv[1]); }
    }
    return bad ? 1 : 0;
}
