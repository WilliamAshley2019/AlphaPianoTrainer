#pragma once
#include "Lesson.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace pt
{
//==============================================================================
// PracticeEngine: the real-time heart of the trainer.
//
//  * Owns an internal transport measured in lesson beats (ppq). The tempo is
//    supplied per block, so it can follow a knob or the DAW without the roll
//    ever jumping.
//  * Judges the player's incoming MIDI against the lesson (timing, pitch and
//    optionally touch/velocity).
//  * Produces backing events (the other hand, or everything in Watch mode)
//    and metronome clicks, both with sample-accurate offsets.
//  * Never allocates, never locks. UI talks to it through atomics and a FIFO.
//==============================================================================
enum PracticeMode { modePlay = 0, modeWait = 1, modeWatch = 2 };

struct PracticeParams
{
    double bpm         = 100.0;
    double sampleRate  = 44100.0;
    int    hand        = 0;            // 0 both, 1 right, 2 left
    int    mode        = modePlay;
    bool   loop        = false;
    double loopStart   = 0.0;          // beats
    double loopEnd     = 0.0;          // beats
    double perfectMs   = 45.0;
    double goodMs      = 120.0;
    bool   metronome   = true;
    int    countInBars = 1;
    float  backingGain = 0.7f;
};

struct EngineEvent { int offset; std::uint8_t pitch; std::uint8_t velocity; bool on; };
struct ClickEvent  { int offset; bool accent; };

struct HitEvent
{
    enum Kind : std::uint8_t { hitPerfect, hitGood, hitOffDynamics, hitMissed, hitWrong };
    int    note   = -1;      // index into lesson.notes (-1 for a wrong note)
    int    pitch  = 0;
    float  errMs  = 0.f;     // + late, - early
    int    velDiff = 0;      // played - target
    Kind   kind   = hitPerfect;
    double beat   = 0.0;
};

struct LiveStats
{
    std::atomic<int>   perfect { 0 }, good { 0 }, offDyn { 0 }, missed { 0 }, wrong { 0 };
    std::atomic<int>   streak { 0 }, bestStreak { 0 }, total { 0 };
    std::atomic<float> meanErrMs { 0.f }, stdErrMs { 0.f };

    int   hits() const noexcept { return perfect.load() + good.load() + offDyn.load(); }
    float accuracy() const noexcept
    {
        const int judged = hits() + missed.load();
        if (judged <= 0) return 0.f;
        const float pts = perfect.load() + 0.75f * good.load() + 0.6f * offDyn.load() - 0.2f * wrong.load();
        return juce::jlimit (0.f, 1.f, pts / (float) judged);
    }
};

class PracticeEngine
{
public:
    static constexpr int maxEvents = 1024, maxClicks = 64;

    void prepare (double sampleRate);

    // Audio thread: swap the active session (nullptr allowed).
    void attach (Session* s);

    // Any thread: transport commands, consumed at the next block.
    void cmdPlay()   noexcept { command.store (cPlay);   }
    void cmdPause()  noexcept { command.store (cPause);  }
    void cmdStop()   noexcept { command.store (cStop);   }
    void cmdToggle() noexcept { command.store (cToggle); }
    void cmdRestart() noexcept { command.store (cRestart); }
    void cmdSeek (double beats) noexcept { seekBeat.store (beats); seekPending.store (true); }

    // Audio thread.
    void process (const PracticeParams& p, const juce::MidiBuffer& userMidi, int numSamples);

    const EngineEvent* events() const noexcept { return ev; }
    int numEvents() const noexcept { return nEv; }
    const ClickEvent* clicks() const noexcept { return clk; }
    int numClicks() const noexcept { return nClk; }

    // UI side -----------------------------------------------------------------
    std::atomic<double> posBeats { 0.0 };
    std::atomic<double> posStamp { 0.0 };        // seconds (Time::getMillisecondCounterHiRes()/1000)
    std::atomic<double> beatsPerSec { 1.6667 };
    std::atomic<bool>   playing { false }, waiting { false };
    std::atomic<int>    finishedCount { 0 };     // bumps once per completed run
    std::atomic<std::uint64_t> heldLo { 0 }, heldHi { 0 };
    LiveStats stats;

    int popHits (HitEvent* dst, int maxCount);

    // Playhead extrapolated between audio blocks, for smooth drawing.
    double smoothPosition() const noexcept
    {
        double p = posBeats.load();
        if (playing.load() && ! waiting.load())
            p += juce::jlimit (0.0, 0.08, juce::Time::getMillisecondCounterHiRes() * 0.001 - posStamp.load()) * beatsPerSec.load();
        return p;
    }

private:
    enum Cmd { cNone = 0, cPlay, cPause, cStop, cToggle, cRestart };

    void advance (int s0, int s1);
    void step (double a, double b, int s0, int n);
    void handleUser (const juce::MidiMessage& m);
    void scorePlay (int pitch, int vel);
    void scoreWait (int pitch, int vel);

    void startPlay();
    void pauseNow();
    void stopNow();
    void seekTo (double beat);
    void wrapLoop();
    void finishRun();

    void recomputeCursors();
    void skipSatisfiedGroups();
    void resetStates (double from, double to);
    void flushMisses (double upToStart);
    void allNotesOff();
    void pushEvent (int offset, bool on, int pitch, int vel);
    void registerResult (HitEvent::Kind k, int noteIdx, int pitch, float errMs, int velDiff, bool timed);
    void resetStats();
    bool isPracticed (const Note& n) const noexcept;
    bool isBacking   (const Note& n) const noexcept;
    double startBeat() const noexcept;
    bool freePlay() const noexcept { return session == nullptr || session->lesson.notes.empty(); }

    Session* session = nullptr;
    PracticeParams params;
    double bps = 0.002;            // beats per sample
    double goodBeats = 0.2, perfectBeats = 0.07;
    int    beatsPerBar = 4;
    int    curHand = -1, curMode = -1;
    int    curOffset = 0;

    double pos = 0.0;
    bool   paused = false;
    bool   stalled = false;
    int    playCursor = 0, missCursor = 0, waitGroup = 0;
    bool   groupWrong = false;

    double active[128];            // end beat of sounding backing notes, <0 = silent
    int    activeCount = 0;
    std::uint64_t held[2] { 0, 0 };

    // timing statistics
    int    nTimed = 0;
    double sumErr = 0.0, sumSq = 0.0;

    std::atomic<int>    command { cNone };
    std::atomic<double> seekBeat { 0.0 };
    std::atomic<bool>   seekPending { false };

    EngineEvent ev[maxEvents];
    ClickEvent  clk[maxClicks];
    int nEv = 0, nClk = 0;

    static constexpr int hitFifoSize = 512;
    juce::AbstractFifo hitFifo { hitFifoSize };
    HitEvent hitBuf[hitFifoSize];
};
} // namespace pt
