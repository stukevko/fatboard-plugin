#include "HarunEditor.h"

using namespace FatboardColours;

static const char* templateHints[HarunTemplates::count] = {
    "Tight double right under the lead. Thickens the voice without sounding like two people.",
    "The double pushed all the way out to the sides. For hooks.",
    "Harmony or second voice: thinner at the bottom, sits behind the lead with a room around it.",
    "Far back, dark, with hall and echo. For stacks, choirs and ad-libs in the distance."
};

HarunEditor::HarunEditor (HarunProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (header);
    addAndMakeVisible (curveView);
    addAndMakeVisible (widthKnob); addAndMakeVisible (levelKnob); addAndMakeVisible (spaceKnob);
    addAndMakeVisible (activity);
    addAndMakeVisible (footer);

    for (int i = 0; i < HarunTemplates::count; ++i)
    {
        templateButtons[i].setButtonText (HarunTemplates::all[i].name);
        templateButtons[i].setClickingTogglesState (false);
        templateButtons[i].onClick = [this, i] { proc.loadTemplate (i); };     // also sets Width / Level / Space to what fits
        addAndMakeVisible (templateButtons[i]);
    }

    spaceType = std::make_unique<PillGroup> (*proc.apvts.getParameter (HarunProcessor::kSpaceType), "Space is");
    addAndMakeVisible (*spaceType);

    widthAtt  = std::make_unique<SliderAtt> (proc.apvts, HarunProcessor::kWidth, widthKnob.slider);
    levelAtt  = std::make_unique<SliderAtt> (proc.apvts, HarunProcessor::kLevel, levelKnob.slider);
    spaceAtt  = std::make_unique<SliderAtt> (proc.apvts, HarunProcessor::kSpace, spaceKnob.slider);
    activeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, HarunProcessor::kActive, header.power);
    for (auto* k : { &widthKnob, &levelKnob, &spaceKnob }) k->slider.onValueChange = [this] { refreshLabels(); };

    setSize (640, 700);
    startTimerHz (20);
    timerCallback();
    refreshLabels();
}

HarunEditor::~HarunEditor() { setLookAndFeel (nullptr); }

void HarunEditor::refreshLabels()
{
    auto text = [this] (const char* id) { auto* prm = proc.apvts.getParameter (id); return prm != nullptr ? prm->getCurrentValueAsText() : juce::String(); };
    widthKnob.setLabel ("Width  " + text (HarunProcessor::kWidth));
    levelKnob.setLabel ("Level  " + text (HarunProcessor::kLevel));
    spaceKnob.setLabel ("Space  " + text (HarunProcessor::kSpace));
}

void HarunEditor::timerCallback()
{
    const auto& e = proc.getEngine();
    const bool on = proc.apvts.getRawParameterValue (HarunProcessor::kActive)->load() > 0.5f;
    activity.setActivity (on ? juce::jmax (e.getActivity() * 0.6f, juce::jlimit (0.0f, 1.0f, e.getSpaceMeter() * 6.0f)) : 0.0f);

    const int t = (int) proc.apvts.getRawParameterValue (HarunProcessor::kTemplate)->load();
    if (t != shownTemplate)
    {
        shownTemplate = t;
        for (int i = 0; i < HarunTemplates::count; ++i) templateButtons[i].setToggleState (i == t, juce::dontSendNotification);
        // double-click on a knob = back to what the template set
        const auto& tp = HarunTemplates::get (t);
        widthKnob.slider.setDoubleClickReturnValue (true, tp.width);
        levelKnob.slider.setDoubleClickReturnValue (true, tp.levelDb);
        spaceKnob.slider.setDoubleClickReturnValue (true, tp.space);
        repaint();
    }
    const auto& tp = HarunTemplates::get (t);
    const float level = proc.apvts.getRawParameterValue (HarunProcessor::kLevel)->load();
    curveView.update ([&] (float hz) { return on ? HarunTemplates::responseDb (tp, hz) + level * 0.35f : 0.0f; });   // level nudges the whole line a little
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

    g.setColour (grey);
    g.setFont (FatboardLookAndFeel::body (12.5f));
    g.drawFittedText (templateHints[juce::jlimit (0, HarunTemplates::count - 1, shownTemplate)], claim.reduced (0, 8), juce::Justification::centredLeft, 3);

    g.drawText ("On your double or harmony track. Pick a template, done. EQ is matched to Scrub.",
                r.removeFromBottom (24), juce::Justification::centred, false);
}

void HarunEditor::resized()
{
    auto r = getLocalBounds().reduced (28);
    header.setBounds (r.removeFromTop (56));
    footer.setBounds (r.removeFromBottom (28));
    r.removeFromTop (74);          // claim
    r.removeFromBottom (24);       // hint

    // templates
    auto row = r.removeFromTop (44);
    const int gap = 10, bw = (row.getWidth() - gap * (HarunTemplates::count - 1)) / HarunTemplates::count;
    for (int i = 0; i < HarunTemplates::count; ++i) { templateButtons[i].setBounds (row.removeFromLeft (bw).withHeight (40)); row.removeFromLeft (gap); }
    r.removeFromTop (8);

    curveView.setBounds (r.removeFromTop (104));
    r.removeFromTop (8);

    // space type + activity at the bottom
    auto bottom = r.removeFromBottom (60);
    activity.setBounds (bottom.removeFromRight (60).withSizeKeepingCentre (34, 34));
    spaceType->setBounds (bottom.removeFromRight (300));

    const int third = r.getWidth() / 3;
    auto a = r.removeFromLeft (third), b = r.removeFromLeft (third), c = r;
    const int size = juce::jmin (third, r.getHeight()) - 6;
    widthKnob.setBounds (a.withSizeKeepingCentre (size, size));
    levelKnob.setBounds (b.withSizeKeepingCentre (size, size));
    spaceKnob.setBounds (c.withSizeKeepingCentre (size, size));
}
