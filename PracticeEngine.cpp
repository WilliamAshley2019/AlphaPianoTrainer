#include "PracticeEngine.h"
#include <algorithm>
#include <cmath>

namespace pt
{
namespace
{
constexpr double eps = 1e-9;
constexpr int dynamicsTolerance = 24;
}

//==============================================================================
void PracticeEngine::prepare (double)
{
    for (auto& a : active) a = -1.0;
    activeCount = 0;
}

void PracticeEngine::attach (Session* s)
{
    session = s;
    pos = 0.0;
    paused = stalled = false;
    playing.store (false);
    waiting.store (false);
    for (auto& a : active) a = -1.0;
    activeCount = 0;
    held[0] = held[1] = 0;
    curHand = curMode = -1;          // forces cursor recompute on the next block
    playCursor = missCursor = waitGroup = 0;
    groupWrong = false;
    resetStats();
}

int PracticeEngine::popHits (HitEvent* dst, int maxCount)
{
    const auto r = hitFifo.read (juce::jmin (maxCount, hitFifo.getNumReady()));
    int n = 0;
    for (int i = 0; i < r.blockSize1; ++i) dst[n++] = hitBuf[r.startIndex1 + i];
    for (int i = 0; i < r.blockSize2; ++i) dst[n++] = hitBuf[r.startIndex2 + i];
    return n;
}

//==============================================================================
bool PracticeEngine::isPracticed (const Note& n) const noexcept
{
    return curHand == 0 || (curHand == 1 && n.hand == handRight) || (curHand == 2 && n.hand == handLeft);
}

bool PracticeEngine::isBacking (const Note& n) const noexcept
{
    if (params.mode == modeWatch) return true;
    return ! isPracticed (n);
}

double PracticeEngine::startBeat() const noexcept
{
    return (params.loop && params.loopEnd - params.loopStart >= 0.25) ? params.loopStart : 0.0;
}

//==============================================================================
void PracticeEngine::pushEvent (int offset, bool on, int pitch, int vel)
{
    if (nEv >= maxEvents) return;
    ev[nEv++] = { offset, (std::uint8_t) pitch, (std::uint8_t) vel, on };
}

void PracticeEngine::allNotesOff()
{
    if (activeCount > 0)
        for (int p = 0; p < 128; ++p)
            if (active[p] >= 0.0) { pushEvent (curOffset, false, p, 0); active[p] = -1.0; }
    activeCount = 0;
}

void PracticeEngine::resetStats()
{
    stats.perfect = 0; stats.good = 0; stats.offDyn = 0; stats.missed = 0; stats.wrong = 0;
    stats.streak = 0; stats.bestStreak = 0;
    stats.meanErrMs = 0.f; stats.stdErrMs = 0.f;
    stats.total = session ? (int) session->targets[juce::jlimit (0, 2, curHand < 0 ? 0 : curHand)].size() : 0;
    nTimed = 0; sumErr = sumSq = 0.0;
}

void PracticeEngine::registerResult (HitEvent::Kind k, int noteIdx, int pitch, float errMs, int velDiff, bool timed)
{
    switch (k)
    {
        case HitEvent::hitPerfect:     ++stats.perfect; break;
        case HitEvent::hitGood:        ++stats.good;    break;
        case HitEvent::hitOffDynamics: ++stats.offDyn;  break;
        case HitEvent::hitMissed:      ++stats.missed;  break;
        case HitEvent::hitWrong:       ++stats.wrong;   break;
    }

    if (k == HitEvent::hitMissed || k == HitEvent::hitWrong) stats.streak = 0;
    else
    {
        const int s = stats.streak.load() + 1;
        stats.streak = s;
        if (s > stats.bestStreak.load()) stats.bestStreak = s;
    }

    if (timed && k != HitEvent::hitMissed && k != HitEvent::hitWrong)
    {
        ++nTimed; sumErr += errMs; sumSq += (double) errMs * errMs;
        const double mean = sumErr / nTimed;
        stats.meanErrMs = (float) mean;
        stats.stdErrMs  = (float) std::sqrt (juce::jmax (0.0, sumSq / nTimed - mean * mean));
    }

    if (hitFifo.getFreeSpace() > 0)
    {
        const auto w = hitFifo.write (1);
        HitEvent h;
        h.note = noteIdx; h.pitch = pitch; h.errMs = errMs; h.velDiff = velDiff; h.kind = k; h.beat = pos;
        if (w.blockSize1 > 0) hitBuf[w.startIndex1] = h; else hitBuf[w.startIndex2] = h;
    }
}

//==============================================================================
void PracticeEngine::recomputeCursors()
{
    if (session == nullptr) return;
    const auto& L = session->lesson;
    const int h = juce::jlimit (0, 2, curHand);

    playCursor = (int) (std::lower_bound (L.notes.begin(), L.notes.end(), pos - eps,
                                          [] (const Note& n, double v) { return n.start < v; }) - L.notes.begin());

    const auto& S = session->targetStart[h];
    missCursor = (int) (std::lower_bound (S.begin(), S.end(), pos - eps) - S.begin());

    const auto& G = session->groups[h];
    waitGroup = (int) (std::lower_bound (G.begin(), G.end(), pos - eps,
                                         [] (const Session::Group& g, double v) { return g.start < v; }) - G.begin());
    groupWrong = false;
    skipSatisfiedGroups();
}

void PracticeEngine::skipSatisfiedGroups()
{
    if (session == nullptr) return;
    const int h = juce::jlimit (0, 2, curHand);
    const auto& G = session->groups[h];
    const auto& T = session->targets[h];
    while (waitGroup < (int) G.size())
    {
        bool allDone = true;
        for (int i = G[(size_t) waitGroup].begin; i < G[(size_t) waitGroup].end; ++i)
            if (session->getState (T[(size_t) i]) == nsPending) { allDone = false; break; }
        if (! allDone) break;
        ++waitGroup;
        groupWrong = false;
    }
}

void PracticeEngine::resetStates (double from, double to)
{
    if (session == nullptr) return;
    const auto& L = session->lesson;
    for (size_t i = 0; i < L.notes.size(); ++i)
        if (L.notes[i].start >= from - eps && L.notes[i].start < to)
            session->setState ((int) i, nsPending);
}

void PracticeEngine::flushMisses (double upToStart)
{
    if (session == nullptr || params.mode != modePlay) return;
    const int h = juce::jlimit (0, 2, curHand);
    const auto& T = session->targets[h];
    const auto& S = session->targetStart[h];
    while (missCursor < (int) S.size() && S[(size_t) missCursor] < upToStart)
    {
        const int ni = T[(size_t) missCursor];
        if (session->getState (ni) == nsPending)
        {
            session->setState (ni, nsMissed);
            registerResult (HitEvent::hitMissed, ni, session->lesson.notes[(size_t) ni].pitch, 0.f, 0, false);
        }
        ++missCursor;
    }
}

//==============================================================================
// Transport
//==============================================================================
void PracticeEngine::startPlay()
{
    if (session == nullptr) return;
    allNotesOff();
    resetStates (-1e18, 1e18);
    resetStats();
    pos = startBeat() - (double) (params.countInBars * beatsPerBar);
    paused = false;
    stalled = false;
    recomputeCursors();
    playing.store (true);
}

void PracticeEngine::pauseNow()
{
    allNotesOff();
    playing.store (false);
    waiting.store (false);
    paused = true;
}

void PracticeEngine::stopNow()
{
    allNotesOff();
    playing.store (false);
    waiting.store (false);
    paused = false;
    pos = startBeat();
    resetStates (-1e18, 1e18);
    recomputeCursors();
}

void PracticeEngine::seekTo (double beat)
{
    if (session == nullptr) return;
    allNotesOff();
    const double maxB = freePlay() ? 1e9 : session->lesson.lengthBeats;
    pos = juce::jlimit (0.0, maxB, beat);
    resetStates (pos, 1e18);
    recomputeCursors();
    if (! playing.load()) paused = true;     // press play to continue from here
}

void PracticeEngine::wrapLoop()
{
    flushMisses (params.loopEnd);
    allNotesOff();
    resetStates (params.loopStart, params.loopEnd);
    pos = params.loopStart;
    recomputeCursors();
}

void PracticeEngine::finishRun()
{
    flushMisses (1e18);
    allNotesOff();
    playing.store (false);
    waiting.store (false);
    paused = false;
    finishedCount.fetch_add (1);
}

//==============================================================================
// Scoring
//==============================================================================
void PracticeEngine::scorePlay (int pitch, int vel)
{
    const auto& L = session->lesson;
    const int h = juce::jlimit (0, 2, curHand);
    const auto& T = session->targets[h];
    const auto& S = session->targetStart[h];

    const double lo = pos - goodBeats, hi = pos + goodBeats;
    int best = -1, bestIdx = -1;
    double bestD = 1e18;
    for (size_t i = (size_t) (std::lower_bound (S.begin(), S.end(), lo) - S.begin()); i < S.size() && S[i] <= hi; ++i)
    {
        const int ni = T[i];
        if (L.notes[(size_t) ni].pitch != pitch || session->getState (ni) != nsPending) continue;
        const double d = std::abs (S[i] - pos);
        if (d < bestD) { bestD = d; best = ni; bestIdx = (int) i; }
    }
    juce::ignoreUnused (bestIdx);

    if (best >= 0)
    {
        const auto& n = L.notes[(size_t) best];
        const float errMs = (float) ((pos - n.start) * 60000.0 / params.bpm);
        const int velDiff = vel - n.velocity;

        HitEvent::Kind k = std::abs (errMs) <= (float) params.perfectMs ? HitEvent::hitPerfect : HitEvent::hitGood;
        NoteState ns = k == HitEvent::hitPerfect ? nsPerfect : nsGood;
        if (L.scoreDynamics && std::abs (velDiff) > dynamicsTolerance) { k = HitEvent::hitOffDynamics; ns = nsOffDynamics; }

        session->setState (best, ns);
        registerResult (k, best, pitch, errMs, velDiff, true);
        return;
    }

    // Not a target. If it belongs to the hand we are not grading, let it pass silently.
    if (curHand != 0)
    {
        const auto first = std::lower_bound (L.notes.begin(), L.notes.end(), lo,
                                             [] (const Note& n, double v) { return n.start < v; });
        for (auto it = first; it != L.notes.end() && it->start <= hi; ++it)
            if (it->pitch == pitch && ! isPracticed (*it)) return;
    }
    registerResult (HitEvent::hitWrong, -1, pitch, 0.f, 0, false);
}

void PracticeEngine::scoreWait (int pitch, int vel)
{
    const auto& L = session->lesson;
    const int h = juce::jlimit (0, 2, curHand);
    const auto& G = session->groups[h];
    const auto& T = session->targets[h];
    if (waitGroup >= (int) G.size()) return;

    const auto& g = G[(size_t) waitGroup];
    if (pos < g.start - 1.0) return;                 // far too early: ignore silently

    for (int i = g.begin; i < g.end; ++i)
    {
        const int ni = T[(size_t) i];
        const auto& n = L.notes[(size_t) ni];
        if (n.pitch != pitch || session->getState (ni) != nsPending) continue;

        const int velDiff = vel - n.velocity;
        HitEvent::Kind k = groupWrong ? HitEvent::hitGood : HitEvent::hitPerfect;
        NoteState ns = groupWrong ? nsGood : nsPerfect;
        if (L.scoreDynamics && std::abs (velDiff) > dynamicsTolerance) { k = HitEvent::hitOffDynamics; ns = nsOffDynamics; }

        session->setState (ni, ns);
        registerResult (k, ni, pitch, 0.f, velDiff, false);

        skipSatisfiedGroups();
        return;
    }
    groupWrong = true;
    registerResult (HitEvent::hitWrong, -1, pitch, 0.f, 0, false);
}

void PracticeEngine::handleUser (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        const int p = m.getNoteNumber();
        held[p >> 6] |= (std::uint64_t) 1 << (p & 63);

        if (! playing.load() || freePlay() || params.mode == modeWatch) return;
        if (params.mode == modeWait) scoreWait (p, m.getVelocity());
        else                         scorePlay (p, m.getVelocity());
    }
    else if (m.isNoteOff())
    {
        const int p = m.getNoteNumber();
        held[p >> 6] &= ~((std::uint64_t) 1 << (p & 63));
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        held[0] = held[1] = 0;
}

//==============================================================================
// Block processing
//==============================================================================
void PracticeEngine::step (double a, double b, int s0, int n)
{
    const auto& L = session->lesson;
    const int last = juce::jmax (0, n - 1);
    auto offsetOf = [&] (double beat) { return s0 + juce::jlimit (0, last, (int) ((beat - a) / bps)); };

    // note-ons
    while (playCursor < (int) L.notes.size() && L.notes[(size_t) playCursor].start < b)
    {
        const auto& note = L.notes[(size_t) playCursor++];
        if (! isBacking (note)) continue;
        const int off = offsetOf (juce::jmax (a, note.start));
        if (active[note.pitch] >= 0.0) pushEvent (off, false, note.pitch, 0);
        else ++activeCount;
        active[note.pitch] = note.end();
        pushEvent (off, true, note.pitch, juce::jlimit (1, 127, (int) (note.velocity * params.backingGain)));
    }

    // note-offs
    if (activeCount > 0)
        for (int p = 0; p < 128; ++p)
            if (active[p] >= 0.0 && active[p] < b)
            {
                pushEvent (offsetOf (juce::jmax (a, active[p])), false, p, 0);
                active[p] = -1.0;
                --activeCount;
            }

    // clicks
    for (double k = std::ceil (a - eps); k < b - eps; k += 1.0)
    {
        if (! (params.metronome || k < 0.0)) continue;
        if (nClk >= maxClicks) break;
        const int bar = ((int) k % beatsPerBar + beatsPerBar) % beatsPerBar;
        clk[nClk++] = { offsetOf (k), bar == 0 };
    }

    if (params.mode == modePlay) flushMisses (b - goodBeats);
}

void PracticeEngine::advance (int s0, int s1)
{
    if (! playing.load() || session == nullptr) return;

    const auto& L = session->lesson;
    const bool free   = L.notes.empty();
    const double endBeat = free ? 1e18 : L.lengthBeats + 0.5;
    const bool loopOn = params.loop && ! free && params.loopEnd - params.loopStart >= 0.25;
    const bool waitOn = params.mode == modeWait && ! free;
    const auto& groups = session->groups[juce::jlimit (0, 2, curHand)];

    enum B { bNone, bLoop, bGate, bEnd };

    while (s0 < s1 && playing.load())
    {
        curOffset = s0;
        const int remain = s1 - s0;
        const double target = pos + remain * bps;
        double stopAt = target;
        B kind = bNone;

        if (target >= endBeat) { stopAt = juce::jmax (pos, endBeat); kind = bEnd; }
        if (loopOn && (target >= params.loopEnd || pos >= params.loopEnd) && params.loopEnd <= stopAt + eps)
        { stopAt = juce::jmax (pos, params.loopEnd); kind = bLoop; }

        if (waitOn && waitGroup < (int) groups.size())
        {
            const double g = groups[(size_t) waitGroup].start;
            if (pos >= g - eps) { stalled = true; return; }           // hold here until the keys are played
            if (g < stopAt) { stopAt = g; kind = bGate; }
        }
        stalled = false;

        int n = kind == bNone ? remain
                              : juce::jlimit (0, remain, (int) std::ceil ((stopAt - pos) / bps - eps));
        const double segEnd = kind == bNone ? pos + n * bps : stopAt;

        step (pos, segEnd, s0, n);
        pos = segEnd;
        s0 += n;

        switch (kind)
        {
            case bLoop: wrapLoop();  break;
            case bEnd:  finishRun(); break;
            default: break;
        }
    }
}

void PracticeEngine::process (const PracticeParams& p, const juce::MidiBuffer& userMidi, int numSamples)
{
    params = p;
    nEv = nClk = 0;
    curOffset = 0;
    stalled = false;
    bps = p.bpm / 60.0 / p.sampleRate;
    goodBeats    = p.goodMs / 1000.0 * p.bpm / 60.0;
    perfectBeats = p.perfectMs / 1000.0 * p.bpm / 60.0;
    beatsPerBar  = session != nullptr ? juce::jmax (1, session->lesson.beatsPerBar) : 4;

    if (p.hand != curHand || p.mode != curMode)
    {
        const bool first = curHand < 0;
        allNotesOff();
        curHand = p.hand; curMode = p.mode;
        recomputeCursors();
        if (! first && playing.load()) { resetStates (pos, 1e18); recomputeCursors(); }
        stats.total = session ? (int) session->targets[juce::jlimit (0, 2, curHand)].size() : 0;
    }

    if (seekPending.exchange (false)) seekTo (seekBeat.load());

    switch (command.exchange (cNone))
    {
        case cPlay:   if (! playing.load()) { if (paused) { playing.store (true); paused = false; } else startPlay(); } break;
        case cPause:  if (playing.load()) pauseNow(); break;
        case cStop:   stopNow(); break;
        case cRestart: if (playing.load()) pauseNow(); startPlay(); break;
        case cToggle:
            if (playing.load()) pauseNow();
            else if (paused) { playing.store (true); paused = false; }
            else startPlay();
            break;
        default: break;
    }

    int cur = 0;
    for (const auto meta : userMidi)
    {
        const int sp = juce::jlimit (cur, numSamples, meta.samplePosition);
        advance (cur, sp);
        cur = sp;
        handleUser (meta.getMessage());
    }
    advance (cur, numSamples);

    // insertion sort by offset (stable)
    for (int i = 1; i < nEv; ++i)
    {
        const auto e = ev[i];
        int j = i - 1;
        while (j >= 0 && ev[j].offset > e.offset) { ev[j + 1] = ev[j]; --j; }
        ev[j + 1] = e;
    }

    posBeats.store (pos);
    posStamp.store (juce::Time::getMillisecondCounterHiRes() * 0.001);
    beatsPerSec.store (p.bpm / 60.0);
    waiting.store (stalled && playing.load());
    heldLo.store (held[0]);
    heldHi.store (held[1]);
}
} // namespace pt
