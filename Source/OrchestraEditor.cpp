#include "OrchestraEditor.h"

namespace
{
constexpr int kWidth = 780;
constexpr int kHeader = 72;
constexpr int kFooter = 58;
constexpr int kGap = 6;
constexpr int kMaxViewportHeight = 560;

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

juce::String noteName (int n)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[n % 12]) + juce::String (n / 12 - 1);   // 60 = C4
}

int noteFromText (const juce::String& t)
{
    auto s = t.trim().toUpperCase();
    if (s.isNotEmpty() && s.containsOnly ("0123456789")) return juce::jlimit (0, 127, s.getIntValue());
    static const int base[] = { 9, 11, 0, 2, 4, 5, 7 };   // A..G
    if (s.isEmpty() || s[0] < 'A' || s[0] > 'G') return 60;
    int n = base[s[0] - 'A'];
    int i = 1;
    if (i < s.length() && (s[i] == '#' || s[i] == 'S')) { ++n; ++i; }
    else if (i < s.length() && s[i] == 'B') { --n; ++i; }
    return juce::jlimit (0, 127, (s.substring (i).getIntValue() + 1) * 12 + n);
}

void setupMini (juce::Component& parent, juce::Slider& s, juce::Label& l, const juce::String& text)
{
    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
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
      soloA (p.apvts, "solo" + juce::String (bayIndex), soloButton),
      atkA (p.apvts, "atk" + juce::String (bayIndex), atk),
      decA (p.apvts, "dec" + juce::String (bayIndex), dec),
      susA (p.apvts, "sus" + juce::String (bayIndex), sus),
      relA (p.apvts, "rel" + juce::String (bayIndex), rel)
{
    setupMini (*this, atk, atkL, "Attack");
    setupMini (*this, dec, decL, "Decay");
    setupMini (*this, sus, susL, "Sustain");
    setupMini (*this, rel, relL, "Release");
    for (auto* s : { &atk, &dec, &rel })
    {
        s->setDoubleClickReturnValue (true, 0.0);
        s->textFromValueFunction = [] (double v) { return v <= 0.0005 ? juce::String ("file") : juce::String (v, 2) + " s"; };
        s->valueFromTextFunction = [] (const juce::String& t) { return t.containsAnyOf ("0123456789") ? t.getDoubleValue() : 0.0; };
        s->updateText();
    }
    sus.setDoubleClickReturnValue (true, 1.0);
    sus.textFromValueFunction = [] (double v) { return v >= 0.995 ? juce::String ("file") : juce::String (juce::roundToInt (v * 100.0)) + " %"; };
    sus.valueFromTextFunction = [] (const juce::String& t) { return t.containsAnyOf ("0123456789") ? t.getDoubleValue() / 100.0 : 1.0; };
    sus.updateText();

    addAndMakeVisible (meterBar);
    auto param = [&] (const char* id) { return p.apvts.getParameter (id + juce::String (bayIndex)); };
    octave = std::make_unique<Spinner> (*param ("oct"),
                                        [] (int v) { return (v > 0 ? "+" : "") + juce::String (v); },
                                        [] (const juce::String& t) { return t.getIntValue(); });
    lowKey = std::make_unique<Spinner> (*param ("lo"), noteName, noteFromText);
    highKey = std::make_unique<Spinner> (*param ("hi"), noteName, noteFromText);
    for (auto* sp : { octave.get(), lowKey.get(), highKey.get() })
        addAndMakeVisible (*sp);
    for (auto* l : { &octaveL, &rangeL, &toL })
    {
        l->setFont (juce::FontOptions (12.0f));
        l->setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (*l);
    }
    octaveL.setText ("Octave", juce::dontSendNotification);
    rangeL.setText ("Range", juce::dontSendNotification);
    toL.setText ("to", juce::dontSendNotification);
    toL.setJustificationType (juce::Justification::centred);

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

    // a small magnetic stop at the middle so centre is easy to find
    pan.onValueChange = [this] { if (pan.getValue() != 0.0 && std::abs (pan.getValue()) < 0.03) pan.setValue (0.0); };
    fine.onValueChange = [this] { if (fine.getValue() != 0.0 && std::abs (fine.getValue()) < 3.0) fine.setValue (0.0); };
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

    meterBar.push (proc.takeMeter (bay));
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
        col.removeFromTop (8);
        meterBar.setBounds (col.removeFromTop (10).withTrimmedRight (6));
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

    // ADSR row at the bottom of the strip. "file" = the instrument's own envelope
    {
        auto adsr = area.removeFromBottom (66);
        juce::Slider* sl[] = { &atk, &dec, &sus, &rel };
        juce::Label* lb[] = { &atkL, &decL, &susL, &relL };
        const int cell = (adsr.getWidth() - 62) / 4;
        for (int i = 0; i < 4; ++i)
        {
            auto c = adsr.removeFromLeft (cell);
            lb[i]->setBounds (c.removeFromTop (14));
            sl[i]->setBounds (c);
        }
    }

    left.removeFromTop (6);
    row = left.removeFromTop (26);
    octaveL.setBounds (row.removeFromLeft (44));
    octave->setBounds (row.removeFromLeft (84));
    row.removeFromLeft (14);
    rangeL.setBounds (row.removeFromLeft (40));
    lowKey->setBounds (row.removeFromLeft (96));
    toL.setBounds (row.removeFromLeft (20));
    highKey->setBounds (row.removeFromLeft (96));

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
namespace
{
constexpr int kMapSide = 14;          // space left and right of the keys
constexpr int kMapTop = 5;
constexpr int kMapRuler = 18;         // keyboard strip
constexpr int kMapBottom = 6;

int laneHeightFor (int bays) { return bays <= 6 ? 18 : bays <= 10 ? 13 : 9; }

bool isBlackKey (int n)
{
    const int pc = n % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

juce::Colour partColour (int i)
{
    return juce::Colour::fromHSV (std::fmod (0.48f + 0.061f * (float) i, 1.0f), 0.55f, 0.95f, 1.0f);
}
}

RangeMap::RangeMap (OrchestraProcessor& p) : proc (p) {}

int RangeMap::heightFor (int bays)
{
    return kMapTop + kMapRuler + bays * laneHeightFor (bays) + kMapBottom;
}

float RangeMap::noteToX (int note) const
{
    const float w = (float) getWidth() - 2.0f * kMapSide;
    return (float) kMapSide + w * (float) note / 128.0f;
}

int RangeMap::xToNote (float x) const
{
    const float w = (float) getWidth() - 2.0f * kMapSide;
    return juce::jlimit (0, 127, (int) std::floor ((x - (float) kMapSide) / w * 128.0f));
}

int RangeMap::laneAt (int y) const
{
    const int n = proc.getNumBays();
    const int lane = (y - kMapTop - kMapRuler) / laneHeightFor (n);
    return (y >= kMapTop + kMapRuler && lane >= 0 && lane < n) ? lane : -1;
}

int RangeMap::getValue (const char* prefix, int lane) const
{
    return (int) (proc.apvts.getRawParameterValue (prefix + juce::String (lane))->load() + 0.5f);
}

void RangeMap::setValue (const char* prefix, int lane, int v)
{
    if (auto* p = proc.apvts.getParameter (prefix + juce::String (lane)))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) v));
}

void RangeMap::gestures (int lane, bool begin)
{
    for (const char* prefix : { "lo", "hi" })
        if (auto* p = proc.apvts.getParameter (prefix + juce::String (lane)))
        {
            if (begin) p->beginChangeGesture(); else p->endChangeGesture();
        }
}

RangeMap::Grab RangeMap::hitTest (const juce::MouseEvent& e, int lane) const
{
    const int lo = getValue ("lo", lane), hi = getValue ("hi", lane);
    const float x = (float) e.x, xl = noteToX (lo), xr = noteToX (hi + 1);
    const float dl = std::abs (x - xl), dr = std::abs (x - xr);
    if (dl <= 7.0f && dl <= dr) return Grab::low;
    if (dr <= 7.0f) return Grab::high;
    if (x > xl && x < xr) return Grab::both;
    return x < xl ? Grab::low : Grab::high;       // clicked beside the bar: pull the nearer end there
}

void RangeMap::mouseMove (const juce::MouseEvent& e)
{
    if (e.y >= kMapTop && e.y < kMapTop + kMapRuler) { setMouseCursor (juce::MouseCursor::CrosshairCursor); return; }
    const int lane = laneAt (e.y);
    if (lane < 0) { setMouseCursor (juce::MouseCursor::NormalCursor); return; }
    const auto g = hitTest (e, lane);
    setMouseCursor (g == Grab::both ? juce::MouseCursor::DraggingHandCursor
                                    : juce::MouseCursor::LeftRightResizeCursor);
}

void RangeMap::mouseDown (const juce::MouseEvent& e)
{
    if (e.y >= kMapTop && e.y < kMapTop + kMapRuler)
    {
        // drag across the keyboard to draw the selected part's range
        dragLane = juce::jmin (selected, proc.getNumBays() - 1);
        grab = Grab::sweep;
        grabOffset = xToNote ((float) e.x);          // where the sweep started
        gestures (dragLane, true);
        mouseDrag (e);
        return;
    }
    dragLane = laneAt (e.y);
    if (dragLane < 0) return;
    selected = dragLane;
    grab = hitTest (e, dragLane);
    grabOffset = xToNote ((float) e.x) - getValue ("lo", dragLane);
    gestures (dragLane, true);
    mouseDrag (e);
}

void RangeMap::mouseDrag (const juce::MouseEvent& e)
{
    if (dragLane < 0) return;
    const int note = xToNote ((float) e.x);
    const int lo = getValue ("lo", dragLane), hi = getValue ("hi", dragLane);

    if (grab == Grab::sweep)
    {
        setValue ("lo", dragLane, std::min (grabOffset, note));
        setValue ("hi", dragLane, std::max (grabOffset, note));
    }
    else if (grab == Grab::low)  setValue ("lo", dragLane, std::min (note, hi));
    else if (grab == Grab::high) setValue ("hi", dragLane, std::max (note, lo));
    else if (grab == Grab::both)
    {
        const int width = hi - lo;
        const int newLo = juce::jlimit (0, 127 - width, note - grabOffset);
        if (newLo > lo) { setValue ("hi", dragLane, newLo + width); setValue ("lo", dragLane, newLo); }
        else            { setValue ("lo", dragLane, newLo); setValue ("hi", dragLane, newLo + width); }
    }
    repaint();
}

void RangeMap::mouseUp (const juce::MouseEvent&)
{
    if (dragLane >= 0) gestures (dragLane, false);
    dragLane = -1;
    grab = Grab::none;
}

void RangeMap::paint (juce::Graphics& g)
{
    goatbrand::drawGlassPanel (g, getLocalBounds().toFloat().reduced (1.0f), 12.0f);

    const int bays = proc.getNumBays();
    const int laneH = laneHeightFor (bays);
    const float keyW = noteToX (1) - noteToX (0);
    const int lanesTop = kMapTop + kMapRuler;
    const float lanesBottom = (float) (lanesTop + bays * laneH);

    // lane backgrounds
    for (int i = 0; i < bays; ++i)
    {
        g.setColour (juce::Colour (i % 2 == 0 ? 0x30000000 : 0x18000000));
        g.fillRect ((float) kMapSide, (float) (lanesTop + i * laneH), noteToX (128) - (float) kMapSide, (float) laneH);
    }

    // the selected part is the one the keyboard strip edits
    const int sel = juce::jmin (selected, bays - 1);
    if (sel >= 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRect ((float) kMapSide, (float) (lanesTop + sel * laneH), noteToX (128) - (float) kMapSide, (float) laneH);
    }

    // keyboard strip with octave lines
    for (int n = 0; n < 128; ++n)
    {
        const bool black = isBlackKey (n);
        g.setColour (black ? juce::Colour (0xff10383d) : juce::Colour (0xffdcecec));
        g.fillRect (noteToX (n), (float) kMapTop, keyW - 0.6f, black ? kMapRuler * 0.6f : (float) kMapRuler - 2.0f);
    }
    if (sel >= 0)
    {
        const int lo = getValue ("lo", sel), hi = getValue ("hi", sel);
        g.setColour (partColour (sel).withAlpha (0.55f));
        g.fillRect (noteToX (lo), (float) kMapTop, noteToX (hi + 1) - noteToX (lo), (float) kMapRuler - 2.0f);
    }
    g.setFont (juce::FontOptions (9.0f));
    for (int n = 0; n < 128; n += 12)
    {
        const bool middleC = n == 60;
        g.setColour (juce::Colours::white.withAlpha (middleC ? 0.55f : 0.18f));
        g.fillRect (noteToX (n), (float) lanesTop, 1.0f, lanesBottom - (float) lanesTop);
        g.setColour (juce::Colour (0xff10383d));
        g.drawText ("C" + juce::String (n / 12 - 1), juce::Rectangle<float> (noteToX (n), (float) kMapTop + 8.0f, 22.0f, 10.0f),
                    juce::Justification::centredLeft, false);
    }

    // one bar per part
    for (int i = 0; i < bays; ++i)
    {
        const int lo = getValue ("lo", i), hi = getValue ("hi", i);
        const bool muted = proc.apvts.getRawParameterValue ("mute" + juce::String (i))->load() > 0.5f;
        const auto colour = partColour (i);
        const float y = (float) (lanesTop + i * laneH);
        const float xl = noteToX (lo), xr = noteToX (hi + 1);
        auto bar = juce::Rectangle<float> (xl, y + 1.5f, std::max (xr - xl, 3.0f), (float) laneH - 3.0f);

        g.setColour (colour.withAlpha (muted ? 0.30f : 0.85f));
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawRoundedRectangle (bar, 3.0f, 1.0f);
        g.setColour (juce::Colour (0xff10383d).withAlpha (0.8f));       // grab handles
        g.fillRect (bar.getX() + 1.5f, bar.getY() + 2.0f, 2.0f, bar.getHeight() - 4.0f);
        g.fillRect (bar.getRight() - 3.5f, bar.getY() + 2.0f, 2.0f, bar.getHeight() - 4.0f);

        // where it actually sounds once the octave shift is applied
        const int oct = (int) std::lround (proc.apvts.getRawParameterValue ("oct" + juce::String (i))->load());
        if (oct != 0)
        {
            const int sl = juce::jlimit (0, 127, lo + 12 * oct), sh = juce::jlimit (0, 127, hi + 12 * oct);
            g.setColour (colour.brighter (0.6f));
            g.fillRect (noteToX (sl), y + (float) laneH - 3.0f, std::max (noteToX (sh + 1) - noteToX (sl), 2.0f), 2.0f);
        }

        if (laneH >= 13 && bar.getWidth() > 40.0f)
        {
            auto name = proc.getBayName (i);
            auto text = juce::String (i + 1) + (name.isNotEmpty() ? "  " + name : juce::String())
                        + "   " + noteName (lo) + " - " + noteName (hi);
            g.setColour (juce::Colour (0xff0b2528));
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (text, bar.reduced (8.0f, 0.0f), juce::Justification::centredLeft, true);
        }
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
    addAndMakeVisible (rangeMap);

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

    const int total = kHeader + RangeMap::heightFor (n) + viewH + kFooter;
    if (getHeight() != total || getWidth() != kWidth)
        setSize (kWidth, total);
    else
        resized();
}

void OrchestraEditor::timerCallback()
{
    if (strips.size() != proc.getNumBays()) rebuild();

    rangeMap.repaint();

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
    rangeMap.setBounds (area.removeFromTop (RangeMap::heightFor (strips.size())).reduced (8, 0));
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
