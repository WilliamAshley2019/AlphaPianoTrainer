#include "Theme.h"

namespace pt::ui
{
//==============================================================================
static juce::String pickTypeface()
{
    static const juce::String choice = []
    {
        const auto names = juce::Font::findAllTypefaceNames();
        for (auto* n : { "Segoe UI", "Inter", "Helvetica Neue", "Roboto", "DejaVu Sans" })
            if (names.contains (n)) return juce::String (n);
        return juce::Font::getDefaultSansSerifFontName();
    }();
    return choice;
}

juce::Font sans (float h, bool bold)
{
    return juce::Font (juce::FontOptions (pickTypeface(), h, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font caps (float h)
{
    return juce::Font (juce::FontOptions (pickTypeface(), h, juce::Font::bold).withKerningFactor (0.12f));
}

//==============================================================================
Textures::Textures()
{
    brushed = juce::Image (juce::Image::ARGB, 256, 64, true);
    juce::Graphics g (brushed);
    juce::Random r (0x70a11);
    for (int y = 0; y < 64; ++y)
    {
        // each scan-line gets a long, faint streak
        for (int pass = 0; pass < 3; ++pass)
        {
            const float a = r.nextFloat() * 0.05f;
            const int x0 = r.nextInt (256), len = 40 + r.nextInt (216);
            g.setColour ((r.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (a));
            g.fillRect (x0, y, len, 1);
            if (x0 + len > 256) g.fillRect (0, y, x0 + len - 256, 1);   // wrap, keeps the tile seamless
        }
    }
}

//==============================================================================
void brushedMetal (juce::Graphics& g, juce::Rectangle<float> r, float corner, bool raised, const Textures& t)
{
    juce::Path p; p.addRoundedRectangle (r, corner);

    juce::ColourGradient grad (raised ? col::metalHi : col::metalLo, r.getX(), r.getY(),
                               raised ? col::metalLo : col::metalHi, r.getX(), r.getBottom(), false);
    grad.addColour (0.5, col::metalMid);
    g.setGradientFill (grad);
    g.fillPath (p);

    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (p);
        g.setTiledImageFill (t.brushed, 0, (int) r.getY() & 63, 0.9f);
        g.fillPath (p);
    }

    // bevel: light catches the top edge, the bottom falls into shadow
    g.setColour (col::edgeLight.withAlpha (raised ? 0.55f : 0.2f));
    g.drawLine (r.getX() + corner * 0.6f, r.getY() + 0.5f, r.getRight() - corner * 0.6f, r.getY() + 0.5f, 1.f);
    g.setColour (col::edgeDark.withAlpha (0.9f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.f);
}

void glassWell (juce::Graphics& g, juce::Rectangle<float> r, float corner)
{
    juce::ColourGradient grad (col::glassHi, r.getX(), r.getY(), col::glass, r.getX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, corner);

    // inner shadow, top and left
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.5f);
    g.setColour (col::edgeLight.withAlpha (0.18f));
    g.drawLine (r.getX() + corner, r.getBottom() + 0.5f, r.getRight() - corner, r.getBottom() + 0.5f, 1.f);
}

void glow (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float spread, float alpha)
{
    const int steps = 5;
    for (int i = steps; i >= 1; --i)
    {
        const float k = (float) i / steps;
        g.setColour (c.withAlpha (alpha * (1.f - k) * 0.5f + 0.01f));
        g.fillRoundedRectangle (r.expanded (spread * k), r.getHeight() * 0.4f + spread * k);
    }
}

void engravedText (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r,
                   juce::Justification j, const juce::Font& f, bool lit)
{
    g.setFont (f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawText (s, r.translated (0.f, -1.f), j, false);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawText (s, r.translated (0.f, 1.f), j, false);
    g.setColour (lit ? col::amber : col::textDim);
    g.drawText (s, r, j, false);
}

//==============================================================================
// Seven-segment display
//==============================================================================
static void addHBar (juce::Path& p, float cx, float cy, float len, float t)
{
    const float h = t * 0.5f, l = len * 0.5f;
    p.startNewSubPath (cx - l, cy);          p.lineTo (cx - l + h, cy - h);
    p.lineTo (cx + l - h, cy - h);           p.lineTo (cx + l, cy);
    p.lineTo (cx + l - h, cy + h);           p.lineTo (cx - l + h, cy + h);
    p.closeSubPath();
}
static void addVBar (juce::Path& p, float cx, float cy, float len, float t)
{
    const float h = t * 0.5f, l = len * 0.5f;
    p.startNewSubPath (cx, cy - l);          p.lineTo (cx + h, cy - l + h);
    p.lineTo (cx + h, cy + l - h);           p.lineTo (cx, cy + l);
    p.lineTo (cx - h, cy + l - h);           p.lineTo (cx - h, cy - l + h);
    p.closeSubPath();
}

float sevenSegmentWidthPerHeight (const juce::String& text)
{
    float w = 0.f;
    for (auto c : text) w += (c == '.' || c == ':' || c == ' ' ? 0.22f : 0.56f) + 0.16f;
    return juce::jmax (0.1f, w - 0.16f + 0.1f);
}

void sevenSegment (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                   juce::Colour on, juce::Colour off, bool ghost)
{
    static const int mask[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

    const float h = area.getHeight();
    const float dw = h * 0.56f, gap = h * 0.16f, t = h * 0.12f, sg = t * 0.18f;
    const float narrow = h * 0.22f;

    // total width
    float total = 0.f;
    for (auto c : text) total += (c == '.' || c == ':' || c == ' ' ? narrow : dw) + gap;
    total -= gap;

    float x = area.getRight() - total;       // right aligned, like a calculator
    const float y = area.getY();
    const auto slant = juce::AffineTransform::shear (-0.1f, 0.f).translated (h * 0.05f, 0.f);

    auto drawDigit = [&] (float dx, int m, bool ghostPass)
    {
        const float L = dw - t - sg * 2.f, V = (h - t * 3.f) * 0.5f - sg * 2.f;
        juce::Path p[7];
        addHBar (p[0], dx + dw * 0.5f, y + t * 0.5f, L, t);
        addVBar (p[1], dx + dw - t * 0.5f, y + t * 0.5f + (h - t) * 0.25f, V + t * 0.5f, t);
        addVBar (p[2], dx + dw - t * 0.5f, y + h - t * 0.5f - (h - t) * 0.25f, V + t * 0.5f, t);
        addHBar (p[3], dx + dw * 0.5f, y + h - t * 0.5f, L, t);
        addVBar (p[4], dx + t * 0.5f, y + h - t * 0.5f - (h - t) * 0.25f, V + t * 0.5f, t);
        addVBar (p[5], dx + t * 0.5f, y + t * 0.5f + (h - t) * 0.25f, V + t * 0.5f, t);
        addHBar (p[6], dx + dw * 0.5f, y + h * 0.5f, L, t);
        for (int s = 0; s < 7; ++s)
        {
            const bool lit = (m >> s) & 1;
            if (ghostPass == lit) continue;
            p[s].applyTransform (slant);
            if (lit)
            {
                g.setColour (on.withAlpha (0.16f));
                g.strokePath (p[s], juce::PathStrokeType (t * 1.1f));
                g.setColour (on);
            }
            else g.setColour (off);
            g.fillPath (p[s]);
        }
    };

    for (auto c : text)
    {
        if (c >= '0' && c <= '9')
        {
            if (ghost) drawDigit (x, 0x7F, true);       // ghost "8"
            drawDigit (x, mask[c - '0'], false);
            x += dw + gap;
        }
        else if (c == '-')
        {
            if (ghost) drawDigit (x, 0x7F, true);
            drawDigit (x, 0x40, false);
            x += dw + gap;
        }
        else if (c == '.' || c == ':')
        {
            g.setColour (on);
            const float cx = x + narrow * 0.4f + h * 0.04f;
            if (c == '.') g.fillEllipse (cx - t * 0.6f, y + h - t * 1.2f, t * 1.2f, t * 1.2f);
            else
            {
                g.fillEllipse (cx - t * 0.55f, y + h * 0.3f, t * 1.1f, t * 1.1f);
                g.fillEllipse (cx - t * 0.55f, y + h * 0.66f, t * 1.1f, t * 1.1f);
            }
            x += narrow + gap;
        }
        else if (c == ' ')
        {
            if (ghost && false) {}
            x += narrow + gap;
        }
        else    // unsupported glyph: blank cell
        {
            if (ghost) drawDigit (x, 0x7F, true);
            x += dw + gap;
        }
    }
}

//==============================================================================
// Look and feel
//==============================================================================
AlumLookAndFeel::AlumLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, col::bg);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::TextButton::textColourOffId, col::textDim);
    setColour (juce::TextButton::textColourOnId, col::amber);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::PopupMenu::backgroundColourId, col::glass);
    setColour (juce::TooltipWindow::backgroundColourId, col::glass);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::TooltipWindow::outlineColourId, col::edgeLight);
}

void AlumLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float a0, float a1, juce::Slider& s)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.f);
    const float d = juce::jmin (b.getWidth(), b.getHeight());
    b = b.withSizeKeepingCentre (d, d);
    const auto c = b.getCentre();
    const float R = d * 0.5f;
    const float ang = a0 + pos * (a1 - a0);
    const bool enabled = s.isEnabled();

    // LED ring ------------------------------------------------------------------------
    {
        const float rr = R - 2.f, th = juce::jmax (2.f, d * 0.045f);
        juce::Path track; track.addCentredArc (c.x, c.y, rr, rr, 0.f, a0, a1, true);
        g.setColour (col::amberGhost.brighter (0.15f));
        g.strokePath (track, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path val; val.addCentredArc (c.x, c.y, rr, rr, 0.f, a0, ang, true);
        if (enabled && pos > 0.f)
        {
            g.setColour (col::amber.withAlpha (0.18f));
            g.strokePath (val, juce::PathStrokeType (th * 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (col::amber);
            g.strokePath (val, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // cap ------------------------------------------------------------------------------------
    const float rc = R * 0.74f;
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (c.x - rc + 1.5f, c.y - rc + 3.f, rc * 2.f, rc * 2.f);          // drop shadow

    juce::ColourGradient body (juce::Colour (0xff6a6d75), c.x - rc * 0.6f, c.y - rc * 0.8f,
                               juce::Colour (0xff1d1e22), c.x + rc * 0.7f, c.y + rc * 0.9f, true);
    body.addColour (0.45, juce::Colour (0xff3b3d44));
    g.setGradientFill (body);
    g.fillEllipse (c.x - rc, c.y - rc, rc * 2.f, rc * 2.f);

    // knurling on the rim
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    const int ticks = 48;
    for (int i = 0; i < ticks; ++i)
    {
        const float t = juce::MathConstants<float>::twoPi * (float) i / ticks;
        g.drawLine (c.x + std::sin (t) * rc * 0.9f, c.y - std::cos (t) * rc * 0.9f,
                    c.x + std::sin (t) * rc, c.y - std::cos (t) * rc, 1.f);
    }

    // turned-aluminium top face: concentric rings + a conic-ish sheen
    const float rf = rc * 0.78f;
    juce::ColourGradient face (juce::Colour (0xff53565e), c.x - rf, c.y - rf, juce::Colour (0xff2a2b30), c.x + rf, c.y + rf, false);
    g.setGradientFill (face);
    g.fillEllipse (c.x - rf, c.y - rf, rf * 2.f, rf * 2.f);
    for (int i = 1; i < 6; ++i)
    {
        const float r2 = rf * (float) i / 6.f;
        g.setColour (juce::Colours::white.withAlpha (i % 2 ? 0.045f : 0.02f));
        g.drawEllipse (c.x - r2, c.y - r2, r2 * 2.f, r2 * 2.f, 1.f);
    }
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    juce::Path sheen; sheen.addCentredArc (c.x, c.y, rf * 0.92f, rf * 0.92f, 0.f, -2.5f, -0.9f, true);
    g.strokePath (sheen, juce::PathStrokeType (1.4f));

    // pointer
    g.setColour (enabled ? col::amber : col::textFaint);
    const float s1 = std::sin (ang), c1 = -std::cos (ang);
    g.drawLine (c.x + s1 * rf * 0.28f, c.y + c1 * rf * 0.28f, c.x + s1 * rf * 0.86f, c.y + c1 * rf * 0.86f, juce::jmax (1.5f, d * 0.035f));
}

void keyPlate (juce::Graphics& g, juce::Rectangle<float> r, bool down, bool on, const Textures& t)
{
    brushedMetal (g, r, 4.f, ! down, t);
    if (down) { g.setColour (juce::Colours::black.withAlpha (0.25f)); g.fillRoundedRectangle (r, 4.f); }

    // tiny LED in the top-right corner
    const float d = juce::jmin (5.f, r.getHeight() * 0.22f);
    const juce::Rectangle<float> led (r.getRight() - d - 5.f, r.getY() + 5.f, d, d);
    if (on) { glow (g, led, col::amber, 5.f, 0.5f); g.setColour (col::amber); }
    else    g.setColour (col::amberGhost.brighter (0.2f));
    g.fillEllipse (led);
}

void AlumLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.f);
    keyPlate (g, r, down, b.getToggleState(), *tex);
    if (hi && ! down) { g.setColour (juce::Colours::white.withAlpha (0.05f)); g.fillRoundedRectangle (r, 4.f); }
}

void AlumLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    g.setFont (caps (juce::jmin (11.5f, b.getHeight() * 0.42f)));
    g.setColour (b.getToggleState() ? col::text : col::textDim);
    g.drawText (b.getButtonText(), b.getLocalBounds().translated (0, down ? 1 : 0).reduced (4, 0),
                juce::Justification::centred, true);
}

void AlumLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hi, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.f);
    keyPlate (g, r, down, b.getToggleState(), *tex);
    if (hi && ! down) { g.setColour (juce::Colours::white.withAlpha (0.05f)); g.fillRoundedRectangle (r, 4.f); }
    g.setFont (caps (juce::jmin (11.f, b.getHeight() * 0.42f)));
    g.setColour (b.getToggleState() ? col::text : col::textDim);
    g.drawText (b.getButtonText(), b.getLocalBounds().reduced (6, 0).translated (0, down ? 1 : 0), juce::Justification::centredLeft, true);
}

void AlumLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox&)
{
    auto r = juce::Rectangle<float> (0.f, 0.f, (float) w, (float) h).reduced (1.f);
    brushedMetal (g, r, 4.f, ! down, *tex);
    juce::Path tri;
    const float cx = (float) w - 12.f, cy = (float) h * 0.5f;
    tri.addTriangle (cx - 4.f, cy - 2.f, cx + 4.f, cy - 2.f, cx, cy + 3.f);
    g.setColour (col::textDim);
    g.fillPath (tri);
}

void AlumLookAndFeel::positionComboBoxText (juce::ComboBox& c, juce::Label& l)
{
    l.setBounds (8, 1, c.getWidth() - 26, c.getHeight() - 2);
    l.setFont (getComboBoxFont (c));
}

void AlumLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    juce::ColourGradient grad (col::glassHi.withAlpha (0.97f), 0.f, 0.f, col::glass.withAlpha (0.97f), 0.f, (float) h, false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (0.f, 0.f, (float) w, (float) h, 6.f);
    g.setColour (col::edgeLight.withAlpha (0.5f));
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.f, (float) h - 1.f, 6.f, 1.f);
}

void AlumLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool sep, bool active,
                                         bool hi, bool ticked, bool sub, const juce::String& text, const juce::String&,
                                         const juce::Drawable*, const juce::Colour*)
{
    if (sep)
    {
        g.setColour (col::edgeLight.withAlpha (0.3f));
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }
    auto r = area.reduced (3, 1);
    if (hi && active) { g.setColour (col::amber.withAlpha (0.2f)); g.fillRoundedRectangle (r.toFloat(), 3.f); }
    g.setColour (! active ? col::textFaint : hi ? col::amber : col::text);
    g.setFont (getPopupMenuFont());
    g.drawText (text, r.withTrimmedLeft (22).withTrimmedRight (16), juce::Justification::centredLeft, true);
    if (ticked)
    {
        g.setColour (col::amber);
        g.fillEllipse ((float) r.getX() + 8.f, (float) r.getCentreY() - 2.5f, 5.f, 5.f);
    }
    if (sub)
    {
        juce::Path p; p.addTriangle ((float) r.getRight() - 12.f, (float) r.getCentreY() - 4.f,
                                     (float) r.getRight() - 12.f, (float) r.getCentreY() + 4.f, (float) r.getRight() - 6.f, (float) r.getCentreY());
        g.fillPath (p);
    }
}

void AlumLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                     int thumbStart, int thumbSize, bool over, bool down)
{
    juce::ignoreUnused (x, y, w, h);
    juce::Rectangle<float> thumb = vertical ? juce::Rectangle<float> ((float) x + 2.f, (float) thumbStart, (float) w - 4.f, (float) thumbSize)
                                            : juce::Rectangle<float> ((float) thumbStart, (float) y + 2.f, (float) thumbSize, (float) h - 4.f);
    g.setColour (col::amber.withAlpha (down ? 0.8f : over ? 0.55f : 0.32f));
    g.fillRoundedRectangle (thumb, thumb.getWidth() * 0.5f);
}

void AlumLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                        juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal) { LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s); return; }
    const float cy = (float) y + (float) h * 0.5f;
    juce::Rectangle<float> track ((float) x, cy - 2.f, (float) w, 4.f);
    glassWell (g, track.expanded (0.f, 1.f), 2.f);
    g.setColour (col::amber.withAlpha (0.85f));
    g.fillRoundedRectangle (track.withRight (pos), 2.f);
    const juce::Rectangle<float> cap (pos - 7.f, cy - 9.f, 14.f, 18.f);
    brushedMetal (g, cap, 3.f, true, *tex);
    g.setColour (col::amber);
    g.fillRect (cap.withSizeKeepingCentre (2.f, 9.f));
}
} // namespace pt::ui
