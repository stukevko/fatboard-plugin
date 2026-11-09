// Spit offline test: synthetic "beatbox" (kick = low thump, snare = noise burst with mids,
// hat = short high noise) in a fixed pattern at 90 BPM. Reports detection + classification accuracy.
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "SpitEngine.h"
#include <cstdio>

int main (int argc, char** argv)
{
    const double sr = 48000.0; const int block = 128; const double bpm = 90.0, beat = 60.0 / bpm, s16 = beat / 4.0;
    // pattern per bar (16 steps): K . H . S . H . K K H . S . H H
    const char* pat = "K.H.S.H.KKH.S.HH";
    const int bars = 4; const int total = (int) (sr * beat * 4 * bars) + (int) sr;
    juce::AudioBuffer<float> src (1, total); src.clear();
    juce::Random rng (11);
    struct Truth { int sample; SpitEngine::Drum d; }; std::vector<Truth> truth;
    juce::dsp::IIR::Filter<float> snMid; snMid.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, 1800.0f, 0.8f);
    juce::dsp::IIR::Filter<float> hatHp; hatHp.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 6000.0f, 0.7f);
    for (int b = 0; b < bars; ++b) for (int st = 0; st < 16; ++st)
    {
        const char c = pat[st]; if (c == '.') continue;
        const int at = (int) (sr * (0.5 + b * 4 * beat + st * s16)) + rng.nextInt (200) - 100;   // ±2 ms human, 0.5 s lead-in
        SpitEngine::Drum d = c == 'K' ? SpitEngine::Kick : c == 'S' ? SpitEngine::Snare : SpitEngine::Hat;
        truth.push_back ({ at, d });
        const int len = d == SpitEngine::Hat ? (int) (sr * 0.05) : (int) (sr * 0.12);
        double ph = 0.0;
        for (int i = 0; i < len && at + i < total; ++i)
        {
            const float t = (float) i / (float) sr;
            float v = 0.0f;
            if (d == SpitEngine::Kick) { const double f = 60.0 + 80.0 * std::exp (-t * 40.0); ph += f / sr; v = (float) std::sin (2 * juce::MathConstants<double>::pi * ph) * std::exp (-t * 18.0f) * 0.8f + (rng.nextFloat() - 0.5f) * 0.1f * std::exp (-t * 80.0f); }
            else if (d == SpitEngine::Snare) { v = (snMid.processSample (rng.nextFloat() - 0.5f) * 2.5f + (rng.nextFloat() - 0.5f) * 0.6f) * std::exp (-t * 22.0f) * 0.7f; }
            else { v = hatHp.processSample (rng.nextFloat() - 0.5f) * std::exp (-t * 60.0f) * 0.6f; }
            src.addSample (0, at + i, v);
        }
    }
    SpitEngine e; e.prepare (sr, block); e.settings.sensitivity = 0.5f;
    std::vector<SpitEngine::Hit> hits; struct Det { int sample; SpitEngine::Drum d; float conf; SpitEngine::Features f; }; std::vector<Det> det;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        e.process (src.getReadPointer (0) + pos, n, hits);
        for (auto& h : hits) det.push_back ({ pos + h.sampleOffset - e.latencySamples(), h.drum, h.confidence, h.features });
    }
    int matched = 0, correct = 0; const int tol = (int) (sr * 0.03);
    std::vector<bool> used (det.size(), false);
    for (auto& t : truth)
    {
        int best = -1; int bd = tol + 1;
        for (size_t i = 0; i < det.size(); ++i) if (! used[i] && std::abs (det[i].sample - t.sample) < bd) { bd = std::abs (det[i].sample - t.sample); best = (int) i; }
        if (best >= 0) { used[(size_t) best] = true; ++matched; if (det[(size_t) best].d == t.d) ++correct; }
    }
    int falsePos = 0; for (bool u : used) if (! u) ++falsePos;
    // confusion + per-hit dump
    int conf[3][3] = {};
    for (auto& t : truth) { int best = -1, bd = tol + 1; for (size_t i = 0; i < det.size(); ++i) if (std::abs (det[i].sample - t.sample) < bd) { bd = std::abs (det[i].sample - t.sample); best = (int) i; } if (best >= 0) conf[(int) t.d][(int) det[(size_t) best].d]++; }
    std::printf ("confusion (rows truth K S H / cols detected K S H):\n");
    for (int i = 0; i < 3; ++i) std::printf ("  %c: %2d %2d %2d\n", "KSH"[i], conf[i][0], conf[i][1], conf[i][2]);
    std::printf ("false positives at (ms): "); for (size_t i = 0; i < det.size(); ++i) if (! used[i]) std::printf ("%.0f(%c) ", 1000.0 * det[i].sample / sr, "KSH"[(int) det[i].d]); std::printf ("\n");
    std::printf ("feature dump (truth → low cen high dec):\n");
    for (auto& t : truth) { int best = -1, bd = tol + 1; for (size_t i = 0; i < det.size(); ++i) if (std::abs (det[i].sample - t.sample) < bd) { bd = std::abs (det[i].sample - t.sample); best = (int) i; }
        if (best >= 0) { auto& f = det[(size_t) best].f; std::printf ("  %c→%c  low %.2f  cen %5.0f  high %.2f  dec %3.0f\n", "KSH"[(int) t.d], "KSH"[(int) det[(size_t) best].d], f.low, f.centroid, f.high, f.decay); } }
    std::printf ("truth %zu hits | detected %zu | matched %d (%.0f%%) | class correct %d/%d (%.0f%%) | false positives %d | latency %.1f ms\n",
                 truth.size(), det.size(), matched, 100.0 * matched / truth.size(), correct, matched, 100.0 * correct / juce::jmax (1, matched), falsePos, 1000.0 * e.latencySamples() / sr);
    // per class
    for (int d = 0; d < 3; ++d) { int n = 0, ok = 0; size_t k = 0; for (auto& t : truth) { if (t.d != d) continue; ++n; } std::printf ("  %s: %d in pattern\n", d == 0 ? "kick" : d == 1 ? "snare" : "hat", n); juce::ignoreUnused (ok, k); }
    if (argc > 1) { juce::File f (argv[1]); f.deleteFile(); juce::WavAudioFormat w; std::unique_ptr<juce::AudioFormatWriter> wr (w.createWriterFor (new juce::FileOutputStream (f), sr, 1, 16, {}, 0)); if (wr) wr->writeFromAudioSampleBuffer (src, 0, total); }
    return 0;
}
