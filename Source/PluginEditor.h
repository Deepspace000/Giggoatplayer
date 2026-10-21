#pragma once

#include "PluginProcessor.h"
#include "GlassLookAndFeel.h"

class GigGoatEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit GigGoatEditor (GigGoatProcessor&);
    ~GigGoatEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseFile();
    void syncInstrumentList (const gigeng::GigEngine::Status&);

    GigGoatProcessor& proc;
    GlassLookAndFeel glassLook;
    juce::Image grain;

    juce::TextButton openButton { "Open .gig file..." };
    juce::TextButton prevButton { "<" };
    juce::TextButton nextButton { ">" };
    juce::Label fileLabel, instLabel, preloadLabel, statusLabel, infoLabel;
    juce::ComboBox instrumentBox, preloadBox;

    juce::Slider volume, tune, attack, release;
    juce::Label volumeL, tuneL, attackL, releaseL;
    juce::AudioProcessorValueTreeState::SliderAttachment volumeA, tuneA, attackA, releaseA;

    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };

    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<std::string> shownInstruments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GigGoatEditor)
};
