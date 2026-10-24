#include "Lesson.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace pt
{
//==============================================================================
void Lesson::finalise()
{
    std::stable_sort (notes.begin(), notes.end(),
                      [] (const Note& a, const Note& b)
                      { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); });

    lengthBeats = 0.0;
    lowestPitch = 127;
    highestPitch = 0;
    for (const auto& n : notes)
    {
        lengthBeats  = juce::jmax (lengthBeats, n.end());
        lowestPitch  = juce::jmin (lowestPitch, n.pitch);
        highestPitch = juce::jmax (highestPitch, n.pitch);
    }
    if (notes.empty()) { lowestPitch = 48; highestPitch = 84; }
}

Lesson Lesson::transposed (int semitones) const
{
    Lesson l = *this;
    if (semitones == 0) return l;

    for (auto& n : l.notes)
    {
        int p = n.pitch + semitones;
        while (p > 127) p -= 12;
        while (p < 0)   p += 12;
        n.pitch = p;
    }
    if (l.keyRoot >= 0)
        l.keyRoot = ((l.keyRoot + semitones) % 12 + 12) % 12;
    l.finalise();
    return l;
}

//==============================================================================
// MIDI import
//==============================================================================
bool importMidiFile (const juce::File& file, Lesson& out, juce::String& error)
{
    if (! file.existsAsFile()) { error = "File not found"; return false; }

    juce::MidiFile mf;
    {
        juce::FileInputStream in (file);
        if (! in.openedOk() || ! mf.readFrom (in)) { error = "Not a readable MIDI file"; return false; }
    }

    const int  tpq   = mf.getTimeFormat();
    const bool smpte = tpq <= 0;
    if (smpte) mf.convertTimestampTicksToSeconds();   // then assume 120 bpm: 2 beats per second

    auto toBeats = [&] (double t) { return smpte ? t * 2.0 : t / (double) tpq; };

    double bpm = 0.0;
    int    beatsPerBar = 0;

    struct Raw { Note n; };
    std::vector<std::vector<Note>> perTrack;

    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        const auto* seq = mf.getTrack (t);
        std::map<int, std::pair<double, int>> open;     // ch*128+pitch -> (start, vel)
        std::vector<Note> notes;

        for (int i = 0; i < seq->getNumEvents(); ++i)
        {
            const auto& msg = seq->getEventPointer (i)->message;
            const double beat = toBeats (msg.getTimeStamp());

            if (msg.isTempoMetaEvent() && bpm <= 0.0)
            {
                const double spq = msg.getTempoSecondsPerQuarterNote();
                if (spq > 0.0) bpm = 60.0 / spq;
            }
            else if (msg.isTimeSignatureMetaEvent() && beatsPerBar == 0)
            {
                int num = 4, den = 4;
                msg.getTimeSignatureInfo (num, den);
                beatsPerBar = juce::jmax (1, (int) std::lround (num * 4.0 / juce::jmax (1, den)));
            }
            else if (msg.isNoteOn())
            {
                if (msg.getChannel() == 10) continue;          // drums
                open[msg.getChannel() * 128 + msg.getNoteNumber()] = { beat, msg.getVelocity() };
            }
            else if (msg.isNoteOff())
            {
                if (msg.getChannel() == 10) continue;
                const int key = msg.getChannel() * 128 + msg.getNoteNumber();
                auto it = open.find (key);
                if (it == open.end()) continue;

                Note n;
                n.start    = it->second.first;
                n.length   = juce::jmax (0.1, beat - n.start);
                n.pitch    = msg.getNoteNumber();
                n.velocity = juce::jlimit (1, 127, it->second.second);
                notes.push_back (n);
                open.erase (it);
            }
        }
        if (! notes.empty()) perTrack.push_back (std::move (notes));
    }

    size_t total = 0;
    for (auto& v : perTrack) total += v.size();
    if (total == 0) { error = "No playable notes found"; return false; }
    if (total > 30000) { error = "File is too dense for a lesson (>30000 notes)"; return false; }

    // ---- hand assignment ----
    auto avgPitch = [] (const std::vector<Note>& v)
    {
        double s = 0; for (auto& n : v) s += n.pitch; return s / (double) v.size();
    };

    Lesson l;
    if (perTrack.size() == 1)
    {
        for (auto n : perTrack[0]) { n.hand = n.pitch < 60 ? handLeft : handRight; l.notes.push_back (n); }
    }
    else
    {
        double mean = 0; for (auto& v : perTrack) mean += avgPitch (v);
        mean /= (double) perTrack.size();
        for (auto& v : perTrack)
        {
            const int hand = avgPitch (v) >= mean ? handRight : handLeft;
            for (auto n : v) { n.hand = hand; l.notes.push_back (n); }
        }
    }

    l.id          = "f:" + file.getFullPathName();
    l.title       = file.getFileNameWithoutExtension();
    l.category    = "Your MIDI files";
    l.blurb       = "Imported from " + file.getFileName();
    l.bpm         = bpm > 20.0 && bpm < 400.0 ? bpm : 100.0;
    l.beatsPerBar = beatsPerBar > 0 ? beatsPerBar : 4;
    l.finalise();

    out = std::move (l);
    return true;
}

//==============================================================================
// Session
//==============================================================================
Session::Session (Lesson l) : lesson (std::move (l))
{
    const size_t n = lesson.notes.size();
    state.reset (new std::atomic<std::uint8_t>[juce::jmax<size_t> (1, n)]);
    for (size_t i = 0; i < juce::jmax<size_t> (1, n); ++i)
        state[i].store (nsPending, std::memory_order_relaxed);

    for (int h = 0; h < 3; ++h)
    {
        for (size_t i = 0; i < n; ++i)
        {
            const int hand = lesson.notes[i].hand;
            if (h == 0 || (h == 1 && hand == handRight) || (h == 2 && hand == handLeft))
            {
                targets[h].push_back ((int) i);
                targetStart[h].push_back (lesson.notes[i].start);
            }
        }

        // Group notes that begin together (within 30 ms at 100 bpm ≈ 0.05 beat).
        const auto& t = targets[h];
        for (int i = 0; i < (int) t.size();)
        {
            const double s = lesson.notes[(size_t) t[(size_t) i]].start;
            int j = i + 1;
            while (j < (int) t.size() && lesson.notes[(size_t) t[(size_t) j]].start - s < 0.05) ++j;
            groups[h].push_back ({ s, i, j });
            i = j;
        }
    }
}
} // namespace pt
