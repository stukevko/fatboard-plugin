// Funke offline check: notes stay in the vibe's scale, strong notes are chord notes, same seed = same idea,
// the drum modes do what they say, the MIDI file round-trips, the drum analysis finds a kick pattern.
#include "FunkeGen.h"
#include <cstdio>
#include <set>
using namespace Funke;

int main()
{
    int fails = 0; auto fail = [&] (const char* m) { ++fails; std::printf ("  FAIL: %s\n", m); };
    DrumGrid grid; grid.valid = true;
    for (int i = 0; i < 32; ++i) { grid.hit[i] = (i % 2 == 0) ? 0.6f : 0.0f; }
    for (int k : { 0, 3, 6, 10, 16, 19, 22, 27 }) { grid.kick[k] = 1.0f; grid.hit[k] = 1.0f; }
    for (int k : { 4, 12, 20, 28 }) grid.hit[k] = 1.0f;

    for (int vb = 0; vb < 6; ++vb)
    {
        const auto& v = vibe (vb); int notes = 0, offScale = 0, badStrong = 0, outOfLoop = 0, melNotes = 0; double minLen = 99; int lowest = 127, highest = 0;
        std::set<juce::String> ideas;
        for (uint32_t seed = 1; seed <= 60; ++seed) for (int key : { 0, 5, 9 }) for (int bars : { 4, 8 })
        {
            Settings s; s.vibe = vb; s.key = key; s.bars = bars; s.motion = 0.1f + 0.015f * (float) seed; s.density = 0.05f + 0.015f * (float) seed;
            s.seedChords = seed; s.seedMelody = seed * 7 + 1; s.seedBass = seed * 13 + 2;
            const auto p = generate (s); const auto prog = progressionFor (s);
            std::set<int> allowed; for (int k = 0; k < 7; ++k) allowed.insert ((key + v.scale[k]) % 12);
            if (v.leadingTone) allowed.insert ((key + 11) % 12);
            for (const auto* layer : { &p.chords, &p.melody, &p.bass }) for (const auto& n : *layer)
            {
                ++notes; if (! allowed.count (n.pitch % 12)) ++offScale;
                if (n.start < -1e-9 || n.start >= p.beats() || n.start + n.length > p.beats() + 1e-6 || n.length <= 0) ++outOfLoop;
                minLen = juce::jmin (minLen, n.length);
            }
            for (const auto& n : p.melody)
            {
                ++melNotes; lowest = juce::jmin (lowest, n.pitch); highest = juce::jmax (highest, n.pitch);
                const int bar = (int) (n.start / 4.0); const double inBar = n.start - bar * 4.0;
                if (std::abs (std::fmod (inBar, 2.0)) < 1e-9)      // on beat 1 or 3
                {
                    const auto c = detail::chordSemis (v, prog[(size_t) bar], false); bool ok = false;
                    for (int x : c) if (((key + x) % 12 + 12) % 12 == n.pitch % 12) ok = true;
                    if (! ok) ++badStrong;
                }
            }
            for (size_t b = 0; b < (size_t) bars; ++b) { bool found = false; const auto c = detail::chordSemis (v, prog[b], false); for (const auto& n : p.bass) if ((int) (n.start / 4.0) == (int) b && n.pitch % 12 == ((key + c[0]) % 12 + 12) % 12) found = true; if (! found) ++badStrong; }
            if (key == 0 && bars == 4) { juce::String id; for (const auto& n : p.melody) id << n.pitch << "@" << juce::String (n.start, 2) << " "; ideas.insert (id); }
            const auto again = generate (s); bool same = again.melody.size() == p.melody.size() && again.chords.size() == p.chords.size();
            for (size_t i = 0; same && i < p.melody.size(); ++i) same = again.melody[i].pitch == p.melody[i].pitch && again.melody[i].start == p.melody[i].start;
            if (! same) { fail ("same seed gave a different idea"); break; }
        }
        std::printf ("%-8s %5d notes: %d outside the scale, %d strong notes not in the chord, %d outside the loop, shortest %.3f, melody %d..%d, %d different melodies from 60 seeds\n",
                     v.name, notes, offScale, badStrong, outOfLoop, minLen, lowest, highest, (int) ideas.size());
        if (offScale > 0) fail ("note outside the scale");
        if (badStrong > 0) fail ("strong melody note or bass root does not fit the chord");
        if (outOfLoop > 0 || minLen < 0.1) fail ("note outside the loop or too short");
        if (ideas.size() < 55) fail ("ideas repeat too often");
        if (melNotes < 200 || highest - lowest > 30) fail ("melody range");
    }

    // drum modes: share of melody notes that sit on a kick step / on any hit
    {
        auto share = [&] (int mode, bool onKick)
        {
            int on = 0, all = 0;
            for (uint32_t seed = 1; seed <= 80; ++seed)
            {
                Settings s; s.vibe = (int) (seed % 6); s.bars = 4; s.density = 0.5f; s.motion = 0.0f; s.drumMode = mode; s.drums = grid; s.seedMelody = seed; s.seedChords = seed + 100;
                for (const auto& n : generate (s).melody) { const int st = ((int) std::lround (n.start * 4.0)) % 32; ++all; if (onKick ? grid.kick[st] > 0.5f : grid.hit[st] > 0.1f) ++on; }
            }
            return 100.0 * on / juce::jmax (1, all);
        };
        const double f = share (Free, true), w = share (WithKick, true), gapsHit = share (InGaps, false), freeHit = share (Free, false);
        std::printf ("melody notes on a kick step: free %.0f %%, with the kick %.0f %%.   on any drum hit: free %.0f %%, in the gaps %.0f %%\n", f, w, freeHit, gapsHit);
        if (! (w > f + 12.0)) fail ("'with the kick' does not follow the kick");
        if (! (gapsHit < freeHit - 12.0)) fail ("'in the gaps' does not avoid the hits");
        Settings s; s.drums = grid; s.drumMode = WithKick; s.bars = 4; const auto p = generate (s); bool ok = ! p.bass.empty();
        for (const auto& n : p.bass) if (grid.kick[((int) std::lround (n.start * 4.0)) % 32] < 0.5f) ok = false;
        std::printf ("bass with drums loaded: %d notes, all on kick steps: %s\n", (int) p.bass.size(), ok ? "yes" : "no"); if (! ok || p.bass.size() != 16) fail ("bass does not follow the kick");
    }

    // MIDI file: write, read back, same notes
    {
        Settings s; s.vibe = 0; s.bars = 4; const auto p = generate (s);
        const auto f = toMidi ({ &p.chords, &p.melody, &p.bass }, 140.0, p.beats());
        juce::MemoryOutputStream mo; const bool wrote = f.writeTo (mo);
        juce::MemoryInputStream mi (mo.getData(), mo.getDataSize(), false); juce::MidiFile back; const bool read = back.readFrom (mi);
        int ons = 0; for (int t = 0; t < back.getNumTracks(); ++t) for (const auto* e : *back.getTrack (t)) if (e->message.isNoteOn()) ++ons;
        const int want = (int) (p.chords.size() + p.melody.size() + p.bass.size());
        std::printf ("MIDI file: %d bytes, %d tracks, %d notes (want %d), %d ticks per quarter\n", (int) mo.getDataSize(), back.getNumTracks(), ons, want, (int) back.getTimeFormat());
        if (! wrote || ! read || ons != want || back.getNumTracks() != 4) fail ("MIDI file");
    }

    // drum analysis: a synthetic loop with a known kick pattern
    {
        const double sr = 44100.0, bpm = 140.0, step = sr * 60.0 / bpm / 4.0; const int n = (int) (step * 32);
        juce::AudioBuffer<float> b (2, n); b.clear(); juce::Random rng (5);
        auto add = [&] (int st, bool kick) { const int s0 = (int) (st * step); for (int i = 0; i < (int) (sr * (kick ? 0.2 : 0.04)) && s0 + i < n; ++i) { const double t = i / sr; const float x = kick ? (float) (0.9 * std::sin (juce::MathConstants<double>::twoPi * 55.0 * t) * std::exp (-t * 14)) : (rng.nextFloat() * 2 - 1) * 0.3f * (float) std::exp (-t * 90); b.addSample (0, s0 + i, x); b.addSample (1, s0 + i, x); } };
        const std::set<int> kicks { 0, 3, 6, 10, 16, 19, 22, 27 };
        for (int k : kicks) add (k, true);
        for (int st = 0; st < 32; st += 2) if (! kicks.count (st)) add (st, false);
        const auto g = analyseDrums (b, sr, bpm); int right = 0, wrong = 0, hits = 0;
        for (int i = 0; i < 32; ++i) { if (g.kick[i] > 0.5f) { if (kicks.count (i)) ++right; else ++wrong; } if (g.hit[i] > 0.1f) ++hits; }
        std::printf ("drum analysis: %d of 8 kicks found, %d wrong, %d steps with a hit (20 in the loop)\n", right, wrong, hits);
        if (! g.valid || right < 8 || wrong > 0 || hits < 18 || hits > 22) fail ("drum analysis");
    }
    std::printf (fails == 0 ? "\nALL OK\n" : "\n%d FAILED\n", fails);
    return fails;
}
