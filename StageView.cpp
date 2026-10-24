#include "StageView.h"
#include "Theme.h"
#include "Theory.h"
#include <cmath>

namespace pt
{

//==============================================================================
void KeyMap::setRange (int a, int b)
{
    lo = a; hi = b; numWhite = 0;
    for (int p = 0; p < 128; ++p) whiteIdx[p] = -1;
    for (int p = lo; p <= hi; ++p)
        if (! theory::isBlack (p)) whiteIdx[p] = numWhite++;
    if (numWhite < 1) numWhite = 1;
}

void KeyMap::unitSpan (int p, float& x, float& w) const noexcept
{
    if (! theory::isBlack (p)) { x = (float) whiteIdx[p]; w = 1.f; return; }
    static const float shift[12] = { 0, -0.08f, 0, 0.08f, 0, 0, -0.10f, 0, 0.0f, 0, 0.10f, 0 };
    const float bw = 0.6f;
    const int left = juce::jmax (0, whiteIdx[juce::jmax (0, p - 1)]);
    x = (float) left + 1.f - bw * 0.5f + shift[p % 12];
    w = bw;
}

void KeyMap::autoRange (const Lesson& l, int& lo, int& hi)
{
    if (l.notes.empty()) { lo = 36; hi = 96; return; }
    lo = juce::jmax (12, l.lowestPitch - 3);  lo -= lo % 12;               // snap down to a C
    hi = juce::jmin (119, l.highestPitch + 3); hi += 11 - hi % 12;         // snap up to a B
    auto whites = [&] { int n = 0; for (int p = lo; p <= hi; ++p) n += ! theory::isBlack (p); return n; };
    bool down = true;
    while (whites() < 22)                                                  // at least three octaves wide
    {
        if (down && lo > 12) lo -= 12;
        else if (hi < 107) hi += 12;
        else if (lo > 12) lo -= 12;
        else break;
        down = ! down;
    }
}

//==============================================================================
StageView::StageView (PianoTrainerProcessor& p) : proc (p), apvts (p.apvts)
{
    auto raw = [this] (const juce::ParameterID& id) { return apvts.getRawParameterValue (id.getParamID()); };
    pLook = raw (pid::lookahead); pLabels = raw (pid::labels); pFingers = raw (pid::fingers);
    pShowScale = raw (pid::showScale); pScaleType = raw (pid::scaleType); pScaleRoot = raw (pid::scaleRoot);
    pHand = raw (pid::hand); pMode = raw (pid::mode); pLoop = raw (pid::loop);
    particles.reserve (512);
    setOpaque (false);
}

void StageView::resized() {}

void StageView::rebuildKeyMapIfNeeded()
{
    const auto* s = proc.getSession();
    if (s == mapSession) return;
    mapSession = s;
    if (s == nullptr) return;
    int lo, hi;
    KeyMap::autoRange (s->lesson, lo, hi);
    km.setRange (lo, hi);
    maxLen = 0.5;
    for (auto& n : s->lesson.notes) maxLen = juce::jmax (maxLen, n.length);
}

double StageView::displayPos() const
{
    auto& e = proc.engine;
    double p = e.posBeats.load();
    if (e.playing.load() && ! e.waiting.load())
    {
        const double dt = juce::jlimit (0.0, 0.08, juce::Time::getMillisecondCounterHiRes() * 0.001 - e.posStamp.load());
        p += dt * e.beatsPerSec.load();
        if (pLoop->load() > 0.5f && p > proc.getLoopEnd() && e.posBeats.load() <= proc.getLoopEnd()) p = proc.getLoopEnd();
    }
    return p;
}

//==============================================================================
// Geometry
//==============================================================================
juce::Rectangle<float> StageView::keyRect (int pitch, juce::Rectangle<float> kb) const
{
    float ux, uw;
    km.unitSpan (pitch, ux, uw);
    const float ww = kb.getWidth() / (float) km.numWhite;
    const bool black = theory::isBlack (pitch);
    return { kb.getX() + ux * ww, kb.getY(), uw * ww, black ? kb.getHeight() * 0.62f : kb.getHeight() };
}

juce::Rectangle<float> StageView::laneRect (int pitch, juce::Rectangle<float> roll) const
{
    float ux, uw;
    km.unitSpan (pitch, ux, uw);
    const float ww = roll.getWidth() / (float) km.numWhite;
    const float inset = theory::isBlack (pitch) ? uw * ww * 0.06f : uw * ww * 0.07f;
    return { roll.getX() + ux * ww + inset, roll.getY(), uw * ww - inset * 2.f, roll.getHeight() };
}

int StageView::pitchAt (juce::Point<float> pt) const
{
    const auto kb = getLocalBounds().toFloat().withTop ((float) getHeight() - kbHeight);
    if (! kb.contains (pt)) return -1;
    for (int p = km.lo; p <= km.hi; ++p)             // black keys first
        if (theory::isBlack (p) && keyRect (p, kb).contains (pt)) return p;
    for (int p = km.lo; p <= km.hi; ++p)
        if (! theory::isBlack (p) && keyRect (p, kb).contains (pt)) return p;
    return -1;
}

int StageView::scaleRootPc (const Session& s) const
{
    const int r = (int) pScaleRoot->load();
    if (r > 0) return r - 1;
    return s.lesson.keyRoot >= 0 ? s.lesson.keyRoot : 0;
}

bool StageView::inScale (int pitch, int root, int type) const
{
    const int rel = ((pitch - root) % 12 + 12) % 12;
    for (int iv : theory::scaleIntervals (type)) if (iv == rel) return true;
    return false;
}

//==============================================================================
// Per-frame update
//==============================================================================
void StageView::spawn (int pitch, juce::Colour c, int count, float speed)
{
    if (! km.contains (pitch)) return;
    const auto roll = getLocalBounds().toFloat().withTrimmedBottom (kbHeight);
    const auto lane = laneRect (pitch, roll);
    static juce::Random rng;
    for (int i = 0; i < count && particles.size() < 480; ++i)
    {
        Particle p;
        p.x = lane.getX() + rng.nextFloat() * lane.getWidth();
        p.y = roll.getBottom() - 2.f;
        p.vx = (rng.nextFloat() - 0.5f) * speed * 0.9f;
        p.vy = -(0.25f + rng.nextFloat() * 0.9f) * speed;
        p.life = 0.5f + rng.nextFloat() * 0.5f;
        p.size = 1.5f + rng.nextFloat() * 2.2f;
        p.c = c;
        particles.push_back (p);
    }
}

void StageView::update (double dt)
{
    rebuildKeyMapIfNeeded();
    pulse = std::fmod (pulse + (float) dt * 2.4f, 1.f);

    heldLo = proc.engine.heldLo.load();
    heldHi = proc.engine.heldHi.load();

    // judgement feedback
    HitEvent buf[32];
    for (int n; (n = proc.engine.popHits (buf, 32)) > 0;)
        for (int i = 0; i < n; ++i)
        {
            const auto& h = buf[i];
            juce::Colour c = h.kind == HitEvent::hitPerfect ? ui::col::perfect
                           : h.kind == HitEvent::hitGood ? ui::col::good
                           : h.kind == HitEvent::hitOffDynamics ? ui::col::offDyn
                           : ui::col::wrong;
            if (h.kind != HitEvent::hitMissed)
            {
                flash[h.pitch & 127] = 1.f; flashColour[h.pitch & 127] = c;
                spawn (h.pitch, c, h.kind == HitEvent::hitPerfect ? 16 : h.kind == HitEvent::hitWrong ? 6 : 9,
                       h.kind == HitEvent::hitWrong ? 70.f : 150.f);
            }
            if (onHit) onHit (h);
        }

    for (auto& f : flash) f = juce::jmax (0.f, f - (float) dt * 2.2f);

    const float g = 260.f * (float) dt;
    for (auto& p : particles) { p.x += p.vx * (float) dt; p.y += p.vy * (float) dt; p.vy += g * 0.5f; p.life -= (float) dt * 1.4f; }
    particles.erase (std::remove_if (particles.begin(), particles.end(), [] (const Particle& p) { return p.life <= 0.f; }), particles.end());

    // which keys to light up
    std::fill (std::begin (hint), std::end (hint), (std::uint8_t) 0);
    if (const auto* s = proc.getSession())
    {
        const double pos = displayPos();
        const int mode = (int) pMode->load(), hand = (int) pHand->load();
        const auto& N = s->lesson.notes;
        auto it = std::lower_bound (N.begin(), N.end(), pos - maxLen, [] (const Note& n, double v) { return n.start < v; });
        for (; it != N.end() && it->start < pos + 0.8; ++it)
        {
            const int idx = (int) (it - N.begin());
            const bool practised = hand == 0 || (hand == 1 && it->hand == handRight) || (hand == 2 && it->hand == handLeft);
            const bool sounding = it->start <= pos + 0.02 && it->end() > pos;
            const int bit = it->hand == handRight ? 1 : 2;
            if (mode == modeWatch) { if (sounding || (it->start - pos < 0.35 && it->start > pos)) hint[it->pitch] |= (std::uint8_t) bit; }
            else if (practised) { if (s->getState (idx) == nsPending && it->start > pos - 0.3) hint[it->pitch] |= (std::uint8_t) bit; }
            else if (sounding) hint[it->pitch] |= 4;
        }
    }
    repaint();
}

//==============================================================================
// Painting
//==============================================================================
void StageView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    rebuildKeyMapIfNeeded();

    const float whiteW = b.getWidth() / (float) km.numWhite;
    kbHeight = juce::jlimit (b.getHeight() * 0.17f, b.getHeight() * 0.30f, whiteW * 5.2f);
    const auto roll = b.withTrimmedBottom (kbHeight);
    const auto kb = b.withTop (b.getBottom() - kbHeight);

    // glass well
    ui::glassWell (g, b.withTrimmedBottom (0.f), 6.f);

    if (const auto* s = proc.getSession())
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (roll.toNearestInt());
        paintRoll (g, roll, *s, displayPos());
    }
    paintKeyboard (g, kb);

    // reflection across the top of the glass
    juce::ColourGradient refl (juce::Colours::white.withAlpha (0.055f), 0.f, b.getY(), juce::Colours::transparentWhite, 0.f, b.getY() + roll.getHeight() * 0.35f, false);
    g.setGradientFill (refl);
    g.fillRoundedRectangle (b.reduced (1.f).withHeight (roll.getHeight() * 0.35f), 6.f);
}

void StageView::paintRoll (juce::Graphics& g, juce::Rectangle<float> r, const Session& s, double pos)
{
    const auto& L = s.lesson;
    const float look = pLook->load();
    const float ppb = r.getHeight() / look;
    const float hitY = r.getBottom();

    // lanes ---------------------------------------------------------------------------------
    const bool showScale = pShowScale->load() > 0.5f;
    const int root = scaleRootPc (s), stype = (int) pScaleType->load();
    for (int p = km.lo; p <= km.hi; ++p)
    {
        const auto lane = laneRect (p, r);
        if (theory::isBlack (p)) { g.setColour (juce::Colours::black.withAlpha (0.28f)); g.fillRect (lane); }
        else if ((km.whiteIdx[p] & 1) == 0) { g.setColour (juce::Colours::white.withAlpha (0.018f)); g.fillRect (lane); }

        if (showScale && inScale (p, root, stype))
        {
            const bool isRoot = ((p - root) % 12 + 12) % 12 == 0;
            g.setColour (ui::col::cyan.withAlpha (isRoot ? 0.16f : 0.065f));
            g.fillRect (lane);
        }
        if (p % 12 == 0)
        {
            g.setColour (ui::col::amber.withAlpha (0.10f));
            g.fillRect (lane.getX() - 0.5f, r.getY(), 1.f, r.getHeight());
        }
    }

    // beat / bar grid ----------------------------------------------------------------------------
    const int bpb = juce::jmax (1, L.beatsPerBar);
    const int firstBeat = (int) std::floor (pos), lastBeat = (int) std::ceil (pos + look);
    for (int bt = firstBeat; bt <= lastBeat; ++bt)
    {
        if (bt < 0 && bt < -bpb * 4) continue;
        const float y = hitY - (float) (bt - pos) * ppb;
        if (y < r.getY() || y > hitY) continue;
        const bool bar = bt % bpb == 0;
        g.setColour (juce::Colours::white.withAlpha (bar ? 0.12f : 0.045f));
        g.fillRect (r.getX(), y - 0.5f, r.getWidth(), bar ? 1.2f : 1.f);
        if (bar && bt >= 0)
        {
            g.setFont (ui::caps (9.5f));
            g.setColour (ui::col::textFaint);
            g.drawText (juce::String (bt / bpb + 1), (int) r.getX() + 6, (int) y - 12, 40, 11, juce::Justification::left);
        }
    }

    // loop region ------------------------------------------------------------------------------
    if (pLoop->load() > 0.5f)
        for (double edge : { proc.getLoopStart(), proc.getLoopEnd() })
        {
            const float y = hitY - (float) (edge - pos) * ppb;
            if (y >= r.getY() && y <= hitY)
            {
                g.setColour (ui::col::cyan.withAlpha (0.7f));
                g.fillRect (r.getX(), y - 1.f, r.getWidth(), 2.f);
            }
        }

    // notes ------------------------------------------------------------------------------------
    const int hand = (int) pHand->load();
    const int mode = (int) pMode->load();
    const bool labels = pLabels->load() > 0.5f, fingers = pFingers->load() > 0.5f;
    const auto& N = L.notes;
    auto it = std::lower_bound (N.begin(), N.end(), pos - maxLen - 0.1, [] (const Note& n, double v) { return n.start < v; });

    for (; it != N.end() && it->start < pos + look; ++it)
    {
        const auto& n = *it;
        if (n.end() < pos - 0.01 || ! km.contains (n.pitch)) continue;
        const int idx = (int) (it - N.begin());

        const auto lane = laneRect (n.pitch, r);
        const float yB = hitY - (float) (n.start - pos) * ppb;
        float yT = hitY - (float) (n.end() - pos) * ppb;
        if (yB - yT < 7.f) yT = yB - 7.f;
        if (yT > hitY) continue;
        auto bar = juce::Rectangle<float> (lane.getX(), yT, lane.getWidth(), yB - yT).withBottom (yB - 1.5f);

        const bool practised = hand == 0 || (hand == 1 && n.hand == handRight) || (hand == 2 && n.hand == handLeft) || mode == modeWatch;
        juce::Colour base = n.hand == handRight ? ui::col::rightHand : ui::col::leftHand;
        float alpha = practised ? 0.92f : 0.32f;

        if (L.scoreDynamics)                                    // brightness shows the intended touch
        {
            const float v = (float) n.velocity / 127.f;
            base = base.withMultipliedBrightness (0.45f + 0.7f * v).withMultipliedSaturation (0.7f + 0.3f * v);
        }

        switch (s.getState (idx))
        {
            case nsPerfect:     base = ui::col::perfect;  alpha = 0.95f; break;
            case nsGood:        base = ui::col::good;     alpha = 0.9f;  break;
            case nsOffDynamics: base = ui::col::offDyn;   alpha = 0.9f;  break;
            case nsMissed:      base = ui::col::missed;   alpha = 0.4f;  break;
            default: break;
        }

        const bool active = n.start <= pos + 0.02 && n.end() > pos;
        if (active && practised) ui::glow (g, bar, base, 7.f, 0.5f);

        const float cr = juce::jmin (5.f, bar.getWidth() * 0.35f, bar.getHeight() * 0.35f);
        juce::ColourGradient grad (base.brighter (0.35f).withAlpha (alpha), bar.getX(), bar.getY(),
                                   base.darker (0.35f).withAlpha (alpha), bar.getX(), bar.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bar, cr);

        g.setColour (base.brighter (0.7f).withAlpha (alpha * 0.8f));            // glass rim
        g.drawRoundedRectangle (bar.reduced (0.5f), cr, 1.f);
        g.setColour (juce::Colours::white.withAlpha (0.28f * alpha));           // specular streak
        g.fillRoundedRectangle (bar.reduced (bar.getWidth() * 0.22f, 2.f).withWidth (juce::jmax (1.5f, bar.getWidth() * 0.12f)), 1.f);

        if (practised && bar.getWidth() >= 17.f)
        {
            // note name at the leading edge, finger badge above it
            const float ty = bar.getBottom() - 15.f;
            if (labels && bar.getHeight() >= 17.f)
            {
                g.setFont (ui::caps (juce::jmin (10.5f, bar.getWidth() * 0.45f)));
                g.setColour (juce::Colours::black.withAlpha (0.65f));
                g.drawText (theory::pcName (n.pitch), bar.withTop (ty).withHeight (13.f), juce::Justification::centred, false);
            }
            if (fingers && n.finger > 0 && bar.getHeight() >= 32.f)
            {
                const auto badge = juce::Rectangle<float> (bar.getCentreX() - 6.f, ty - 15.f, 12.f, 12.f);
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.fillEllipse (badge);
                g.setColour (ui::col::text);
                g.setFont (ui::sans (9.f, true));
                g.drawText (juce::String (n.finger), badge, juce::Justification::centred, false);
            }
        }
    }

    // hit line ---------------------------------------------------------------------------------------
    const bool waiting = proc.engine.waiting.load();
    const float wp = waiting ? 0.5f + 0.5f * std::sin (pulse * juce::MathConstants<float>::twoPi) : 0.f;
    const auto lineCol = ui::col::amber.withAlpha (0.5f + 0.4f * wp);
    ui::glow (g, juce::Rectangle<float> (r.getX(), hitY - 2.f, r.getWidth(), 2.f), ui::col::amber, waiting ? 12.f : 7.f, 0.4f + 0.3f * wp);
    g.setColour (lineCol);
    g.fillRect (r.getX(), hitY - 2.f, r.getWidth(), 2.f);

    // particles -------------------------------------------------------------------------------------
    for (auto& p : particles)
    {
        const float a = juce::jlimit (0.f, 1.f, p.life);
        g.setColour (p.c.withAlpha (a * 0.35f)); g.fillEllipse (p.x - p.size * 1.8f, p.y - p.size * 1.8f, p.size * 3.6f, p.size * 3.6f);
        g.setColour (p.c.withAlpha (a));         g.fillEllipse (p.x - p.size * 0.5f, p.y - p.size * 0.5f, p.size, p.size);
    }

    // overlays ---------------------------------------------------------------------------------------
    auto& e = proc.engine;
    if (e.playing.load() && pos < 0.0)
    {
        const int n = (int) std::ceil (-pos);
        const float h = r.getHeight() * 0.3f;
        const auto box = juce::Rectangle<float> (0.f, 0.f, h * 0.9f, h).withCentre (r.getCentre().translated (0.f, -r.getHeight() * 0.06f));
        ui::sevenSegment (g, juce::String (juce::jlimit (0, 9, n)), box, ui::col::amber.withAlpha (0.9f), ui::col::amberGhost, false);
    }
    else if (! e.playing.load())
    {
        g.setFont (ui::caps (12.f));
        g.setColour (ui::col::textDim.withAlpha (0.8f));
        const bool fresh = pos <= 0.001 || pos >= L.lengthBeats;
        const juce::String msg = L.isFreePlay() ? u8 ("FREE PLAY  \xc2\xb7  PLAY ANYTHING, THE LENS NAMES WHAT YOU HOLD")
                               : fresh ? "PRESS PLAY OR SPACE" : u8 ("PAUSED  \xc2\xb7  SPACE TO CONTINUE");
        g.drawText (msg, r.withHeight (r.getHeight() * 0.9f), juce::Justification::centred, false);
    }
    if (waiting)
    {
        g.setFont (ui::caps (11.f));
        g.setColour (ui::col::amber.withAlpha (0.55f + 0.4f * wp));
        g.drawText ("WAITING FOR YOU", r.withTrimmedBottom (12.f).removeFromBottom (26.f), juce::Justification::centred, false);
    }
}

void StageView::paintKeyboard (juce::Graphics& g, juce::Rectangle<float> kb)
{
    // base plate / key bed
    g.setColour (juce::Colour (0xff050607));
    g.fillRect (kb);

    auto keyColourTint = [&] (int p, juce::Colour& tint, float& amt)
    {
        amt = 0.f;
        const bool held = (p < 64 ? (heldLo >> p) : (heldHi >> (p - 64))) & 1u;
        if (held) { tint = ui::col::amber; amt = 1.f; }
        if (flash[p] > 0.f && flash[p] > amt * 0.6f) { tint = flashColour[p]; amt = juce::jmax (amt, flash[p]); }
        if (hint[p] != 0 && amt < 0.5f)
        {
            const float pul = 0.45f + 0.35f * std::sin (pulse * juce::MathConstants<float>::twoPi);
            tint = (hint[p] & 1) ? ui::col::rightHand : (hint[p] & 2) ? ui::col::leftHand : ui::col::textDim;
            amt = juce::jmax (amt, (hint[p] & 3) ? pul : 0.3f);
        }
        return held;
    };

    // white keys
    for (int p = km.lo; p <= km.hi; ++p)
    {
        if (theory::isBlack (p)) continue;
        auto k = keyRect (p, kb).reduced (0.7f, 0.f);
        juce::Colour tint; float amt;
        const bool held = keyColourTint (p, tint, amt);
        if (held) k = k.withTrimmedTop (2.f);

        juce::ColourGradient grad (juce::Colour (0xfff4f1ea), k.getX(), k.getY(), juce::Colour (0xffc9c4b9), k.getX(), k.getBottom(), false);
        grad.addColour (0.85, juce::Colour (0xffe6e2d8));
        g.setGradientFill (grad);
        g.fillRoundedRectangle (k.withTrimmedTop (-4.f), 3.f);
        if (amt > 0.f)
        {
            juce::ColourGradient tg (tint.withAlpha (0.55f * amt), 0.f, k.getBottom() - k.getHeight() * 0.55f,
                                     tint.withAlpha (0.12f * amt), 0.f, k.getY(), false);
            g.setGradientFill (tg);
            g.fillRoundedRectangle (k.withTrimmedTop (-4.f), 3.f);
        }
        g.setColour (juce::Colours::black.withAlpha (0.25f));                       // seam
        g.drawLine (k.getRight(), k.getY(), k.getRight(), k.getBottom(), 1.f);
        g.setColour (juce::Colours::white.withAlpha (0.55f));                       // front lip
        g.fillRect (k.getX() + 2.f, k.getBottom() - 3.f, k.getWidth() - 4.f, 1.f);

        if (p % 12 == 0)
        {
            g.setFont (ui::caps (juce::jmin (10.f, k.getWidth() * 0.5f)));
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.drawText ("C" + juce::String (p / 12 - 1), k.withTrimmedBottom (4.f), juce::Justification::centredBottom, false);
        }
    }

    // black keys
    for (int p = km.lo; p <= km.hi; ++p)
    {
        if (! theory::isBlack (p)) continue;
        auto k = keyRect (p, kb);
        juce::Colour tint; float amt;
        const bool held = keyColourTint (p, tint, amt);
        if (held) k = k.withTrimmedBottom (3.f);

        g.setColour (juce::Colours::black.withAlpha (0.5f));                         // cast shadow
        g.fillRoundedRectangle (k.translated (2.f, 2.f), 2.5f);

        juce::ColourGradient grad (juce::Colour (0xff3a3c42), k.getX(), k.getY(), juce::Colour (0xff0b0c0e), k.getX(), k.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (k, 2.5f);
        if (amt > 0.f)
        {
            g.setColour (tint.withAlpha (0.65f * amt));
            g.fillRoundedRectangle (k.withTrimmedTop (k.getHeight() * 0.45f), 2.5f);
        }
        g.setColour (juce::Colours::white.withAlpha (0.16f));                        // gloss
        g.fillRoundedRectangle (k.reduced (k.getWidth() * 0.22f, 3.f).withWidth (juce::jmax (1.5f, k.getWidth() * 0.14f)).withHeight (k.getHeight() * 0.7f), 1.f);
    }

    // scale dots on the white keys
    if (pShowScale->load() > 0.5f)
        if (const auto* s = proc.getSession())
        {
            const int root = scaleRootPc (*s), type = (int) pScaleType->load();
            for (int p = km.lo; p <= km.hi; ++p)
                if (inScale (p, root, type))
                {
                    const auto k = keyRect (p, kb);
                    const bool black = theory::isBlack (p);
                    const bool isRoot = ((p - root) % 12 + 12) % 12 == 0;
                    g.setColour (ui::col::cyan.withAlpha (isRoot ? 1.f : 0.75f));
                    const float d = isRoot ? 6.f : 4.f;
                    g.fillEllipse (k.getCentreX() - d * 0.5f, k.getY() + (black ? k.getHeight() - 10.f : k.getHeight() - (p % 12 == 0 ? 26.f : 14.f)), d, d);
                }
        }

    // keyboard top shadow cast by the glass
    juce::ColourGradient sh (juce::Colours::black.withAlpha (0.55f), 0.f, kb.getY(), juce::Colours::transparentBlack, 0.f, kb.getY() + 12.f, false);
    g.setGradientFill (sh);
    g.fillRect (kb.withHeight (12.f));
}

//==============================================================================
// Mouse: play the on-screen keys (velocity from where you click)
//==============================================================================
void StageView::mouseDown (const juce::MouseEvent& e)
{
    if (auto* p = getParentComponent()) p->grabKeyboardFocus();
    const int p = pitchAt (e.position);
    if (p < 0) return;
    const float f = juce::jlimit (0.f, 1.f, (e.position.y - ((float) getHeight() - kbHeight)) / kbHeight);
    proc.virtualNote (p, (int) (45.f + 80.f * f), true);
    mouseNote = p;
}

void StageView::mouseDrag (const juce::MouseEvent& e)
{
    if (mouseNote < 0) return;
    const int p = pitchAt (e.position);
    if (p >= 0 && p != mouseNote)
    {
        proc.virtualNote (mouseNote, 64, false);
        proc.virtualNote (p, 85, true);
        mouseNote = p;
    }
}

void StageView::mouseUp (const juce::MouseEvent&)
{
    if (mouseNote >= 0) proc.virtualNote (mouseNote, 64, false);
    mouseNote = -1;
}

void StageView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (auto* p = apvts.getParameter (pid::lookahead.getParamID()))
        p->setValueNotifyingHost (juce::jlimit (0.f, 1.f, p->getValue() - w.deltaY * 0.25f));
}
} // namespace pt
