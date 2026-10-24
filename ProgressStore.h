#pragma once
#include <juce_core/juce_core.h>
#include <map>

namespace pt
{
struct LessonRecord
{
    int    plays = 0, waitRuns = 0;
    float  bestAccuracy = 0.f, lastAccuracy = 0.f;
    double bestCleanBpm = 0.0;           // fastest tempo played at >= 90 %
    juce::int64 lastPlayedMs = 0;

    int stars() const noexcept
    {
        return bestAccuracy >= 0.97f ? 3 : bestAccuracy >= 0.88f ? 2 : bestAccuracy >= 0.70f ? 1 : 0;
    }
};

//==============================================================================
// Practice history, kept as a small JSON file in the user's app-data folder.
// Message-thread only.
//==============================================================================
class ProgressStore
{
public:
    ProgressStore();

    LessonRecord get (const juce::String& lessonId) const;
    void recordRun (const juce::String& lessonId, float accuracy, double bpm, bool graded);
    void addPracticeSeconds (double seconds);

    double todaySeconds() const;
    double totalSeconds() const;
    int    streakDays() const;
    std::vector<double> lastDays (int n) const;     // oldest first, minutes
    int    totalStars() const;

    void save();

private:
    void load();
    static juce::String dayKey (juce::Time t);

    std::map<juce::String, LessonRecord> lessons;
    std::map<juce::String, double> days;            // yyyy-MM-dd -> seconds
    juce::File file;
    bool dirty = false;
    juce::uint32 lastSaveTick = 0;
};
} // namespace pt
