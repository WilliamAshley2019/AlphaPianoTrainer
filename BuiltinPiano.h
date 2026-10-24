#pragma once
#include <juce_core/juce_core.h>
#include <array>

namespace pt
{
//==============================================================================
// A small additive piano used when no SoundFont is loaded, so the plugin makes
// a decent sound out of the box. Inharmonic partials, pitch-dependent decay,
// velocity-dependent brightness, a hammer thump, sustain pedal.
// Everything is pre-allocated: safe to use on the audio thread.
//==============================================================================
class BuiltinPiano
{
public:
    void prepare (double sampleRate);
    void noteOn (int pitch, int velocity);
    void noteOff (int pitch);
    void setSustain (bool down);
    void allOff();

    // Adds into the buffers.
    void render (float* left, float* right, int numSamples);

private:
    static constexpr int maxPartials = 12;
    static constexpr int numVoices   = 32;

    struct Voice
    {
        bool   active = false, keyDown = false, sustained = false;
        int    pitch = 0;
        std::uint64_t age = 0;
        int    nPartials = 0;
        double re[maxPartials], im[maxPartials], c[maxPartials], s[maxPartials];
        double amp[maxPartials], decay[maxPartials];
        double releaseMul = 0.9998;
        double hammer = 0.0, hammerDecay = 0.99, lp = 0.0, lpCoef = 0.2;
        float  gainL = 0.7f, gainR = 0.7f;
        double envFloor = 1.0;
        bool   releasing = false;
        double relGain = 1.0;
    };

    void startRelease (Voice& v) noexcept;

    std::array<Voice, numVoices> voices;
    double sr = 44100.0;
    bool sustain = false;
    std::uint64_t clock = 0;
    std::uint32_t rng = 22222u;
};
} // namespace pt
