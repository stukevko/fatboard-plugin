#include "TapEditor.h"
using namespace FatboardColours;
static const char keyChars[4] = { 'Q', 'W', 'E', 'R' };

void PadView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const auto accent = FatboardLookAndFeel::accentOf (*this);
    g.setColour (panel.interpolatedWith (accent, 0.75f * glow)); g.fillRoundedRectangle (r, 16.0f);
    g.setColour (glow > 0.3f ? black : bone); g.setFont (FatboardLookAndFeel::display (58.0f));
    g.drawText (key, r.withTrimmedBottom (26.0f), juce::Justification::centred, false);
    g.setColour (glow > 0.3f ? black : grey); g.setFont (FatboardLookAndFeel::body (12.5f));
    g.drawText (name, r.removeFromBottom (34.0f).reduced (8.0f, 0.0f), juce::Justification::centred, true);
}

void LoopView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const auto accent = FatboardLookAndFeel::accentOf (*this);
    g.setColour (panel); g.fillRoundedRectangle (r, 12.0f);
    auto in = r.reduced (14.0f, 8.0f); const float rowH = in.getHeight() / 4.0f;
    const int steps = (int) (loopBeats * 4.0);
    for (int s = 0; s <= steps; ++s)
    {
        const float x = in.getX() + in.getWidth() * (float) s / (float) steps;
        g.setColour (s % 16 == 0 ? grey.withAlpha (0.7f) : s % 4 == 0 ? line.brighter (0.2f) : line.withAlpha (0.5f));
        g.fillRect (juce::Rectangle<float> (x, in.getY(), 1.0f, in.getHeight()));
    }
    g.setFont (FatboardLookAndFeel::displayBold (10.0f));
    for (int p = 0; p < 4; ++p) { g.setColour (grey.withAlpha (0.55f)); g.drawText (juce::String::charToString ((juce::juce_wchar) keyChars[p]), juce::Rectangle<float> (in.getX() + 4.0f, in.getY() + (float) p * rowH, 20.0f, rowH), juce::Justification::centredLeft, false); }
    for (const auto& e : events)
    {
        const float x = in.getX() + (float) (e.beat / loopBeats) * in.getWidth();
        g.setColour (accent.withAlpha (0.45f + 0.55f * e.vel));
        g.fillRoundedRectangle (juce::Rectangle<float> (x, in.getY() + (float) e.pad * rowH + 3.0f, juce::jmax (4.0f, in.getWidth() / (float) steps - 2.0f), rowH - 6.0f), 2.0f);
    }
    if (events.empty()) { g.setColour (grey); g.setFont (FatboardLookAndFeel::body (12.5f)); g.drawText ("Press Rec, start FL and tap. Your loop shows up here.", in, juce::Justification::centred, false); }
    if (playBeat >= 0.0) { g.setColour (bone.withAlpha (0.8f)); g.fillRect (juce::Rectangle<float> (in.getX() + (float) (playBeat / loopBeats) * in.getWidth(), in.getY(), 1.5f, in.getHeight())); }
}

void LoopView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || events.empty() || ! makeFile || e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 8) return;
    dragStarted = true;
    const auto f = makeFile();
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

TapEditor::TapEditor (TapProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    header.power.setVisible (false);
    for (auto* c : std::initializer_list<juce::Component*> { &header, &loopView, &recButton, &clearButton, &levelKnob, &footer }) addAndMakeVisible (c);
    for (int i = 0; i < 4; ++i)
    {
        addAndMakeVisible (pads[i]);
        pads[i].onHit = [this, i] { proc.hit (i); grabKeyboardFocus(); };
        pads[i].onReset = [this, i] { proc.resetSample (i); };
    }
    bars  = std::make_unique<PillGroup> (*proc.apvts.getParameter (TapProcessor::kBars), "Loop");
    quant = std::make_unique<PillGroup> (*proc.apvts.getParameter (TapProcessor::kQuant), "Snap to grid");
    addAndMakeVisible (*bars); addAndMakeVisible (*quant);
    recButton.setClickingTogglesState (true);
    recButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFFFF3B3B));
    recAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, TapProcessor::kRec, recButton);
    clearButton.onClick = [this] { proc.clearLoop(); grabKeyboardFocus(); };
    recButton.onStateChange = [this] { if (! recButton.isDown()) grabKeyboardFocus(); };
    levelAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, TapProcessor::kLevel, levelKnob.slider);
    loopView.makeFile = [this] { return proc.writeMidi(); };
    loopView.onClearPad = [this] (int pad) { proc.clearPad (pad); };
    // every child passes its keys on to us
    for (auto* c : getChildren()) { c->setWantsKeyboardFocus (false); c->setMouseClickGrabsKeyboardFocus (false); }
    setWantsKeyboardFocus (true);
    setSize (640, 620);
    startTimerHz (40);
    timerCallback();
}
TapEditor::~TapEditor() { setLookAndFeel (nullptr); }

bool TapEditor::keyPressed (const juce::KeyPress& k)
{
    const auto c = juce::CharacterFunctions::toUpperCase (k.getTextCharacter());
    // some hosts only pass key presses, not key states: play from here too. `down` keeps auto-repeat from re-triggering.
    for (int i = 0; i < 4; ++i) if (c == (juce::juce_wchar) keyChars[i] || k.getKeyCode() == keyChars[i] || k.getKeyCode() == keyChars[i] + 32) { if (! down[i]) { down[i] = true; proc.hit (i); } return true; }
    if (k == juce::KeyPress::backspaceKey || k == juce::KeyPress::deleteKey) { proc.clearLoop(); return true; }
    return false;
}

bool TapEditor::keyStateChanged (bool)
{
    bool any = false;
    for (int i = 0; i < 4; ++i)
    {
        const bool now = juce::KeyPress::isKeyCurrentlyDown (keyChars[i]) || juce::KeyPress::isKeyCurrentlyDown (keyChars[i] + 32);
        if (now && ! down[i]) proc.hit (i);
        any = any || now != down[i]; down[i] = now;
    }
    return any;
}

bool TapEditor::isInterestedInFileDrag (const juce::StringArray& files) { for (const auto& f : files) if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) return true; return false; }
void TapEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    for (int i = 0; i < 4; ++i)
        if (pads[i].getBounds().contains (x, y))
            for (const auto& f : files) if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) { message = proc.loadSample (i, juce::File (f)); return; }
    message = "Drop the sound on one of the four pads.";
}

void TapEditor::timerCallback()
{
    for (int i = 0; i < 4; ++i) if (down[i] && ! (juce::KeyPress::isKeyCurrentlyDown (keyChars[i]) || juce::KeyPress::isKeyCurrentlyDown (keyChars[i] + 32))) down[i] = false;      // key is up again
    for (int i = 0; i < 4; ++i) { pads[i].set (juce::String::charToString ((juce::juce_wchar) keyChars[i]), proc.getPadName (i)); pads[i].flash (proc.getFlash (i)); }
    if (proc.getEventCount() != shown) { shown = proc.getEventCount(); loopView.set (proc.getEvents(), proc.getLoopBeats()); }
    loopView.setPlayBeat (proc.getPlayBeat());
    levelKnob.setLabel ("Level " + proc.apvts.getParameter (TapProcessor::kLevel)->getCurrentValueAsText());
    repaint (0, getHeight() - 28 - 28 - 22, getWidth(), 22);
}

void TapEditor::paint (juce::Graphics& g)
{
    g.fillAll (black);
    g.setFont (FatboardLookAndFeel::body (12.0f));
    juce::String t = message;
    if (t.isEmpty())
    {
        if (! hasKeyboardFocus (true)) t = "Click this window once so it gets your keys.";
        else if (recButton.getToggleState() && ! proc.isHostPlaying()) t = "Rec is on. Start FL and tap.";
        else t = "Q W E R to play.  Drop your own WAV on a pad (right-click: back to the built-in sound).  Drag the grid out as MIDI.";
    }
    g.setColour (hasKeyboardFocus (true) || message.isNotEmpty() ? grey : lnf.accent);
    g.drawText (t, getLocalBounds().reduced (28).removeFromBottom (28 + 22).removeFromTop (20), juce::Justification::centred, false);
}

void TapEditor::resized()
{
    auto r = getLocalBounds().reduced (28);
    header.setBounds (r.removeFromTop (56));
    footer.setBounds (r.removeFromBottom (28));
    r.removeFromBottom (22);
    auto padRow = r.removeFromTop (150);
    const int pw = padRow.getWidth() / 4;
    for (auto& p : pads) p.setBounds (padRow.removeFromLeft (pw).reduced (4, 0));
    r.removeFromTop (10);
    loopView.setBounds (r.removeFromTop (150));
    r.removeFromTop (10);
    auto row = r.removeFromTop (62);
    bars->setBounds (row.removeFromLeft (row.getWidth() / 2 - 6)); row.removeFromLeft (12);
    quant->setBounds (row);
    r.removeFromTop (8);
    auto bottom = r;
    levelKnob.setBounds (bottom.removeFromRight (130).withSizeKeepingCentre (96, 96));
    auto btn = bottom.withSizeKeepingCentre (bottom.getWidth(), 46);
    recButton.setBounds (btn.removeFromLeft (btn.getWidth() / 2 - 6)); btn.removeFromLeft (12);
    clearButton.setBounds (btn.withTrimmedRight (12));
}
