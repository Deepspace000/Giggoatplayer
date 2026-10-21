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

        // face
        auto face = r.reduced (rad * 0.1f);
        g.setGradientFill (ColourGradient (pressed ? Colour (0xff8d9ba1) : Colour (0xffe6edf0), face.getX(), face.getY(),
                                           pressed ? Colour (0xffd5dee2) : Colour (0xff6c7a80), face.getRight(), face.getBottom(), false));
        g.fillEllipse (face);

        // brushed concentric grain
        Path clip;
        clip.addEllipse (face);
        g.saveState();
        g.reduceClipRegion (clip);
        const float fr = face.getWidth() * 0.5f;
        for (float q = 1.0f; q < fr; q += 1.3f)
        {
            const int h = (int) (q * 13.0f) % 5;
            g.setColour ((h < 2 ? Colours::white : Colours::black).withAlpha (0.035f + 0.015f * (float) h));
            g.drawEllipse (cx - q, cy - q, q * 2.0f, q * 2.0f, 0.6f);
        }
        // soft specular glow, top left
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.55f), cx - fr * 0.45f, cy - fr * 0.55f,
                                           Colours::transparentWhite, cx, cy + fr * 0.2f, true));
        g.fillEllipse (face);
        g.restoreState();

        g.setColour (Colour (0xff1c2427).withAlpha (0.8f));
        g.drawEllipse (face, 1.0f);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override
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
        val.addCentredArc (cx, cy, ringR, ringR, 0.0f, startAngle, angle, true);
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
        g.setGradientFill (ColourGradient (down ? Colour (0xff9aa8ae) : Colour (0xfff0f5f7), 0, r.getY(),
                                           Colour (0xff68767c), 0, r.getBottom(), false));
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
        g.setColour (Colours::white.withAlpha (0.3f));
        g.fillRect (r.withHeight (r.getHeight() * 0.45f));
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
        g.setGradientFill (ColourGradient (Colour (0x66021c20), 0, r.getY(), Colour (0x33021c20), 0, r.getBottom(), false));
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
