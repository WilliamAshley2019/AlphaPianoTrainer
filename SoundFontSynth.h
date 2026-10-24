#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>
#include <vector>
#include "BuiltinPiano.h"

struct tsf;

namespace pt
{
//==============================================================================
// SoundFontSynth: TinySoundFont when a .sf2 is loaded, the built-in piano
// otherwise. Loading happens off the audio thread; installing the new font is
// a pointer swap under a SpinLock the audio thread only ever try-locks.
//==============================================================================
class SoundFontSynth
{
public:
    SoundFontSynth();
    ~SoundFontSynth();

    void prepare (double sampleRate, int maxBlock);

    // Any non-audio thread. Parses the file (can take a while for big fonts).
    // Returns an opaque handle for install(), or nullptr and fills 'error'.
    struct Loaded { tsf* font = nullptr; juce::StringArray presets; juce::String name; };
    bool load (const juce::File& file, Loaded& out, juce::String& error) const;
    bool loadMemory (const void* data, size_t size, Loaded& out, juce::String& error) const;
    static void discard (Loaded& l);         // free a font that never got installed

    // Message thread: swap in a loaded font (or nullptr Loaded to go back to built-in).
    void install (Loaded&& l);

    // Message thread info.
    juce::StringArray getPresetNames() const { return presetNames; }
    juce::String getFontName() const { return fontName; }
    bool hasFont() const noexcept { return fontLoaded.load(); }

    // Audio thread.
    void setPreset (int presetIndex);          // index into getPresetNames()
    void setUseBuiltin (bool b) noexcept { forceBuiltin.store (b); }
    void handleMidi (const juce::MidiMessage& m);
    void allNotesOff();
    void render (float* left, float* right, int numSamples);   // adds

private:
    void closeFont (tsf* f);

    juce::SpinLock lock;
    tsf* font = nullptr;                       // guarded by 'lock'
    std::atomic<bool> fontLoaded { false };
    std::atomic<bool> forceBuiltin { false };
    int currentPreset = -1;

    BuiltinPiano builtin;
    std::vector<float> scratch;
    double sampleRate = 44100.0;
    int pendingPresetForNewFont = 0;

    juce::StringArray presetNames;
    juce::String fontName;
};
} // namespace pt
