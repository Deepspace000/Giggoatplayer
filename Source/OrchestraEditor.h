#pragma once

#include "OrchestraProcessor.h"
#include "GlassLookAndFeel.h"
#include "GoatBranding.h"

// One load bay: file, instrument, volume, pan, fine pitch and MIDI channel.
class BayStrip : public juce::Component
{
public:
    BayStrip (OrchestraProcessor&, int bayIndex);

    static constexpr int kHeight = 104;

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
    juce::Label fileLabel, statusLabel, volumeL, panL, fineL, channelL;
    juce::ComboBox instrumentBox, channelBox;
    juce::Slider volume, pan, fine;

    juce::AudioProcessorValueTreeState::SliderAttachment volumeA, panA, fineA;
    juce::AudioProcessorValueTreeState::ComboBoxAttachment channelA;

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
