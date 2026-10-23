#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Turquoise glass panel look with round brushed-steel knobs and buttons.
class GlassLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GlassLookAndFeel()
    {
        using namespace juce;
        setColour (Label::textColourId, Colours::white);
        setColour (Slider::textBoxTextColourId, Colours::white);
        setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour (TextButton::textColourOffId, Colour (0xff1b2326));
        setColour (TextButton::textColourOnId, Colour (0xff1b2326));
        setColour (ComboBox::textColourId, Colours::white);
        setColour (ComboBox::arrowColourId, Colours::white);
        setColour (PopupMenu::backgroundColourId, Colour (0xff07424a));
        setColour (PopupMenu::textColourId, Colours::white);
        setColour (PopupMenu::highlightedBackgroundColourId, Colour (0xff2fd6c9));
        setColour (PopupMenu::highlightedTextColourId, Colour (0xff062a2f));
        setColour (ProgressBar::foregroundColourId, Colour (0xff6ff3e6));
        setColour (ProgressBar::backgroundColourId, Colour (0x55021c20));
    }

    // A disc of brushed steel with a bevel and a soft highlight.
    static void drawSteelDisc (juce::Graphics& g, juce::Rectangle<float> r, bool pressed)
    {
        using namespace juce;
        const float cx = r.getCentreX(), cy = r.getCentreY();
        const float rad = jmin (r.getWidth(), r.getHeight()) * 0.5f;

        Path disc;
        disc.addEllipse (r);
        DropShadow (Colours::black.withAlpha (0.5f), 7, { 0, pressed ? 1 : 3 }).drawForPath (g, disc);

        // outer bevel ring
        g.setGradientFill (ColourGradient (Colour (0xfff4f8fa), cx - rad, cy - rad,
                                           Colour (0xff3b464b), cx + rad, cy + rad, false));
        g.fillEllipse (r);

        // face: polished steel, light catching two opposite lobes like a turned-metal cap
        auto face = r.reduced (rad * 0.1f);
        Path clip;
        clip.addEllipse (face);
        g.saveState();
        g.reduceClipRegion (clip);
        const float fr = face.getWidth() * 0.5f;
        const int wedges = 180;
        for (int i = 0; i < wedges; ++i)
        {
            const float a0 = MathConstants<float>::twoPi * (float) i / wedges;
            const float a1 = MathConstants<float>::twoPi * (float) (i + 1) / wedges + 0.01f;
            const float m = 0.5f * (a0 + a1);
            const float l1 = std::pow (0.5f + 0.5f * std::cos (2.0f * (m - 0.9f)), 3.0f);
            const float l2 = 0.35f * std::pow (0.5f + 0.5f * std::cos (4.0f * (m - 2.4f)), 2.0f);
            float v = jlimit (0.0f, 1.0f, 0.18f + 0.85f * l1 + l2);
            if (pressed) v *= 0.8f;
            Path wedge;
            wedge.addTriangle (cx, cy, cx + std::sin (a0) * fr * 1.5f, cy - std::cos (a0) * fr * 1.5f,
                               cx + std::sin (a1) * fr * 1.5f, cy - std::cos (a1) * fr * 1.5f);
            g.setColour (Colour::fromFloatRGBA (0.20f + 0.78f * v, 0.25f + 0.74f * v, 0.28f + 0.72f * v, 1.0f));
            g.fillPath (wedge);
        }
        // fine brushed rings
        for (float q = 1.0f; q < fr; q += 1.1f)
        {
            g.setColour (((int) (q * 11.0f) % 3 == 0 ? Colours::white : Colours::black).withAlpha (0.045f));
            g.drawEllipse (cx - q, cy - q, q * 2.0f, q * 2.0f, 0.5f);
        }
        // darker edge so the cap looks domed
        g.setGradientFill (ColourGradient (Colours::transparentBlack, cx, cy,
                                           Colours::black.withAlpha (0.35f), cx + fr, cy, true));
        g.fillEllipse (face);
        // sharp glint (crescent) top left
        Path glint;
        glint.addCentredArc (cx, cy, fr * 0.86f, fr * 0.86f, 0.0f, -2.5f, -1.2f, true);
        g.setColour (Colours::white.withAlpha (0.9f));
        g.strokePath (glint, PathStrokeType (fr * 0.07f, PathStrokeType::curved, PathStrokeType::rounded));
        // bright soft spot
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.55f), cx - fr * 0.35f, cy - fr * 0.45f,
                                           Colours::transparentWhite, cx - fr * 0.35f + fr * 0.6f, cy - fr * 0.45f, true));
        g.fillEllipse (face);
        g.restoreState();

        g.setColour (Colours::white.withAlpha (0.8f));
        g.drawEllipse (face.reduced (0.5f), 0.8f);
        g.setColour (Colour (0xff1c2427).withAlpha (0.8f));
        g.drawEllipse (face, 1.0f);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        using namespace juce;
        auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
        const float rad = jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const float cx = b.getCentreX(), cy = b.getCentreY();
        const float angle = startAngle + pos * (endAngle - startAngle);

        // glowing value ring
        const float ringR = rad - 2.0f;
        Path track, val;
        track.addCentredArc (cx, cy, ringR, ringR, 0.0f, startAngle, endAngle, true);
        // knobs that go both ways (pan, fine pitch) light up from the middle, not from the left
        const bool bipolar = slider.getMinimum() < 0.0 && std::abs (slider.getMinimum() + slider.getMaximum()) < 1.0e-6;
        const float mid = (startAngle + endAngle) * 0.5f;
        if (bipolar) { if (std::abs (angle - mid) > 0.01f) val.addCentredArc (cx, cy, ringR, ringR, 0.0f, mid, angle, true); }
        else         val.addCentredArc (cx, cy, ringR, ringR, 0.0f, startAngle, angle, true);
        g.setColour (Colour (0x66021c20));
        g.strokePath (track, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Colour (0x556ff3e6));
        g.strokePath (val, PathStrokeType (8.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Colour (0xff9ffcf2));
        g.strokePath (val, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));

        const float k = rad - 9.0f;
        drawSteelDisc (g, { cx - k, cy - k, k * 2.0f, k * 2.0f }, false);

        // pointer
        Path ptr;
        ptr.addRoundedRectangle (-1.8f, -k * 0.82f, 3.6f, k * 0.38f, 1.6f);
        auto t = AffineTransform::rotation (angle).translated (cx, cy);
        g.setColour (Colours::white.withAlpha (0.55f));
        g.fillPath (ptr, t.translated (0.0f, 0.8f));
        g.setColour (Colour (0xff10383d));
        g.fillPath (ptr, t);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& bt, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        using namespace juce;
        auto r = bt.getLocalBounds().toFloat().reduced (3.0f);
        if (r.getWidth() <= r.getHeight() * 1.2f)
        {
            const float d = jmin (r.getWidth(), r.getHeight());
            r = r.withSizeKeepingCentre (d, d);
            drawSteelDisc (g, r, down);
            if (highlighted && ! down)
            {
                g.setColour (Colour (0x446ff3e6));
                g.drawEllipse (r.expanded (1.5f), 2.0f);
            }
            return;
        }

        // pill-shaped steel button (the "Open" button)
        const float cr = r.getHeight() * 0.5f;
        Path p;
        p.addRoundedRectangle (r, cr);
        DropShadow (Colours::black.withAlpha (0.5f), 6, { 0, down ? 1 : 3 }).drawForPath (g, p);
        ColourGradient chrome (down ? Colour (0xffb8c4c9) : Colour (0xfffbfdfe), 0, r.getY(),
                               Colour (0xffdbe5e9), 0, r.getBottom(), false);
        chrome.addColour (0.48, Colour (0xff8a9aa1));
        chrome.addColour (0.52, Colour (0xff3d4a50));
        chrome.addColour (0.75, Colour (0xff9fb1b8));
        g.setGradientFill (chrome);
        g.fillPath (p);
        g.saveState();
        g.reduceClipRegion (p);
        for (float yy = r.getY(); yy < r.getBottom(); yy += 1.4f) // brushed grain
        {
            if (((int) (yy * 9.0f) % 3) == 0)
            {
                g.setColour (Colours::white.withAlpha (0.07f));
                g.drawHorizontalLine ((int) yy, r.getX(), r.getRight());
            }
        }
        g.setColour (Colours::white.withAlpha (0.55f));
        g.fillRoundedRectangle (r.reduced (cr * 0.5f, 2.0f).withHeight (r.getHeight() * 0.22f), 4.0f);
        g.restoreState();
        g.setColour (Colour (0xff1c2427).withAlpha (0.8f));
        g.drawRoundedRectangle (r, cr, 1.0f);
        if (highlighted && ! down)
        {
            g.setColour (Colour (0x446ff3e6));
            g.drawRoundedRectangle (r.expanded (1.5f), cr + 1.5f, 2.0f);
        }
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
    {
        using namespace juce;
        auto r = Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
        g.setGradientFill (ColourGradient (Colour (0x33021c20), 0, r.getY(), Colour (0x14021c20), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (Colours::white.withAlpha (0.35f));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        Path arrow;
        const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
        arrow.addTriangle (ax - 4.5f, ay - 2.5f, ax + 4.5f, ay - 2.5f, ax, ay + 3.0f);
        g.setColour (Colours::white);
        g.fillPath (arrow);
    }
};
