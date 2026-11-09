#include "HarunEditor.h"

using namespace FatboardColours;

HarunEditor::HarunEditor (HarunProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (header);
    addAndMakeVisible (amountKnob); addAndMakeVisible (widthKnob); addAndMakeVisible (tuckKnob);
    addAndMakeVisible (activity);
    addAndMakeVisible (footer);

    modeGroup = std::make_unique<PillGroup> (*proc.apvts.getParameter (HarunProcessor::kMode), "Mode");
    addAndMakeVisible (*modeGroup);

    amountAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, HarunProcessor::kAmount, amountKnob.slider);
    widthAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, HarunProcessor::kWidth,  widthKnob.slider);
    tuckAtt   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, HarunProcessor::kTuck,   tuckKnob.slider);
    activeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, HarunProcessor::kActive, header.power);
    amountKnob.slider.setDoubleClickReturnValue (true, 1.0);
    widthKnob.slider.setDoubleClickReturnValue (true, 0.8);
    tuckKnob.slider.setDoubleClickReturnValue (true, 0.5);

    setSize (640, 540);
    startTimerHz (20);
}

HarunEditor::~HarunEditor() { setLookAndFeel (nullptr); }

void HarunEditor::timerCallback()
{
    const auto& e = proc.getEngine();
    activity.setActivity (e.getActivity());
    const bool sc = e.usingSidechain(); const float ride = e.getRideDb();
    if (sc != scShown || std::abs (ride - rideShown) > 0.3f) { scShown = sc; rideShown = ride; repaint(); }
}

void HarunEditor::paint (juce::Graphics& g)
{
    g.fillAll (black);
    auto r = getLocalBounds().reduced (28);
    r.removeFromTop (56);
    r.removeFromBottom (28);

    auto claim = r.removeFromTop (74);
    auto left = claim.removeFromLeft (claim.getWidth() / 2);
    g.setColour (bone);
    g.setFont (FatboardLookAndFeel::display (30.0f));
    g.drawText ("ONE TAKE.", left.removeFromTop (37), juce::Justification::centredLeft, false);
    g.setColour (lnf.accent);
    g.drawText ("WIDE OPEN.", left, juce::Justification::centredLeft, false);

    // status: sidechain + ride
    g.setColour (grey);
    g.setFont (FatboardLookAndFeel::displayBold (10.0f));
    g.drawText ("LEAD", claim.removeFromTop (14), juce::Justification::centredRight, false);
    g.setFont (FatboardLookAndFeel::display (22.0f));
    g.setColour (scShown ? lnf.accent : dim);
    g.drawText (scShown ? "LINKED" : "NOT LINKED", claim.removeFromTop (28), juce::Justification::centredRight, false);
    g.setColour (grey);
    g.setFont (FatboardLookAndFeel::body (11.0f));
    g.drawText (scShown ? juce::String ("riding ") + (rideShown >= 0 ? "+" : "") + juce::String (rideShown, 1) + " dB"
                        : "send the lead to the sidechain", claim, juce::Justification::centredRight, false);

    g.setColour (grey);
    g.setFont (FatboardLookAndFeel::body (12.5f));
    g.drawText ("Put Harun on your double or harmony track. It sits it left and right, under the lead, done.",
                r.removeFromBottom (24), juce::Justification::centred, false);
}

void HarunEditor::resized()
{
    auto r = getLocalBounds().reduced (28);
    header.setBounds (r.removeFromTop (56));
    footer.setBounds (r.removeFromBottom (28));
    r.removeFromTop (74);
    r.removeFromBottom (24);

    auto row = r.removeFromTop (62);
    modeGroup->setBounds (row.removeFromLeft (220));
    r.removeFromTop (8);

    auto act = r.removeFromBottom (44);
    activity.setBounds (act.withSizeKeepingCentre (36, 36));

    const int third = r.getWidth() / 3;
    auto a = r.removeFromLeft (third), b = r.removeFromLeft (third), c = r;
    const int size = juce::jmin (third, r.getHeight()) - 12;
    amountKnob.setBounds (a.withSizeKeepingCentre (size, size));
    widthKnob.setBounds  (b.withSizeKeepingCentre (size, size));
    tuckKnob.setBounds   (c.withSizeKeepingCentre (size, size));
}
