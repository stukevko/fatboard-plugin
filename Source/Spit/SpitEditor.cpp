#include "SpitEditor.h"

using namespace FatboardColours;

// ---------------------------------------------------------------- MidiChip
void MidiChip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const int n = hitCount ? hitCount() : 0;
    const auto accent = FatboardLookAndFeel::accentOf (*this);
    g.setColour (n > 0 ? accent : panel); g.fillRoundedRectangle (r, 16.0f);
    g.setColour (n > 0 ? accent : line); g.drawRoundedRectangle (r.reduced (1.0f), 16.0f, 1.5f);
    g.setColour (n > 0 ? black : grey);
    g.setFont (FatboardLookAndFeel::displayBold (13.0f));
    g.drawText (n > 0 ? "DRAG MIDI  \xe2\x86\x92  " + juce::String (n) + " HITS" : "RECORD A TAKE FIRST", r, juce::Justification::centred, false);
}

void MidiChip::mouseDrag (const juce::MouseEvent& e)
{
    if (! buildFile || ! hitCount || hitCount() == 0 || e.getDistanceFromDragStart() < 8) return;
    if (tempFile == juce::File()) tempFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Spit take.mid");
    tempFile.replaceWithData (buildFile().getData(), buildFile().getSize());
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ tempFile.getFullPathName() }, false, this);
}

// ---------------------------------------------------------------- SpitEditor
SpitEditor::SpitEditor (SpitProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    for (auto* c : std::initializer_list<juce::Component*> { &header, &kick, &snare, &hat, &sensKnob, &tightKnob, &recButton, &clearButton, &chip, &activity, &footer })
        addAndMakeVisible (c);
    gridGroup = std::make_unique<PillGroup> (*proc.apvts.getParameter (SpitProcessor::kGrid), "Grid");
    addAndMakeVisible (*gridGroup);

    sensAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, SpitProcessor::kSens,  sensKnob.slider);
    tightAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, SpitProcessor::kTight, tightKnob.slider);
    activeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, SpitProcessor::kActive, header.power);
    sensKnob.slider.setDoubleClickReturnValue (true, 0.5);
    tightKnob.slider.setDoubleClickReturnValue (true, 0.7);

    kick.learn.onClick  = [this] { proc.getEngine().startLearn (SpitEngine::Kick); };
    snare.learn.onClick = [this] { proc.getEngine().startLearn (SpitEngine::Snare); };
    hat.learn.onClick   = [this] { proc.getEngine().startLearn (SpitEngine::Hat); };

    recButton.setClickingTogglesState (true);
    recButton.onClick = [this] { if (recButton.getToggleState()) { proc.startRecording(); recButton.setButtonText ("Stop"); } else { proc.stopRecording(); recButton.setButtonText ("Record take"); } };
    clearButton.onClick = [this] { proc.clearRecording(); chip.repaint(); };
    chip.buildFile = [this] { return proc.buildMidiFile(); };
    chip.hitCount  = [this] { return (int) proc.getRecordedHits().size(); };

    setSize (680, 720);
    startTimerHz (30);
}

SpitEditor::~SpitEditor() { setLookAndFeel (nullptr); }

void SpitEditor::timerCallback()
{
    const int c = proc.hitCounter.load();
    if (c != lastCounter)
    {
        lastCounter = c;
        const int d = proc.lastHitDrum.load(); const float v = proc.lastHitVelocity.load();
        if (d == 0) kick.flash (v); else if (d == 1) snare.flash (v); else if (d == 2) hat.flash (v);
        activity.setActivity (1.0f);
        if (proc.isRecording()) chip.repaint();
    }
    kick.tick(); snare.tick(); hat.tick();
    auto& e = proc.getEngine();
    kick.setLearning  (e.isLearning() && e.learningDrum() == SpitEngine::Kick,  e.learnProgress());
    snare.setLearning (e.isLearning() && e.learningDrum() == SpitEngine::Snare, e.learnProgress());
    hat.setLearning   (e.isLearning() && e.learningDrum() == SpitEngine::Hat,   e.learnProgress());
    activity.setActivity (juce::jmax (0.0f, activity.getActivity() - 0.05f));
}

void SpitEditor::paint (juce::Graphics& g)
{
    g.fillAll (black);
    auto r = getLocalBounds().reduced (28);
    r.removeFromTop (56); r.removeFromBottom (28);
    auto claim = r.removeFromTop (74);
    auto left = claim.removeFromLeft (claim.getWidth() / 2);
    g.setColour (bone); g.setFont (FatboardLookAndFeel::display (30.0f));
    g.drawText ("SAY IT.", left.removeFromTop (37), juce::Justification::centredLeft, false);
    g.setColour (lnf.accent);
    g.drawText ("PLAY IT.", left, juce::Justification::centredLeft, false);
    g.setColour (grey); g.setFont (FatboardLookAndFeel::body (12.5f));
    g.drawFittedText ("Beatbox into the mic. Spit turns it into Kick / Snare / Hat MIDI for your own drums. " + juce::String (juce::roundToInt (proc.getBpm())) + " BPM.",
                      claim.reduced (0, 8), juce::Justification::centredLeft, 3);
    g.drawText ("Live: route Spit's MIDI to your drum sampler. Take: press Record, play, drag the chip into your pattern.",
                r.removeFromBottom (24), juce::Justification::centred, false);
}

void SpitEditor::resized()
{
    auto r = getLocalBounds().reduced (28);
    header.setBounds (r.removeFromTop (56));
    footer.setBounds (r.removeFromBottom (28));
    r.removeFromTop (74); r.removeFromBottom (24);

    auto pads = r.removeFromTop (140);
    const int pw = (pads.getWidth() - 2 * 14) / 3;
    kick.setBounds (pads.removeFromLeft (pw)); pads.removeFromLeft (14);
    snare.setBounds (pads.removeFromLeft (pw)); pads.removeFromLeft (14);
    hat.setBounds (pads);
    r.removeFromTop (16);

    auto take = r.removeFromBottom (48);
    recButton.setBounds (take.removeFromLeft (150).reduced (0, 4)); take.removeFromLeft (10);
    clearButton.setBounds (take.removeFromLeft (90).reduced (0, 4)); take.removeFromLeft (14);
    chip.setBounds (take.reduced (0, 2));
    r.removeFromBottom (12);

    auto gridRow = r.removeFromTop (62);
    gridGroup->setBounds (gridRow.removeFromLeft (320));
    activity.setBounds (gridRow.removeFromRight (44).withSizeKeepingCentre (36, 36));
    r.removeFromTop (8);

    auto leftArea = r.removeFromLeft (r.getWidth() / 2);
    const int size = juce::jmin (leftArea.getWidth(), r.getHeight()) - 8;
    sensKnob.setBounds (leftArea.withSizeKeepingCentre (size, size));
    tightKnob.setBounds (r.withSizeKeepingCentre (size, size));
}
