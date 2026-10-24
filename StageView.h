#pragma once
#include "PluginProcessor.h"
#include "Theme.h"
#include <functional>

namespace pt
{
//==============================================================================
// Key geometry shared by the falling-notes roll and the keyboard, so every
// note bar sits exactly above its key (white keys wide, black keys narrow).
//==============================================================================
struct KeyMap
{
    int lo = 36, hi = 96, numWhite = 36;
    int whiteIdx[128] {};

    void setRange (int a, int b);
    bool contains (int p) const noexcept { return p >= lo && p <= hi; }
    // x / width in "white key units"
    void unitSpan (int pitch, float& x, float& w) const noexcept;
    static void autoRange (const Lesson& l, int& lo, int& hi);
};

class StageView : public juce::Component
{
public:
    explicit StageView (PianoTrainerProcessor&);

    void update (double dtSeconds);          // 60 Hz tick from the editor
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    double displayPos() const;
    std::function<void (const HitEvent&)> onHit;

private:
    struct Particle { float x, y, vx, vy, life, size; juce::Colour c; };

    void rebuildKeyMapIfNeeded();
    void paintRoll (juce::Graphics&, juce::Rectangle<float> area, const Session&, double pos);
    void paintKeyboard (juce::Graphics&, juce::Rectangle<float> area);
    juce::Rectangle<float> keyRect (int pitch, juce::Rectangle<float> kb) const;
    juce::Rectangle<float> laneRect (int pitch, juce::Rectangle<float> roll) const;
    int pitchAt (juce::Point<float> p) const;
    void spawn (int pitch, juce::Colour c, int count, float speed);
    int scaleRootPc (const Session&) const;
    bool inScale (int pitch, int rootPc, int type) const;

    PianoTrainerProcessor& proc;
    juce::SharedResourcePointer<ui::Textures> tex;
    KeyMap km;
    const Session* mapSession = nullptr;
    float kbHeight = 100.f;

    std::vector<Particle> particles;
    float flash[128] {};
    juce::Colour flashColour[128];
    std::uint8_t hint[128] {};         // bit0 right, bit1 left, bit2 backing
    std::uint64_t heldLo = 0, heldHi = 0;
    double maxLen = 4.0;
    int mouseNote = -1;
    float pulse = 0.f;
    double lastPos = 0.0;

    juce::AudioProcessorValueTreeState& apvts;
    std::atomic<float> *pLook, *pLabels, *pFingers, *pShowScale, *pScaleType, *pScaleRoot, *pHand, *pMode, *pLoop;
};
} // namespace pt
