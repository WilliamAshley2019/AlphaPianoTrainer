#pragma once
#include "Lesson.h"

namespace pt::library
{
struct Info
{
    juce::String id, title, category, blurb;
    int level = 1;           // 1 beginner .. 4 advanced
    bool dynamics = false;   // lesson grades touch (velocity) as well as timing/pitch
};

// Every built-in lesson, in curriculum order.
const std::vector<Info>& all();

// Builds a lesson by id ("b:..."). Returns false if the id is unknown.
bool build (const juce::String& id, Lesson& out);

// Id of the "no notes, just play" lesson.
inline const char* freePlayId() { return "b:free"; }
} // namespace pt::library
