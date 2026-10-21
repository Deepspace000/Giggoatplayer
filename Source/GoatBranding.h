#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <BinaryData.h>

// Shared look for both plugins: the goat head picture, the handwritten title and the glass body.
namespace goatbrand
{

inline juce::Image goatHead()
{
    return juce::ImageCache::getFromMemory (BinaryData::goat_head_png, BinaryData::goat_head_pngSize);
}

// First handwriting font that exists on this PC.
inline juce::Font handwritten (float height)
{
    static const juce::String name = []
    {
        const auto all = juce::Font::findAllTypefaceNames();
        for (auto* n : { "Ink Free", "Segoe Script", "Lucida Handwriting", "Brush Script MT", "Comic Sans MS" })
            if (all.contains (n)) return juce::String (n);
        return juce::String ("Georgia");
    }();
    return juce::Font (juce::FontOptions (name, height, juce::Font::bold));
}

inline void drawGoatHead (juce::Graphics& g, juce::Rectangle<float> box)
{
    auto img = goatHead();
    if (img.isNull()) return;
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    const auto r = juce::RectanglePlacement (juce::RectanglePlacement::centred)
                       .appliedTo (img.getBounds().toFloat(), box);
    juce::Path shape;
    shape.addEllipse (r.reduced (r.getWidth() * 0.12f));
    juce::DropShadow (juce::Colour (0x99021c20), 6, { 1, 3 }).drawForPath (g, shape);
    g.setOpacity (1.0f);
    g.drawImage (img, r);
}

// Handwritten title: chrome-white fill, dark outline, stacked edge and a soft deep shadow.
inline void drawTitle (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (handwritten (area.getHeight() * 0.9f), text, 0.0f, 0.0f);
    juce::Path title;
    ga.createPath (title);
    if (title.isEmpty()) return;

    auto tb = title.getBounds();
    const float scale = juce::jmin (1.0f, (area.getWidth() - 8.0f) / tb.getWidth());
    title.applyTransform (juce::AffineTransform::translation (-tb.getX(), -tb.getY())
                              .scaled (scale)
                              .translated (area.getX(), area.getY() + (area.getHeight() - tb.getHeight() * scale) * 0.5f));
    tb = title.getBounds();

    for (int i = 4; i >= 1; --i)
    {
        g.setColour (juce::Colour (0xff04373e).withAlpha (0.6f));
        g.fillPath (title, juce::AffineTransform::translation ((float) i * 0.6f, (float) i * 0.8f));
    }
    juce::DropShadow (juce::Colour (0xaa011a1e), 9, { 3, 6 }).drawForPath (g, title);

    juce::ColourGradient fill (juce::Colours::white, 0, tb.getY(), juce::Colour (0xffbfe9ee), 0, tb.getBottom(), false);
    fill.addColour (0.55, juce::Colour (0xff9fd5da));
    fill.addColour (0.9, juce::Colour (0xffb8f5ee));
    g.setGradientFill (fill);
    g.fillPath (title);
    g.setColour (juce::Colour (0xff053f47));
    g.strokePath (title, juce::PathStrokeType (1.0f));
}

inline void drawByline (juce::Graphics& g, float x, float y)
{
    g.setFont (handwritten (15.0f));
    g.setColour (juce::Colour (0x77021c20));
    g.drawText ("by Bickering Goat", (int) x + 1, (int) y + 1, 300, 20, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xffd9fff9));
    g.drawText ("by Bickering Goat", (int) x, (int) y, 300, 20, juce::Justification::centredLeft);
}

// Turquoise glass body, same as the single-part player.
inline void drawGlassBody (juce::Graphics& g, juce::Rectangle<float> b, const juce::Image& grain)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3fe0d2), 0, 0, juce::Colour (0xff0b7f86), 0, b.getHeight(), false));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x7766fff0), b.getWidth() * 0.2f, b.getHeight() * 0.05f,
                                             juce::Colours::transparentBlack, b.getWidth() * 0.9f, b.getHeight() * 0.8f, true));
    g.fillAll();
    struct Blob { float x, y, r, a; };
    const Blob blobs[] = { { 0.12f, 0.80f, 150.0f, 0.34f }, { 0.55f, 0.95f, 190.0f, 0.28f },
                           { 0.92f, 0.55f, 130.0f, 0.30f }, { 0.78f, 0.12f, 110.0f, 0.22f } };
    for (auto& bl : blobs)
    {
        const float bx = bl.x * b.getWidth(), by = bl.y * juce::jmin (b.getHeight(), 700.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8fff9).withAlpha (bl.a), bx, by,
                                                 juce::Colours::transparentWhite, bx + bl.r, by, true));
        g.fillEllipse (bx - bl.r, by - bl.r, bl.r * 2.0f, bl.r * 2.0f);
    }
    g.setTiledImageFill (grain, 0, 0, 1.0f);
    g.fillAll();
    juce::Path sheen;
    sheen.startNewSubPath (0, 0);
    sheen.lineTo (b.getWidth(), 0);
    sheen.lineTo (b.getWidth(), 80.0f);
    sheen.quadraticTo (b.getWidth() * 0.5f, 120.0f, 0, 76.0f);
    sheen.closeSubPath();
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.30f), 0, 0,
                                             juce::Colours::white.withAlpha (0.04f), 0, 110.0f, false));
    g.fillPath (sheen);
}

// A raised glass panel (used for the main panel and every bay).
inline void drawGlassPanel (juce::Graphics& g, juce::Rectangle<float> panel, float corner = 14.0f)
{
    juce::Path pp;
    pp.addRoundedRectangle (panel, corner);
    juce::DropShadow (juce::Colour (0x55021c20), 10, { 0, 4 }).drawForPath (g, pp);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.24f), 0, panel.getY(),
                                             juce::Colours::white.withAlpha (0.06f), 0, panel.getBottom(), false));
    g.fillPath (pp);
    g.saveState();
    g.reduceClipRegion (pp);
    juce::Path refl;
    refl.startNewSubPath (panel.getX(), panel.getY());
    refl.lineTo (panel.getX() + panel.getWidth() * 0.45f, panel.getY());
    refl.lineTo (panel.getX() + panel.getWidth() * 0.2f, panel.getBottom());
    refl.lineTo (panel.getX(), panel.getBottom());
    refl.closeSubPath();
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.13f), panel.getX(), panel.getY(),
                                             juce::Colours::white.withAlpha (0.0f), panel.getX() + panel.getWidth() * 0.4f, panel.getBottom(), false));
    g.fillPath (refl);
    g.restoreState();
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.85f), 0, panel.getY(),
                                             juce::Colours::white.withAlpha (0.15f), 0, panel.getBottom(), false));
    g.drawRoundedRectangle (panel, corner, 1.3f);
}

inline juce::Image makeGrain()
{
    juce::Image grain (juce::Image::ARGB, 128, 128, true);
    juce::Random rnd (7);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
        {
            const auto a = (juce::uint8) rnd.nextInt (22);
            grain.setPixelAt (x, y, rnd.nextBool() ? juce::Colour::fromRGBA (255, 255, 255, a)
                                                   : juce::Colour::fromRGBA (0, 30, 30, a));
        }
    return grain;
}

} // namespace goatbrand
