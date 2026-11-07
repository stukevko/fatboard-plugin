#pragma once
#include <cmath>
#include <array>

// ============================================================================
//  Harun – key / scale / interval logic
//
//  Turns "the singer is on this pitch" into "the harmony voice should be
//  this many semitones away", diatonically: a third above C in C major is
//  E (+4), a third above D is F (+3). Everything is done in scale degrees so
//  it stays inside the key.
// ============================================================================

namespace Harmony
{
    enum class Scale { Major = 0, Minor = 1 };
    enum class Interval { ThirdBelow = 0, ThirdAbove = 1, FifthBelow = 2, FifthAbove = 3, OctaveBelow = 4, OctaveAbove = 5 };

    static constexpr std::array<int, 7> majorSteps { 0, 2, 4, 5, 7, 9, 11 };
    static constexpr std::array<int, 7> minorSteps { 0, 2, 3, 5, 7, 8, 10 };   // natural minor

    inline const std::array<int, 7>& steps (Scale s) { return s == Scale::Major ? majorSteps : minorSteps; }

    inline float hzToMidi (float hz) { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }
    inline float midiToHz (float m)  { return 440.0f * std::pow (2.0f, (m - 69.0f) / 12.0f); }

    /** Nearest scale degree (0..6) and its octave offset for a MIDI note in the given key. */
    struct Degree { int index; int octave; int semitoneInKey; };

    inline Degree nearestDegree (float midi, int key, Scale scale)
    {
        const auto& st = steps (scale);
        const float rel = midi - (float) key;                        // relative to the tonic
        const int   oct = (int) std::floor (rel / 12.0f);
        const float inOct = rel - 12.0f * (float) oct;               // 0..12
        int bestIdx = 0; float bestDist = 1.0e9f; int bestOct = oct;
        for (int i = 0; i < 7; ++i)
        {
            const float d = std::abs (inOct - (float) st[(size_t) i]);
            if (d < bestDist) { bestDist = d; bestIdx = i; bestOct = oct; }
        }
        // the tonic of the next octave might be closer than the 7th
        const float dUp = std::abs (inOct - 12.0f);
        if (dUp < bestDist) { bestIdx = 0; bestOct = oct + 1; }
        return { bestIdx, bestOct, st[(size_t) bestIdx] };
    }

    inline int degreeOffset (Interval iv)
    {
        switch (iv)
        {
            case Interval::ThirdBelow:  return -2;
            case Interval::ThirdAbove:  return  2;
            case Interval::FifthBelow:  return -4;
            case Interval::FifthAbove:  return  4;
            case Interval::OctaveBelow: return -7;
            case Interval::OctaveAbove: return  7;
        }
        return 2;
    }

    /** Semitone shift the harmony voice needs, given the sung pitch (MIDI, float). */
    inline float shiftFor (float midi, int key, Scale scale, Interval iv)
    {
        const auto& st = steps (scale);
        const Degree d = nearestDegree (midi, key, scale);
        int idx = d.index + degreeOffset (iv);
        int oct = d.octave;
        while (idx < 0)  { idx += 7; --oct; }
        while (idx >= 7) { idx -= 7; ++oct; }
        const float sungQuantised = (float) key + 12.0f * (float) d.octave + (float) d.semitoneInKey;
        const float target        = (float) key + 12.0f * (float) oct + (float) st[(size_t) idx];
        // shift relative to the *quantised* sung note, so the singer's own
        // vibrato / slides are carried over to the harmony (sounds human, not auto-tuned)
        return target - sungQuantised;
    }

    static constexpr const char* noteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    inline const char* noteName (float midi)
    {
        const int n = (int) std::lround (midi);
        return noteNames[((n % 12) + 12) % 12];
    }
}
