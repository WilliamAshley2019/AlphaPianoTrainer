#include "LessonLibrary.h"
#include "Theory.h"
#include <functional>

namespace pt::library
{
namespace
{
//==============================================================================
// Tiny score notation:   token = pitches[:beats][@velocity][^finger]
//   pitches : "C4", "F#3", "Bb2", chords with '+', rest = "r"
//   beats   : decimal or fraction ("1.5", "1/3"); default 1
//   Octave numbering: C4 = middle C = MIDI 60.
//==============================================================================
int pitchFromName (const juce::String& s)
{
    int pc = 0;
    switch (juce::CharacterFunctions::toUpperCase (s[0]))
    {
        case 'C': pc = 0;  break;  case 'D': pc = 2;  break;  case 'E': pc = 4;  break;
        case 'F': pc = 5;  break;  case 'G': pc = 7;  break;  case 'A': pc = 9;  break;
        case 'B': pc = 11; break;  default: break;
    }
    int i = 1;
    if (s[i] == '#') { ++pc; ++i; }
    else if (s[i] == 'b') { --pc; ++i; }
    return (s.substring (i).getIntValue() + 1) * 12 + pc;
}

struct Builder
{
    Lesson& L;

    // Appends the sequence starting at t0, returns the beat where it ends.
    double seq (int hand, const juce::String& text, double t0, int defVel = 88)
    {
        double t = t0;
        for (auto tok : juce::StringArray::fromTokens (text, " \t\r\n", ""))
        {
            int finger = 0, vel = defVel;
            if (const int c = tok.indexOfChar ('^'); c >= 0)
            { finger = tok.substring (c + 1).getIntValue(); tok = tok.substring (0, c); }
            if (const int a = tok.indexOfChar ('@'); a >= 0)
            { vel = tok.substring (a + 1).getIntValue(); tok = tok.substring (0, a); }

            double dur = 1.0;
            if (const int c = tok.indexOfChar (':'); c >= 0)
            {
                const auto d = tok.substring (c + 1);
                if (const int sl = d.indexOfChar ('/'); sl > 0)
                    dur = d.substring (0, sl).getDoubleValue() / d.substring (sl + 1).getDoubleValue();
                else
                    dur = d.getDoubleValue();
                tok = tok.substring (0, c);
            }

            if (tok != "r")
                for (auto p : juce::StringArray::fromTokens (tok, "+", ""))
                    add (pitchFromName (p), t, dur * 0.97, vel, hand, finger);

            t += dur;
        }
        return t;
    }

    void add (int pitch, double start, double len, int vel, int hand, int finger = 0)
    {
        Note n;
        n.pitch = pitch; n.start = start; n.length = len;
        n.velocity = juce::jlimit (1, 127, vel); n.hand = hand; n.finger = finger;
        L.notes.push_back (n);
    }
};

constexpr int R = handRight, Lh = handLeft;

//==============================================================================
// Chords & progressions
//==============================================================================
enum Quality { qMaj, qMin, qDom7, qMaj7, qMin7 };

std::vector<int> qualityIntervals (Quality q)
{
    switch (q)
    {
        case qMaj:  return { 0, 4, 7 };
        case qMin:  return { 0, 3, 7 };
        case qDom7: return { 0, 4, 7, 10 };
        case qMaj7: return { 0, 4, 7, 11 };
        case qMin7: return { 0, 3, 7, 10 };
    }
    return { 0, 4, 7 };
}

struct Chord { int root; Quality q; };   // root = pitch class, C = 0

// Picks the inversion nearest the previous voicing -> smooth voice leading.
std::vector<int> voice (const Chord& c, const std::vector<int>& prev)
{
    const auto iv = qualityIntervals (c.q);
    std::vector<int> best;
    int bestCost = 1 << 30;
    const int n = (int) iv.size();
    for (int inv = 0; inv < n; ++inv)
    {
        std::vector<int> v;
        for (int k = 0; k < n; ++k)
        {
            int p = 60 + c.root + iv[(size_t) ((k + inv) % n)];
            v.push_back (p);
        }
        // normalise to ascending order, lowest note within 55..67
        for (size_t k = 1; k < v.size(); ++k) while (v[k] < v[k - 1]) v[k] += 12;
        while (v.front() > 67) for (auto& p : v) p -= 12;
        while (v.front() < 55) for (auto& p : v) p += 12;

        int cost = 0;
        if (prev.empty()) cost = std::abs (v.front() - 60);
        else for (size_t k = 0; k < v.size(); ++k)
                 cost += std::abs (v[k] - prev[juce::jmin (k, prev.size() - 1)]);
        if (cost < bestCost) { bestCost = cost; best = v; }
    }
    return best;
}

enum Comp { compBlock, compHalves, compArp };

void progression (Lesson& L, const std::vector<Chord>& chords, int repeats, Comp style, int bpm)
{
    Builder b { L };
    L.bpm = bpm;
    L.keyRoot = 0;
    double t = 0.0;
    std::vector<int> prev;

    for (int rep = 0; rep < repeats; ++rep)
        for (const auto& c : chords)
        {
            const auto v = voice (c, prev);
            prev = v;
            const int bass = 36 + c.root + (c.root > 6 ? -12 : 0);   // C2..B2 region

            b.add (bass + 12, t, 3.9, 80, Lh);
            b.add (bass + 19, t, 3.9, 70, Lh);

            if (style == compBlock)
                for (int p : v) b.add (p, t, 3.9, 84, R);
            else if (style == compHalves)
                for (double h : { 0.0, 2.0 }) for (int p : v) b.add (p, t + h, 1.9, 84, R);
            else
                for (int i = 0; i < 8; ++i)
                {
                    const int p = v[(size_t) (i % (int) v.size())] + (i >= (int) v.size() ? 12 : 0);
                    b.add (p, t + 0.5 * i, 0.48, 78 + (i % 4 == 0 ? 10 : 0), R);
                }
            t += 4.0;
        }
}

//==============================================================================
// Scales
//==============================================================================
void scaleLesson (Lesson& L, int type)
{
    Builder b { L };
    L.keyRoot = 0;
    L.bpm = 84;

    auto up   = theory::scaleIntervals (type);
    auto down = type == 3 ? theory::scaleIntervals (1) : up;   // melodic minor: natural form descending

    std::vector<int> asc, desc;
    for (int o = 0; o < 2; ++o) for (int iv : up)   asc.push_back (60 + o * 12 + iv);
    asc.push_back (84);
    for (int o = 0; o < 2; ++o) for (int iv : down) desc.push_back (60 + o * 12 + iv);
    desc.push_back (84);
    std::reverse (desc.begin(), desc.end());

    static const int cMajFingers[] = { 1, 2, 3, 1, 2, 3, 4, 1, 2, 3, 1, 2, 3, 4, 5 };

    double t = 0.0;
    auto play = [&] (const std::vector<int>& pitches, bool ascending, double last)
    {
        for (size_t i = 0; i < pitches.size(); ++i)
        {
            const double len = i + 1 == pitches.size() ? last : 0.5;
            int finger = 0;
            if (type == 0 && pitches.size() == 15 + 1)
                finger = ascending ? cMajFingers[juce::jmin<size_t> (i, 14)]
                                   : cMajFingers[juce::jmin<size_t> (pitches.size() - 1 - i, 14)];
            b.add (pitches[i],      t, len * 0.96, 86, R, finger);
            b.add (pitches[i] - 12, t, len * 0.96, 80, Lh);
            t += len;
        }
    };
    play (asc, true, 0.5);
    desc.erase (desc.begin());                      // top note already played
    play (desc, false, 2.0);
}

//==============================================================================
// Registry
//==============================================================================
struct Entry { Info info; std::function<void (Lesson&)> make; double bpm = 100; };

std::vector<Entry>& registry()
{
    static std::vector<Entry> r;
    if (! r.empty()) return r;

    auto add = [&] (const char* id, int level, const char* cat, const char* title, const char* blurb,
                    double bpm, std::function<void (Lesson&)> fn, bool dyn = false)
    {
        Entry e;
        e.info = { juce::String ("b:") + id, u8 (title), u8 (cat), u8 (blurb), level, dyn };
        e.make = std::move (fn);
        e.bpm  = bpm;
        r.push_back (std::move (e));
    };

    // ---- Free play --------------------------------------------------------
    add ("free", 1, "Free play", "Free play  \xc2\xb7  chord lens",
         "No notes to follow. Play anything: the display names what you hold, and the scale overlay shows where you are.",
         100, [] (Lesson&) {});

    // ---- 1. First steps -------------------------------------------------------
    const char* c1 = "1  \xc2\xb7  First steps";
    add ("fs.rh", 1, c1, "Five fingers  \xc2\xb7  right hand",
         "Thumb on middle C. One finger per key, up and back down. Keep the hand relaxed and the wrist level.", 70,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0;
             double t = 0;
             for (int i = 0; i < 2; ++i)
                 t = b.seq (R, "C4:1^1 D4:1^2 E4:1^3 F4:1^4 G4:1^5 F4:1^4 E4:1^3 D4:1^2 C4:2^1 r:2", t); });

    add ("fs.lh", 1, c1, "Five fingers  \xc2\xb7  left hand",
         "Pinky on the C below middle C. The left hand mirrors the right: 5 on C, thumb on G.", 70,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0;
             double t = 0;
             for (int i = 0; i < 2; ++i)
                 t = b.seq (Lh, "C3:1^5 D3:1^4 E3:1^3 F3:1^2 G3:1^1 F3:1^2 E3:1^3 D3:1^4 C3:2^5 r:2", t); });

    add ("fs.both", 1, c1, "Five fingers  \xc2\xb7  hands together",
         "Both hands in step, an octave apart. Listen for the two notes landing as one.", 66,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0;
             double t = 0;
             for (int i = 0; i < 2; ++i)
             {
                 b.seq (Lh, "C3:1^5 D3:1^4 E3:1^3 F3:1^2 G3:1^1 F3:1^2 E3:1^3 D3:1^4 C3:2^5 r:2", t);
                 t = b.seq (R, "C4:1^1 D4:1^2 E4:1^3 F4:1^4 G4:1^5 F4:1^4 E4:1^3 D4:1^2 C4:2^1 r:2", t);
             } });

    add ("fs.contrary", 2, c1, "Contrary motion",
         "Hands move away from each other from middle C, then come back. Great for hand independence.", 60,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0;
             double t = 0;
             for (int i = 0; i < 2; ++i)
             {
                 b.seq (Lh, "C4:1^1 B3:1^2 A3:1^3 G3:1^4 F3:1^5 G3:1^4 A3:1^3 B3:1^2 C4:2^1 r:2", t);
                 t = b.seq (R, "C4:1^1 D4:1^2 E4:1^3 F4:1^4 G4:1^5 F4:1^4 E4:1^3 D4:1^2 C4:2^1 r:2", t);
             } });

    add ("fs.twoone", 2, c1, "Two against one",
         "Right hand plays quarter notes while the left holds half notes. A first taste of hand independence.", 64,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0;
             double t = 0;
             for (int i = 0; i < 2; ++i)
             {
                 b.seq (Lh, "C3:2 G3:2 E3:2 G3:2 F3:2 A3:2 G3:2 C3:2", t);
                 t = b.seq (R, "C4 D4 E4 F4 G4 F4 E4 D4 E4 F4 G4 A4 G4 E4 D4 C4", t);
             } });

    // ---- 2. Rhythm ------------------------------------------------------------
    const char* c2 = "2  \xc2\xb7  Rhythm";
    add ("rh.quarters", 1, c2, "Steady pulse",
         "Eight bars of quarter notes. Aim for the centre of every beat. The timing meter shows early or late.", 80,
         [] (Lesson& L) { Builder b { L }; double t = 0;
             for (int i = 0; i < 8; ++i) t = b.seq (R, "G4 G4 G4 G4", t); });

    add ("rh.eighths", 1, c2, "Quarters and eighths",
         "Mixing one-beat and half-beat notes. Count 1-and-2-and.", 76,
         [] (Lesson& L) { Builder b { L }; double t = 0;
             for (int i = 0; i < 2; ++i)
             {
                 t = b.seq (R, "G4 G4 G4 G4", t);
                 t = b.seq (R, "G4:.5 G4:.5 G4:.5 G4:.5 G4:.5 G4:.5 G4:.5 G4:.5", t);
                 t = b.seq (R, "G4 G4:.5 G4:.5 G4 G4", t);
                 t = b.seq (R, "G4:.5 G4:.5 G4 G4:.5 G4:.5 G4", t);
             } });

    add ("rh.dotted", 2, c2, "Dotted rhythms",
         "Long-short pairs: a dotted quarter then an eighth. This is the lilt of countless melodies.", 80,
         [] (Lesson& L) { Builder b { L }; double t = 0;
             for (int i = 0; i < 4; ++i) t = b.seq (R, "A4:1.5 A4:.5 A4:1.5 A4:.5", t);
             for (int i = 0; i < 4; ++i) t = b.seq (R, "A4:.5 A4:1.5 A4:.5 A4:1.5", t); });

    add ("rh.sync", 3, c2, "Syncopation",
         "Notes that land between the beats. Feel the pulse in your foot and play against it.", 84,
         [] (Lesson& L) { Builder b { L }; double t = 0;
             for (int i = 0; i < 4; ++i) t = b.seq (R, "r:.5 E4:1 E4:1 E4:1 E4:.5", t);
             for (int i = 0; i < 4; ++i) t = b.seq (R, "E4:1.5 E4:1.5 E4:1", t); });

    add ("rh.triplets", 3, c2, "Triplets",
         "Three notes to the beat. Roll them off evenly: tri-pl-et, tri-pl-et.", 72,
         [] (Lesson& L) { Builder b { L }; double t = 0;
             for (int i = 0; i < 4; ++i) t = b.seq (R, "D4:1/3 F4:1/3 A4:1/3 D4:1/3 F4:1/3 A4:1/3 D4:1/3 F4:1/3 A4:1/3 D5:1", t); });

    // ---- 3. Scales ----------------------------------------------------------------
    const char* c3 = "3  \xc2\xb7  Scales";
    static const char* scaleBlurb[] = {
        "The foundation. Two octaves, hands an octave apart. Fingering is shown for the right hand.",
        "The sad sister of the major scale: a lowered 3rd, 6th and 7th.",
        "A raised 7th gives the exotic, tense leading-tone sound.",
        "Rises with a bright 6th and 7th, falls as natural minor.",
        "A minor scale with a bright 6th. The sound of funk, soul and many jazz lines.",
        "A major scale with a flat 7th. Rock, blues and folk live here.",
        "Five notes, no half-steps: impossible to play a wrong note. Great for improvising.",
        "The backbone of rock and blues solos.",
        "The minor pentatonic plus the 'blue' note." };
    static const char* scaleId[] = { "sc.major", "sc.minor", "sc.harm", "sc.mel", "sc.dorian", "sc.mixo", "sc.pent", "sc.mpent", "sc.blues" };
    static const char* scaleTitle[] = { "Major scale", "Natural minor scale", "Harmonic minor scale", "Melodic minor scale",
                                        "Dorian mode", "Mixolydian mode", "Major pentatonic", "Minor pentatonic", "Blues scale" };
    static const int scaleLevel[] = { 2, 2, 3, 3, 3, 3, 2, 2, 3 };
    for (int i = 0; i < 9; ++i)
        add (scaleId[i], scaleLevel[i], c3, scaleTitle[i], scaleBlurb[i], 84, [i] (Lesson& L) { scaleLesson (L, i); });

    // ---- 4. Chords -----------------------------------------------------------------
    const char* c4 = "4  \xc2\xb7  Chords";
    add ("ch.triads", 2, c4, "Major and minor triads",
         "Spell each chord, then play it as a block. Hear how one lowered note turns bright into sad.", 66,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0; double t = 0;
             const struct { const char* rh; const char* lh; } c[] = {
                 { "C4 E4 G4 C4+E4+G4:2", "C3:4" },      { "C4 Eb4 G4 C4+Eb4+G4:2", "C3:4" },
                 { "F4 A4 C5 F4+A4+C5:2", "F3:4" },      { "F4 Ab4 C5 F4+Ab4+C5:2", "F3:4" },
                 { "G4 B4 D5 G4+B4+D5:2", "G3:4" },      { "G4 Bb4 D5 G4+Bb4+D5:2", "G3:4" } };
             for (auto& x : c) { b.seq (Lh, x.lh, t); t = b.seq (R, x.rh, t); } });

    add ("ch.inv", 2, c4, "Inversions  \xc2\xb7  C major",
         "The same chord, three shapes. Smooth players move the smallest distance to the next chord.", 66,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0; double t = 0;
             for (int rep = 0; rep < 2; ++rep)
                 t = b.seq (R, "C4+E4+G4:2 E4+G4+C5:2 G4+C5+E5:2 E4+G4+C5:2 C4+E4+G4:4", t);
             b.seq (Lh, "C3:8 C3:8", 0); });

    add ("ch.sevenths", 3, c4, "Seventh chords",
         "Add one more colour: major 7th (dreamy), dominant 7th (wants to move), minor 7th (mellow), diminished 7th (tense).", 60,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0; double t = 0;
             t = b.seq (R, "C4+E4+G4+B4:3 r:1 C4+E4+G4+Bb4:3 r:1 C4+Eb4+G4+Bb4:3 r:1 C4+Eb4+Gb4+A4:3 r:1", t);
             b.seq (Lh, "C3:4 C3:4 C3:4 C3:4", 0); });

    add ("ch.arp", 3, c4, "Arpeggios  \xc2\xb7  major, minor, dominant",
         "Broken chords across two octaves. Keep the thumb passing under smoothly.", 76,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0; double t = 0;
             const char* shapes[] = {
                 "C4:.5 E4:.5 G4:.5 C5:.5 E5:.5 G5:.5 C6:.5 G5:.5 E5:.5 C5:.5 G4:.5 E4:.5 C4:2",
                 "C4:.5 Eb4:.5 G4:.5 C5:.5 Eb5:.5 G5:.5 C6:.5 G5:.5 Eb5:.5 C5:.5 G4:.5 Eb4:.5 C4:2",
                 "C4:.5 E4:.5 G4:.5 Bb4:.5 C5:.5 E5:.5 G5:.5 Bb5:.5 G5:.5 E5:.5 C5:.5 Bb4:.5 G4:.5 E4:.5 C4:2" };
             for (auto s : shapes) t = b.seq (R, s, t); });

    // ---- 5. Progressions ----------------------------------------------------------
    const char* c5 = "5  \xc2\xb7  Progressions";
    add ("pr.pop", 2, c5, "Pop  \xc2\xb7  I  V  vi  IV",
         "C  G  Am  F: the progression behind hundreds of hit songs. Left hand root and fifth, right hand smooth chords.", 80,
         [] (Lesson& L) { progression (L, { { 0, qMaj }, { 7, qMaj }, { 9, qMin }, { 5, qMaj } }, 2, compBlock, 80); });

    add ("pr.fifties", 2, c5, "Fifties  \xc2\xb7  I  vi  IV  V",
         "C  Am  F  G: doo-wop and early rock'n'roll. Play it with a half-note pulse.", 84,
         [] (Lesson& L) { progression (L, { { 0, qMaj }, { 9, qMin }, { 5, qMaj }, { 7, qMaj } }, 2, compHalves, 84); });

    add ("pr.canon", 3, c5, "Canon progression",
         "C  G  Am  Em  F  C  F  G: eight chords, the Pachelbel pattern. Notice the stepwise descent in the bass.", 72,
         [] (Lesson& L) { progression (L, { { 0, qMaj }, { 7, qMaj }, { 9, qMin }, { 4, qMin },
                                            { 5, qMaj }, { 0, qMaj }, { 5, qMaj }, { 7, qMaj } }, 1, compBlock, 72); });

    add ("pr.feel", 3, c5, "Emotional  \xc2\xb7  vi  IV  I  V (arpeggiated)",
         "Am  F  C  G as flowing broken chords. The shape of countless ballads and film cues. Let it breathe.", 76,
         [] (Lesson& L) { progression (L, { { 9, qMin }, { 5, qMaj }, { 0, qMaj }, { 7, qMaj } }, 2, compArp, 76); });

    add ("pr.cadence", 2, c5, "Cadence  \xc2\xb7  I  IV  V  I",
         "The sentence structure of tonal music: home, away, tension, home.", 72,
         [] (Lesson& L) { progression (L, { { 0, qMaj }, { 5, qMaj }, { 7, qMaj }, { 0, qMaj } }, 2, compBlock, 72); });

    add ("pr.jazz", 4, c5, "Jazz  \xc2\xb7  ii  V  I",
         "Dm7  G7  Cmaj7: the most important cadence in jazz. Four-note voicings with minimal movement.", 76,
         [] (Lesson& L) { progression (L, { { 2, qMin7 }, { 7, qDom7 }, { 0, qMaj7 }, { 0, qMaj7 } }, 2, compBlock, 76); });

    add ("pr.blues", 4, c5, "Twelve-bar blues",
         "Three chords, twelve bars, endless music. Comp on beats 1 and 3 and feel the form.", 92,
         [] (Lesson& L) { progression (L, { { 0, qDom7 }, { 0, qDom7 }, { 0, qDom7 }, { 0, qDom7 },
                                            { 5, qDom7 }, { 5, qDom7 }, { 0, qDom7 }, { 0, qDom7 },
                                            { 7, qDom7 }, { 5, qDom7 }, { 0, qDom7 }, { 7, qDom7 } }, 1, compHalves, 92); });

    // ---- 6. Technique --------------------------------------------------------------
    const char* c6 = "6  \xc2\xb7  Technique";
    add ("te.hanon", 3, c6, "Finger patterns  \xc2\xb7  Hanon style",
         "A climbing five-finger pattern. Built for evenness and strength. Keep every note the same volume.", 76,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0; double t = 0;
             static const int major[] = { 0, 2, 4, 5, 7, 9, 11 };
             auto deg = [&] (int d) { return major[d % 7] + 12 * (d / 7); };
             static const int rhF[] = { 1, 2, 3, 4, 5, 4, 3, 2 }, lhF[] = { 5, 4, 3, 2, 1, 2, 3, 4 };
             static const int pat[] = { 0, 2, 3, 4, 5, 4, 3, 2 };
             for (int bar = 0; bar < 8; ++bar)
                 for (int i = 0; i < 8; ++i)
                 {
                     const int d = bar + pat[i];
                     b.add (60 + deg (d),      t + 0.5 * i, 0.46, 84, R,  rhF[i]);
                     b.add (48 + deg (d),      t + 0.5 * i, 0.46, 78, Lh, lhF[i]);
                 }
             });

    add ("te.alberti", 3, c6, "Alberti bass",
         "The classic left-hand figure low-high-middle-high under a slow melody. Keep it even and quiet.", 80,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0; double t = 0;
             const int chords[4][4] = { { 48, 55, 52, 55 }, { 53, 60, 57, 60 }, { 55, 62, 59, 62 }, { 48, 55, 52, 55 } };
             const char* mel[] = { "E4:4", "F4:4", "D4:4", "C4:4" };
             for (int c = 0; c < 4; ++c)
             {
                 for (int rep = 0; rep < 2; ++rep)
                     for (int i = 0; i < 4; ++i)
                         b.add (chords[c][i], t + 0.5 * (rep * 4 + i), 0.46, 70, Lh);
                 b.seq (R, mel[c], t, 92);
                 t += 4;
             } });

    add ("te.thirds", 3, c6, "Scale in thirds",
         "Two notes at once, a third apart, up and down C major. Think of moving the hand as one unit.", 72,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0;
             b.seq (R, "C4+E4:.5 D4+F4:.5 E4+G4:.5 F4+A4:.5 G4+B4:.5 A4+C5:.5 B4+D5:.5 C5+E5:1 "
                       "B4+D5:.5 A4+C5:.5 G4+B4:.5 F4+A4:.5 E4+G4:.5 D4+F4:.5 C4+E4:2", 0); });

    add ("te.octaves", 3, c6, "Octave scale",
         "Hand stretched to an octave. Stay loose in the wrist and let the arm weight do the work.", 70,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0;
             b.seq (R, "C4+C5:1 D4+D5:1 E4+E5:1 F4+F5:1 G4+G5:1 A4+A5:1 B4+B5:1 C5+C6:2 "
                       "B4+B5:1 A4+A5:1 G4+G5:1 F4+F5:1 E4+E5:1 D4+D5:1 C4+C5:2", 0); });

    add ("te.trill", 3, c6, "Trill warm-up",
         "Fast alternation with fingers 3 and 2. Light and quick, not heavy.", 56,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0; double t = 0;
             const char* pairs[][2] = { { "C4", "D4" }, { "D4", "E4" }, { "E4", "F4" }, { "F4", "G4" }, { "G4", "A4" } };
             for (auto& p : pairs)
             {
                 for (int i = 0; i < 8; ++i) t = b.seq (R, juce::String (p[i % 2]) + ":.25", t);
                 t = b.seq (R, juce::String (p[0]) + ":1 r:1", t);
             } });

    add ("te.blackkeys", 2, c6, "Black-key pentatonic",
         "F# G# A# C# D#: all black keys. Lovely shapes under the hand and a good change of feel.", 80,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 6; double t = 0;
             for (int rep = 0; rep < 2; ++rep)
             {
                 const char* up = "F#4:.5 G#4:.5 A#4:.5 C#5:.5 D#5:.5 F#5:1 D#5:.5 C#5:.5 A#4:.5 G#4:.5 F#4:2";
                 b.seq (Lh, "F#3:.5 G#3:.5 A#3:.5 C#4:.5 D#4:.5 F#4:1 D#4:.5 C#4:.5 A#3:.5 G#3:.5 F#3:2", t);
                 t = b.seq (R, up, t);
             } });

    // ---- 7. Dynamics & feeling --------------------------------------------------
    const char* c7 = "7  \xc2\xb7  Dynamics and feeling";
    add ("dy.cresc", 2, c7, "Crescendo and diminuendo",
         "A C major scale that grows from whisper to roar and back. Match the brightness of each note bar: it IS the volume.", 72,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0; L.scoreDynamics = true; double t = 0;
             const int p[] = { 60, 62, 64, 65, 67, 69, 71, 72 };
             for (int i = 0; i < 8; ++i)  b.add (p[i], t++, 0.9, 40 + i * 10, R);
             for (int i = 7; i >= 0; --i) b.add (p[i], t++, 0.9, 40 + i * 10, R);
             b.add (60, t, 3.5, 40, R); }, true);

    add ("dy.echo", 2, c7, "Echoes  \xc2\xb7  loud and soft",
         "The same phrase played strongly, then as a faint echo. Control of touch is control of mood.", 76,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0; L.scoreDynamics = true; double t = 0;
             for (int i = 0; i < 4; ++i)
             {
                 t = b.seq (R, "C4 E4 G4 E4 C5:2 r:2", t, 106);
                 t = b.seq (R, "C4 E4 G4 E4 C5:2 r:2", t, 48);
             } }, true);

    add ("dy.accent", 2, c7, "Accents",
         "A strong first beat and soft others, then a backbeat on 2 and 4. Rhythm you can feel.", 88,
         [] (Lesson& L) { Builder b { L }; L.scoreDynamics = true; double t = 0;
             for (int i = 0; i < 4; ++i) { for (int k = 0; k < 4; ++k) b.add (67, t++, 0.9, k == 0 ? 110 : 62, R); }
             for (int i = 0; i < 4; ++i) { for (int k = 0; k < 4; ++k) b.add (67, t++, 0.9, (k == 1 || k == 3) ? 108 : 58, R); } }, true);

    add ("dy.voicing", 3, c7, "Voicing  \xc2\xb7  sing the top note",
         "Chords where the top note is the melody. Play it clearly and keep the inner notes soft. Pianists call this voicing.", 66,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0; L.scoreDynamics = true; double t = 0;
             const struct { int top, mid, low; } c[] = { { 69, 65, 60 }, { 71, 67, 62 }, { 72, 67, 64 }, { 72, 69, 64 },
                                                          { 74, 69, 65 }, { 72, 67, 64 }, { 71, 67, 62 }, { 72, 67, 64 } };
             for (auto& x : c)
             {
                 b.add (x.top, t, 1.9, 100, R);
                 b.add (x.mid, t, 1.9, 54, R);
                 b.add (x.low, t, 1.9, 54, R);
                 t += 2;
             } }, true);

    add ("dy.ode", 3, c7, "Ode to Joy  \xc2\xb7  shape the phrase",
         "Beethoven's tune with the phrasing written into the velocities. Swell toward the middle of each phrase, relax at the end.", 92,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0; L.scoreDynamics = true;
             const int mel[] = { 64, 64, 65, 67, 67, 65, 64, 62, 60, 60, 62, 64, 64, 62, 62 };
             double t = 0;
             const double dur[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1.5, .5, 2 };
             for (int i = 0; i < 15; ++i)
             {
                 const double phrase = (double) i / 14.0;
                 const int vel = (int) (58 + 42 * std::sin (phrase * juce::MathConstants<double>::pi));
                 b.add (mel[i], t, dur[i] * 0.95, vel, R);
                 t += dur[i];
             } }, true);

    // ---- 8. Melodies ---------------------------------------------------------------
    const char* c8 = "8  \xc2\xb7  Melodies";
    add ("me.ode", 2, c8, "Ode to Joy",
         "Beethoven, Symphony No. 9. Right hand tune, left hand roots and fifths.", 100,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0;
             const char* a  = "E4 E4 F4 G4 G4 F4 E4 D4 C4 C4 D4 E4 E4:1.5 D4:.5 D4:2 ";
             const char* a2 = "E4 E4 F4 G4 G4 F4 E4 D4 C4 C4 D4 E4 D4:1.5 C4:.5 C4:2 ";
             const char* bb = "D4 D4 E4 C4 D4 E4:.5 F4:.5 E4 C4 D4 E4:.5 F4:.5 E4 D4 C4 D4 G3:2 ";
             b.seq (R, juce::String (a) + a2 + bb + a2, 0);
             const char* C = "C3+G3:4 ", *G = "G2+D3:4 ", *CG = "C3+G3:2 G2+D3:2 ", *GC = "G2+D3:2 C3+G3:2 ";
             b.seq (Lh, juce::String (C) + C + C + CG + C + C + C + GC + G + G + G + CG + C + C + C + GC, 0, 70); });

    add ("me.twinkle", 1, c8, "Twinkle, Twinkle, Little Star",
         "The classic. Watch the left hand shift between C, F and G chords.", 100,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0;
             const juce::String l1 = "C4 C4 G4 G4 A4 A4 G4:2 ", l2 = "F4 F4 E4 E4 D4 D4 C4:2 ", l3 = "G4 G4 F4 F4 E4 E4 D4:2 ";
             b.seq (R, l1 + l2 + l3 + l3 + l1 + l2, 0);
             b.seq (Lh, "C3+G3:4 F3+C4:2 C3+G3:2 F3+C4:2 C3+G3:2 G2+D3:2 C3+G3:2 C3+G3:4 C3+G3:2 G2+D3:2 "
                        "C3+G3:4 C3+G3:2 G2+D3:2 C3+G3:4 F3+C4:2 C3+G3:2 F3+C4:2 C3+G3:2 G2+D3:2 C3+G3:2", 0, 70); });

    add ("me.mary", 1, c8, "Mary Had a Little Lamb",
         "Only three notes at first: E, D, C. A good first tune for the right hand.", 110,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0;
             b.seq (R, "E4 D4 C4 D4 E4 E4 E4:2 D4 D4 D4:2 E4 G4 G4:2 E4 D4 C4 D4 E4 E4 E4 E4 D4 D4 E4 D4 C4:4", 0); });

    add ("me.frere", 1, c8, "Fr\xc3\xa8re Jacques",
         "Each phrase is played twice. Look for the repeats and the little eighth-note run.", 104,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0;
             b.seq (R, "C4 D4 E4 C4 C4 D4 E4 C4 E4 F4 G4:2 E4 F4 G4:2 G4:.5 A4:.5 G4:.5 F4:.5 E4 C4 "
                       "G4:.5 A4:.5 G4:.5 F4:.5 E4 C4 C4 G3 C4:2 C4 G3 C4:2", 0); });

    add ("me.elise", 3, c8, "F\xc3\xbcr Elise  \xc2\xb7  main theme",
         "Beethoven's famous opening, simplified. Keep the sixteenth notes light and even. Slow it down first.", 108,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 9;
             const juce::String m = "E5:.25 D#5:.25 E5:.25 D#5:.25 E5:.25 B4:.25 D5:.25 C5:.25 ";
             b.seq (R, m + "A4:.75 r:.25 C4:.25 E4:.25 A4:.25 B4:.75 r:.25 E4:.25 G#4:.25 B4:.25 C5:.75 r:.25 "
                      + m + "A4:.75 r:.25 C4:.25 E4:.25 A4:.25 B4:.75 r:.25 E4:.25 C5:.25 B4:.25 A4:2", 0);
             b.seq (Lh, "r:3 A2:.25 E3:.25 A3:.25 r:.75 E2:.25 E3:.25 G#3:.25 r:.75 A2:.25 E3:.25 A3:.25 r:1.5 "
                        "r:3 A2:.25 E3:.25 A3:.25 r:.75 E2:.25 E3:.25 G#3:.25 r:.75 A2:.25 E3:.25 A3:.25", 0, 62); });

    add ("me.prelude", 4, c8, "Prelude in C  \xc2\xb7  opening",
         "The opening bars of Bach's famous prelude (BWV 846): one pattern, four chords. Hypnotic and perfect for finger evenness.", 70,
         [] (Lesson& L) { Builder b { L }; L.keyRoot = 0; double t = 0;
             const int bars[4][5] = { { 60, 64, 67, 72, 76 }, { 60, 62, 69, 74, 77 }, { 59, 62, 67, 74, 77 }, { 60, 64, 67, 72, 76 } };
             for (int bar = 0; bar < 4; ++bar)
                 for (int half = 0; half < 2; ++half)
                 {
                     const auto* c = bars[bar];
                     const int fig[8] = { c[0], c[1], c[2], c[3], c[4], c[2], c[3], c[4] };
                     b.add (fig[0] - 12, t, 1.95, 66, Lh);
                     for (int i = 1; i < 8; ++i) b.add (fig[i], t + 0.25 * i, 0.24, 80, R);
                     t += 2.0;
                 }
             b.add (48, t, 4, 70, Lh); b.add (64, t, 4, 78, R); b.add (67, t, 4, 78, R); b.add (72, t, 4, 78, R); });

    return r;
}
} // namespace

//==============================================================================
const std::vector<Info>& all()
{
    static const std::vector<Info> list = [] { std::vector<Info> v; for (auto& e : registry()) v.push_back (e.info); return v; }();
    return list;
}

bool build (const juce::String& id, Lesson& out)
{
    for (auto& e : registry())
        if (e.info.id == id)
        {
            Lesson l;
            l.id = e.info.id; l.title = e.info.title; l.category = e.info.category; l.blurb = e.info.blurb;
            l.bpm = e.bpm; l.scoreDynamics = e.info.dynamics;
            e.make (l);
            if (l.bpm <= 0) l.bpm = e.bpm;
            l.finalise();
            out = std::move (l);
            return true;
        }
    return false;
}
} // namespace pt::library
