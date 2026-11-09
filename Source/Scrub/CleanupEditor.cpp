#include "CleanupEditor.h"

using namespace FatboardColours;

CleanupEditor::CleanupEditor (CleanupProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (header);
    addAndMakeVisible (knob);
    addAndMakeVisible (activity);
    addAndMakeVisible (footer);

    amountAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, CleanupProcessor::kAmount, knob.slider);
    activeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, CleanupProcessor::kActive, header.power);

    setSize (420, 540);
    startTimerHz (30);
}

CleanupEditor::~CleanupEditor()
{
    setLookAndFeel (nullptr);
}

void CleanupEditor::timerCallback()
{
    // activity: how much we are pulling out right now, mapped to 0..1
    const float total = proc.getEngine().getTotalActivity();
    activity.setActivity (juce::jlimit (0.0f, 1.0f, total / 18.0f));
}

void CleanupEditor::paint (juce::Graphics& g)
{
    g.fillAll (black);

    auto r = getLocalBounds().reduced (28);
    r.removeFromTop (56);
    r.removeFromBottom (28);

    // claim above the knob
    g.setColour (bone);
    g.setFont (FatboardLookAndFeel::display (30.0f));
    auto claim = r.removeFromTop (74);
    g.drawText ("THIN IN.", claim.removeFromTop (37), juce::Justification::centredLeft, false);
    g.setColour (lnf.accent);
    g.drawText ("FAT OUT.", claim, juce::Justification::centredLeft, false);

    // hint under the knob
    g.setColour (grey);
    g.setFont (FatboardLookAndFeel::body (12.5f));
    g.drawText ("Turn until it sounds right. Double-click resets.", r.removeFromBottom (24), juce::Justification::centred, false);
}

void CleanupEditor::resized()
{
    auto r = getLocalBounds().reduced (28);
    header.setBounds (r.removeFromTop (56));
    footer.setBounds (r.removeFromBottom (28));
    r.removeFromTop (74);          // claim
    r.removeFromBottom (24);       // hint

    auto act = r.removeFromBottom (44);
    activity.setBounds (act.withSizeKeepingCentre (36, 36));

    const int size = juce::jmin (r.getWidth(), r.getHeight());
    knob.setBounds (r.withSizeKeepingCentre (size, size));
}
