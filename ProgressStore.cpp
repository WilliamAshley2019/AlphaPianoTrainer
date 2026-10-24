#include "ProgressStore.h"

namespace pt
{
ProgressStore::ProgressStore()
{
    file = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("AlphaAudio").getChildFile ("PianoTrainer").getChildFile ("progress.json");
    load();
}

juce::String ProgressStore::dayKey (juce::Time t) { return t.formatted ("%Y-%m-%d"); }

void ProgressStore::load()
{
    if (! file.existsAsFile()) return;
    const auto v = juce::JSON::parse (file);
    if (auto* root = v.getDynamicObject())
    {
        if (auto* ls = root->getProperty ("lessons").getDynamicObject())
            for (auto& p : ls->getProperties())
                if (auto* o = p.value.getDynamicObject())
                {
                    LessonRecord r;
                    r.plays        = (int) o->getProperty ("plays");
                    r.waitRuns     = (int) o->getProperty ("waitRuns");
                    r.bestAccuracy = (float) (double) o->getProperty ("best");
                    r.lastAccuracy = (float) (double) o->getProperty ("last");
                    r.bestCleanBpm = (double) o->getProperty ("bpm");
                    r.lastPlayedMs = (juce::int64) o->getProperty ("when");
                    lessons[p.name.toString()] = r;
                }
        if (auto* ds = root->getProperty ("days").getDynamicObject())
            for (auto& p : ds->getProperties())
                days[p.name.toString()] = (double) p.value;
    }
}

void ProgressStore::save()
{
    if (! dirty) return;
    auto* root = new juce::DynamicObject();
    auto* ls = new juce::DynamicObject();
    for (auto& [id, r] : lessons)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("plays", r.plays);
        o->setProperty ("waitRuns", r.waitRuns);
        o->setProperty ("best", (double) r.bestAccuracy);
        o->setProperty ("last", (double) r.lastAccuracy);
        o->setProperty ("bpm", r.bestCleanBpm);
        o->setProperty ("when", r.lastPlayedMs);
        ls->setProperty (id, juce::var (o));
    }
    auto* ds = new juce::DynamicObject();
    for (auto& [d, s] : days) ds->setProperty (d, s);
    root->setProperty ("lessons", juce::var (ls));
    root->setProperty ("days", juce::var (ds));

    file.getParentDirectory().createDirectory();
    if (file.replaceWithText (juce::JSON::toString (juce::var (root))))
        dirty = false;
}

LessonRecord ProgressStore::get (const juce::String& id) const
{
    auto it = lessons.find (id);
    return it != lessons.end() ? it->second : LessonRecord();
}

void ProgressStore::recordRun (const juce::String& id, float accuracy, double bpm, bool graded)
{
    auto& r = lessons[id];
    r.lastPlayedMs = juce::Time::currentTimeMillis();
    if (graded)
    {
        ++r.plays;
        r.lastAccuracy = accuracy;
        r.bestAccuracy = juce::jmax (r.bestAccuracy, accuracy);
        if (accuracy >= 0.9f) r.bestCleanBpm = juce::jmax (r.bestCleanBpm, bpm);
    }
    else ++r.waitRuns;
    dirty = true;
    save();
}

void ProgressStore::addPracticeSeconds (double s)
{
    if (s <= 0.0) return;
    days[dayKey (juce::Time::getCurrentTime())] += s;
    dirty = true;
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastSaveTick > 30000) { lastSaveTick = now; save(); }
}

double ProgressStore::todaySeconds() const
{
    auto it = days.find (dayKey (juce::Time::getCurrentTime()));
    return it != days.end() ? it->second : 0.0;
}

double ProgressStore::totalSeconds() const
{
    double t = 0; for (auto& d : days) t += d.second; return t;
}

int ProgressStore::streakDays() const
{
    auto t = juce::Time::getCurrentTime();
    auto at = [&] (juce::Time x) { auto it = days.find (dayKey (x)); return it != days.end() ? it->second : 0.0; };
    if (at (t) < 60.0) t -= juce::RelativeTime::days (1);      // today not started yet doesn't break the streak
    int n = 0;
    while (at (t) >= 60.0 && n < 3650) { ++n; t -= juce::RelativeTime::days (1); }
    return n;
}

std::vector<double> ProgressStore::lastDays (int n) const
{
    std::vector<double> v;
    auto t = juce::Time::getCurrentTime() - juce::RelativeTime::days ((double) (n - 1));
    for (int i = 0; i < n; ++i)
    {
        auto it = days.find (dayKey (t));
        v.push_back (it != days.end() ? it->second / 60.0 : 0.0);
        t += juce::RelativeTime::days (1);
    }
    return v;
}

int ProgressStore::totalStars() const
{
    int s = 0; for (auto& l : lessons) s += l.second.stars(); return s;
}
} // namespace pt
