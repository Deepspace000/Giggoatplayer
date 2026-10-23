#include "OrchestraEditor.h"

namespace
{
constexpr int kWidth = 780;
constexpr int kHeader = 72;
constexpr int kFooter = 58;
constexpr int kGap = 6;
constexpr int kMaxViewportHeight = 620;

void setupKnob (juce::Component& parent, juce::Slider& s, juce::Label& l, const juce::String& text)
{
    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    parent.addAndMakeVisible (s);
    l.setText (text, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setFont (juce::FontOptions (12.0f));
    parent.addAndMakeVisible (l);
}

juce::ComboBox& primeChannelBox (juce::ComboBox& box)
{
    box.addItem ("All", 1);
    for (int c = 1; c <= 16; ++c) box.addItem (juce::String (c), c + 1);
    return box;
}
} // namespace

// ===========================================================================
BayStrip::BayStrip (OrchestraProcessor& p, int bayIndex)
    : proc (p), bay (bayIndex),
      volumeA (p.apvts, "vol" + juce::String (bayIndex), volume),
      panA    (p.apvts, "pan" + juce::String (bayIndex), pan),
      fineA   (p.apvts, "fine" + juce::String (bayIndex), fine),
      channelA (p.apvts, "ch" + juce::String (bayIndex), primeChannelBox (channelBox)),
      muteA (p.apvts, "mute" + juce::String (bayIndex), muteButton),
      soloA (p.apvts, "solo" + juce::String (bayIndex), soloButton)
{
    addAndMakeVisible (nameEditor);
    nameEditor.setTextToShowWhenEmpty ("Name...", juce::Colour (0xff7fb5b0));
    nameEditor.setText (proc.getBayName (bay), false);
    nameEditor.setSelectAllWhenFocused (true);
    nameEditor.setFont (juce::FontOptions (14.0f));
    nameEditor.onTextChange = [this] { proc.setBayName (bay, nameEditor.getText()); };
    nameEditor.onReturnKey = [this] { nameEditor.giveAwayKeyboardFocus(); };

    muteButton.setClickingTogglesState (true);
    soloButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9534f));
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe8c34a));
    muteButton.setTooltip ("Mute this part");
    soloButton.setTooltip ("Solo this part");
    addAndMakeVisible (muteButton);
    addAndMakeVisible (soloButton);

    addAndMakeVisible (openButton);
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);
    addAndMakeVisible (instrumentBox);
    addAndMakeVisible (channelBox);

    for (auto* l : { &fileLabel, &statusLabel })
        addAndMakeVisible (*l);
    fileLabel.setText ("No file loaded", juce::dontSendNotification);
    fileLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    statusLabel.setFont (juce::FontOptions (12.0f));
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb5e6e1));

    setupKnob (*this, volume, volumeL, "Volume");
    setupKnob (*this, pan,    panL,    "Pan");
    setupKnob (*this, fine,   fineL,   "Fine pitch");
    volume.setTextValueSuffix (" dB");
    fine.setTextValueSuffix (" ct");
    volume.setDoubleClickReturnValue (true, 0.0);
    pan.setDoubleClickReturnValue (true, 0.0);
    fine.setDoubleClickReturnValue (true, 0.0);
    pan.textFromValueFunction = [] (double v)
    {
        if (v < -0.005) return "L " + juce::String (juce::roundToInt (-v * 100.0));
        if (v >  0.005) return "R " + juce::String (juce::roundToInt (v * 100.0));
        return juce::String ("C");
    };
    pan.valueFromTextFunction = [] (const juce::String& t)
    {
        const double n = t.getTrailingIntValue() / 100.0;
        return t.startsWithIgnoreCase ("L") ? -n : t.startsWithIgnoreCase ("R") ? n : 0.0;
    };

    pan.updateText();
    channelL.setText ("MIDI ch", juce::dontSendNotification);
    channelL.setJustificationType (juce::Justification::centred);
    channelL.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (channelL);

    openButton.onClick = [this] { chooseFile(); };
    instrumentBox.onChange = [this]
    {
        const int id = instrumentBox.getSelectedId();
        if (id >= 1) proc.chooseInstrument (bay, id - 1);
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
}

void BayStrip::chooseFile()
{
    juce::File start = proc.getFile (bay).existsAsFile()
                           ? proc.getFile (bay).getParentDirectory()
                           : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    chooser = std::make_unique<juce::FileChooser> ("Choose a .gig file for part " + juce::String (bay + 1), start, "*.gig");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) proc.openFile (bay, f);
                          });
}

void BayStrip::syncInstrumentList (const gigeng::GigEngine::Status& st)
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

void BayStrip::refresh()
{
    auto* e = proc.getEngine (bay);
    if (e == nullptr) return;
    e->cleanupRetired();
    const auto st = e->getStatus();
    syncInstrumentList (st);

    if (! st.path.empty())
        fileLabel.setText (juce::File (juce::String::fromUTF8 (st.path.c_str())).getFileName(), juce::dontSendNotification);

    const bool isError = st.message.rfind ("Error", 0) == 0;
    statusLabel.setColour (juce::Label::textColourId, isError ? juce::Colour (0xffff8a5c) : juce::Colour (0xffc9fff8));
    juce::String msg = juce::String::fromUTF8 (st.message.c_str());
    if (st.loading) msg << "  " << juce::String (juce::roundToInt (st.progress * 100.0f)) << "%";
    statusLabel.setText (msg, juce::dontSendNotification);

    voices = st.activeVoices;
    ramMB = (double) st.ramBytes / (1024.0 * 1024.0);
}

void BayStrip::paint (juce::Graphics& g)
{
    goatbrand::drawGlassPanel (g, getLocalBounds().toFloat().reduced (2.0f), 12.0f);

    // numbered steel badge
    auto badge = juce::Rectangle<float> (16.0f, 12.0f, 30.0f, 30.0f);
    GlassLookAndFeel::drawSteelDisc (g, badge, false);
    g.setColour (juce::Colour (0xff10383d));
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (juce::String (bay + 1), badge.toNearestInt(), juce::Justification::centred);
}

void BayStrip::resized()
{
    auto area = getLocalBounds().reduced (6);
    {
        auto col = area.removeFromLeft (62);
        col.removeFromTop (44);
        auto ms = col.removeFromTop (26);
        muteButton.setBounds (ms.removeFromLeft (28));
        soloButton.setBounds (ms.removeFromRight (28));
    }

    auto knobs = area.removeFromRight (3 * 78 + 66);
    auto left = area.reduced (0, 4);

    auto row = left.removeFromTop (26);
    openButton.setBounds (row.removeFromLeft (118));
    row.removeFromLeft (8);
    nameEditor.setBounds (row.removeFromLeft (170));
    row.removeFromLeft (8);
    fileLabel.setBounds (row);

    left.removeFromTop (6);
    row = left.removeFromTop (26);
    prevButton.setBounds (row.removeFromLeft (28));
    row.removeFromLeft (4);
    nextButton.setBounds (row.removeFromRight (28));
    row.removeFromRight (4);
    instrumentBox.setBounds (row);

    left.removeFromTop (6);
    statusLabel.setBounds (left.removeFromTop (20));

    auto chCol = knobs.removeFromRight (62);
    channelL.setBounds (chCol.removeFromTop (16).withTrimmedTop (0));
    channelBox.setBounds (chCol.removeFromTop (26).reduced (2, 0));

    juce::Slider* sl[] = { &volume, &pan, &fine };
    juce::Label* lb[] = { &volumeL, &panL, &fineL };
    for (int i = 0; i < 3; ++i)
    {
        auto cell = knobs.removeFromLeft (78);
        lb[i]->setBounds (cell.removeFromTop (16));
        sl[i]->setBounds (cell);
    }
}

// ===========================================================================
OrchestraEditor::OrchestraEditor (OrchestraProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&glassLook);
    grain = goatbrand::makeGrain();

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);

    addAndMakeVisible (addButton);
    addButton.onClick = [this]
    {
        if (proc.addBay())
        {
            rebuild();
            viewport.setViewPosition (0, content.getHeight());
        }
    };

    preloadLabel.setText ("RAM per sample", juce::dontSendNotification);
    preloadLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    infoLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb5e6e1));
    infoLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (preloadLabel);
    addAndMakeVisible (infoLabel);
    addAndMakeVisible (preloadBox);

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

    rebuild();
    startTimerHz (10);
}

OrchestraEditor::~OrchestraEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void OrchestraEditor::rebuild()
{
    const int n = proc.getNumBays();
    while (strips.size() < n)
    {
        auto* s = strips.add (new BayStrip (proc, strips.size()));
        content.addAndMakeVisible (s);
    }
    updateSize();
}

void OrchestraEditor::updateSize()
{
    const int n = strips.size();
    const int contentH = n * (BayStrip::kHeight + kGap);
    const int viewH = juce::jmin (contentH, kMaxViewportHeight);
    addButton.setEnabled (n < OrchestraProcessor::kMaxBays);

    if (getHeight() != kHeader + viewH + kFooter || getWidth() != kWidth)
        setSize (kWidth, kHeader + viewH + kFooter);
    else
        resized();
}

void OrchestraEditor::timerCallback()
{
    if (strips.size() != proc.getNumBays()) rebuild();

    int voices = 0;
    double ram = 0.0;
    for (auto* s : strips)
    {
        s->refresh();
        voices += s->voices;
        ram += s->ramMB;
    }
    infoLabel.setText ("Voices: " + juce::String (voices) + "    RAM: " + juce::String (ram, 0) + " MB",
                       juce::dontSendNotification);
}

void OrchestraEditor::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    goatbrand::drawGlassBody (g, b, grain);
    goatbrand::drawGoatHead (g, { 12.0f, 4.0f, 64.0f, 64.0f });
    goatbrand::drawTitle (g, { 82.0f, 8.0f, 560.0f, 40.0f }, "Orchestra of Goats GiG Player");
    goatbrand::drawByline (g, 90.0f, 46.0f);

    // footer plate
    goatbrand::drawGlassPanel (g, b.withTop (b.getBottom() - (float) kFooter + 4.0f).reduced (8.0f, 4.0f), 12.0f);
}

void OrchestraEditor::resized()
{
    auto area = getLocalBounds();
    auto footer = area.removeFromBottom (kFooter).reduced (18, 14);
    area.removeFromTop (kHeader);
    viewport.setBounds (area.reduced (8, 0));

    const int w = viewport.getMaximumVisibleWidth();
    const int n = strips.size();
    content.setSize (w, n * (BayStrip::kHeight + kGap));
    for (int i = 0; i < n; ++i)
        strips[i]->setBounds (0, i * (BayStrip::kHeight + kGap), w, BayStrip::kHeight);

    addButton.setBounds (footer.removeFromLeft (150));
    footer.removeFromLeft (16);
    preloadLabel.setBounds (footer.removeFromLeft (100));
    preloadBox.setBounds (footer.removeFromLeft (200));
    infoLabel.setBounds (footer);
}
