#include "BrettEditor.h"

using namespace FatboardColours;

BrettEditor::BrettEditor (BrettProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (header);
    addAndMakeVisible (loudKnob);
    addAndMakeVisible (bar);
    addAndMakeVisible (activity);
    addAndMakeVisible (relearn);
    addAndMakeVisible (footer);

    style = std::make_unique<PillGroup> (*proc.apvts.getParameter (BrettProcessor::kStyle), "Style");
    check = std::make_unique<PillGroup> (*proc.apvts.getParameter (BrettProcessor::kCheck), "Check: how it sounds on");
    addAndMakeVisible (*style); addAndMakeVisible (*check);

    relearn.setTooltip ("Brett sets one gain for the whole song from its loudest part. Changed the mix a lot? Click, then play the loudest part once.");
    relearn.onClick = [this] { proc.relearn(); };

    loudAtt   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, BrettProcessor::kLoud, loudKnob.slider);
    activeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, BrettProcessor::kActive, header.power);

    setSize (640, 700);
    startTimerHz (20);
}

BrettEditor::~BrettEditor() { setLookAndFeel (nullptr); }

void BrettEditor::timerCallback()
{
    const auto& e = proc.getEngine();
    const bool on = proc.apvts.getRawParameterValue (BrettProcessor::kActive)->load() > 0.5f;
    activity.setActivity (on ? juce::jlimit (0.0f, 1.0f, e.getReductionDb() / 4.0f + e.getGainDb() / 40.0f) : 0.0f);
    bar.set (e.getOutputLufs(), on && e.hasHeard() && e.getOutputLufs() > -40.0f);
    const bool c = proc.apvts.getRawParameterValue (BrettProcessor::kCheck)->load() > 0.5f, h = e.hasHeard();
    if (c != checkOn || h != heard) { checkOn = c; heard = h; repaint(); }
}

void BrettEditor::paint (juce::Graphics& g)
{
    g.fillAll (black);
    auto r = getLocalBounds().reduced (28);
    r.removeFromTop (56);
    r.removeFromBottom (28);

    auto claim = r.removeFromTop (74);
    auto left = claim.removeFromLeft (claim.getWidth() / 2);
    g.setColour (bone);
    g.setFont (FatboardLookAndFeel::display (30.0f));
    g.drawText ("FETT.", left.removeFromTop (37), juce::Justification::centredLeft, false);
    g.setColour (lnf.accent);
    g.drawText ("BRETT.", left, juce::Justification::centredLeft, false);

    g.setColour (grey);
    g.setFont (FatboardLookAndFeel::body (12.5f));
    g.drawFittedText ("Last plugin on the master. I make the song release loud and keep the 808 round. Play the loudest part once so I know the song.",
                      claim.reduced (0, 8), juce::Justification::centredLeft, 3);

    auto hint = r.removeFromBottom (24);
    if (checkOn)
    {
        g.setColour (juce::Colour (0xFFFF4A3D));
        g.drawText ("CHECK is on: you hear a simulation. It switches itself off when you export.", hint, juce::Justification::centred, false);
    }
    else
        g.drawText (heard ? "Turn until the dot sits in the bright zone. Double-click resets." : "Press play. I'm listening.", hint, juce::Justification::centred, false);
}

void BrettEditor::resized()
{
    auto r = getLocalBounds().reduced (28);
    header.setBounds (r.removeFromTop (56));
    footer.setBounds (r.removeFromBottom (28));
    r.removeFromTop (74);          // claim
    r.removeFromBottom (24);       // hint

    auto checkRow = r.removeFromBottom (62);
    check->setBounds (checkRow);
    r.removeFromBottom (8);
    auto styleRow = r.removeFromBottom (62);
    relearn.setBounds (styleRow.removeFromRight (150).withSizeKeepingCentre (140, 36).translated (0, 10));
    activity.setBounds (styleRow.removeFromRight (50).withSizeKeepingCentre (30, 30).translated (0, 10));
    style->setBounds (styleRow);
    r.removeFromBottom (10);

    bar.setBounds (r.removeFromBottom (36));
    r.removeFromBottom (6);
    const int size = juce::jmin (r.getWidth(), r.getHeight());
    loudKnob.setBounds (r.withSizeKeepingCentre (size, size));
}
