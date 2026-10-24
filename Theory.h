#pragma once
#include <juce_core/juce_core.h>
#include <cstdint>
#include <vector>

namespace pt::theory
{
inline juce::String pcName (int pc)
{
    static const char* n[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return n[((pc % 12) + 12) % 12];
}

inline juce::String noteName (int midi)
{
    return pcName (midi) + juce::String (midi / 12 - 1);
}

inline bool isBlack (int midi) noexcept
{
    const int pc = midi % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

//------------------------------------------------------------------------------
// Scale overlay types (order matches the "scaleType" choice parameter).
//------------------------------------------------------------------------------
inline const char* const* scaleTypeNames()
{
    static const char* n[] = { "Major", "Natural minor", "Harmonic minor", "Melodic minor",
                               "Dorian", "Mixolydian", "Major pentatonic", "Minor pentatonic",
                               "Blues", nullptr };
    return n;
}

inline const std::vector<int>& scaleIntervals (int type)
{
    static const std::vector<int> s[] = {
        { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 },
        { 0, 2, 3, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 2, 4, 5, 7, 9, 10 },
        { 0, 2, 4, 7, 9 },        { 0, 3, 5, 7, 10 },       { 0, 3, 5, 6, 7, 10 } };
    return s[juce::jlimit (0, 8, type)];
}

//------------------------------------------------------------------------------
// Chord detection from a 128-bit held-note mask (lo = notes 0..63, hi = 64..127).
//------------------------------------------------------------------------------
inline juce::String detectChord (std::uint64_t lo, std::uint64_t hi)
{
    int bass = -1, count = 0;
    unsigned mask = 0;
    for (int n = 0; n < 128; ++n)
    {
        const bool on = n < 64 ? ((lo >> n) & 1u) : ((hi >> (n - 64)) & 1u);
        if (! on) continue;
        if (bass < 0) bass = n;
        mask |= 1u << (n % 12);
        ++count;
    }
    if (count == 0) return {};
    if (count == 1) return noteName (bass);

    int pcs = 0;
    for (int i = 0; i < 12; ++i) pcs += (mask >> i) & 1u;

    if (pcs == 1) return noteName (bass) + " (unison)";
    if (pcs == 2)
    {
        int hiPc = -1;
        for (int i = 0; i < 12; ++i)
            if (((mask >> i) & 1u) && i != bass % 12) hiPc = i;
        static const char* iv[12] = { "", "minor 2nd", "major 2nd", "minor 3rd", "major 3rd", "perfect 4th",
                                      "tritone", "perfect 5th", "minor 6th", "major 6th", "minor 7th", "major 7th" };
        const int d = ((hiPc - bass % 12) + 12) % 12;
        if (d == 7) return pcName (bass) + "5";
        return pcName (bass) + " + " + juce::String (iv[d]);
    }

    struct Tpl { std::vector<int> iv; const char* name; };
    static const std::vector<Tpl> templates = {
        { { 0, 4, 7 }, "" },          { { 0, 3, 7 }, "m" },        { { 0, 3, 6 }, "dim" },
        { { 0, 4, 8 }, "aug" },       { { 0, 2, 7 }, "sus2" },     { { 0, 5, 7 }, "sus4" },
        { { 0, 4, 7, 10 }, "7" },     { { 0, 4, 7, 11 }, "maj7" }, { { 0, 3, 7, 10 }, "m7" },
        { { 0, 3, 6, 10 }, "m7b5" },  { { 0, 3, 6, 9 }, "dim7" },  { { 0, 3, 7, 11 }, "m(maj7)" },
        { { 0, 4, 7, 9 }, "6" },      { { 0, 3, 7, 9 }, "m6" },    { { 0, 2, 4, 7 }, "add9" },
        { { 0, 2, 3, 7 }, "m(add9)" },{ { 0, 5, 7, 10 }, "7sus4" },{ { 0, 2, 4, 7, 10 }, "9" },
        { { 0, 2, 4, 7, 11 }, "maj9" },{ { 0, 2, 3, 7, 10 }, "m9" } };

    // Try the bass note as root first, then every other pitch class.
    int order[12], no = 0;
    order[no++] = bass % 12;
    for (int i = 0; i < 12; ++i)
        if (((mask >> i) & 1u) && i != bass % 12) order[no++] = i;

    for (int k = 0; k < no; ++k)
    {
        const int root = order[k];
        unsigned rel = 0;
        for (int i = 0; i < 12; ++i)
            if ((mask >> i) & 1u) rel |= 1u << (((i - root) + 12) % 12);

        for (const auto& t : templates)
        {
            unsigned tm = 0;
            for (int v : t.iv) tm |= 1u << v;
            if (tm == rel)
            {
                juce::String s = pcName (root) + t.name;
                if (root != bass % 12) s << "/" << pcName (bass);
                return s;
            }
        }
    }
    return juce::String (pcs) + " notes";
}
} // namespace pt::theory
