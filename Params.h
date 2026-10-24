#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace pt::pid
{
// 'inline const' (not 'static const') so every TU shares one object.
inline const juce::ParameterID speed         { "speed", 1 };
inline const juce::ParameterID followHost    { "followHost", 1 };
inline const juce::ParameterID syncTransport { "syncTransport", 1 };
inline const juce::ParameterID mode          { "mode", 1 };
inline const juce::ParameterID hand          { "hand", 1 };
inline const juce::ParameterID loop          { "loop", 1 };
inline const juce::ParameterID metronome     { "metronome", 1 };
inline const juce::ParameterID countIn       { "countIn", 1 };
inline const juce::ParameterID transpose     { "transpose", 1 };
inline const juce::ParameterID backing       { "backing", 1 };
inline const juce::ParameterID volume        { "volume", 1 };
inline const juce::ParameterID metroVol      { "metroVol", 1 };
inline const juce::ParameterID timing        { "timing", 1 };
inline const juce::ParameterID scaleType     { "scaleType", 1 };
inline const juce::ParameterID scaleRoot     { "scaleRoot", 1 };
inline const juce::ParameterID showScale     { "showScale", 1 };
inline const juce::ParameterID builtinOnly   { "builtinOnly", 1 };
inline const juce::ParameterID preset        { "preset", 1 };
inline const juce::ParameterID lookahead     { "lookahead", 1 };
inline const juce::ParameterID labels        { "labels", 1 };
inline const juce::ParameterID fingers       { "fingers", 1 };
inline const juce::ParameterID autoSpeed     { "autoSpeed", 1 };
} // namespace pt::pid
