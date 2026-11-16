#include "FunkeEditor.h"
using namespace FatboardColours;

void LaneView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const auto accent = FatboardLookAndFeel::accentOf (*this);
    g.setColour (panel); g.fillRoundedRectangle (r, 10.0f);
    auto in = r.reduced (12.0f, 8.0f);
    auto top = in.removeFromTop (14.0f);
    g.setColour (grey); g.setFont (FatboardLookAndFeel::displayBold (10.0f));
    g.drawText (name.toUpperCase(), top, juce::Justification::centredLeft, false);
    const int bars = juce::jmax (1, (int) (loopBeats / 4.0));
    g.setFont (FatboardLookAndFeel::body (11.0f));
    for (int b = 0; b < bars; ++b)
    {
        const float x = in.getX() + in.getWidth() * (float) b / (float) bars;
        g.setColour (line); g.fillRect (juce::Rectangle<float> (x, in.getY(), 1.0f, in.getHeight()));
        if (b < labels.size()) { g.setColour (bone.withAlpha (0.8f)); g.drawText (labels[b], juce::Rectangle<float> (x + 70.0f, top.getY(), in.getWidth() / (float) bars - 70.0f, top.getHeight()), juce::Justification::centredLeft, false); }
    }
    if (notes.empty()) return;
    int lo = 127, hi = 0; for (const auto& n : notes) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
    const float span = (float) juce::jmax (7, hi - lo + 1), h = juce::jmax (2.5f, in.getHeight() / span);
    for (const auto& n : notes)
    {
        const float x = in.getX() + (float) (n.start / loopBeats) * in.getWidth(), w = juce::jmax (2.5f, (float) (n.length / loopBeats) * in.getWidth() - 1.0f);
        const float y = in.getBottom() - ((float) (n.pitch - lo) + 1.0f) / span * in.getHeight();
        const bool playing = playBeat >= n.start && playBeat < n.start + n.length;
        g.setColour (playing ? bone : accent.withAlpha (0.55f + 0.45f * n.vel));
        g.fillRoundedRectangle (juce::Rectangle<float> (x, y, w, juce::jmin (h, 9.0f)), 1.5f);
    }
    if (playBeat >= 0.0) { g.setColour (bone.withAlpha (0.7f)); g.fillRect (juce::Rectangle<float> (in.getX() + (float) (playBeat / loopBeats) * in.getWidth(), in.getY(), 1.5f, in.getHeight())); }
}

void LaneView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || notes.empty() || ! makeFile || e.getDistanceFromDragStart() < 8) return;
    dragStarted = true;
    const auto f = makeFile();
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

void DrumStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const auto accent = FatboardLookAndFeel::accentOf (*this);
    g.setColour (panel); g.fillRoundedRectangle (r, 10.0f);
    auto in = r.reduced (12.0f, 6.0f);
    g.setFont (FatboardLookAndFeel::body (12.0f));
    if (! grid.valid) { g.setColour (grey); g.drawText (message, in, juce::Justification::centred, false); return; }
    auto text = in.removeFromLeft (150.0f);
    g.setColour (bone.withAlpha (0.85f)); g.drawFittedText (message, text.toNearestInt(), juce::Justification::centredLeft, 2);
    const float w = in.getWidth() / 32.0f, rowH = in.getHeight() / 2.0f;
    for (int i = 0; i < 32; ++i)
    {
        const float x = in.getX() + (float) i * w + (i >= 16 ? 4.0f : 0.0f) - 2.0f;
        for (int row = 0; row < 2; ++row)
        {
            const float v = row == 0 ? grid.kick[i] : grid.hit[i];
            auto cell = juce::Rectangle<float> (x + 1.5f, in.getY() + (float) row * rowH + 2.0f, w - 3.0f, rowH - 4.0f);
            g.setColour (i % 4 == 0 ? ring.brighter (0.25f) : ring); g.fillRoundedRectangle (cell, 2.0f);
            if (v > 0.0f) { g.setColour ((row == 0 ? accent : bone).withAlpha (0.35f + 0.65f * v)); g.fillRoundedRectangle (cell, 2.0f); }
        }
    }
}

FunkeEditor::FunkeEditor (FunkeProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    header.power.setVisible (false);
    for (auto* c : std::initializer_list<juce::Component*> { &header, &drumStrip, &rollAll, &listenButton, &allMidi, &clearDrums, &motionKnob, &densityKnob, &levelKnob, &footer }) addAndMakeVisible (c);
    vibe = std::make_unique<PillGroup> (*proc.apvts.getParameter (FunkeProcessor::kVibe), "Vibe");
    key  = std::make_unique<PillGroup> (*proc.apvts.getParameter (FunkeProcessor::kKey), "Key (the note your 808 sits on)");
    bars = std::make_unique<PillGroup> (*proc.apvts.getParameter (FunkeProcessor::kBars), "Length");
    drumMode = std::make_unique<PillGroup> (*proc.apvts.getParameter (FunkeProcessor::kDrumMode), "Against the drums");
    for (auto* g : { vibe.get(), key.get(), bars.get(), drumMode.get() }) addAndMakeVisible (*g);
    static const char* names[2] = { "Chords", "Melody" };
    for (int i = 0; i < 2; ++i)
    {
        addAndMakeVisible (lanes[i]); addAndMakeVisible (newButtons[i]);
        newButtons[i].setButtonText ("New");
        newButtons[i].setTooltip (juce::String ("Roll new ") + names[i] + ", keep the rest");
        newButtons[i].onClick = [this, i] { proc.roll ((FunkeProcessor::Layer) i); };
        lanes[i].makeFile = [this, i] { return proc.writeMidi ((FunkeProcessor::Layer) i); };
    }
    rollAll.onClick = [this] { proc.roll (FunkeProcessor::All); };
    clearDrums.onClick = [this] { proc.clearDrums(); drumError.clear(); };
    clearDrums.setTooltip ("Forget the drum loop");
    allMidi.setTooltip ("Drag from here: chords and melody in one MIDI file");
    allMidi.addMouseListener (this, false);
    listenButton.setClickingTogglesState (true);
    listenAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, FunkeProcessor::kListen, listenButton);
    listenButton.setTooltip ("Plays the idea with a simple built-in sound, in time with FL. Turn it off once the MIDI is on your own instruments.");
    motionAtt  = std::make_unique<SliderAtt> (proc.apvts, FunkeProcessor::kMotion, motionKnob.slider);
    densityAtt = std::make_unique<SliderAtt> (proc.apvts, FunkeProcessor::kDensity, densityKnob.slider);
    levelAtt   = std::make_unique<SliderAtt> (proc.apvts, FunkeProcessor::kLevel, levelKnob.slider);
    setSize (760, 760);
    startTimerHz (24);
    timerCallback();
}
FunkeEditor::~FunkeEditor() { allMidi.removeMouseListener (this); setLookAndFeel (nullptr); }

void FunkeEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (e.eventComponent != &allMidi || allDragStarted || e.getDistanceFromDragStart() < 8) return;
    allDragStarted = true;
    const auto f = proc.writeMidi (FunkeProcessor::All);
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

bool FunkeEditor::isInterestedInFileDrag (const juce::StringArray& files) { for (const auto& f : files) if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) return true; return false; }
void FunkeEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files) if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) { drumError = proc.loadDrums (juce::File (f)); return; }
}

void FunkeEditor::timerCallback()
{
    if (proc.getPatternCount() != shown)
    {
        shown = proc.getPatternCount();
        if (const auto p = proc.getPattern())
        {
            lanes[0].setNotes (p->chords, p->beats(), "Chords", p->chordNames);
            lanes[1].setNotes (p->melody, p->beats(), "Melody", {});
        }
    }
    const double beat = proc.getPlayBeat();
    for (auto& l : lanes) l.setPlayBeat (beat);
    const auto g = proc.getDrums();
    drumStrip.set (g, g.valid ? proc.getDrumName() + "\nkick / all hits" : (drumError.isNotEmpty() ? drumError : juce::String ("Drop your drum loop here (WAV at the project tempo, starting on the 1). Then the notes can follow the kick.")));
    clearDrums.setVisible (g.valid);
    drumMode->setAlpha (g.valid ? 1.0f : 0.4f);
    auto val = [this] (const char* id) { return proc.apvts.getParameter (id)->getCurrentValueAsText(); };
    motionKnob.setLabel ("Motion " + val (FunkeProcessor::kMotion)); densityKnob.setLabel ("Density " + val (FunkeProcessor::kDensity)); levelKnob.setLabel ("Level " + val (FunkeProcessor::kLevel));
}

void FunkeEditor::paint (juce::Graphics& g)
{
    g.fillAll (black);
    g.setColour (grey); g.setFont (FatboardLookAndFeel::body (12.0f));
    g.drawText ("Roll until it fits. Then drag a lane into the piano roll and play it with your own sound.  " + juce::String (proc.getHostBpm(), 0) + " bpm",
                getLocalBounds().reduced (28).removeFromBottom (28 + 22).removeFromTop (20), juce::Justification::centred, false);
}

void FunkeEditor::resized()
{
    auto r = getLocalBounds().reduced (28);
    header.setBounds (r.removeFromTop (56));
    footer.setBounds (r.removeFromBottom (28));
    r.removeFromBottom (22);
    vibe->setBounds (r.removeFromTop (62)); r.removeFromTop (4);
    key->setBounds (r.removeFromTop (62)); r.removeFromTop (4);
    auto row = r.removeFromTop (62);
    bars->setBounds (row.removeFromLeft (200)); row.removeFromLeft (12);
    drumMode->setBounds (row);
    r.removeFromTop (6);
    auto ds = r.removeFromTop (44);
    clearDrums.setBounds (ds.removeFromRight (30).reduced (0, 8)); ds.removeFromRight (6);
    drumStrip.setBounds (ds);
    r.removeFromTop (8);
    auto bottom = r.removeFromBottom (104);
    for (int i = 0; i < 2; ++i)
    {
        auto lane = r.removeFromTop ((r.getHeight() - (1 - i) * 6) / (2 - i)); if (i < 1) r.removeFromTop (6);
        newButtons[i].setBounds (lane.removeFromRight (64).reduced (0, lane.getHeight() / 2 - 17)); lane.removeFromRight (8);
        lanes[i].setBounds (lane);
    }
    bottom.removeFromTop (8);
    auto buttons = bottom.removeFromLeft (300);
    rollAll.setBounds (buttons.removeFromTop (44).reduced (0, 1)); buttons.removeFromTop (8);
    listenButton.setBounds (buttons.removeFromLeft (146).reduced (0, 1)); buttons.removeFromLeft (8);
    allMidi.setBounds (buttons.reduced (0, 1));
    bottom.removeFromLeft (20);
    const int w = bottom.getWidth() / 3;
    for (auto* k : { &motionKnob, &densityKnob, &levelKnob }) k->setBounds (bottom.removeFromLeft (w).withSizeKeepingCentre (96, 96));
}
