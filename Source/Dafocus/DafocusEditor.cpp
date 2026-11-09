#include "DafocusEditor.h"

using namespace FatboardColours;

DafocusEditor::DafocusEditor (DafocusProcessor& p)
    : AudioProcessorEditor (&p), proc (p), lnf (FatboardColours::ice)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (header);
    addAndMakeVisible (amountKnob);
    addAndMakeVisible (duckKnob);
    addAndMakeVisible (activity);
    addAndMakeVisible (footer);

    spaceGroup = std::make_unique<PillGroup> (*proc.apvts.getParameter (DafocusProcessor::kSpace), "Space");
    echoGroup  = std::make_unique<PillGroup> (*proc.apvts.getParameter (DafocusProcessor::kEcho),  "Echo");
    addAndMakeVisible (*spaceGroup); addAndMakeVisible (*echoGroup);

    amountAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, DafocusProcessor::kAmount, amountKnob.slider);
    duckAtt   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, DafocusProcessor::kDuck, duckKnob.slider);
    activeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, DafocusProcessor::kActive, header.power);
    amountKnob.slider.setDoubleClickReturnValue (true, 0.4);
    duckKnob.slider.setDoubleClickReturnValue (true, 0.6);

    setSize (640, 560);
    startTimerHz (30);
}

DafocusEditor::~DafocusEditor() { setLookAndFeel (nullptr); }

void DafocusEditor::timerCallback()
{
    const auto& e = proc.getEngine();
    // the mark glows with the space: dim while the singer ducks it, bright when it blooms
    activity.setActivity (juce::jlimit (0.0f, 1.0f, e.getWetMeter() * 6.0f));
    const float d = e.getDuckGain();
    juce::String b = juce::String (juce::roundToInt (e.getBpm())) + " BPM";
    if (std::abs (d - duckShown) > 0.02f || b != bpmText) { duckShown = d; bpmText = b; repaint(); }
}

void DafocusEditor::paint (juce::Graphics& g)
{
    g.fillAll (black);
    auto r = getLocalBounds().reduced (28);
    r.removeFromTop (56);
    r.removeFromBottom (28);

    auto claim = r.removeFromTop (74);
    auto left = claim.removeFromLeft (claim.getWidth() / 2);
    g.setColour (bone);
    g.setFont (FatboardLookAndFeel::display (30.0f));
    g.drawText ("UP FRONT.", left.removeFromTop (37), juce::Justification::centredLeft, false);
    g.setColour (lnf.accent);
    g.drawText ("IN THE ROOM.", left, juce::Justification::centredLeft, false);

    // duck meter: a bar that gets pushed down while singing
    auto meter = claim.reduced (0, 10);
    g.setColour (grey);
    g.setFont (FatboardLookAndFeel::displayBold (10.0f));
    g.drawText ("SPACE", meter.removeFromTop (14), juce::Justification::centredRight, false);
    auto bar = meter.removeFromTop (12).toFloat();
    g.setColour (ring);
    g.fillRoundedRectangle (bar, 6.0f);
    const float open = juce::jlimit (0.0f, 1.0f, duckShown);
    g.setColour (lnf.accent);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * open).withX (bar.getRight() - bar.getWidth() * open), 6.0f);
    g.setColour (grey);
    g.setFont (FatboardLookAndFeel::body (11.0f));
    g.drawText (open < 0.9f ? "ducking" : "open", meter.removeFromTop (16), juce::Justification::centredRight, false);

    g.setColour (grey);
    g.setFont (FatboardLookAndFeel::body (12.5f));
    g.drawText ("Space and echo step back while you sing, bloom when you stop. " + bpmText + " from the host.",
                r.removeFromBottom (24), juce::Justification::centred, false);
}

void DafocusEditor::resized()
{
    auto r = getLocalBounds().reduced (28);
    header.setBounds (r.removeFromTop (56));
    footer.setBounds (r.removeFromBottom (28));
    r.removeFromTop (74);
    r.removeFromBottom (24);

    auto row = r.removeFromTop (62);
    spaceGroup->setBounds (row.removeFromLeft (260));
    row.removeFromLeft (16);
    echoGroup->setBounds (row);
    r.removeFromTop (8);

    auto act = r.removeFromBottom (44);
    activity.setBounds (act.withSizeKeepingCentre (36, 36));

    auto leftArea = r.removeFromLeft (r.getWidth() / 2);
    const int size = juce::jmin (leftArea.getWidth(), leftArea.getHeight()) - 8;
    amountKnob.setBounds (leftArea.withSizeKeepingCentre (size, size));
    duckKnob.setBounds (r.withSizeKeepingCentre (size, size));
}
