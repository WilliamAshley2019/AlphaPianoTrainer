#include "Widgets.h"
#include "Theory.h"
#include <cmath>

namespace pt
{

//==============================================================================
// LCD
//==============================================================================
void LcdDisplay::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    ui::glassWell (g, b, 8.f);

    // warm backlight bloom
    juce::ColourGradient bloom (ui::col::amber.withAlpha (0.07f), b.getCentreX(), b.getCentreY(), juce::Colours::transparentBlack, b.getX(), b.getY(), true);
    g.setGradientFill (bloom);
    g.fillRoundedRectangle (b.reduced (1.f), 8.f);

    const auto inner = b.reduced (14.f, 9.f);
    auto left = inner.withWidth (inner.getWidth() * ui::invPhi * ui::invPhi);        // 0.382
    auto right = inner.withTrimmedLeft (left.getWidth() + 14.f);

    // title block
    g.setFont (ui::caps (9.5f));
    g.setColour (ui::col::amberDim.brighter (0.4f));
    g.drawText (data.category.toUpperCase(), left.removeFromTop (13.f), juce::Justification::centredLeft, true);
    g.setFont (ui::sans (17.f, true));
    g.setColour (ui::col::amber);
    g.drawFittedText (data.title, left.toNearestInt().removeFromTop (44), juce::Justification::topLeft, 2, 0.85f);
    g.setFont (ui::sans (11.f));
    g.setColour (ui::col::textDim);
    g.drawText (data.hostNote, left.removeFromBottom (14.f), juce::Justification::centredLeft, true);

    // numeric readouts
    struct Cell { const char* caption; juce::String value; float weight; bool seg; };
    const Cell cells[] = { { "BAR : BEAT", data.barBeat, 1.35f, true }, { "TEMPO", data.bpm, 1.0f, true },
                           { "ACCURACY", data.acc, 1.0f, true }, { "STREAK", data.streak, 1.0f, true },
                           { "CHORD", data.chord, 1.25f, false } };
    float tw = 0.f; for (auto& c : cells) tw += c.weight;
    float x = right.getX();
    const float segH = right.getHeight() * 0.52f;
    float commonH = segH;                                   // one digit height across the whole row
    for (auto& c : cells)
        if (c.seg) commonH = juce::jmin (commonH, (right.getWidth() * c.weight / tw - 12.f) / ui::sevenSegmentWidthPerHeight (c.value));
    for (auto& c : cells)
    {
        const float w = right.getWidth() * c.weight / tw;
        const auto cell = juce::Rectangle<float> (x, right.getY(), w - 8.f, right.getHeight());
        const auto digits = cell.withHeight (segH).withY (cell.getY() + 2.f);
        if (c.seg)
        {
            ui::sevenSegment (g, c.value, digits.withTrimmedTop (digits.getHeight() - commonH).reduced (2.f, 0.f), ui::col::amber, ui::col::amberGhost, true);
        }
        else
        {
            g.setFont (ui::sans (juce::jmin (26.f, segH * 0.9f), true));
            g.setColour (c.value.isEmpty() ? ui::col::amberGhost.brighter (0.3f) : ui::col::amber);
            g.drawFittedText (c.value.isEmpty() ? "--" : c.value, digits.toNearestInt(), juce::Justification::centredRight, 1, 0.6f);
        }
        g.setFont (ui::caps (9.f));
        g.setColour (ui::col::textFaint);
        g.drawText (c.caption, cell.withTop (digits.getBottom() + 5.f), juce::Justification::centredRight, false);
        x += w;
    }

    // scan lines and a diagonal glass reflection
    g.setColour (juce::Colours::black.withAlpha (0.10f));
    for (float y = b.getY() + 2.f; y < b.getBottom(); y += 3.f) g.fillRect (b.getX() + 2.f, y, b.getWidth() - 4.f, 1.f);
    juce::ColourGradient refl (juce::Colours::white.withAlpha (0.07f), b.getX(), b.getY(), juce::Colours::transparentWhite, b.getX() + b.getWidth() * 0.5f, b.getBottom(), false);
    g.setGradientFill (refl);
    g.fillRoundedRectangle (b.reduced (1.f), 8.f);
}

//==============================================================================
// Knob
//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& apvts, const juce::ParameterID& id, const juce::String& cap)
    : attach (*apvts.getParameter (id.getParamID()), slider), caption (cap)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    slider.setDoubleClickReturnValue (true, apvts.getParameter (id.getParamID())->convertFrom0to1 (apvts.getParameter (id.getParamID())->getDefaultValue()));
    addAndMakeVisible (slider);
}

void Knob::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedBottom (28));
}

void Knob::paintOverChildren (juce::Graphics& g)
{
    auto r = getLocalBounds().removeFromBottom (28);
    g.setFont (ui::caps (9.5f));
    g.setColour (ui::col::textDim);
    g.drawText (caption.toUpperCase(), r.removeFromTop (13), juce::Justification::centred, false);
    g.setFont (ui::sans (11.5f, true));
    g.setColour (ui::col::amber);
    g.drawText (slider.getTextFromValue (slider.getValue()), r, juce::Justification::centred, false);
}

//==============================================================================
// Segmented selector
//==============================================================================
Segmented::Segmented (juce::AudioProcessorValueTreeState& apvts, const juce::ParameterID& id, juce::StringArray l)
    : param (apvts.getParameter (id.getParamID())),
      attach (*param, [this] (float v) { index = juce::roundToInt (v); repaint(); }),
      labels (std::move (l))
{
    attach.sendInitialUpdate();
}

void Segmented::paint (juce::Graphics& g)
{
    const int n = labels.size();
    if (n == 0) return;
    const float w = (float) getWidth() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        auto r = juce::Rectangle<float> ((float) i * w, 0.f, w, (float) getHeight()).reduced (1.5f, 1.f);
        const bool sel = i == index;
        ui::keyPlate (g, r, sel, sel, *tex);
        g.setFont (ui::caps (juce::jmin (11.f, (float) getHeight() * 0.4f)));
        g.setColour (sel ? ui::col::text : ui::col::textDim);
        g.drawText (labels[i].toUpperCase(), r.translated (0.f, sel ? 1.f : 0.f), juce::Justification::centred, false);
    }
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    const int n = labels.size();
    if (n == 0) return;
    const int i = juce::jlimit (0, n - 1, (int) (e.position.x / ((float) getWidth() / (float) n)));
    attach.setValueAsCompleteGesture ((float) i);
}

//==============================================================================
// Icon button
//==============================================================================
void IconButton::paintButton (juce::Graphics& g, bool hi, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.f);
    ui::keyPlate (g, r, down, getToggleState(), *tex);
    if (hi && ! down) { g.setColour (juce::Colours::white.withAlpha (0.05f)); g.fillRoundedRectangle (r, 4.f); }

    const auto c = r.getCentre().translated (0.f, down ? 1.f : 0.f);
    const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.28f;
    const auto ink = getToggleState() ? ui::col::amber : ui::col::text;
    g.setColour (ink);
    juce::Path p;
    switch (icon)
    {
        case play:  p.addTriangle (c.x - s * 0.7f, c.y - s, c.x - s * 0.7f, c.y + s, c.x + s, c.y); g.fillPath (p); break;
        case pause: g.fillRoundedRectangle (c.x - s * 0.8f, c.y - s, s * 0.6f, s * 2.f, 1.f);
                    g.fillRoundedRectangle (c.x + s * 0.2f, c.y - s, s * 0.6f, s * 2.f, 1.f); break;
        case stop:  g.fillRoundedRectangle (c.x - s * 0.85f, c.y - s * 0.85f, s * 1.7f, s * 1.7f, 2.f); break;
        case restart:
            g.fillRect (c.x - s, c.y - s, s * 0.35f, s * 2.f);
            p.addTriangle (c.x + s, c.y - s, c.x + s, c.y + s, c.x - s * 0.5f, c.y); g.fillPath (p); break;
        case loop:
            p.addCentredArc (c.x, c.y, s, s * 0.8f, 0.f, 0.6f, juce::MathConstants<float>::twoPi - 0.2f, true);
            g.strokePath (p, juce::PathStrokeType (2.f));
            { juce::Path a; a.addTriangle (c.x + s * 0.55f, c.y - s * 0.95f, c.x + s * 1.3f, c.y - s * 0.3f, c.x + s * 0.2f, c.y - s * 0.2f); g.fillPath (a); }
            break;
        case gear:
        {
            juce::Path gp;
            gp.addStar (c, 8, s * 0.75f, s * 1.1f, 0.f);
            g.fillPath (gp);
            g.setColour (ui::col::metalMid);
            g.fillEllipse (c.x - s * 0.42f, c.y - s * 0.42f, s * 0.84f, s * 0.84f);
            break;
        }
        case folder:
            g.fillRoundedRectangle (c.x - s, c.y - s * 0.6f, s * 2.f, s * 1.5f, 2.f);
            g.fillRoundedRectangle (c.x - s, c.y - s * 0.95f, s * 0.9f, s * 0.5f, 1.5f);
            break;
    }
}

//==============================================================================
// Timing meter
//==============================================================================
void TimingMeter::push (float ms, HitEvent::Kind k)
{
    ticks[head] = { ms, k, juce::Time::getMillisecondCounterHiRes() };
    head = (head + 1) % 24;
    n = juce::jmin (24, n + 1);
    repaint();
}

void TimingMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    ui::glassWell (g, b, 6.f);
    const auto in = b.reduced (10.f, 6.f);
    const float cy = in.getCentreY() + 2.f;
    const float range = 150.f;
    auto xOf = [&] (float ms) { return in.getCentreX() + juce::jlimit (-1.f, 1.f, ms / range) * in.getWidth() * 0.5f; };

    // zones: perfect / good
    g.setColour (ui::col::perfect.withAlpha (0.07f)); g.fillRect (xOf (-45.f), in.getY() + 8.f, xOf (45.f) - xOf (-45.f), in.getHeight() - 8.f);
    g.setColour (ui::col::good.withAlpha (0.04f));    g.fillRect (xOf (-120.f), in.getY() + 8.f, xOf (120.f) - xOf (-120.f), in.getHeight() - 8.f);
    g.setColour (ui::col::textFaint);
    g.fillRect (in.getX(), cy, in.getWidth(), 1.f);
    g.setColour (ui::col::amber.withAlpha (0.8f));
    g.fillRect (in.getCentreX() - 0.75f, in.getY() + 6.f, 1.5f, in.getHeight() - 6.f);

    g.setFont (ui::caps (8.5f));
    g.setColour (ui::col::textFaint);
    g.drawText ("EARLY", in.withHeight (10.f), juce::Justification::topLeft, false);
    g.drawText ("TIMING", in.withHeight (10.f), juce::Justification::centredTop, false);
    g.drawText ("LATE", in.withHeight (10.f), juce::Justification::topRight, false);

    float sum = 0.f; int cnt = 0;
    for (int i = 0; i < n; ++i)
    {
        const int idx = (head - 1 - i + 48) % 24;
        const auto& t = ticks[idx];
        const float a = 1.f - (float) i / 24.f;
        const auto c = t.kind == HitEvent::hitPerfect ? ui::col::perfect : t.kind == HitEvent::hitGood ? ui::col::good : ui::col::offDyn;
        g.setColour (c.withAlpha (a * 0.9f));
        const float x = xOf (t.ms);
        g.fillRoundedRectangle (x - 1.5f, cy - 9.f, 3.f, 18.f, 1.5f);
        sum += t.ms; ++cnt;
    }
    if (cnt > 3)
    {
        const float mx = xOf (sum / (float) cnt);
        juce::Path tri; tri.addTriangle (mx - 4.f, in.getBottom(), mx + 4.f, in.getBottom(), mx, in.getBottom() - 6.f);
        g.setColour (ui::col::amber); g.fillPath (tri);
    }
}

//==============================================================================
// Overview strip
//==============================================================================
double OverviewStrip::beatAt (float x) const
{
    const auto* s = proc.getSession();
    if (s == nullptr) return 0.0;
    const auto in = getLocalBounds().toFloat().reduced (4.f, 3.f);
    return juce::jlimit (0.0, s->lesson.lengthBeats, (double) ((x - in.getX()) / in.getWidth()) * s->lesson.lengthBeats);
}

void OverviewStrip::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    ui::glassWell (g, b, 5.f);
    const auto* s = proc.getSession();
    if (s == nullptr) return;
    const auto& L = s->lesson;
    const auto in = b.reduced (4.f, 3.f);

    if (L.notes.empty())
    {
        g.setFont (ui::caps (10.f)); g.setColour (ui::col::textFaint);
        g.drawText (u8 ("NO NOTES  \xc2\xb7  FREE PLAY"), in, juce::Justification::centred, false);
        return;
    }

    const double len = L.lengthBeats;
    const float iw = in.getWidth(), ih = in.getHeight();
    const int lo = L.lowestPitch, hi = L.highestPitch;
    const float rowH = ih / (float) (hi - lo + 1);

    // bars (and weak bars from the last run)
    const int bpb = juce::jmax (1, L.beatsPerBar);
    const auto& res = proc.getLastResult();
    const bool haveRes = res.valid && res.lessonId == L.id;
    for (int bar = 0; (double) bar * bpb < len; ++bar)
    {
        const float x = in.getX() + (float) (bar * bpb / len) * iw;
        if (haveRes && bar < (int) res.barAccuracy.size() && res.barAccuracy[(size_t) bar] >= 0.f && res.barAccuracy[(size_t) bar] < 0.8f)
        {
            g.setColour (ui::col::wrong.withAlpha (0.14f));
            g.fillRect (x, in.getY(), (float) (bpb / len) * iw, ih);
        }
        g.setColour (juce::Colours::white.withAlpha (bar % 4 == 0 ? 0.09f : 0.035f));
        g.fillRect (x, in.getY(), 1.f, ih);
    }

    // loop region
    if (proc.apvts.getRawParameterValue (pid::loop.getParamID())->load() > 0.5f)
    {
        const float x0 = in.getX() + (float) (proc.getLoopStart() / len) * iw, x1 = in.getX() + (float) (proc.getLoopEnd() / len) * iw;
        g.setColour (ui::col::cyan.withAlpha (0.16f)); g.fillRect (x0, in.getY(), x1 - x0, ih);
        g.setColour (ui::col::cyan.withAlpha (0.8f));  g.fillRect (x0, in.getY(), 1.5f, ih); g.fillRect (x1 - 1.5f, in.getY(), 1.5f, ih);
    }

    // notes
    for (size_t i = 0; i < L.notes.size(); ++i)
    {
        const auto& n = L.notes[i];
        juce::Colour c = n.hand == handRight ? ui::col::rightHand.withAlpha (0.6f) : ui::col::leftHand.withAlpha (0.6f);
        switch (s->getState ((int) i))
        {
            case nsPerfect: c = ui::col::perfect; break;
            case nsGood: c = ui::col::good; break;
            case nsOffDynamics: c = ui::col::offDyn; break;
            case nsMissed: c = ui::col::missed; break;
            default: break;
        }
        g.setColour (c);
        g.fillRect (in.getX() + (float) (n.start / len) * iw, in.getY() + (float) (hi - n.pitch) * rowH,
                    juce::jmax (1.5f, (float) (n.length / len) * iw), juce::jmax (1.5f, rowH * 0.85f));
    }

    // playhead
    const double pos = juce::jlimit (0.0, len, proc.engine.smoothPosition());
    const float px = in.getX() + (float) (pos / len) * iw;
    g.setColour (ui::col::amber.withAlpha (0.25f)); g.fillRect (px - 2.5f, in.getY(), 5.f, ih);
    g.setColour (ui::col::amber);                    g.fillRect (px - 0.75f, in.getY(), 1.5f, ih);
}

void OverviewStrip::mouseDown (const juce::MouseEvent& e)
{
    selecting = e.mods.isShiftDown();
    if (selecting) dragAnchor = beatAt (e.position.x);
    else proc.engine.cmdSeek (beatAt (e.position.x));
}

void OverviewStrip::mouseDrag (const juce::MouseEvent& e)
{
    const double b = beatAt (e.position.x);
    if (selecting) { proc.setLoop (juce::jmin (dragAnchor, b), juce::jmax (dragAnchor, b)); repaint(); }
    else proc.engine.cmdSeek (b);
}

void OverviewStrip::mouseUp (const juce::MouseEvent&)
{
    if (selecting)
        if (auto* p = proc.apvts.getParameter (pid::loop.getParamID())) p->setValueNotifyingHost (1.f);
    selecting = false;
}

//==============================================================================
// Lesson browser
//==============================================================================
LessonBrowser::LessonBrowser (PianoTrainerProcessor& p) : proc (p)
{
    list.setRowHeight (28);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setOutlineThickness (0);
    addAndMakeVisible (list);
    addAndMakeVisible (addFile);
    addAndMakeVisible (addFolder);
    addFile.onClick = [this] { chooseFile(); };
    addFolder.onClick = [this] { chooseFolder(); };
    proc.addChangeListener (this);
    refresh();
}

LessonBrowser::~LessonBrowser() { proc.removeChangeListener (this); }

void LessonBrowser::resized()
{
    auto r = getLocalBounds().reduced (6);
    auto btn = r.removeFromBottom (28);
    addFile.setBounds (btn.removeFromLeft (btn.getWidth() / 2 - 2));
    addFolder.setBounds (btn.withTrimmedLeft (4));
    r.removeFromBottom (6);
    cardArea = r.removeFromBottom (juce::jlimit (96, 150, (int) (r.getHeight() * 0.24f)));
    r.removeFromBottom (6);
    list.setBounds (r);
}

void LessonBrowser::refresh()
{
    entries = proc.getLessonList();
    rows.clear();
    std::vector<juce::String> order;
    for (auto& e : entries) if (std::find (order.begin(), order.end(), e.category) == order.end()) order.push_back (e.category);
    int selRow = -1;
    for (auto& cat : order)
    {
        const bool open = collapsed.count (cat) == 0;
        int count = 0;
        for (auto& e : entries) count += e.category == cat;
        rows.push_back ({ true, cat + "  (" + juce::String (count) + ")", -1, open });
        if (! open) continue;
        for (int i = 0; i < (int) entries.size(); ++i)
            if (entries[(size_t) i].category == cat)
            {
                if (entries[(size_t) i].id == proc.getLessonId()) selRow = (int) rows.size();
                rows.push_back ({ false, entries[(size_t) i].title, i, true });
            }
    }
    list.updateContent();
    if (selRow >= 0) { list.selectRow (selRow, true, false); list.scrollToEnsureRowIsOnscreen (selRow); }
    repaint();
}

void LessonBrowser::paint (juce::Graphics& g)
{
    // lesson card
    ui::glassWell (g, cardArea.toFloat(), 6.f);
    const auto* s = proc.getSession();
    if (s == nullptr) return;
    auto r = cardArea.reduced (10, 7);
    g.setFont (ui::sans (13.5f, true));
    g.setColour (ui::col::amber);
    g.drawText (s->lesson.title, r.removeFromTop (18), juce::Justification::centredLeft, true);
    g.setFont (ui::sans (11.5f));
    g.setColour (ui::col::textDim);
    g.drawFittedText (s->lesson.blurb, r.removeFromTop (juce::jmax (30, r.getHeight() - 16)), juce::Justification::topLeft, 5, 0.9f);
    g.setFont (ui::caps (9.f));
    g.setColour (ui::col::textFaint);
    g.drawText (juce::String (s->lesson.notes.size()) + u8 (" NOTES  \xc2\xb7  ") + juce::String ((int) std::lround (s->lesson.bpm)) + u8 (" BPM  \xc2\xb7  ")
                    + juce::String (s->lesson.beatsPerBar) + "/4", r, juce::Justification::bottomLeft, false);
}

static void drawStar (juce::Graphics& g, float cx, float cy, float r, bool on)
{
    juce::Path p; p.addStar ({ cx, cy }, 5, r * 0.45f, r, 0.f);
    g.setColour (on ? ui::col::amber : ui::col::textFaint.withAlpha (0.45f));
    g.fillPath (p);
}

void LessonBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) rows.size()) return;
    const auto& rw = rows[(size_t) row];
    if (rw.header)
    {
        g.setColour (juce::Colours::white.withAlpha (0.03f));
        g.fillRect (0, 2, w, h - 3);
        g.setFont (ui::caps (10.5f));
        g.setColour (ui::col::amber.withAlpha (0.9f));
        g.drawText (rw.text.toUpperCase(), 24, 0, w - 30, h, juce::Justification::centredLeft, true);
        juce::Path t;
        if (rw.open) t.addTriangle (8.f, (float) h * 0.5f - 2.f, 16.f, (float) h * 0.5f - 2.f, 12.f, (float) h * 0.5f + 3.f);
        else         t.addTriangle (9.f, (float) h * 0.5f - 4.f, 9.f, (float) h * 0.5f + 4.f, 14.f, (float) h * 0.5f);
        g.fillPath (t);
        return;
    }

    const auto& e = entries[(size_t) rw.entry];
    if (selected)
    {
        g.setColour (ui::col::amber.withAlpha (0.16f));
        g.fillRoundedRectangle (3.f, 1.f, (float) w - 6.f, (float) h - 2.f, 4.f);
        g.setColour (ui::col::amber);
        g.fillRect (3.f, 6.f, 2.f, (float) h - 12.f);
    }
    g.setFont (ui::sans (13.f));
    g.setColour (selected ? ui::col::text : ui::col::textDim.brighter (0.2f));
    g.drawText (e.title, 16, 0, w - 100, h, juce::Justification::centredLeft, true);

    const auto rec = proc.progress.get (e.id);
    for (int i = 0; i < 3; ++i) drawStar (g, (float) w - 50.f + (float) i * 13.f, (float) h * 0.5f, 5.f, i < rec.stars());
    if (e.user) { g.setFont (ui::caps (8.5f)); g.setColour (ui::col::cyan); g.drawText ("MIDI", w - 100, 0, 36, h, juce::Justification::centredRight, false); }
    else
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (i < e.level ? ui::col::amber.withAlpha (0.75f) : ui::col::textFaint.withAlpha (0.35f));
            g.fillEllipse ((float) w - 100.f + (float) i * 7.f, (float) h * 0.5f - 1.5f, 3.f, 3.f);
        }
}

void LessonBrowser::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= (int) rows.size()) return;
    const auto& rw = rows[(size_t) row];
    if (rw.header)
    {
        auto cat = rw.text.upToLastOccurrenceOf ("  (", false, false);
        if (! collapsed.erase (cat)) collapsed.insert (cat);
        refresh();
        return;
    }
    proc.selectLesson (entries[(size_t) rw.entry].id);
    if (onLessonChosen) onLessonChosen();
}

void LessonBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < (int) rows.size() && ! rows[(size_t) row].header)
        proc.engine.cmdPlay();
}

void LessonBrowser::deleteKeyPressed (int row)
{
    if (row >= 0 && row < (int) rows.size() && ! rows[(size_t) row].header && entries[(size_t) rows[(size_t) row].entry].user)
        proc.removeUserLesson (entries[(size_t) rows[(size_t) row].entry].id);
}

void LessonBrowser::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose MIDI files", juce::File(), "*.mid;*.midi");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
    {
        bool first = true;
        for (auto& f : fc.getResults()) { proc.addMidiFile (f, first); first = false; }
    });
}

void LessonBrowser::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a folder of MIDI files", juce::File());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
    {
        if (fc.getResult().isDirectory())
        {
            const int n = proc.addMidiFolder (fc.getResult());
            proc.postStatus (juce::String (n) + " MIDI files added");
        }
    });
}

//==============================================================================
// Results
//==============================================================================
ResultsPanel::ResultsPanel (PianoTrainerProcessor& p) : proc (p)
{
    for (auto* b : { &retry, &loopWeak, &close }) addAndMakeVisible (*b);
    retry.onClick    = [this] { setVisible (false); if (onRetry) onRetry(); };
    close.onClick    = [this] { setVisible (false); if (onClose) onClose(); };
    loopWeak.onClick = [this] { setVisible (false); if (onLoopBars) onLoopBars (r.weakStartBar, r.weakEndBar); };
    setVisible (false);
}

void ResultsPanel::show (const RunResult& res)
{
    r = res;
    loopWeak.setVisible (r.weakStartBar >= 0 && r.mode == modePlay);
    resized();
    setVisible (true);
    toFront (false);
    repaint();
}

void ResultsPanel::resized()
{
    const float w = juce::jmin ((float) getWidth() - 24.f, 640.f);
    card = juce::Rectangle<float> (w, w / ui::phi).withCentre (getLocalBounds().toFloat().getCentre());
    auto br = card.toNearestInt().reduced (16).removeFromBottom (30);
    close.setBounds (br.removeFromRight (80));
    br.removeFromRight (6);
    retry.setBounds (br.removeFromRight (110));
    br.removeFromRight (6);
    loopWeak.setBounds (br.removeFromRight (140));
}

void ResultsPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    ui::glow (g, card, ui::col::amber, 14.f, 0.18f);
    juce::ColourGradient grad (juce::Colour (0xf0181b21), card.getX(), card.getY(), juce::Colour (0xf00a0c10), card.getX(), card.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (card, 10.f);
    g.setColour (ui::col::edgeLight.withAlpha (0.55f));
    g.drawRoundedRectangle (card.reduced (0.5f), 10.f, 1.f);
    if (! r.valid) return;

    auto in = card.reduced (18.f, 14.f).withTrimmedBottom (36.f);
    auto left = in.removeFromLeft (in.getWidth() * ui::invPhi * ui::invPhi + 18.f);
    in.removeFromLeft (14.f);

    // left: verdict
    g.setFont (ui::sans (22.f, true));
    g.setColour (ui::col::amber);
    g.drawText (r.headline, left.removeFromTop (28.f), juce::Justification::centredLeft, true);
    g.setFont (ui::sans (12.f));
    g.setColour (ui::col::textDim);
    g.drawText (r.lessonTitle, left.removeFromTop (16.f), juce::Justification::centredLeft, true);
    left.removeFromTop (8.f);

    auto digits = left.removeFromTop (juce::jmin (62.f, left.getHeight() * 0.42f));
    ui::sevenSegment (g, juce::String ((int) std::lround (r.accuracy * 100.f)).paddedLeft ('0', 3), digits.withTrimmedRight (26.f), ui::col::amber, ui::col::amberGhost, true);
    g.setFont (ui::caps (14.f)); g.setColour (ui::col::amber);
    g.drawText ("%", digits.removeFromRight (24.f), juce::Justification::bottomLeft, false);

    left.removeFromTop (6.f);
    for (int i = 0; i < 3; ++i) drawStar (g, left.getX() + 10.f + (float) i * 24.f, left.getY() + 10.f, 9.f, i < r.stars);
    if (r.newBest)
    {
        g.setFont (ui::caps (10.f)); g.setColour (ui::col::perfect);
        g.drawText ("NEW BEST", left.withTrimmedLeft (82.f).withHeight (20.f), juce::Justification::centredLeft, false);
    }

    // right: numbers
    auto chips = in.removeFromTop (44.f);
    struct Chip { const char* name; int v; juce::Colour c; };
    std::vector<Chip> cs = { { "PERFECT", r.perfect, ui::col::perfect }, { "GOOD", r.good, ui::col::good } };
    if (r.dynamics) cs.push_back ({ "OFF TOUCH", r.offDyn, ui::col::offDyn });
    cs.push_back ({ "MISSED", r.missed, ui::col::missed.brighter (0.5f) });
    cs.push_back ({ "WRONG", r.wrong, ui::col::wrong });
    const float cw = chips.getWidth() / (float) cs.size();
    for (size_t i = 0; i < cs.size(); ++i)
    {
        auto c = chips.removeFromLeft (cw);
        g.setFont (ui::sans (20.f, true)); g.setColour (cs[i].c);
        g.drawText (juce::String (cs[i].v), c.removeFromTop (26.f), juce::Justification::centred, false);
        g.setFont (ui::caps (8.5f)); g.setColour (ui::col::textFaint);
        g.drawText (cs[i].name, c, juce::Justification::centredTop, false);
    }

    if (r.mode == modePlay && (r.perfect + r.good + r.offDyn) > 3)
    {
        g.setFont (ui::sans (11.f)); g.setColour (ui::col::textDim);
        g.drawText ((r.meanErrMs >= 0 ? "Average +" : "Average ") + juce::String ((int) r.meanErrMs) + u8 (" ms  \xc2\xb7  spread \xc2\xb1")
                        + juce::String ((int) r.stdErrMs) + u8 (" ms  \xc2\xb7  best streak ") + juce::String (r.bestStreak),
                    in.removeFromTop (18.f), juce::Justification::centred, true);
    }

    // per-bar bars
    auto chart = in.removeFromTop (40.f).reduced (0.f, 2.f);
    if (! r.barAccuracy.empty())
    {
        const int nb = (int) r.barAccuracy.size();
        const float bw = juce::jmin (16.f, chart.getWidth() / (float) nb);
        const float x0 = chart.getCentreX() - bw * (float) nb * 0.5f;
        for (int i = 0; i < nb; ++i)
        {
            const float a = r.barAccuracy[(size_t) i];
            const auto cell = juce::Rectangle<float> (x0 + (float) i * bw, chart.getY(), bw - 2.f, chart.getHeight());
            g.setColour (juce::Colours::white.withAlpha (0.05f)); g.fillRoundedRectangle (cell, 1.5f);
            if (a >= 0.f)
            {
                g.setColour ((a >= 0.9f ? ui::col::perfect : a >= 0.7f ? ui::col::good : ui::col::wrong).withAlpha (0.9f));
                g.fillRoundedRectangle (cell.withTop (cell.getBottom() - juce::jmax (2.f, cell.getHeight() * a)), 1.5f);
            }
        }
        g.setFont (ui::caps (8.f)); g.setColour (ui::col::textFaint);
        g.drawText ("ACCURACY BY BAR", chart.translated (0.f, chart.getHeight() + 1.f).withHeight (10.f), juce::Justification::centred, false);
    }
    in.removeFromTop (12.f);

    g.setFont (ui::sans (12.f)); g.setColour (ui::col::text.withAlpha (0.9f));
    g.drawFittedText (r.advice, in.toNearestInt(), juce::Justification::topLeft, 6, 0.9f);
}

//==============================================================================
// Settings
//==============================================================================
SettingsPanel::SettingsPanel (PianoTrainerProcessor& p) : proc (p)
{
    auto& a = proc.apvts;
    auto par = [&a] (const juce::ParameterID& id) { return a.getParameter (id.getParamID()); };

    timing.addItemList ({ "Relaxed", "Normal", "Strict" }, 1);
    juce::StringArray sc; for (auto* const* n = theory::scaleTypeNames(); *n != nullptr; ++n) sc.add (*n);
    scaleType.addItemList (sc, 1);
    juce::StringArray roots { "Auto (lesson key)" }; for (int i = 0; i < 12; ++i) roots.add (theory::pcName (i));
    scaleRoot.addItemList (roots, 1);
    countIn.addItemList ({ "Off", "1 bar", "2 bars", "3 bars", "4 bars" }, 1);

    cAtt.push_back (std::make_unique<juce::ComboBoxParameterAttachment> (*par (pid::timing), timing));
    cAtt.push_back (std::make_unique<juce::ComboBoxParameterAttachment> (*par (pid::scaleType), scaleType));
    cAtt.push_back (std::make_unique<juce::ComboBoxParameterAttachment> (*par (pid::scaleRoot), scaleRoot));
    cAtt.push_back (std::make_unique<juce::ComboBoxParameterAttachment> (*par (pid::countIn), countIn));

    auto toggle = [&] (juce::ToggleButton& t, const juce::ParameterID& id)
    { bAtt.push_back (std::make_unique<juce::ButtonParameterAttachment> (*par (id), t)); };
    toggle (showScale, pid::showScale); toggle (labels, pid::labels); toggle (fingers, pid::fingers);
    toggle (followHost, pid::followHost); toggle (syncTransport, pid::syncTransport);
    toggle (autoSpeed, pid::autoSpeed); toggle (builtin, pid::builtinOnly);

    for (auto* s : { &transpose, &lookahead })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        s->setColour (juce::Slider::textBoxTextColourId, ui::col::amber);
        s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }
    sAtt.push_back (std::make_unique<juce::SliderParameterAttachment> (*par (pid::transpose), transpose));
    sAtt.push_back (std::make_unique<juce::SliderParameterAttachment> (*par (pid::lookahead), lookahead));

    addHeader ("Practice");
    addRow ("Timing strictness", timing);
    addRow ("Count-in", countIn);
    addRow ("Transpose (semitones)", transpose);
    addRow ({}, autoSpeed); addRow ({}, followHost); addRow ({}, syncTransport);
    addHeader ("Display");
    addRow ("Roll window (beats)", lookahead);
    addRow ({}, labels); addRow ({}, fingers); addRow ({}, showScale);
    addRow ("Scale", scaleType); addRow ("Scale root", scaleRoot);
    addHeader ("Sound");
    addRow ("Source", soundSource);
    addRow ("Instrument", instrument);
    addRow ({}, builtin);

    fonts = proc.findSoundFonts();
    soundSource.addItem ("Built-in piano", 1);
    for (int i = 0; i < fonts.size(); ++i) soundSource.addItem (fonts[i].getFileNameWithoutExtension(), 2 + i);
    soundSource.addItem ("Browse for a .sf2 file...", 1000);
    soundSource.setSelectedId (1, juce::dontSendNotification);
    soundSource.onChange = [this]
    {
        const int id = soundSource.getSelectedId();
        if (id == 1) proc.useBuiltinSound();
        else if (id >= 2 && id < 1000) proc.loadSoundFont (fonts[id - 2]);
        else if (id == 1000)
        {
            chooser = std::make_unique<juce::FileChooser> ("Choose a SoundFont", juce::File(), "*.sf2");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) proc.loadSoundFont (fc.getResult()); });
        }
    };

    presetAtt = std::make_unique<juce::ParameterAttachment> (*par (pid::preset), [this] (float v)
    {
        if (instrument.getNumItems() > 0) instrument.setSelectedItemIndex (juce::roundToInt (v), juce::dontSendNotification);
    });
    instrument.onChange = [this] { if (instrument.getSelectedId() > 0) presetAtt->setValueAsCompleteGesture ((float) (instrument.getSelectedId() - 1)); };
    refreshInstruments();

    for (auto& r : rows) if (r.control != nullptr) addAndMakeVisible (r.control);
    addAndMakeVisible (closeBtn);
    closeBtn.onClick = [this] { setVisible (false); if (onClose) onClose(); };
    setVisible (false);
}

SettingsPanel::~SettingsPanel() = default;

void SettingsPanel::addRow (const juce::String& name, juce::Component& c) { rows.push_back ({ name, &c, false }); }

void SettingsPanel::refreshInstruments()
{
    const auto names = proc.getSynth().getPresetNames();
    if (names.size() == instrument.getNumItems() && names.size() > 0) return;
    instrument.clear (juce::dontSendNotification);
    for (int i = 0; i < names.size(); ++i) instrument.addItem (juce::String (i + 1) + "  " + names[i], i + 1);
    instrument.setEnabled (names.size() > 0);
    if (names.size() == 0) instrument.setTextWhenNothingSelected ("(built-in piano)");
    else instrument.setSelectedItemIndex (juce::jlimit (0, names.size() - 1, (int) proc.apvts.getRawParameterValue (pid::preset.getParamID())->load()), juce::dontSendNotification);
}

void SettingsPanel::resized()
{
    auto card = getLocalBounds().toFloat();
    const float w = juce::jmin (card.getWidth() - 24.f, 560.f);
    const float h = juce::jmin (card.getHeight() - 16.f, w * 1.15f);
    auto area = juce::Rectangle<float> (w, h).withCentre (card.getCentre()).toNearestInt().reduced (20, 16);
    closeBtn.setBounds (area.removeFromBottom (30).removeFromRight (90));
    area.removeFromBottom (6);

    int nHeaders = 0, nRows = 0;
    for (auto& r : rows) (r.header ? nHeaders : nRows)++;
    const int rowH = juce::jlimit (22, 30, (area.getHeight() - nHeaders * 22) / juce::jmax (1, nRows));
    int y = area.getY();
    for (auto& r : rows)
    {
        if (r.header) { y += 22; continue; }
        auto row = juce::Rectangle<int> (area.getX(), y, area.getWidth(), rowH - 3);
        if (r.text.isEmpty()) r.control->setBounds (row.withTrimmedLeft (4));
        else
        {
            r.control->setBounds (row.withTrimmedLeft ((int) (area.getWidth() * 0.42f)));
        }
        y += rowH;
    }
}

void SettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.6f));
    const float w = juce::jmin ((float) getWidth() - 24.f, 560.f);
    const float h = juce::jmin ((float) getHeight() - 16.f, w * 1.15f);
    const auto card = juce::Rectangle<float> (w, h).withCentre (getLocalBounds().toFloat().getCentre());
    juce::ColourGradient grad (juce::Colour (0xf4181b21), card.getX(), card.getY(), juce::Colour (0xf40a0c10), card.getX(), card.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (card, 10.f);
    g.setColour (ui::col::edgeLight.withAlpha (0.55f));
    g.drawRoundedRectangle (card.reduced (0.5f), 10.f, 1.f);

    auto area = card.toNearestInt().reduced (20, 16);
    int nHeaders = 0, nRows = 0;
    for (auto& r : rows) (r.header ? nHeaders : nRows)++;
    area.removeFromBottom (36);
    const int rowH = juce::jlimit (22, 30, (area.getHeight() - nHeaders * 22) / juce::jmax (1, nRows));
    int y = area.getY();
    for (auto& r : rows)
    {
        if (r.header)
        {
            g.setFont (ui::caps (10.5f)); g.setColour (ui::col::amber);
            g.drawText (r.text.toUpperCase(), area.getX(), y, area.getWidth(), 22, juce::Justification::centredLeft, false);
            g.setColour (ui::col::edgeLight.withAlpha (0.35f));
            g.fillRect (area.getX(), y + 20, area.getWidth(), 1);
            y += 22;
            continue;
        }
        if (r.text.isNotEmpty())
        {
            g.setFont (ui::sans (12.5f)); g.setColour (ui::col::textDim);
            g.drawText (r.text, area.getX(), y, (int) (area.getWidth() * 0.42f), rowH - 3, juce::Justification::centredLeft, true);
        }
        y += rowH;
    }
}
} // namespace pt
