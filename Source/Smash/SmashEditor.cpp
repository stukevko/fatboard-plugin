#include "SmashEditor.h"
using namespace FatboardColours;

SmashEditor::SmashEditor (SmashProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    addChildComponent (licenseBar); licenseBar.setVisible (proc.license.isEnabled());
    for (auto* c : std::initializer_list<juce::Component*> { &header, &smashKnob, &fitKnob, &activity, &footer }) addAndMakeVisible (c);
    styleGroup = std::make_unique<PillGroup> (*proc.apvts.getParameter (SmashProcessor::kStyle), "Style");
    addAndMakeVisible (*styleGroup);
    smashAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, SmashProcessor::kSmash, smashKnob.slider);
    fitAtt   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, SmashProcessor::kFit, fitKnob.slider);
    activeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, SmashProcessor::kActive, header.power);
    smashKnob.slider.setDoubleClickReturnValue (true, 0.5); fitKnob.slider.setDoubleClickReturnValue (true, 0.5);
    setSize (640, 560);
    startTimerHz (30);
}
SmashEditor::~SmashEditor() { setLookAndFeel (nullptr); }

void SmashEditor::timerCallback()
{
    const auto& e = proc.getEngine();
    activity.setActivity (e.getActivity());
    bool changed = std::abs (e.getGrDb() - gr) > 0.2f || std::abs (e.getMudDb() - mud) > 0.2f || std::abs (e.getHiDb() - hi) > 0.2f || e.usingSidechain() != sc;
    for (int b = 0; b < 3; ++b) changed |= std::abs (e.getFitDb (b) - fit[(size_t) b]) > 0.2f;
    if (changed) { gr = e.getGrDb(); mud = e.getMudDb(); hi = e.getHiDb(); sc = e.usingSidechain(); for (int b = 0; b < 3; ++b) fit[(size_t) b] = e.getFitDb (b); repaint(); }
}

void SmashEditor::paint (juce::Graphics& g)
{
    g.fillAll (black);
    auto r = getLocalBounds().reduced (28);
    r.removeFromTop (56); r.removeFromBottom (28);
    auto claim = r.removeFromTop (74);
    auto left = claim.removeFromLeft (claim.getWidth() / 2);
    g.setColour (bone); g.setFont (FatboardLookAndFeel::display (30.0f));
    g.drawText ("HIT HARD.", left.removeFromTop (37), juce::Justification::centredLeft, false);
    g.setColour (lnf.accent); g.drawText ("STAY CLEAN.", left, juce::Justification::centredLeft, false);

    // three tiny meters: GLUE  MUD  TOP   (what Smash is doing right now – as bars, no numbers)
    auto meters = claim.reduced (0, 6);
    const char* names[3] = { "GLUE", "MUD", "TOP" };
    const float vals[3] = { juce::jlimit (0.0f, 1.0f, -gr / 6.0f), juce::jlimit (0.0f, 1.0f, mud / 5.0f), juce::jlimit (0.0f, 1.0f, hi / 5.0f) };
    const int w = meters.getWidth() / 3;
    for (int i = 0; i < 3; ++i)
    {
        auto col = meters.removeFromLeft (w).reduced (6, 0);
        g.setColour (grey); g.setFont (FatboardLookAndFeel::displayBold (10.0f));
        g.drawText (names[i], col.removeFromTop (14), juce::Justification::centred, false);
        auto bar = col.removeFromTop (10).toFloat();
        g.setColour (ring); g.fillRoundedRectangle (bar, 5.0f);
        g.setColour (lnf.accent); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * vals[i]), 5.0f);
    }
    g.setColour (grey); g.setFont (FatboardLookAndFeel::body (11.0f));
    g.drawText (sc ? "sample linked, carving a window" : "no sample on the sidechain, Fit idle", claim.withTrimmedTop (36), juce::Justification::centredRight, false);

    g.setColour (grey); g.setFont (FatboardLookAndFeel::body (12.5f));
    g.drawText ("Drum bus only. 808 on its own track. Send the sample to the sidechain and Fit makes room for the drums.",
                r.removeFromBottom (24), juce::Justification::centred, false);
}

void SmashEditor::resized()
{
    licenseBar.setBounds (28, 3, getWidth() - 56, 24);
    auto r = getLocalBounds().reduced (28);
    header.setBounds (r.removeFromTop (56)); footer.setBounds (r.removeFromBottom (28));
    r.removeFromTop (74); r.removeFromBottom (24);
    auto row = r.removeFromTop (62); styleGroup->setBounds (row.removeFromLeft (260)); r.removeFromTop (8);
    auto act = r.removeFromBottom (44); activity.setBounds (act.withSizeKeepingCentre (36, 36));
    auto leftArea = r.removeFromLeft (r.getWidth() / 2);
    const int size = juce::jmin (leftArea.getWidth(), leftArea.getHeight()) - 8;
    smashKnob.setBounds (leftArea.withSizeKeepingCentre (size, size)); fitKnob.setBounds (r.withSizeKeepingCentre (size, size));
}
