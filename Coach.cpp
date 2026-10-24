#include "Coach.h"
#include <cmath>

namespace pt
{
RunResult buildResult (const Session& s, const LiveStats& st, int mode, int hand, double bpm, float previousBest)
{
    RunResult r;
    r.valid = true;
    r.lessonId = s.lesson.id; r.lessonTitle = s.lesson.title;
    r.mode = mode; r.hand = hand; r.bpm = bpm;
    r.perfect = st.perfect.load(); r.good = st.good.load(); r.offDyn = st.offDyn.load();
    r.missed = st.missed.load(); r.wrong = st.wrong.load();
    r.total = st.total.load(); r.bestStreak = st.bestStreak.load();
    r.accuracy = st.accuracy();
    r.meanErrMs = st.meanErrMs.load(); r.stdErrMs = st.stdErrMs.load();
    r.previousBest = previousBest;
    r.newBest = mode == modePlay && r.accuracy > previousBest + 0.001f;
    r.dynamics = s.lesson.scoreDynamics;
    r.beatsPerBar = juce::jmax (1, s.lesson.beatsPerBar);
    r.stars = r.accuracy >= 0.97f ? 3 : r.accuracy >= 0.88f ? 2 : r.accuracy >= 0.70f ? 1 : 0;

    // ---- per-bar accuracy ----
    const int h = juce::jlimit (0, 2, hand);
    const int bars = juce::jmax (1, (int) std::ceil (s.lesson.lengthBeats / r.beatsPerBar));
    std::vector<double> pts ((size_t) bars, 0.0), cnt ((size_t) bars, 0.0);
    for (int ni : s.targets[h])
    {
        const auto& n = s.lesson.notes[(size_t) ni];
        const int bar = juce::jlimit (0, bars - 1, (int) (n.start / r.beatsPerBar));
        double w = -1.0;
        switch (s.getState (ni))
        {
            case nsPerfect: w = 1.0; break;
            case nsGood: w = 0.75; break;
            case nsOffDynamics: w = 0.6; break;
            case nsMissed: w = 0.0; break;
            default: break;
        }
        if (w >= 0.0) { pts[(size_t) bar] += w; cnt[(size_t) bar] += 1.0; }
    }
    r.barAccuracy.resize ((size_t) bars, -1.f);
    for (int b = 0; b < bars; ++b)
        if (cnt[(size_t) b] > 0.0) r.barAccuracy[(size_t) b] = (float) (pts[(size_t) b] / cnt[(size_t) b]);

    // weakest stretch (up to 4 bars)
    int worst = -1; float worstV = 0.8f;
    for (int b = 0; b < bars; ++b)
        if (r.barAccuracy[(size_t) b] >= 0.f && r.barAccuracy[(size_t) b] < worstV) { worstV = r.barAccuracy[(size_t) b]; worst = b; }
    if (worst >= 0)
    {
        int a = worst, z = worst;
        while (z - a < 3 && z + 1 < bars && r.barAccuracy[(size_t) (z + 1)] >= 0.f && r.barAccuracy[(size_t) (z + 1)] < 0.8f) ++z;
        while (z - a < 3 && a > 0 && r.barAccuracy[(size_t) (a - 1)] >= 0.f && r.barAccuracy[(size_t) (a - 1)] < 0.8f) --a;
        r.weakStartBar = a; r.weakEndBar = z;
    }

    // ---- words ----
    const int pct = (int) std::lround (r.accuracy * 100.f);
    if (mode == modeWait)
        r.headline = "Notes learned";
    else if (r.accuracy >= 0.97f) r.headline = "Flawless";
    else if (r.accuracy >= 0.9f)  r.headline = "Excellent";
    else if (r.accuracy >= 0.8f)  r.headline = "Solid";
    else if (r.accuracy >= 0.65f) r.headline = "Getting there";
    else                          r.headline = "Keep at it";

    juce::StringArray tips;
    const int judged = juce::jmax (1, r.perfect + r.good + r.offDyn + r.missed);

    if (mode == modeWait)
        tips.add ("Wait mode checks the notes, not the timing. When you can play it without hesitating, switch to Play.");
    else
    {
        if (r.missed > judged * 0.3)
        {
            const int slower = juce::jlimit (30, 95, (int) std::lround (juce::jmax (30.0, 85.0)));
            tips.add ("You missed " + juce::String (r.missed) + " notes. Drop the speed to about " + juce::String (slower)
                      + "% and loop the weak bars until they are comfortable.");
        }
        if ((r.perfect + r.good + r.offDyn) > 8 && std::abs (r.meanErrMs) > 28.f)
            tips.add (r.meanErrMs > 0 ? "You played about " + juce::String ((int) r.meanErrMs) + " ms behind the beat on average. Anticipate a little: let your hand arrive early."
                                      : "You played about " + juce::String ((int) -r.meanErrMs) + " ms ahead of the beat on average. Relax and let the click lead you.");
        else if ((r.perfect + r.good + r.offDyn) > 8 && r.stdErrMs > 45.f)
            tips.add ("Your timing wanders by about " + juce::String ((int) r.stdErrMs) + " ms. Slow down and aim for evenness before speed.");

        if (r.wrong > juce::jmax (3, judged / 10))
            tips.add (juce::String (r.wrong) + " wrong notes. Try Wait mode or Watch mode first so your hands learn the shape.");
        if (r.dynamics && r.offDyn > judged / 5)
            tips.add ("Your touch did not match the lesson's dynamics. Watch the brightness of the bars: softer notes are dimmer.");
        if (r.accuracy >= 0.9f)
            tips.add ("Ready to push the tempo up 5 to 10 percent.");
    }
    if (r.weakStartBar >= 0 && mode == modePlay)
        tips.add ("Weakest spot: " + (r.weakStartBar == r.weakEndBar ? "bar " + juce::String (r.weakStartBar + 1)
                                                                    : "bars " + juce::String (r.weakStartBar + 1) + " to " + juce::String (r.weakEndBar + 1)) + ".");

    if (tips.isEmpty())
        tips.add (r.accuracy >= 0.9f ? "Clean playing. Raise the speed a notch and go again."
                                     : "A little more repetition and this will settle. Try the loop on the trickiest bars.");
    r.advice = tips.joinIntoString ("\n");
    juce::ignoreUnused (pct);
    return r;
}
} // namespace pt
