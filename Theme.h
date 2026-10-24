#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// Visual language
//   * Chassis: anodised graphite aluminium with a fine horizontal brush grain
//     (Jacob Jensen / B&O flat metal plates, Ive's unibody)
//   * Display: smoked glass over a warm-amber seven-segment LCD, with ghost
//     segments (Dieter Rams / Braun calculators: "less, but better")
//   * Controls: knurled aluminium caps ringed by a single LED arc, flat keys
//     that light a tiny LED, nothing else
//   * Proportions follow the golden ratio (see ui::phi and the editor layout)
//==============================================================================
namespace pt::ui
{
constexpr float phi = 1.6180339887f;
constexpr float invPhi = 0.6180339887f;

namespace col
{
inline const juce::Colour bg         { 0xff0d0e10 };
inline const juce::Colour metalHi    { 0xff3b3d43 };
inline const juce::Colour metalMid   { 0xff2a2c30 };
inline const juce::Colour metalLo    { 0xff1c1d21 };
inline const juce::Colour edgeLight  { 0xff5d6068 };
inline const juce::Colour edgeDark   { 0xff08090a };
inline const juce::Colour glass      { 0xff090b0f };
inline const juce::Colour glassHi    { 0xff151a23 };
inline const juce::Colour amber      { 0xffffb347 };
inline const juce::Colour amberDim   { 0xff6f4a1c };
inline const juce::Colour amberGhost { 0xff221a0d };
inline const juce::Colour cyan       { 0xff49dad2 };
inline const juce::Colour text       { 0xffdadce1 };
inline const juce::Colour textDim    { 0xff8d919a };
inline const juce::Colour textFaint  { 0xff5a5d66 };
inline const juce::Colour perfect    { 0xff9dffb8 };
inline const juce::Colour good       { 0xffe9f77e };
inline const juce::Colour offDyn     { 0xffff8fd0 };
inline const juce::Colour missed     { 0xff80404a };
inline const juce::Colour wrong      { 0xffff4d5e };
inline const juce::Colour rightHand  = amber;
inline const juce::Colour leftHand   = cyan;
} // namespace col

// Typography ---------------------------------------------------------------------
juce::Font sans (float height, bool bold = false);
juce::Font caps (float height);                 // tracked-out small caps feel for labels

// Painting helpers ----------------------------------------------------------------
class Textures                                    // shared, created on first use
{
public:
    Textures();
    juce::Image brushed;                         // tileable fine grain
};

void brushedMetal (juce::Graphics&, juce::Rectangle<float>, float corner, bool raised, const Textures&);
void glassWell    (juce::Graphics&, juce::Rectangle<float>, float corner);          // recessed smoked glass
void glow         (juce::Graphics&, juce::Rectangle<float>, juce::Colour, float spread, float alpha);
void engravedText (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification, const juce::Font&, bool lit = false);
void keyPlate     (juce::Graphics&, juce::Rectangle<float>, bool down, bool on, const Textures&);   // flat aluminium key with a tiny LED
float sevenSegmentWidthPerHeight (const juce::String&);   // total width / height of the string
void sevenSegment (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Colour on, juce::Colour off, bool ghost = true);

//==============================================================================
class AlumLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AlumLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return sans (13.f); }
    juce::Font getPopupMenuFont() override { return sans (14.f); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>&, bool separator, bool active, bool highlighted,
                            bool ticked, bool hasSubMenu, const juce::String& text, const juce::String& shortcut,
                            const juce::Drawable*, const juce::Colour*) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool down) override;
    int getDefaultScrollbarWidth() override { return 8; }
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Font getLabelFont (juce::Label&) override { return sans (13.f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return caps (11.f); }

    const Textures& textures() const { return *tex; }

private:
    juce::SharedResourcePointer<Textures> tex;
};
} // namespace pt::ui
