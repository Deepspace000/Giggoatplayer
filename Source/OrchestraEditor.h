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

// A whole-number control for a parameter: [-] [editable text] [+]. Click the buttons,
// click the text and type, or use the mouse wheel.
class Spinner : public juce::Component
{
public:
    Spinner (juce::RangedAudioParameter& p,
             std::function<juce::String (int)> toText,
             std::function<int (const juce::String&)> fromText)
        : param (p), format (std::move (toText)), parse (std::move (fromText)),
          attachment (p, [this] (float v) { show ((int) std::lround (v)); })
    {
        label.setEditable (true, false, false);        // a single click starts typing
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, juce::Colours::white);
        label.setColour (juce::Label::backgroundColourId, juce::Colour (0xff0d2b2e));
        label.setColour (juce::Label::outlineColourId, juce::Colour (0xff3b7f86));
        label.onTextChange = [this] { set (parse (label.getText())); };
        addAndMakeVisible (label);
        addAndMakeVisible (up);
        addAndMakeVisible (down);
        down.onClick = [this] { set (value() - 1); };
        up.onClick   = [this] { set (value() + 1); };
        attachment.sendInitialUpdate();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto arrows = r.removeFromRight (22);
        up.setBounds (arrows.removeFromTop (arrows.getHeight() / 2).reduced (2, 1));
        down.setBounds (arrows.reduced (2, 1));
        label.setBounds (r.reduced (1, 0));
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (w.deltaY != 0.0f) set (value() + (w.deltaY > 0.0f ? 1 : -1));
    }

private:
    int value() const { return (int) std::lround (param.convertFrom0to1 (param.getValue())); }
    void show (int v) { label.setText (format (v), juce::dontSendNotification); }
    void set (int v)
    {
        const auto& range = param.getNormalisableRange();
        v = juce::jlimit ((int) range.start, (int) range.end, v);
        attachment.setValueAsCompleteGesture ((float) v);
        show (v);                                        // also puts back the text if the typing was junk
    }

    juce::RangedAudioParameter& param;
    std::function<juce::String (int)> format;
    std::function<int (const juce::String&)> parse;
    juce::ArrowButton up { "up", 0.75f, juce::Colour (0xffd8f6f2) }, down { "down", 0.25f, juce::Colour (0xffd8f6f2) };
    juce::Label label;
    juce::ParameterAttachment attachment;
};

// One load bay: file, instrument, volume, pan, fine pitch and MIDI channel.
class BayStrip : public juce::Component
{
public:
    BayStrip (OrchestraProcessor&, int bayIndex);

    static constexpr int kHeight = 192;

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
    std::unique_ptr<Spinner> octave, lowKey, highKey;
    juce::Label octaveL, rangeL, toL;
    juce::TextEditor nameEditor;
    juce::TextButton muteButton { "M" }, soloButton { "S" };
    juce::Label fileLabel, statusLabel, volumeL, panL, fineL, channelL;
    juce::ComboBox instrumentBox, channelBox;
    juce::Slider volume, pan, fine, atk, dec, sus, rel;
    juce::Label atkL, decL, susL, relL;

    juce::AudioProcessorValueTreeState::SliderAttachment volumeA, panA, fineA;
    juce::AudioProcessorValueTreeState::ComboBoxAttachment channelA;
    juce::AudioProcessorValueTreeState::ButtonAttachment muteA, soloA;
    juce::AudioProcessorValueTreeState::SliderAttachment atkA, decA, susA, relA;

    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<std::string> shownInstruments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BayStrip)
};

// One lane per part over a keyboard strip. Drag either end of a bar to change that part's
// lowest / highest key, or drag the middle to slide the whole range.
class RangeMap : public juce::Component
{
public:
    explicit RangeMap (OrchestraProcessor&);

    static int heightFor (int bays);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    enum class Grab { none, low, high, both, sweep };

    float noteToX (int note) const;
    int xToNote (float x) const;
    int laneAt (int y) const;
    int getValue (const char* prefix, int lane) const;
    void setValue (const char* prefix, int lane, int v);
    Grab hitTest (const juce::MouseEvent&, int lane) const;
    void gestures (int lane, bool begin);

    OrchestraProcessor& proc;
    int dragLane = -1;
    Grab grab = Grab::none;
    int grabOffset = 0;
    int selected = 0;          // the part the keyboard strip edits

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RangeMap)
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

    RangeMap rangeMap { proc };
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<BayStrip> strips;

    juce::TextButton addButton { "+  Add part" };
    juce::ComboBox preloadBox;
    juce::Label preloadLabel, infoLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrchestraEditor)
};
