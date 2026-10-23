#pragma once

#include "OrchestraProcessor.h"
#include "GlassLookAndFeel.h"
#include "GoatBranding.h"

// Small level bar: green to red, falls back smoothly.
class LevelMeter : public juce::Component
{
public:
    void push (float peak)
    {
        shown = std::max (peak, shown * 0.8f);
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0d2b2e));
        g.fillRoundedRectangle (b, 3.0f);
        const float db = juce::Decibels::gainToDecibels (shown, -60.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        auto fill = b.reduced (1.5f);
        fill.setWidth (fill.getWidth() * frac);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3fd9a0), b.getX(), 0.0f,
                                                 juce::Colour (0xffff5a4a), b.getRight(), 0.0f, false));
        g.fillRoundedRectangle (fill, 2.0f);
    }
private:
    float shown = 0.0f;
};

// One load bay: file, instrument, volume, pan, fine pitch and MIDI channel.
class BayStrip : public juce::Component
{
public:
    BayStrip (OrchestraProcessor&, int bayIndex);

    static constexpr int kHeight = 140;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();                       // called 10 times a second

    // totals for the footer
    int voices = 0;
    double ramMB = 0.0;

private:
    void chooseFile();
    void syncInstrumentList (const gigeng::GigEngine::Status&);

    OrchestraProcessor& proc;
    const int bay;

    juce::TextButton openButton { "Open .gig..." };
    juce::TextButton prevButton { "<" };
    juce::TextButton nextButton { ">" };
    LevelMeter meterBar;
    juce::Slider octave, lowKey, highKey;
    juce::Label octaveL, rangeL, toL;
    juce::TextEditor nameEditor;
    juce::TextButton muteButton { "M" }, soloButton { "S" };
    juce::Label fileLabel, statusLabel, volumeL, panL, fineL, channelL;
    juce::ComboBox instrumentBox, channelBox;
    juce::Slider volume, pan, fine;

    juce::AudioProcessorValueTreeState::SliderAttachment volumeA, panA, fineA;
    juce::AudioProcessorValueTreeState::ComboBoxAttachment channelA;
    juce::AudioProcessorValueTreeState::ButtonAttachment muteA, soloA;
    juce::AudioProcessorValueTreeState::SliderAttachment octA, loA, hiA;

    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<std::string> shownInstruments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BayStrip)
};

class OrchestraEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit OrchestraEditor (OrchestraProcessor&);
    ~OrchestraEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuild();
    void updateSize();

    OrchestraProcessor& proc;
    GlassLookAndFeel glassLook;
    juce::Image grain;

    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<BayStrip> strips;

    juce::TextButton addButton { "+  Add part" };
    juce::ComboBox preloadBox;
    juce::Label preloadLabel, infoLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrchestraEditor)
};
