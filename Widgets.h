#pragma once
#include "PluginProcessor.h"
#include "Theme.h"
#include <set>

namespace pt
{
//==============================================================================
// Small custom widgets
//==============================================================================
struct LcdData
{
    juce::String category, title, barBeat, bpm, acc, streak, chord, hostNote;
    bool operator== (const LcdData& o) const
    {
        return category == o.category && title == o.title && barBeat == o.barBeat && bpm == o.bpm && acc == o.acc
            && streak == o.streak && chord == o.chord && hostNote == o.hostNote;
    }
};

class LcdDisplay : public juce::Component
{
public:
    void set (const LcdData& d) { if (! (d == data)) { data = d; repaint(); } }
    void paint (juce::Graphics&) override;
private:
    LcdData data;
};

//------------------------------------------------------------------------------
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::ParameterID&, const juce::String& caption);
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
private:
    juce::Slider slider;
    juce::SliderParameterAttachment attach;
    juce::String caption;
};

//------------------------------------------------------------------------------
class Segmented : public juce::Component
{
public:
    Segmented (juce::AudioProcessorValueTreeState&, const juce::ParameterID&, juce::StringArray labels);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    juce::RangedAudioParameter* param;
    juce::ParameterAttachment attach;
    juce::StringArray labels;
    int index = 0;
    juce::SharedResourcePointer<ui::Textures> tex;
};

//------------------------------------------------------------------------------
class IconButton : public juce::Button
{
public:
    enum Icon { play, pause, stop, loop, gear, folder, restart };
    explicit IconButton (Icon i) : juce::Button ({}), icon (i) {}
    void setIcon (Icon i) { icon = i; repaint(); }
    void paintButton (juce::Graphics&, bool hi, bool down) override;
private:
    Icon icon;
    juce::SharedResourcePointer<ui::Textures> tex;
};

//------------------------------------------------------------------------------
class TimingMeter : public juce::Component
{
public:
    void push (float errMs, HitEvent::Kind k);
    void clear() { n = 0; repaint(); }
    void paint (juce::Graphics&) override;
private:
    struct Tick { float ms; HitEvent::Kind kind; double t; };
    Tick ticks[24] {};
    int n = 0, head = 0;
};

//------------------------------------------------------------------------------
class OverviewStrip : public juce::Component
{
public:
    explicit OverviewStrip (PianoTrainerProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    double beatAt (float x) const;
private:
    PianoTrainerProcessor& proc;
    double dragAnchor = 0.0;
    bool selecting = false;
};

//------------------------------------------------------------------------------
class LessonBrowser : public juce::Component, private juce::ListBoxModel, private juce::ChangeListener
{
public:
    explicit LessonBrowser (PianoTrainerProcessor&);
    ~LessonBrowser() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();
    int getNumRows() override { return (int) rows.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void deleteKeyPressed (int row) override;
    std::function<void()> onLessonChosen;
    void chooseFile();
    void chooseFolder();

private:
    struct Row { bool header; juce::String text; int entry; bool open; };
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    PianoTrainerProcessor& proc;
    juce::ListBox list { {}, this };
    juce::TextButton addFile { "+ MIDI FILE" }, addFolder { "+ FOLDER" };
    std::vector<LessonEntry> entries;
    std::vector<Row> rows;
    std::set<juce::String> collapsed;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> cardArea;
    juce::SharedResourcePointer<ui::Textures> tex;
};

//------------------------------------------------------------------------------
class ResultsPanel : public juce::Component
{
public:
    explicit ResultsPanel (PianoTrainerProcessor&);
    void show (const RunResult&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {}      // swallow clicks behind the card
    std::function<void()> onRetry, onClose;
    std::function<void (int, int)> onLoopBars;
private:
    PianoTrainerProcessor& proc;
    RunResult r;
    juce::TextButton retry { "TRY AGAIN" }, loopWeak { "LOOP WEAK BARS" }, close { "CLOSE" };
    juce::Rectangle<float> card;
    juce::SharedResourcePointer<ui::Textures> tex;
};

//------------------------------------------------------------------------------
class SettingsPanel : public juce::Component
{
public:
    explicit SettingsPanel (PianoTrainerProcessor&);
    ~SettingsPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {}
    void refreshInstruments();
    std::function<void()> onClose;
private:
    using APVTS = juce::AudioProcessorValueTreeState;
    PianoTrainerProcessor& proc;
    struct Row { juce::String text; juce::Component* control; bool header; };
    std::vector<Row> rows;
    void addRow (const juce::String& name, juce::Component& c);
    void addHeader (const juce::String& name) { rows.push_back ({ name, nullptr, true }); }

    juce::ComboBox timing, scaleType, scaleRoot, countIn, soundSource, instrument;
    juce::ToggleButton showScale { "Scale overlay" }, labels { "Note names" }, fingers { "Fingering" }, followHost { "Follow DAW tempo" },
                       syncTransport { "Follow DAW transport" }, autoSpeed { "Auto speed-up" }, builtin { "Built-in piano only" };
    juce::Slider transpose, lookahead;
    juce::TextButton closeBtn { "DONE" };
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> cAtt;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> bAtt;
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sAtt;
    std::unique_ptr<juce::ParameterAttachment> presetAtt;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Array<juce::File> fonts;
    juce::SharedResourcePointer<ui::Textures> tex;
};
} // namespace pt
