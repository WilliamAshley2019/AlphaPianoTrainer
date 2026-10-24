#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace pt
{
// JUCE treats plain char* as ASCII, so literals with accents / middots go through this.
inline juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

//==============================================================================
// Lesson model.
//
// A Lesson is plain immutable-after-finalise() data measured in QUARTER-NOTE
// BEATS (ppq).  Tempo is metadata only, so the same lesson plays at any speed
// and the roll never jitters when the tempo changes.
//==============================================================================
enum Hand { handRight = 0, handLeft = 1 };

struct Note
{
    double start    = 0.0;   // beats
    double length   = 1.0;   // beats
    int    pitch    = 60;    // MIDI note
    int    velocity = 90;    // 1..127  (target dynamics)
    int    hand     = handRight;
    int    finger   = 0;     // 0 = unknown, 1..5 = thumb..pinky

    double end() const noexcept { return start + length; }
};

struct Lesson
{
    juce::String id, title, category, blurb;
    double bpm          = 100.0;
    int    beatsPerBar  = 4;
    int    keyRoot      = -1;      // pitch class of the home key, -1 = n/a
    bool   scoreDynamics = false;  // also grade how hard the keys are hit

    std::vector<Note> notes;       // sorted by (start, pitch) after finalise()
    double lengthBeats = 0.0;
    int    lowestPitch = 60, highestPitch = 72;

    void finalise();
    Lesson transposed (int semitones) const;
    bool   isFreePlay() const noexcept { return notes.empty(); }
};

// Result of a MIDI-file import.
bool importMidiFile (const juce::File& file, Lesson& out, juce::String& error);

//==============================================================================
// Per-note judgement state (written by the audio thread, read by the UI).
//==============================================================================
enum NoteState : std::uint8_t
{
    nsPending = 0, nsPerfect, nsGood, nsOffDynamics, nsMissed, nsSkipped
};

//==============================================================================
// Session = a lesson plus all the pre-computed index tables the audio thread
// needs, plus the mutable judgement state.  Built on the message thread,
// published to the audio thread as a raw pointer, kept alive by the processor.
//
// Hand-mode index: 0 = both hands, 1 = right hand, 2 = left hand.
//==============================================================================
struct Session
{
    struct Group { double start; int begin; int end; };   // ranges into targets[h]

    explicit Session (Lesson l);

    Lesson lesson;
    std::vector<int>    targets[3];       // indices into lesson.notes, by start
    std::vector<double> targetStart[3];
    std::vector<Group>  groups[3];        // chords / simultaneous notes
    std::unique_ptr<std::atomic<std::uint8_t>[]> state;   // one per note

    NoteState getState (int noteIndex) const noexcept
    {
        return (NoteState) state[(size_t) noteIndex].load (std::memory_order_relaxed);
    }
    void setState (int noteIndex, NoteState s) noexcept
    {
        state[(size_t) noteIndex].store ((std::uint8_t) s, std::memory_order_relaxed);
    }
};
} // namespace pt
