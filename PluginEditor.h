#pragma once
#include "PluginProcessor.h"
#include "Theme.h"
#include "StageView.h"
#include "Widgets.h"

namespace pt
{
// Static metal plate behind everything. Cached as an image, so it costs nothing per frame.
class ChassisBackground : public juce::Component
{
public:
    ChassisBackground() { setInterceptsMouseClicks (false, false); setBufferedToImage (true); }
    void paint (juce::Graphics&) override;
    std::vector<juce::Rectangle<float>> wells, plates;
private:
    juce::SharedResourcePointer<ui::Textures> tex;
};

class PianoTrainerEditor final : public juce::AudioProcessorEditor,
                                 public juce::FileDragAndDropTarget,
                                 private juce::Timer,
                                 private juce::ChangeListener
{
public:
    explicit PianoTrainerEditor (PianoTrainerProcessor&);
    ~PianoTrainerEditor() override;

    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateLcd();

    PianoTrainerProcessor& proc;
    ui::AlumLookAndFeel laf;

    ChassisBackground chassis;
    LcdDisplay lcd;
    StageView stage;
    OverviewStrip overview;
    TimingMeter timingMeter;
    LessonBrowser browser;

    IconButton restartBtn { IconButton::restart }, playBtn { IconButton::play }, stopBtn { IconButton::stop },
               loopBtn { IconButton::loop }, gearBtn { IconButton::gear };
    juce::ToggleButton clickBtn { "Click" };
    Segmented modeSel, handSel;
    Knob kSpeed, kBacking, kClick, kVolume;

    ResultsPanel results;
    SettingsPanel settings;

    std::unique_ptr<juce::ButtonParameterAttachment> loopAtt, clickAtt;
    juce::Rectangle<int> knobPlate;

    int seenResult = 0, tick = 0;
    juce::String seenLesson;
    double lastMs = 0.0;

    // computer-keyboard piano
    struct KeyNote { int key; int semi; };
    std::vector<KeyNote> keyNotes;
    std::set<int> keysDown;
    int keyOctave = 4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoTrainerEditor)
};
} // namespace pt
