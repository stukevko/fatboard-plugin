// Offline test for Harun: synthetic "voice" (saw + vibrato + formant filters)
// singing a melody in A minor. Checks the pitch tracker, prints what the
// harmony voice sings, verifies output is finite, and writes a WAV to listen to.
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "HarunEngine.h"
#include <cstdio>

int main (int argc, char** argv)
{
    const double sr = 48000.0; const int block = 256;
    const float melody[] = { 69, 72, 74, 76, 74, 72, 69, 67 };       // A C D E D C A G (A minor)
    const double noteSec = 0.6;
    const int total = (int) (sr * noteSec * 8);

    // synth voice
    juce::AudioBuffer<float> src (2, total);
    juce::dsp::IIR::Filter<float> f1, f2;
    f1.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 700.0f, 2.0f, 4.0f);
    f2.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, 4000.0f, 0.7f);
    double phase = 0.0;
    for (int i = 0; i < total; ++i)
    {
        const int ni = juce::jmin (7, (int) (i / (sr * noteSec)));
        const float midi = melody[ni] + 0.3f * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * i / sr); // vibrato
        const double hz = 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
        phase += hz / sr; if (phase >= 1.0) phase -= 1.0;
        float s = (float) (2.0 * phase - 1.0) * 0.3f;
        s = f2.processSample (f1.processSample (s));
        // envelope per note
        const double t = std::fmod (i / sr, noteSec);
        const float env = (float) juce::jmin (1.0, t / 0.03) * (float) juce::jmin (1.0, (noteSec - t) / 0.05);
        s *= env;
        src.setSample (0, i, s); src.setSample (1, i, s);
    }

    HarunEngine e; e.prepare (sr, block, 2);
    e.setParams (9 /*A*/, Harmony::Scale::Minor, HarunEngine::Voice::High, HarunEngine::Mode::Harmony, 0.8f, 0.6f);
    std::printf ("latency: %d samples (%.0f ms)\n", e.getLatencySamples(), 1000.0 * e.getLatencySamples() / sr);

    juce::AudioBuffer<float> out (2, total); out.makeCopyOf (src);
    bool bad = false; float peak = 0.0f; int lastNote = -1;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, n);
        e.process (view);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i) { const float v = view.getSample (ch, i); if (! std::isfinite (v)) bad = true; peak = juce::jmax (peak, std::abs (v)); }
        const int ni = juce::jmin (7, (int) (pos / (sr * noteSec)));
        if (ni != lastNote && pos > ni * sr * noteSec + sr * 0.25)
        {
            lastNote = ni;
            std::printf ("note %d: sung %s (%.0f)  tracker %s (%.1f) voiced=%d  harmony shift %+.2f st → %s\n",
                         ni, Harmony::noteName (melody[ni]), melody[ni], Harmony::noteName (e.getNoteMidi()), e.getNoteMidi(),
                         (int) e.isVoiced(), e.getVoiceShift (0), Harmony::noteName (e.getNoteMidi() + e.getVoiceShift (0)));
        }
    }
    std::printf ("finite:%s peak:%.2f dBFS\n", bad ? "NO!" : "yes", juce::Decibels::gainToDecibels (peak));

    if (argc > 1)
    {
        juce::File f (argv[1]);
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 16, {}, 0));
        if (w) { w->writeFromAudioSampleBuffer (out, 0, total); std::printf ("wrote %s\n", argv[1]); }
    }
    return bad ? 1 : 0;
}
