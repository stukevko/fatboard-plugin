#include "ProEditor.h"

using namespace FatboardColours;

ProEditor::ProEditor (ProProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (header);
    addAndMakeVisible (scrubKnob);
    addAndMakeVisible (airKnob);
    addAndMakeVisible (roomButton);
    addAndMakeVisible (activity);
    addAndMakeVisible (footer);

    roomButton.setClickingTogglesState (true);
    roomButton.setTooltip ("Put Scrub Pro on the BEAT and send the vocal to its sidechain. The beat steps aside while you rap.");
    airKnob.slider.setDoubleClickReturnValue (true, 0.35);

    scrubAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, ProProcessor::kCleanup, scrubKnob.slider);
    airAtt    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, ProProcessor::kAir, airKnob.slider);
    activeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ProProcessor::kActive, header.power);
    roomAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ProProcessor::kRoom, roomButton);

    setSize (640, 600);
    refreshRoom();
    startTimerHz (30);
}

ProEditor::~ProEditor()
{
    setLookAndFeel (nullptr);
}

void ProEditor::refreshRoom()
{
    roomOn = proc.apvts.getRawParameterValue (ProProcessor::kRoom)->load() > 0.5f;
    scrubKnob.setLabel (roomOn ? "Room" : "Scrub");
    airKnob.setEnabled (! roomOn);
    airKnob.setAlpha (roomOn ? 0.3f : 1.0f);
    repaint();
}

void ProEditor::timerCallback()
{
    const bool r = proc.apvts.getRawParameterValue (ProProcessor::kRoom)->load() > 0.5f;
    if (r != roomOn) refreshRoom();

    const float total = roomOn ? proc.getRoom().getTotalCutDb() * 1.5f
                               : proc.getEngine().getTotalActivity() + proc.getAir().getGuardDb();
    activity.setActivity (juce::jlimit (0.0f, 1.0f, total / 18.0f));

    const bool l = roomOn && proc.getRoom().isLinked(), v = roomOn && proc.getRoom().vocalPresent();
    if (l != roomLinked || v != roomVocal) { roomLinked = l; roomVocal = v; repaint(); }
}

void ProEditor::paint (juce::Graphics& g)
{
    g.fillAll (black);

    auto r = getLocalBounds().reduced (28);
    r.removeFromTop (56);
    r.removeFromBottom (28);

    // claim
    g.setColour (bone);
    g.setFont (FatboardLookAndFeel::display (30.0f));
    auto claim = r.removeFromTop (74);
    auto left = claim.removeFromLeft (claim.getWidth() / 2);
    g.drawText ("THIN IN.", left.removeFromTop (37), juce::Justification::centredLeft, false);
    g.setColour (lnf.accent);
    g.drawText ("FAT OUT.", left, juce::Justification::centredLeft, false);

    g.setColour (grey);
    g.setFont (FatboardLookAndFeel::body (12.5f));
    g.drawFittedText (roomOn ? "I'm on the BEAT now. Send the vocal to my sidechain and the beat steps aside while you rap."
                             : "Scrub cleans the voice up. Air opens it. That's the whole vocal chain.",
                      claim.reduced (0, 8), juce::Justification::centredLeft, 3);

    juce::String hint = "Turn until it sounds right. Double-click resets.";
    if (roomOn)
        hint = roomLinked ? (roomVocal ? "Vocal linked: making room." : "Vocal linked: waiting for the next line.")
                          : "No vocal on the sidechain yet. Room does nothing until you link it.";
    g.drawText (hint, r.removeFromBottom (24), juce::Justification::centred, false);
}

void ProEditor::resized()
{
    auto r = getLocalBounds().reduced (28);
    header.setBounds (r.removeFromTop (56));
    footer.setBounds (r.removeFromBottom (28));
    r.removeFromTop (74);          // claim
    r.removeFromBottom (24);       // hint
    r.removeFromTop (8);

    // big Scrub knob on the left, Air + Room stacked on the right
    auto rightCol = r.removeFromRight (190);
    r.removeFromRight (16);
    const int big = juce::jmin (r.getWidth(), r.getHeight());
    scrubKnob.setBounds (r.withSizeKeepingCentre (big, big));

    auto stack = rightCol.withSizeKeepingCentre (190, 170 + 18 + 40 + 22 + 36);
    airKnob.setBounds (stack.removeFromTop (170).withSizeKeepingCentre (170, 170));
    stack.removeFromTop (18);
    roomButton.setBounds (stack.removeFromTop (40).withSizeKeepingCentre (130, 40));
    stack.removeFromTop (22);
    activity.setBounds (stack.removeFromTop (36).withSizeKeepingCentre (36, 36));
}
