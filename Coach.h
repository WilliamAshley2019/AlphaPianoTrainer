#pragma once
#include "PracticeEngine.h"

namespace pt
{
struct RunResult
{
    bool   valid = false;
    juce::String lessonId, lessonTitle;
    int    mode = 0, hand = 0;
    double bpm = 0.0;
    int    perfect = 0, good = 0, offDyn = 0, missed = 0, wrong = 0, total = 0, bestStreak = 0;
    float  accuracy = 0.f, meanErrMs = 0.f, stdErrMs = 0.f, previousBest = 0.f;
    bool   newBest = false, dynamics = false;
    int    stars = 0;
    int    beatsPerBar = 4;
    std::vector<float> barAccuracy;          // -1 = no judged notes in that bar
    int    weakStartBar = -1, weakEndBar = -1;   // 0-based, inclusive
    juce::String headline, advice;
};

// Builds a run report from the session's per-note judgement and the live stats.
RunResult buildResult (const Session& s, const LiveStats& st, int mode, int hand, double bpm, float previousBest);
} // namespace pt
