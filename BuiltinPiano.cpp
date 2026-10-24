#include "BuiltinPiano.h"
#include <cmath>

namespace pt
{
void BuiltinPiano::prepare (double sampleRate)
{
    sr = sampleRate;
    allOff();
}

void BuiltinPiano::allOff()
{
    for (auto& v : voices) v.active = false;
    sustain = false;
}

void BuiltinPiano::setSustain (bool down)
{
    sustain = down;
    if (! down)
        for (auto& v : voices)
            if (v.active && v.sustained && ! v.keyDown) { v.sustained = false; startRelease (v); }
}

void BuiltinPiano::startRelease (Voice& v) noexcept
{
    v.releasing = true;
    // ~0.35 s release for the body of the note, shorter for high notes
    const double t = juce::jmap ((double) v.pitch, 21.0, 108.0, 0.6, 0.18);
    v.releaseMul = std::exp (-6.9 / (t * sr));
}

void BuiltinPiano::noteOn (int pitch, int velocity)
{
    // free voice, or steal the oldest
    Voice* slot = nullptr;
    for (auto& v : voices) if (! v.active) { slot = &v; break; }
    if (slot == nullptr)
    {
        slot = &voices[0];
        for (auto& v : voices) if (v.age < slot->age) slot = &v;
    }

    // retrigger: kill an existing voice on the same pitch quickly
    for (auto& v : voices)
        if (v.active && v.pitch == pitch && &v != slot) startRelease (v);

    auto& v = *slot;
    v = Voice();
    v.active = true; v.keyDown = true;
    v.pitch = pitch;
    v.age = ++clock;

    const double f0   = 440.0 * std::pow (2.0, (pitch - 69) / 12.0);
    const double vel  = juce::jlimit (1, 127, velocity) / 127.0;
    const double B    = 0.00025 * std::pow (2.0, (pitch - 60) / 18.0);       // inharmonicity grows with pitch
    const double t60  = juce::jmap ((double) pitch, 21.0, 108.0, 11.0, 1.6);  // fundamental decay (s)
    const double bright = 0.35 + 0.65 * vel;                                  // harder hit -> more upper partials

    int n = 0;
    for (int k = 1; k <= maxPartials; ++k)
    {
        const double fk = k * f0 * std::sqrt (1.0 + B * k * k);
        if (fk > sr * 0.45) break;

        const double w = 2.0 * juce::MathConstants<double>::pi * fk / sr;
        v.c[n] = std::cos (w); v.s[n] = std::sin (w);
        v.re[n] = std::sin ((double) k * 0.7);     // varied start phases
        v.im[n] = std::cos ((double) k * 0.7);

        // spectral tilt: strong fundamental, gentle roll-off, velocity-dependent
        double a = std::pow (1.0 / k, 0.9 + (1.0 - bright) * 1.6);
        if (k == 2) a *= 1.15;                      // piano's characteristic strong 2nd
        if (k == 1 && pitch > 84) a *= 1.3;
        v.amp[n] = a * 0.22;

        const double t = t60 / (1.0 + 0.55 * (k - 1)) ;   // upper partials die first
        v.decay[n] = std::exp (-6.9 / (t * sr));
        ++n;
    }
    v.nPartials = n;

    v.hammer = 0.35 * vel * vel;
    v.hammerDecay = std::exp (-1.0 / (0.012 * sr));
    v.lpCoef = juce::jlimit (0.02, 0.6, 0.05 + vel * 0.35 - (pitch - 60) * 0.0015);

    const float pan = juce::jlimit (-0.6f, 0.6f, (pitch - 64) * 0.012f);
    const float g = (float) (0.35 + 0.65 * vel);
    v.gainL = g * (1.0f - 0.5f * juce::jmax (0.f, pan)) * 0.8f;
    v.gainR = g * (1.0f + 0.5f * juce::jmin (0.f, pan)) * 0.8f;
}

void BuiltinPiano::noteOff (int pitch)
{
    for (auto& v : voices)
        if (v.active && v.keyDown && v.pitch == pitch)
        {
            v.keyDown = false;
            if (sustain) v.sustained = true;
            else startRelease (v);
        }
}

void BuiltinPiano::render (float* left, float* right, int numSamples)
{
    for (auto& v : voices)
    {
        if (! v.active) continue;

        for (int i = 0; i < numSamples; ++i)
        {
            double sum = 0.0;
            for (int k = 0; k < v.nPartials; ++k)
            {
                const double r = v.re[k] * v.c[k] - v.im[k] * v.s[k];
                const double m = v.re[k] * v.s[k] + v.im[k] * v.c[k];
                v.re[k] = r; v.im[k] = m;
                sum += m * v.amp[k];
                v.amp[k] *= v.decay[k];
            }

            if (v.hammer > 1e-5)
            {
                rng = rng * 1664525u + 1013904223u;
                const double nz = ((double) (rng >> 8) / 8388608.0 - 1.0) * v.hammer;
                v.lp += v.lpCoef * (nz - v.lp);
                sum += v.lp;
                v.hammer *= v.hammerDecay;
            }

            if (v.releasing)
            {
                v.relGain *= v.releaseMul;
                sum *= v.relGain;
            }

            left[i]  += (float) (sum * v.gainL);
            right[i] += (float) (sum * v.gainR);
        }

        // keep the oscillators on the unit circle
        for (int k = 0; k < v.nPartials; ++k)
        {
            const double mag = std::sqrt (v.re[k] * v.re[k] + v.im[k] * v.im[k]);
            if (mag > 0.0) { v.re[k] /= mag; v.im[k] /= mag; }
        }

        if (v.relGain < 0.0005 || v.amp[0] < 1e-6) v.active = false;
    }
}
} // namespace pt
