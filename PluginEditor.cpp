#include "PluginEditor.h"
#include "Theory.h"

namespace pt
{

//==============================================================================
void ChassisBackground::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    ui::brushedMetal (g, b, 0.f, true, *tex);

    for (auto& p : plates) ui::brushedMetal (g, p, 8.f, false, *tex);
    for (auto& w : wells)  ui::glassWell (g, w, 6.f);

    // engraved wordmark, bottom right, set on the golden section of the base
    g.setFont (ui::caps (10.f));
    ui::engravedText (g, "ALPHAAUDIO", b.withTrimmedRight (18.f).removeFromBottom (18.f).withTrimmedBottom (4.f),
                  juce::Justification::centredRight, ui::caps (10.f), true);
    ui::engravedText (g, "PIANO TRAINER", b.withTrimmedRight (110.f).removeFromBottom (18.f).withTrimmedBottom (4.f),
                  juce::Justification::centredRight, ui::caps (10.f), false);
}

//==============================================================================
PianoTrainerEditor::PianoTrainerEditor (PianoTrainerProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p),
      stage (p), overview (p), browser (p),
      modeSel (p.apvts, pid::mode, { "Play", "Wait", "Watch" }),
      handSel (p.apvts, pid::hand, { "Both", "Right", "Left" }),
      kSpeed (p.apvts, pid::speed, "Speed"), kBacking (p.apvts, pid::backing, "Backing"),
      kClick (p.apvts, pid::metroVol, "Click"), kVolume (p.apvts, pid::volume, "Volume"),
      results (p), settings (p)
{
    setLookAndFeel (&laf);

    // QWERTY piano (FL Studio style): home row = white keys, row above = black keys
    const char* white = "asdfghjkl;'";
    const int whiteSemi[] = { 0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17 };
    for (int i = 0; i < 11; ++i) keyNotes.push_back ({ (int) white[i], whiteSemi[i] });
    const char* black = "wetyuop";
    const int blackSemi[] = { 1, 3, 6, 8, 10, 13, 15 };
    for (int i = 0; i < 7; ++i) keyNotes.push_back ({ (int) black[i], blackSemi[i] });

    addAndMakeVisible (chassis);
    for (auto* c : std::initializer_list<juce::Component*> { &lcd, &stage, &overview, &timingMeter, &browser,
                                                             &restartBtn, &playBtn, &stopBtn, &loopBtn, &gearBtn, &clickBtn,
                                                             &modeSel, &handSel, &kSpeed, &kBacking, &kClick, &kVolume })
        addAndMakeVisible (*c);
    addChildComponent (results);
    addChildComponent (settings);

    loopAtt  = std::make_unique<juce::ButtonParameterAttachment> (*p.apvts.getParameter (pid::loop.getParamID()), loopBtn);
    clickAtt = std::make_unique<juce::ButtonParameterAttachment> (*p.apvts.getParameter (pid::metronome.getParamID()), clickBtn);
    loopBtn.setClickingTogglesState (true);
    clickBtn.setClickingTogglesState (true);

    restartBtn.onClick = [this] { results.setVisible (false); proc.engine.cmdRestart(); };
    playBtn.onClick    = [this] { results.setVisible (false); proc.engine.cmdToggle(); };
    stopBtn.onClick    = [this] { results.setVisible (false); proc.engine.cmdStop(); };
    gearBtn.setClickingTogglesState (false);
    gearBtn.onClick    = [this] { settings.refreshInstruments(); settings.setVisible (! settings.isVisible()); gearBtn.setToggleState (settings.isVisible(), juce::dontSendNotification); if (settings.isVisible()) settings.toFront (false); };
    settings.onClose   = [this] { gearBtn.setToggleState (false, juce::dontSendNotification); };

    results.onRetry = [this] { proc.engine.cmdRestart(); };
    results.onLoopBars = [this] (int a, int b)
    {
        const int bpb = juce::jmax (1, proc.getSession() ? proc.getSession()->lesson.beatsPerBar : 4);
        proc.setLoop (a * bpb, (b + 1) * bpb);
        if (auto* lp = proc.apvts.getParameter (pid::loop.getParamID())) lp->setValueNotifyingHost (1.f);
        proc.engine.cmdRestart();
    };

    stage.onHit = [this] (const HitEvent& h)
    {
        if (h.kind == HitEvent::hitPerfect || h.kind == HitEvent::hitGood || h.kind == HitEvent::hitOffDynamics)
            if ((int) proc.apvts.getRawParameterValue (pid::mode.getParamID())->load() == modePlay)
                timingMeter.push (h.errMs, h.kind);
    };

    browser.onLessonChosen = [this] { grabKeyboardFocus(); };

    for (auto* b : { (juce::Button*) &restartBtn, (juce::Button*) &playBtn, (juce::Button*) &stopBtn, (juce::Button*) &loopBtn, (juce::Button*) &gearBtn })
        b->setWantsKeyboardFocus (false);
    clickBtn.setWantsKeyboardFocus (false);

    proc.addChangeListener (this);
    seenResult = proc.getResultSerial();
    seenLesson = proc.getLessonId();
    setWantsKeyboardFocus (true);

    setResizable (true, true);
    setResizeLimits (860, 531, 2000, 1236);
    setSize (1100, 680);                       // 1.618 : 1, constructed last so resized() sees every child

    lastMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
    updateLcd();
}

PianoTrainerEditor::~PianoTrainerEditor()
{
    stopTimer();
    proc.removeChangeListener (this);
    for (auto n : keysDown) juce::ignoreUnused (n);
    setLookAndFeel (nullptr);
}

//==============================================================================
void PianoTrainerEditor::resized()
{
    auto b = getLocalBounds();
    chassis.setBounds (b);

    const int m = 12;
    auto area = b.reduced (m);
    area.removeFromBottom (14);                                   // room for the wordmark

    const int rightW = (int) std::round (area.getWidth() * ui::invPhi * ui::invPhi);   // 38.2 %
    auto right = area.removeFromRight (rightW);
    area.removeFromRight (m);
    auto left = area;

    // right column: browser over the knob plate
    knobPlate = right.removeFromBottom (juce::jlimit (96, 130, (int) (right.getHeight() * 0.2f)));
    right.removeFromBottom (m);
    browser.setBounds (right);

    {
        auto k = knobPlate.reduced (10, 8);
        const int w = k.getWidth() / 4;
        for (auto* kn : { &kSpeed, &kBacking, &kClick, &kVolume }) kn->setBounds (k.removeFromLeft (w).reduced (2, 0));
    }

    // left column: display, transport, stage, overview
    lcd.setBounds (left.removeFromTop (juce::jlimit (68, 96, (int) (left.getHeight() * 0.135f))));
    left.removeFromTop (8);

    auto transport = left.removeFromTop (36);
    {
        const int bw = 44;
        restartBtn.setBounds (transport.removeFromLeft (bw)); transport.removeFromLeft (4);
        playBtn.setBounds (transport.removeFromLeft (bw + 12)); transport.removeFromLeft (4);
        stopBtn.setBounds (transport.removeFromLeft (bw)); transport.removeFromLeft (4);
        loopBtn.setBounds (transport.removeFromLeft (bw)); transport.removeFromLeft (14);
        gearBtn.setBounds (transport.removeFromRight (bw)); transport.removeFromRight (4);
        clickBtn.setBounds (transport.removeFromRight (74)); transport.removeFromRight (14);
        const int segW = (int) (transport.getWidth() * ui::invPhi * 0.5f);
        modeSel.setBounds (transport.removeFromLeft (juce::jmax (150, segW))); transport.removeFromLeft (10);
        handSel.setBounds (transport.removeFromLeft (juce::jmax (150, segW)));
    }
    left.removeFromTop (8);

    auto bottom = left.removeFromBottom (46);
    left.removeFromBottom (8);
    stage.setBounds (left);

    timingMeter.setBounds (bottom.removeFromRight ((int) (bottom.getWidth() * ui::invPhi * ui::invPhi * 0.8f)));
    bottom.removeFromRight (8);
    overview.setBounds (bottom);

    results.setBounds (stage.getBounds());
    settings.setBounds (b);

    chassis.wells = { stage.getBounds().toFloat(), browser.getBounds().toFloat(), overview.getBounds().toFloat() };
    chassis.plates = { knobPlate.toFloat() };
    chassis.wells.erase (chassis.wells.begin());                  // stage / overview paint their own glass
    chassis.wells.erase (chassis.wells.begin() + 1);
    chassis.repaint();
}

//==============================================================================
void PianoTrainerEditor::updateLcd()
{
    LcdData d;
    const auto* s = proc.getSession();
    if (s != nullptr)
    {
        d.category = s->lesson.category;
        d.title = s->lesson.title;
        const double pos = stage.displayPos();
        const int bpb = juce::jmax (1, s->lesson.beatsPerBar);
        if (pos < 0.0) d.barBeat = "-" + juce::String ((int) std::ceil (-pos));
        else d.barBeat = juce::String ((int) (pos / bpb) + 1).paddedLeft ('0', 3) + ":" + juce::String ((int) std::fmod (pos, (double) bpb) + 1);
    }
    d.bpm = juce::String ((int) std::lround (proc.getEffectiveBpm())).paddedLeft ('0', 3);
    const auto& st = proc.engine.stats;
    d.acc = st.hits() + st.missed.load() > 0 ? juce::String ((int) std::lround (st.accuracy() * 100.f)).paddedLeft ('0', 3) : "---";
    d.streak = juce::String (st.streak.load()).paddedLeft ('0', 3);
    d.chord = theory::detectChord (proc.engine.heldLo.load(), proc.engine.heldHi.load());

    const auto status = proc.getStatusText();
    if (status.isNotEmpty()) d.hostNote = status;
    else if (proc.apvts.getRawParameterValue (pid::followHost.getParamID())->load() > 0.5f)
        d.hostNote = proc.hostBpm.load() > 0 ? "Following DAW tempo " + juce::String (proc.hostBpm.load(), 1) : "Waiting for DAW tempo";
    else
        d.hostNote = proc.getSynth().hasFont() ? "SoundFont: " + proc.getSynth().getFontName() : "Built-in piano";
    lcd.set (d);
}

void PianoTrainerEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = juce::jlimit (0.001, 0.1, (now - lastMs) * 0.001);
    lastMs = now;

    stage.update (dt);
    if ((++tick & 3) == 0) overview.repaint();
    updateLcd();

    const bool playing = proc.engine.playing.load();
    playBtn.setIcon (playing ? IconButton::pause : IconButton::play);
    playBtn.setToggleState (playing, juce::dontSendNotification);
    if (playing && results.isVisible()) results.setVisible (false);

    if (const int r = proc.getResultSerial(); r != seenResult)
    {
        seenResult = r;
        results.show (proc.getLastResult());
    }
}

void PianoTrainerEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (proc.getLessonId() != seenLesson)
    {
        seenLesson = proc.getLessonId();
        timingMeter.clear();
        results.setVisible (false);
    }
    settings.refreshInstruments();
}

//==============================================================================
// Keyboard & drag-and-drop
//==============================================================================
bool PianoTrainerEditor::keyPressed (const juce::KeyPress& k)
{
    const auto c = k.getTextCharacter();
    if (k == juce::KeyPress::spaceKey) { results.setVisible (false); proc.engine.cmdToggle(); return true; }
    if (k == juce::KeyPress::escapeKey) { results.setVisible (false); proc.engine.cmdStop(); return true; }
    if (k == juce::KeyPress::returnKey) { results.setVisible (false); proc.engine.cmdRestart(); return true; }

    auto nudge = [this] (const juce::ParameterID& id, float delta)
    {
        if (auto* p = proc.apvts.getParameter (id.getParamID()))
            p->setValueNotifyingHost (juce::jlimit (0.f, 1.f, p->getValue() + delta));
    };
    if (c == '[') { nudge (pid::speed, -0.04f); return true; }
    if (c == ']') { nudge (pid::speed,  0.04f); return true; }
    if (c == 'z' || c == 'Z') { keyOctave = juce::jmax (1, keyOctave - 1); return true; }
    if (c == 'x' || c == 'X') { keyOctave = juce::jmin (7, keyOctave + 1); return true; }

    for (auto& kn : keyNotes)
        if (juce::CharacterFunctions::toLowerCase (c) == (juce::juce_wchar) kn.key)
        {
            if (keysDown.insert (kn.key).second)
                proc.virtualNote ((keyOctave + 1) * 12 + kn.semi, 90, true);
            return true;
        }
    return false;
}

bool PianoTrainerEditor::keyStateChanged (bool)
{
    bool handled = false;
    for (auto it = keysDown.begin(); it != keysDown.end();)
    {
        if (! juce::KeyPress::isKeyCurrentlyDown (*it))
        {
            for (auto& kn : keyNotes)
                if (kn.key == *it) proc.virtualNote ((keyOctave + 1) * 12 + kn.semi, 64, false);
            it = keysDown.erase (it);
            handled = true;
        }
        else ++it;
    }
    return handled;
}

bool PianoTrainerEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi") || f.endsWithIgnoreCase (".sf2")) return true;
    return false;
}

void PianoTrainerEditor::filesDropped (const juce::StringArray& files, int, int)
{
    bool first = true;
    for (auto& f : files)
    {
        if (f.endsWithIgnoreCase (".sf2")) proc.loadSoundFont (juce::File (f));
        else if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi")) { proc.addMidiFile (juce::File (f), first); first = false; }
    }
}
} // namespace pt
