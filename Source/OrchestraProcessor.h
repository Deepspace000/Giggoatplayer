#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "GigEngine.h"

// Many .gig players in one plugin ("bays"). Each bay has its own file, volume, pan,
// fine pitch and MIDI channel. The list of bays only grows while you work.
class OrchestraProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kMaxBays = 16;

    OrchestraProcessor();
    ~OrchestraProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Orchestra of Goats GiG Player"; }
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

    // used by the editor (message thread)
    int  getNumBays() const { return numBays.load(); }
    bool addBay();
    gigeng::GigEngine* getEngine (int bay) const { return engines[bay].load (std::memory_order_acquire); }
    void openFile (int bay, const juce::File& file);
    void chooseInstrument (int bay, int index);
    juce::File getFile (int bay) const { return files[bay]; }
    void setPreloadFrames (int frames);
    int  getPreloadFrames() const { return preloadFrames; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void ensureEngine (int bay);
    void renderChunk (float* left, float* right, int n, int nb);

    std::atomic<gigeng::GigEngine*> engines[kMaxBays] = {};
    std::unique_ptr<gigeng::GigEngine> owned[kMaxBays];
    juce::File files[kMaxBays];
    int instruments[kMaxBays] = {};
    std::atomic<int> numBays { 1 };
    int preloadFrames = 32768;

    std::mutex setupMutex;
    double sampleRate = 44100.0;
    int blockSize = 512;

    juce::AudioBuffer<float> scratch;
    float lastPanL[kMaxBays], lastPanR[kMaxBays];
    std::atomic<float>* pVol[kMaxBays] = {};
    std::atomic<float>* pPan[kMaxBays] = {};
    std::atomic<float>* pFine[kMaxBays] = {};
    std::atomic<float>* pCh[kMaxBays] = {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrchestraProcessor)
};
