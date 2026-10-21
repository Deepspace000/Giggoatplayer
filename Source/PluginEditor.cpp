#include "PluginEditor.h"

static void setupKnob (juce::Component& parent, juce::Slider& s, juce::Label& l, const juce::String& text,
                       const juce::String& suffix)
{
    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    s.setTextValueSuffix (suffix);
    parent.addAndMakeVisible (s);
    l.setText (text, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    parent.addAndMakeVisible (l);
}

GigGoatEditor::GigGoatEditor (GigGoatProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      volumeA (p.apvts, "volume", volume),
      tuneA (p.apvts, "tune", tune),
      attackA (p.apvts, "attack", attack),
      releaseA (p.apvts, "release", release)
{
    setLookAndFeel (&glassLook);
    grain = goatbrand::makeGrain();
    setSize (640, 440);

    addAndMakeVisible (openButton);
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);
    addAndMakeVisible (instrumentBox);
    addAndMakeVisible (preloadBox);
    addAndMakeVisible (progressBar);

    for (auto* l : { &fileLabel, &instLabel, &preloadLabel, &statusLabel, &infoLabel })
    {
        l->setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (*l);
    }
    fileLabel.setText ("No file loaded", juce::dontSendNotification);
    instLabel.setText ("Instrument", juce::dontSendNotification);
    preloadLabel.setText ("RAM per sample", juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffc9fff8));
    infoLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb5e6e1));

    preloadBox.addItem ("Small (8k frames)", 1);
    preloadBox.addItem ("Medium (16k frames)", 2);
    preloadBox.addItem ("Normal (32k frames)", 3);
    preloadBox.addItem ("Large (64k frames)", 4);
    preloadBox.addItem ("Extra large (128k frames)", 5);
    {
        const int f = proc.getPreloadFrames();
        preloadBox.setSelectedId (f <= 8192 ? 1 : f <= 16384 ? 2 : f <= 32768 ? 3 : f <= 65536 ? 4 : 5,
                                  juce::dontSendNotification);
    }
    preloadBox.onChange = [this]
    {
        static const int sizes[] = { 8192, 16384, 32768, 65536, 131072 };
        const int id = preloadBox.getSelectedId();
        if (id >= 1 && id <= 5) proc.setPreloadFrames (sizes[id - 1]);
    };

    openButton.onClick = [this] { chooseFile(); };
    instrumentBox.onChange = [this]
    {
        const int id = instrumentBox.getSelectedId();
        if (id >= 1) proc.chooseInstrument (id - 1);
    };
    prevButton.onClick = [this]
    {
        const int id = instrumentBox.getSelectedId();
        if (id > 1) instrumentBox.setSelectedId (id - 1, juce::sendNotificationSync);
    };
    nextButton.onClick = [this]
    {
        const int id = instrumentBox.getSelectedId();
        if (id < instrumentBox.getNumItems()) instrumentBox.setSelectedId (id + 1, juce::sendNotificationSync);
    };

    setupKnob (*this, volume,  volumeL,  "Volume", " dB");
    setupKnob (*this, tune,    tuneL,    "Tune", " cents");
    setupKnob (*this, attack,  attackL,  "Extra attack", " ms");
    setupKnob (*this, release, releaseL, "Release time", " x");

    startTimerHz (10);
    timerCallback();
}

GigGoatEditor::~GigGoatEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void GigGoatEditor::chooseFile()
{
    juce::File start = proc.getCurrentFile().existsAsFile()
                           ? proc.getCurrentFile().getParentDirectory()
                           : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    chooser = std::make_unique<juce::FileChooser> ("Choose a .gig file", start, "*.gig");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) proc.openFile (f);
                          });
}

void GigGoatEditor::syncInstrumentList (const gigeng::GigEngine::Status& st)
{
    if (st.instruments != shownInstruments)
    {
        shownInstruments = st.instruments;
        instrumentBox.clear (juce::dontSendNotification);
        int id = 1;
        for (auto& n : shownInstruments)
            instrumentBox.addItem (juce::String::fromUTF8 (n.c_str()), id++);
    }
    if (st.currentInstrument >= 0 && instrumentBox.getSelectedId() != st.currentInstrument + 1)
        instrumentBox.setSelectedId (st.currentInstrument + 1, juce::dontSendNotification);
}

void GigGoatEditor::timerCallback()
{
    proc.engine.cleanupRetired();
    const auto st = proc.engine.getStatus();

    syncInstrumentList (st);

    if (! st.path.empty())
        fileLabel.setText (juce::File (juce::String::fromUTF8 (st.path.c_str())).getFileName(), juce::dontSendNotification);

    const bool isError = st.message.rfind ("Error", 0) == 0;
    statusLabel.setColour (juce::Label::textColourId, isError ? juce::Colour (0xffff8a5c) : juce::Colour (0xffc9fff8));
    statusLabel.setText (juce::String::fromUTF8 (st.message.c_str()), juce::dontSendNotification);

    progressValue = st.progress;
    progressBar.setVisible (st.loading);

    infoLabel.setText ("Voices: " + juce::String (st.activeVoices)
                           + "    RAM: " + juce::String ((double) st.ramBytes / (1024.0 * 1024.0), 0) + " MB"
                           + "    Disk underruns: " + juce::String (st.underruns),
                       juce::dontSendNotification);
}

void GigGoatEditor::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    goatbrand::drawGlassBody (g, b, grain);
    goatbrand::drawGlassPanel (g, b.reduced (10.0f).withTrimmedTop (62.0f));
    goatbrand::drawGoatHead (g, { 12.0f, 4.0f, 60.0f, 60.0f });
    goatbrand::drawTitle (g, { 76.0f, 8.0f, 380.0f, 36.0f }, "Goat GiG Player");
    goatbrand::drawByline (g, 82.0f, 42.0f);
}

void GigGoatEditor::resized()
{
    auto area = getLocalBounds().reduced (22);
    area.removeFromTop (52);

    auto row = area.removeFromTop (28);
    openButton.setBounds (row.removeFromLeft (170));
    row.removeFromLeft (10);
    fileLabel.setBounds (row);

    area.removeFromTop (8);
    row = area.removeFromTop (28);
    instLabel.setBounds (row.removeFromLeft (90));
    prevButton.setBounds (row.removeFromLeft (30));
    row.removeFromLeft (4);
    nextButton.setBounds (row.removeFromRight (30));
    row.removeFromRight (4);
    instrumentBox.setBounds (row);

    area.removeFromTop (8);
    row = area.removeFromTop (28);
    preloadLabel.setBounds (row.removeFromLeft (110));
    preloadBox.setBounds (row.removeFromLeft (220));

    area.removeFromTop (14);
    auto knobs = area.removeFromTop (150);
    const int w = knobs.getWidth() / 4;
    juce::Slider* sl[] = { &volume, &tune, &attack, &release };
    juce::Label* lb[] = { &volumeL, &tuneL, &attackL, &releaseL };
    for (int i = 0; i < 4; ++i)
    {
        auto cell = knobs.removeFromLeft (w);
        lb[i]->setBounds (cell.removeFromTop (20));
        sl[i]->setBounds (cell.reduced (8, 0));
    }

    area.removeFromTop (6);
    statusLabel.setBounds (area.removeFromTop (22));
    progressBar.setBounds (area.removeFromTop (16));
    area.removeFromTop (4);
    infoLabel.setBounds (area.removeFromTop (22));
}
