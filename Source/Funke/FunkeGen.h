#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

// ============================================================================
//  Funke – ideas for chords, melody and bass (MIDI, not audio)      v0.1
//
//  No AI and no samples: every vibe is a small set of musical rules, and inside those rules the dice decide.
//  Same seed = same idea, new seed = new idea in the same mood.
//
//    Chords   one per bar from a list of progressions that work in the vibe, voiced so the notes move little.
//    Melody   a one-bar motif (rhythm + shape) that comes back with small changes: A A' B A''.
//             Notes on the strong beats and at the end of a phrase are chord notes, the ones between walk the scale.
//             Motion adds leaps and quick run-ups ("wind"). Density sets how many notes a bar gets.
//    Bass     the root of each chord. With a drum loop loaded it plays where the kick plays (an 808 line).
//
//    Drums    a grid of 32 sixteenths (two bars): how strong the kick and how strong any hit is on each step.
//             "With the kick": notes prefer the steps where the kick is. "In the gaps": they avoid the hits.
//
//  Pure functions, deterministic on every machine (own random numbers, no std distributions).
// ============================================================================
namespace Funke
{
    struct Note { double start = 0, length = 0; int pitch = 60; float vel = 0.8f; };       // start/length in quarter notes
    struct DrumGrid { bool valid = false; float kick[32] {}; float hit[32] {}; };
    enum DrumMode { Free = 0, WithKick, InGaps };

    struct Settings
    {
        int vibe = 0, key = 0, bars = 4;
        float motion = 0.4f, density = 0.45f;
        int drumMode = Free;
        uint32_t seedChords = 1, seedMelody = 2, seedBass = 3;
        DrumGrid drums;
    };
    struct Pattern
    {
        std::vector<Note> chords, melody, bass;
        juce::StringArray chordNames;          // one per bar
        int bars = 4;
        double beats() const { return bars * 4.0; }
    };

    struct Rng
    {
        uint32_t s;
        explicit Rng (uint32_t seed) : s (seed * 2654435761u + 0x9E3779B9u) { next(); next(); }
        uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
        float unit() { return (float) (next() >> 8) / 16777216.0f; }
        int below (int n) { return n <= 1 ? 0 : (int) (next() % (uint32_t) n); }
        bool chance (float p) { return unit() < p; }
    };

    struct Vibe
    {
        const char* name;
        int scale[7];
        bool leadingTone;            // the chord on the 5th step is major (the classic pull back home)
        bool sevenths;
        int progressions[8][4]; int numProgressions;
        int melodyBase;              // MIDI note of the key's root where the melody lives
        float downBias, legato, restBias;
        bool stabs;                  // chords as short hits instead of held
    };

    inline const Vibe& vibe (int i)
    {
        static const Vibe v[6] = {
            { "Rain",     { 0, 2, 3, 5, 7, 8, 10 }, false, true,  { { 0, 5, 2, 6 }, { 0, 3, 5, 4 }, { 5, 6, 0, 0 }, { 0, 2, 6, 5 }, { 3, 0, 5, 6 }, { 0, 6, 5, 6 } }, 6, 72, 0.62f, 0.85f, 0.25f, false },
            { "Night",    { 0, 2, 3, 5, 7, 8, 10 }, true,  false, { { 0, 0, 5, 4 }, { 0, 3, 0, 4 }, { 0, 5, 3, 4 }, { 0, 0, 3, 4 }, { 0, 6, 5, 4 } },                 5, 60, 0.55f, 0.6f,  0.35f, false },
            { "Dream",    { 0, 2, 3, 5, 7, 9, 10 }, false, true,  { { 0, 3, 0, 6 }, { 0, 3, 6, 3 }, { 3, 0, 6, 0 }, { 0, 2, 3, 6 } },                                 4, 72, 0.45f, 1.0f,  0.2f,  false },
            { "Sun",      { 0, 2, 4, 5, 7, 9, 11 }, false, false, { { 0, 4, 5, 3 }, { 3, 0, 4, 5 }, { 0, 2, 3, 4 }, { 5, 3, 0, 4 }, { 0, 3, 5, 4 } },                 5, 72, 0.4f,  0.7f,  0.15f, false },
            { "Pressure", { 0, 1, 3, 5, 7, 8, 10 }, false, false, { { 0, 1, 0, 6 }, { 0, 0, 1, 0 }, { 0, 5, 6, 0 }, { 0, 1, 5, 6 } },                                 4, 60, 0.5f,  0.45f, 0.3f,  true  },
            { "Pain",     { 0, 2, 3, 5, 7, 8, 10 }, true,  true,  { { 0, 5, 3, 4 }, { 5, 3, 0, 4 }, { 0, 4, 5, 3 }, { 3, 0, 4, 0 }, { 0, 2, 5, 4 } },                 5, 72, 0.55f, 0.9f,  0.2f,  false },
        };
        return v[juce::jlimit (0, 5, i)];
    }
    inline juce::StringArray vibeNames() { juce::StringArray a; for (int i = 0; i < 6; ++i) a.add (vibe (i).name); return a; }

    namespace detail
    {
        /** Semitones above the key's root for scale position p (p can be negative or above 6). */
        inline int semis (const Vibe& v, int p) { const int o = (int) std::floor (p / 7.0); return v.scale[p - o * 7] + 12 * o; }
        /** Melody pitch above the key's root: over the major chord on the 5th step the scale's 7th is raised too, or it would rub against the chord. */
        inline int melodySemis (const Vibe& v, int p, int chordStep)
        {
            const int m = ((p % 7) + 7) % 7;
            return semis (v, p) + ((v.leadingTone && chordStep == 4 && m == 6) ? 1 : 0);
        }
        /** The three (or four) notes of the chord on scale step d, as semitones above the key's root, close position. */
        inline std::vector<int> chordSemis (const Vibe& v, int d, bool seventh)
        {
            std::vector<int> c { semis (v, d), semis (v, d + 2), semis (v, d + 4) };
            if (v.leadingTone && d == 4) c[1] = c[0] + 4;
            if (seventh) c.push_back (semis (v, d + 6));
            return c;
        }
        inline juce::String chordName (const Vibe& v, int key, int d, bool seventh)
        {
            static const char* sharp[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            static const char* flat[12]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
            // spell with flats where musicians would (F minor has Ab and Db, not G# and C#)
            const bool major = v.scale[2] == 4;
            const bool useSharps = key == 4 || key == 6 || key == 11 || key == 9 || (major && (key == 2 || key == 7));
            const char* const* n = useSharps ? sharp : flat;
            const auto c = chordSemis (v, d, seventh);
            const int third = c[1] - c[0], fifth = c[2] - c[0];
            juce::String s = n[((key + c[0]) % 12 + 12) % 12];
            if (fifth == 6) s << "dim"; else if (third == 3) s << "m";
            if (seventh) s << (c[3] - c[0] == 11 ? "maj7" : "7");
            return s;
        }
        inline bool isChordStep (int p, int d) { const int m = ((p - d) % 7 + 7) % 7; return m == 0 || m == 2 || m == 4; }
        inline int nearestChordStep (int p, int d) { for (int k = 0; k < 4; ++k) { if (isChordStep (p - k, d)) return p - k; if (isChordStep (p + k, d)) return p + k; } return p; }
    }

    inline std::vector<int> progressionFor (const Settings& s)
    {
        const auto& v = vibe (s.vibe); Rng r (s.seedChords);
        const int* p = v.progressions[r.below (v.numProgressions)];
        std::vector<int> out;
        for (int b = 0; b < s.bars; ++b) out.push_back (p[b % 4]);
        if (s.bars == 8 && r.chance (0.5f)) out[7] = v.leadingTone ? 4 : (r.chance (0.5f) ? 6 : out[7]);       // second half ends a little differently
        return out;
    }

    inline Pattern generate (const Settings& s)
    {
        Pattern out; out.bars = juce::jlimit (1, 16, s.bars);
        const auto& v = vibe (s.vibe); const int key = ((s.key % 12) + 12) % 12;
        const auto prog = progressionFor (s);
        const bool drums = s.drums.valid && s.drumMode != Free;
        auto kickAt = [&] (int bar, int step) { return s.drums.valid ? s.drums.kick[((bar % 2) * 16 + step) % 32] : 0.0f; };
        auto hitAt  = [&] (int bar, int step) { return s.drums.valid ? s.drums.hit[((bar % 2) * 16 + step) % 32] : 0.0f; };

        // ---------------- chords ----------------
        {
            Rng r (s.seedChords ^ 0x51ed27u);
            const int rootBase = 36 + ((key + 3) % 12) + 9;      // key root somewhere in A2..G#3 (45..56)
            const int keyRoot = rootBase - ((rootBase - key) % 12 + 12) % 12 + 0;
            juce::ignoreUnused (keyRoot);
            for (int b = 0; b < out.bars; ++b)
            {
                const int d = prog[(size_t) b]; const auto c = detail::chordSemis (v, d, v.sevenths);
                out.chordNames.add (detail::chordName (v, key, d, v.sevenths));
                std::vector<int> pitches;
                int root = 48 + key + c[0]; while (root > 56) root -= 12; while (root < 45) root += 12;
                pitches.push_back (root);
                for (size_t i = 1; i < c.size(); ++i) { int p = 48 + key + c[i]; while (p >= 69) p -= 12; while (p < 57) p += 12; pitches.push_back (p); }      // upper notes stay in one octave: little movement between chords
                { int p = 48 + key + c[0]; while (p >= 69) p -= 12; while (p < 57) p += 12; if (std::find (pitches.begin(), pitches.end(), p) == pitches.end() && ! v.sevenths) pitches.push_back (p); }
                std::vector<double> starts;
                if (! v.stabs) starts.push_back (0.0);
                else
                {
                    for (int st = 0; st < 16; ++st) if (drums ? kickAt (b, st) > 0.5f : (st == 0 || st == 6 || st == 10)) starts.push_back (st * 0.25);
                    if (starts.empty()) starts.push_back (0.0);
                }
                for (size_t k = 0; k < starts.size(); ++k)
                {
                    const double len = v.stabs ? 0.4 : 3.92;
                    for (int p : pitches) out.chords.push_back ({ b * 4.0 + starts[k], len, p, 0.62f + 0.1f * r.unit() });
                }
            }
        }

        // ---------------- melody ----------------
        {
            Rng r (s.seedMelody);
            const int perBar = juce::jlimit (1, 10, (int) std::lround (2.0f + s.density * 7.0f));
            auto rhythm = [&] (int bar, int count)
            {
                float w[16];
                for (int st = 0; st < 16; ++st)
                {
                    float x = st % 4 == 0 ? 1.0f : st % 2 == 0 ? 0.7f : 0.12f + 0.6f * s.density;
                    if (st == 0) x *= 1.5f;
                    if (drums && s.drumMode == WithKick) x *= 0.35f + 1.8f * juce::jmax (kickAt (bar, st), 0.5f * hitAt (bar, st));
                    if (drums && s.drumMode == InGaps)   x *= juce::jmax (0.1f, 1.4f - 1.25f * hitAt (bar, st));
                    w[st] = x;
                }
                std::vector<int> steps;
                for (int k = 0; k < count; ++k)
                {
                    float tot = 0; for (float x : w) tot += x; if (tot <= 1.0e-6f) break;
                    float pick = r.unit() * tot; int st = 0; for (; st < 15; ++st) { pick -= w[st]; if (pick <= 0) break; }
                    steps.push_back (st); w[st] = 0.0f;
                }
                std::sort (steps.begin(), steps.end());
                return steps;
            };
            auto stepMove = [&] ()
            {
                if (r.chance (s.motion * 0.28f)) return (r.chance (v.downBias) ? -1 : 1) * (3 + r.below (2));       // a leap
                const float u = r.unit();
                if (u < 0.12f) return 0;
                const int size = u > 0.8f ? 2 : 1;
                return (r.chance (v.downBias) ? -1 : 1) * size;
            };
            // the motif: rhythm and shape of bar A, a quieter bar B
            const auto rhythmA = rhythm (0, perBar), rhythmB = rhythm (2 % out.bars, juce::jmax (1, perBar - 1 - (int) (v.restBias * 3.0f)));
            std::vector<int> movesA, movesB;
            for (size_t i = 0; i < rhythmA.size(); ++i) movesA.push_back (stepMove());
            for (size_t i = 0; i < rhythmB.size(); ++i) movesB.push_back (stepMove());

            int pos = 0 + (r.chance (0.5f) ? 4 : 2);              // scale position we are on (0 = the key's root at melodyBase)
            const int lo = -2, hi = 7;
            for (int b = 0; b < out.bars; ++b)
            {
                const int d = prog[(size_t) b], form = b % 4;
                auto steps = form == 2 ? rhythmB : rhythmA; auto moves = form == 2 ? movesB : movesA;
                if (form == 1 || form == 3)
                {
                    // A': the same bar, one thing changed
                    if (steps.size() > 2 && r.chance (0.35f + 0.4f * s.motion)) { const int k = 1 + r.below ((int) steps.size() - 1); steps.erase (steps.begin() + k); moves.erase (moves.begin() + k); }
                    if (! moves.empty() && r.chance (0.6f)) moves.back() = stepMove();
                }
                if (r.chance (v.restBias * 0.25f) && form != 0 && steps.size() > 1) { steps.erase (steps.begin()); moves.erase (moves.begin()); }       // let the bar start with air
                const bool phraseEnd = form == 3 || b == out.bars - 1;
                for (size_t i = 0; i < steps.size(); ++i)
                {
                    const int st = steps[i]; const bool last = i + 1 == steps.size();
                    int target = juce::jlimit (lo, hi, pos + moves[i]);
                    if (target == lo || target == hi) target = pos - moves[i];                       // turn around at the edges
                    const bool strong = i == 0 || st % 8 == 0 || (last && phraseEnd);
                    if (strong) target = detail::nearestChordStep (target, d);
                    target = juce::jlimit (lo - 1, hi + 1, target);
                    const double start = b * 4.0 + st * 0.25;
                    const double nextStart = last ? (b + 1) * 4.0 : b * 4.0 + steps[i + 1] * 0.25;
                    double len = (nextStart - start) * v.legato;
                    if (last && phraseEnd) len = nextStart - start - 0.05; else len = juce::jmin (len, 2.0);
                    len = juce::jmax (0.11, len);
                    // run-up: two quick notes stepping into the target, if there is room before it
                    const double prevEnd = i == 0 ? b * 4.0 : b * 4.0 + steps[i - 1] * 0.25 + 0.25;
                    if (r.chance (s.motion * 0.4f) && st % 4 == 0 && st > 0 && start - prevEnd >= 0.25 - 1.0e-9)       // only into a beat, so the quick notes never sit on one
                    {
                        const int dir = r.chance (0.5f) ? 1 : -1;
                        for (int g = 2; g >= 1; --g)
                            out.melody.push_back ({ start - g * 0.125, 0.115, v.melodyBase + key + detail::melodySemis (v, target + dir * g, d), 0.5f + 0.1f * (float) (2 - g) });
                    }
                    const float vel = juce::jlimit (0.35f, 1.0f, (st % 4 == 0 ? 0.86f : 0.72f) + 0.12f * (r.unit() - 0.5f));
                    out.melody.push_back ({ start, len, v.melodyBase + key + detail::melodySemis (v, target, d), vel });
                    pos = target;
                }
            }
        }

        // ---------------- bass ----------------
        {
            Rng r (s.seedBass);
            for (int b = 0; b < out.bars; ++b)
            {
                const int d = prog[(size_t) b];
                int root = 36 + key + detail::semis (v, d); while (root > 43) root -= 12; while (root < 31) root += 12;       // G1..G2: 808 range
                std::vector<int> steps;
                if (s.drums.valid) { for (int st = 0; st < 16; ++st) if (kickAt (b, st) > 0.5f) steps.push_back (st); }
                if (steps.empty()) { steps.push_back (0); if (r.chance (0.25f + 0.6f * s.density)) steps.push_back (r.chance (0.5f) ? 10 : 6); if (r.chance (s.density * s.motion)) steps.push_back (14); }
                std::sort (steps.begin(), steps.end());
                for (size_t i = 0; i < steps.size(); ++i)
                {
                    const double start = b * 4.0 + steps[i] * 0.25, next = i + 1 < steps.size() ? b * 4.0 + steps[i + 1] * 0.25 : (b + 1) * 4.0;
                    const bool up = i + 1 == steps.size() && steps.size() > 1 && r.chance (s.motion * 0.35f);
                    out.bass.push_back ({ start, juce::jmax (0.12, (next - start) * 0.96), root + (up ? 12 : 0), i == 0 ? 0.95f : 0.82f });
                }
            }
        }
        return out;
    }

    /** One layer (or all of them) as a standard MIDI file at the given tempo. */
    inline juce::MidiFile toMidi (const std::vector<const std::vector<Note>*>& layers, double bpm, double beats)
    {
        juce::MidiFile f; const int tpq = 960; f.setTicksPerQuarterNote (tpq);
        juce::MidiMessageSequence tempo;
        tempo.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / juce::jlimit (20.0, 400.0, bpm))), 0.0);
        tempo.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
        tempo.addEvent (juce::MidiMessage::endOfTrack(), beats * tpq);
        f.addTrack (tempo);
        int channel = 1;
        for (const auto* layer : layers)
        {
            juce::MidiMessageSequence seq;
            for (const auto& n : *layer)
            {
                const int p = juce::jlimit (0, 127, n.pitch);
                seq.addEvent (juce::MidiMessage::noteOn (channel, p, juce::jlimit (0.05f, 1.0f, n.vel)), n.start * tpq);
                seq.addEvent (juce::MidiMessage::noteOff (channel, p), juce::jmin (beats, n.start + n.length) * tpq);
            }
            seq.sort(); seq.updateMatchedPairs();
            seq.addEvent (juce::MidiMessage::endOfTrack(), beats * tpq);
            f.addTrack (seq); ++channel;
        }
        return f;
    }

    /** Where the kick and where any hit sits, from a drum loop that starts on the 1 and runs at `bpm`. */
    inline DrumGrid analyseDrums (const juce::AudioBuffer<float>& loop, double sr, double bpm)
    {
        DrumGrid g; const int n = loop.getNumSamples(); if (n < (int) (sr * 0.5) || bpm < 20.0) return g;
        const int hop = juce::jmax (16, (int) (sr * 0.005)), frames = n / hop;
        std::vector<float> eAll ((size_t) frames, 0.0f), eLow ((size_t) frames, 0.0f), eHigh ((size_t) frames, 0.0f);
        float lp1 = 0, lp2 = 0, hp = 0; const float aHigh = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 4000.0f / (float) sr); const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 110.0f / (float) sr);
        for (int f = 0; f < frames; ++f)
        {
            double sa = 0, sl = 0, sh = 0;
            for (int i = 0; i < hop; ++i)
            {
                float x = 0; for (int ch = 0; ch < loop.getNumChannels(); ++ch) x += loop.getSample (ch, f * hop + i); x /= (float) loop.getNumChannels();
                lp1 += (x - lp1) * a; lp2 += (lp1 - lp2) * a; sa += (double) x * x; sl += (double) lp2 * lp2; const float hi = x - hp; hp += (x - hp) * aHigh; sh += (double) hi * hi;
            }
            eAll[(size_t) f] = (float) std::log1p (4000.0 * sa / hop); eLow[(size_t) f] = (float) std::log1p (4000.0 * sl / hop); eHigh[(size_t) f] = (float) std::log1p (40000.0 * sh / hop);
        }
        auto rise = [&] (const std::vector<float>& e, int f) { float before = 0, after = 0; for (int k = 0; k < 4; ++k) { before += e[(size_t) (((f - 1 - k) % frames + frames) % frames)]; after += e[(size_t) (((f + k) % frames + frames) % frames)]; } return juce::jmax (0.0f, (after - before) / 4.0f); };       // it is a loop: before the start is the end
        const double stepSamples = sr * 60.0 / bpm / 4.0; const int steps = (int) std::floor (n / stepSamples + 0.25);
        if (steps < 8) return g;
        int counts[32] {};
        for (int st = 0; st < steps; ++st)
        {
            const int centre = (int) (st * stepSamples / hop), win = juce::jmax (1, (int) (stepSamples / hop / 3.0));
            float k = 0, h = 0; for (int f = centre - win; f <= centre + win; ++f) if (f >= -win && f < frames) { k = juce::jmax (k, rise (eLow, f)); h = juce::jmax (h, juce::jmax (rise (eAll, f), rise (eHigh, f))); }
            g.kick[st % 32] += k; g.hit[st % 32] += h; ++counts[st % 32];
        }
        float mk = 1.0e-6f, mh = 1.0e-6f;
        for (int i = 0; i < 32; ++i) { if (counts[i] > 0) { g.kick[i] /= (float) counts[i]; g.hit[i] /= (float) counts[i]; } mk = juce::jmax (mk, g.kick[i]); mh = juce::jmax (mh, g.hit[i]); }
        if (steps < 32) for (int i = 16; i < 32; ++i) if (counts[i] == 0) { g.kick[i] = g.kick[i - 16]; g.hit[i] = g.hit[i - 16]; }      // a one-bar loop repeats
        for (int i = 0; i < 32; ++i) { g.kick[i] = g.kick[i] / mk; g.hit[i] = g.hit[i] / mh; if (g.kick[i] < 0.3f) g.kick[i] = 0.0f; if (g.hit[i] < 0.2f) g.hit[i] = 0.0f; }
        g.valid = mh > 0.05f;
        return g;
    }
}
