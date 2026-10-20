#include "PluginProcessor.h"
#include "PluginEditor.h"

GigGoatProcessor::GigGoatProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    pVolume  = apvts.getRawParameterValue ("volume");
    pTune    = apvts.getRawParameterValue ("tune");
    pAttack  = apvts.getRawParameterValue ("attack");
    pRelease = apvts.getRawParameterValue ("release");
}

GigGoatProcessor::~GigGoatProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout GigGoatProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "volume", 1 }, "Volume",
                                                        NormalisableRange<float> (-48.0f, 12.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "tune", 1 }, "Tune (cents)",
                                                        NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "attack", 1 }, "Extra attack (ms)",
                                                        NormalisableRange<float> (0.0f, 2000.0f, 1.0f, 0.4f), 0.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "release", 1 }, "Release time (x)",
                                                        NormalisableRange<float> (0.1f, 4.0f, 0.01f, 0.5f), 1.0f));
    return { p.begin(), p.end() };
}

void GigGoatProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, std::max (samplesPerBlock, 512));
}

bool GigGoatProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void GigGoatProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();
    const int numSamples = buffer.getNumSamples();
    float* left  = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;

    // parameters
    engine.masterGain.store (juce::Decibels::decibelsToGain (pVolume->load()));
    engine.tuneCents.store (pTune->load());
    engine.attackExtra.store (pAttack->load() * 0.001f);
    engine.releaseScale.store (pRelease->load());

    int pos = 0;
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        const int at = std::min (std::max (metadata.samplePosition, pos), numSamples);

        if (at > pos)
        {
            engine.render (left + pos, right + pos, at - pos);
            pos = at;
        }

        if (msg.isNoteOn())                engine.noteOn (msg.getNoteNumber(), msg.getVelocity());
        else if (msg.isNoteOff())          engine.noteOff (msg.getNoteNumber());
        else if (msg.isController())       engine.controller (msg.getControllerNumber(), msg.getControllerValue());
        else if (msg.isPitchWheel())       engine.pitchBend (msg.getPitchWheelValue());
        else if (msg.isChannelPressure())  engine.channelPressure (msg.getChannelPressureValue());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) engine.allNotesOff();
    }

    if (pos < numSamples)
        engine.render (left + pos, right + pos, numSamples - pos);

    if (buffer.getNumChannels() > 1 && right == left)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    midi.clear();
}

juce::AudioProcessorEditor* GigGoatProcessor::createEditor()
{
    return new GigGoatEditor (*this);
}

// ---------------------------------------------------------------------------
void GigGoatProcessor::openFile (const juce::File& file)
{
    currentFile = file;
    currentInstrument = 0;
    engine.loadFile (file.getFullPathName().toStdString(), 0, preloadFrames);
}

void GigGoatProcessor::chooseInstrument (int index)
{
    currentInstrument = index;
    engine.selectInstrument (index, preloadFrames);
}

void GigGoatProcessor::setPreloadFrames (int frames)
{
    preloadFrames = frames;
    if (currentFile.existsAsFile())
        engine.selectInstrument (currentInstrument, preloadFrames);
}

// ---------------------------------------------------------------------------
void GigGoatProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("gigPath", currentFile.getFullPathName(), nullptr);
    state.setProperty ("gigInstrument", currentInstrument, nullptr);
    state.setProperty ("gigPreload", preloadFrames, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void GigGoatProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;

    auto state = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (state);

    preloadFrames = (int) state.getProperty ("gigPreload", 32768);
    currentInstrument = (int) state.getProperty ("gigInstrument", 0);
    const juce::File f (state.getProperty ("gigPath", "").toString());
    if (f.existsAsFile())
    {
        currentFile = f;
        engine.loadFile (f.getFullPathName().toStdString(), currentInstrument, preloadFrames);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GigGoatProcessor();
}
