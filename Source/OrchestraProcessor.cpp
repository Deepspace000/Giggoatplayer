#include "OrchestraProcessor.h"
#include "OrchestraEditor.h"

OrchestraProcessor::OrchestraProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int i = 0; i < kMaxBays; ++i)
    {
        const auto n = juce::String (i);
        pVol[i]  = apvts.getRawParameterValue ("vol" + n);
        pPan[i]  = apvts.getRawParameterValue ("pan" + n);
        pFine[i] = apvts.getRawParameterValue ("fine" + n);
        pCh[i]   = apvts.getRawParameterValue ("ch" + n);
        lastPanL[i] = lastPanR[i] = 1.0f;
    }
    scratch.setSize (2, blockSize);
    ensureEngine (0);
}

OrchestraProcessor::~OrchestraProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout OrchestraProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    StringArray channels { "All" };
    for (int c = 1; c <= 16; ++c) channels.add (String (c));

    for (int i = 0; i < kMaxBays; ++i)
    {
        const auto n = String (i);
        const auto part = "Part " + String (i + 1) + " ";
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "vol" + n, 1 }, part + "Volume",
                                                            NormalisableRange<float> (-48.0f, 12.0f, 0.1f), 0.0f));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "pan" + n, 1 }, part + "Pan",
                                                            NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "fine" + n, 1 }, part + "Fine pitch (cents)",
                                                            NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "ch" + n, 1 }, part + "MIDI channel",
                                                             channels, 0));
    }
    return { p.begin(), p.end() };
}

void OrchestraProcessor::ensureEngine (int bay)
{
    std::lock_guard<std::mutex> lock (setupMutex);
    if (owned[bay] != nullptr) return;
    owned[bay] = std::make_unique<gigeng::GigEngine>();
    owned[bay]->prepare (sampleRate, std::max (blockSize, 512));
    engines[bay].store (owned[bay].get(), std::memory_order_release);   // only now can the audio thread see it
}

bool OrchestraProcessor::addBay()
{
    const int n = numBays.load();
    if (n >= kMaxBays) return false;
    ensureEngine (n);
    numBays.store (n + 1);
    return true;
}

void OrchestraProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    std::lock_guard<std::mutex> lock (setupMutex);
    sampleRate = sr;
    blockSize = std::max (samplesPerBlock, 512);
    scratch.setSize (2, blockSize, false, false, true);
    for (int i = 0; i < kMaxBays; ++i)
        if (owned[i] != nullptr) owned[i]->prepare (sr, blockSize);
}

bool OrchestraProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void OrchestraProcessor::renderChunk (float* left, float* right, int n, int nb)
{
    float* sl = scratch.getWritePointer (0);
    float* sr = scratch.getWritePointer (1);

    for (int i = 0; i < nb; ++i)
    {
        auto* eng = getEngine (i);
        if (eng == nullptr) continue;

        std::fill (sl, sl + n, 0.0f);
        std::fill (sr, sr + n, 0.0f);
        eng->render (sl, sr, n);

        // balance-style pan: centre is unity, moving away turns the far side down
        const float pan = pPan[i]->load();
        const float tl = pan > 0.0f ? std::cos (pan * juce::MathConstants<float>::halfPi) : 1.0f;
        const float tr = pan < 0.0f ? std::cos (-pan * juce::MathConstants<float>::halfPi) : 1.0f;
        const float dl = (tl - lastPanL[i]) / (float) n, dr = (tr - lastPanR[i]) / (float) n;
        float gl = lastPanL[i], gr = lastPanR[i];
        for (int s = 0; s < n; ++s)
        {
            gl += dl; gr += dr;
            left[s]  += sl[s] * gl;
            right[s] += sr[s] * gr;
        }
        lastPanL[i] = tl; lastPanR[i] = tr;
    }
}

void OrchestraProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();
    const int numSamples = buffer.getNumSamples();
    float* left  = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;
    const int nb = numBays.load();
    const int maxChunk = scratch.getNumSamples();
    if (maxChunk <= 0) { midi.clear(); return; }

    for (int i = 0; i < nb; ++i)
        if (auto* e = getEngine (i))
        {
            e->masterGain.store (juce::Decibels::decibelsToGain (pVol[i]->load()));
            e->tuneCents.store (pFine[i]->load());
        }

    auto renderTo = [&] (int from, int to)
    {
        while (from < to)
        {
            const int n = std::min (to - from, maxChunk);
            renderChunk (left + from, right + from, n, nb);
            from += n;
        }
    };

    int pos = 0;
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        const int at = std::min (std::max (metadata.samplePosition, pos), numSamples);
        if (at > pos) { renderTo (pos, at); pos = at; }

        const int channel = msg.getChannel();           // 1..16, or 0 for system messages
        for (int i = 0; i < nb; ++i)
        {
            auto* e = getEngine (i);
            if (e == nullptr) continue;
            const int want = (int) (pCh[i]->load() + 0.5f);   // 0 = all
            if (want != 0 && channel != want) continue;

            if (msg.isNoteOn())                e->noteOn (msg.getNoteNumber(), msg.getVelocity());
            else if (msg.isNoteOff())          e->noteOff (msg.getNoteNumber());
            else if (msg.isController())       e->controller (msg.getControllerNumber(), msg.getControllerValue());
            else if (msg.isPitchWheel())       e->pitchBend (msg.getPitchWheelValue());
            else if (msg.isChannelPressure())  e->channelPressure (msg.getChannelPressureValue());
            else if (msg.isAllNotesOff() || msg.isAllSoundOff()) e->allNotesOff();
        }
    }
    if (pos < numSamples) renderTo (pos, numSamples);

    if (buffer.getNumChannels() > 1 && right == left)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
    midi.clear();
}

juce::AudioProcessorEditor* OrchestraProcessor::createEditor()
{
    return new OrchestraEditor (*this);
}

// ---------------------------------------------------------------------------
void OrchestraProcessor::openFile (int bay, const juce::File& file)
{
    ensureEngine (bay);
    files[bay] = file;
    instruments[bay] = 0;
    owned[bay]->loadFile (file.getFullPathName().toStdString(), 0, preloadFrames);
}

void OrchestraProcessor::chooseInstrument (int bay, int index)
{
    instruments[bay] = index;
    if (auto* e = getEngine (bay)) e->selectInstrument (index, preloadFrames);
}

void OrchestraProcessor::setPreloadFrames (int frames)
{
    preloadFrames = frames;
    for (int i = 0; i < numBays.load(); ++i)
        if (files[i].existsAsFile())
            if (auto* e = getEngine (i)) e->selectInstrument (instruments[i], preloadFrames);
}

// ---------------------------------------------------------------------------
void OrchestraProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    const int n = numBays.load();
    state.setProperty ("bays", n, nullptr);
    state.setProperty ("gigPreload", preloadFrames, nullptr);
    for (int i = 0; i < n; ++i)
    {
        state.setProperty ("gigPath" + juce::String (i), files[i].getFullPathName(), nullptr);
        state.setProperty ("gigInstrument" + juce::String (i), instruments[i], nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void OrchestraProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;

    auto state = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (state);
    preloadFrames = (int) state.getProperty ("gigPreload", 32768);

    const int n = juce::jlimit (1, kMaxBays, (int) state.getProperty ("bays", 1));
    for (int i = 0; i < n; ++i) ensureEngine (i);
    numBays.store (std::max (n, numBays.load()));

    for (int i = 0; i < n; ++i)
    {
        instruments[i] = (int) state.getProperty ("gigInstrument" + juce::String (i), 0);
        const juce::File f (state.getProperty ("gigPath" + juce::String (i), "").toString());
        if (f.existsAsFile())
        {
            files[i] = f;
            owned[i]->loadFile (f.getFullPathName().toStdString(), instruments[i], preloadFrames);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OrchestraProcessor();
}
