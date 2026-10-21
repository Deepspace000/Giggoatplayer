#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "GigEngine.h"

class GigGoatProcessor : public juce::AudioProcessor
{
public:
    GigGoatProcessor();
    ~GigGoatProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Goat GiG Player"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // used by the editor
    void openFile (const juce::File& file);
    void chooseInstrument (int index);
    void setPreloadFrames (int frames);
    int  getPreloadFrames() const { return preloadFrames; }
    juce::File getCurrentFile() const { return currentFile; }

    gigeng::GigEngine engine;
    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::File currentFile;
    int currentInstrument = 0;
    int preloadFrames = 32768;
    std::atomic<float>* pVolume  = nullptr;
    std::atomic<float>* pTune    = nullptr;
    std::atomic<float>* pAttack  = nullptr;
    std::atomic<float>* pRelease = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GigGoatProcessor)
};
