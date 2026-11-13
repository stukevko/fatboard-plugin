#include "ThumpEditor.h"

using namespace FatboardColours;

static juce::String noteName (float hz)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int midi = juce::roundToInt (69.0f + 12.0f * std::log2 (hz / 440.0f));
    return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}

ThumpEditor::ThumpEditor (ThumpProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (header);
    header.power.setVisible (false);                    // an instrument has no bypass
    tone = std::make_unique<PillGroup> (*proc.apvts.getParameter (ThumpProcessor::kTone), "808");
    addAndMakeVisible (*tone);
    for (auto* k : { &subKnob, &kickKnob, &decayKnob, &clipKnob }) addAndMakeVisible (*k);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (activity);
    addAndMakeVisible (footer);

    subAtt   = std::make_unique<SliderAtt> (proc.apvts, ThumpProcessor::kSub, subKnob.slider);
    kickAtt  = std::make_unique<SliderAtt> (proc.apvts, ThumpProcessor::kKick, kickKnob.slider);
    decayAtt = std::make_unique<SliderAtt> (proc.apvts, ThumpProcessor::kDecay, decayKnob.slider);
    clipAtt  = std::make_unique<SliderAtt> (proc.apvts, ThumpProcessor::kClip, clipKnob.slider);

    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Pick an 808 sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { const auto f = fc.getResult(); if (f.existsAsFile()) load (f); });
    };

    setSize (640, 660);
    startTimerHz (20);
    timerCallback();
}

ThumpEditor::~ThumpEditor() { setLookAndFeel (nullptr); }

void ThumpEditor::load (const juce::File& f)
{
    message = proc.loadSample (f, true);
    timerCallback(); repaint();
}

bool ThumpEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && juce::File (files[0]).hasFileExtension ("wav;aif;aiff;flac");
}

void ThumpEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragging = false;
    if (files.size() > 0) load (juce::File (files[0]));
}

void ThumpEditor::timerCallback()
{
    activity.setActivity (juce::jlimit (0.0f, 1.0f, proc.getEngine().getMeter() * 1.2f));
    juce::String t = "Drop your own 808 here (wav). It plays in tune on every key.";
    if (auto s = proc.getSample())
        t = s->name + "   ·   root " + noteName (s->rootHz) + (s->rootDetected ? "" : " (guessed, could not hear the note)");
    if (t != sampleText) { sampleText = t; repaint(); }
}

void ThumpEditor::paint (juce::Graphics& g)
{
    g.fillAll (black);
    auto r = getLocalBounds().reduced (28);
    r.removeFromTop (56);
    r.removeFromBottom (28);

    auto claim = r.removeFromTop (74);
    auto left = claim.removeFromLeft (claim.getWidth() / 2);
    g.setColour (bone);
    g.setFont (FatboardLookAndFeel::display (30.0f));
    g.drawText ("ONE NOTE.", left.removeFromTop (37), juce::Justification::centredLeft, false);
    g.setColour (lnf.accent);
    g.drawText ("KICK + 808.", left, juce::Justification::centredLeft, false);

    g.setColour (grey);
    g.setFont (FatboardLookAndFeel::body (12.5f));
    g.drawFittedText ("An instrument. Play a note: the 808 plays it, the kick hits with it, and Clip rounds both off together. A new note cuts the old one.",
                      claim.reduced (0, 8), juce::Justification::centredLeft, 3);

    // sample slot
    g.setColour (dragging ? lnf.accent.withAlpha (0.18f) : panel);
    g.fillRoundedRectangle (sampleBox.toFloat(), 10.0f);
    g.setColour (dragging ? lnf.accent : line);
    g.drawRoundedRectangle (sampleBox.toFloat().reduced (0.5f), 10.0f, 1.0f);
    g.setColour (message.isNotEmpty() ? juce::Colour (0xFFFF4A3D) : bone.withAlpha (0.85f));
    g.setFont (FatboardLookAndFeel::body (13.0f));
    g.drawFittedText (message.isNotEmpty() ? message : sampleText, sampleBox.reduced (14, 4).withTrimmedRight (170), juce::Justification::centredLeft, 2);

    g.setColour (grey);
    g.setFont (FatboardLookAndFeel::body (12.5f));
    g.drawText ("Kick at 0 = 808 only. Double-click resets a knob.", r.removeFromBottom (24), juce::Justification::centred, false);
}

void ThumpEditor::resized()
{
    auto r = getLocalBounds().reduced (28);
    header.setBounds (r.removeFromTop (56));
    footer.setBounds (r.removeFromBottom (28));
    r.removeFromTop (74);          // claim
    r.removeFromBottom (24);       // hint

    tone->setBounds (r.removeFromTop (62));
    r.removeFromTop (8);
    sampleBox = r.removeFromTop (52);
    loadButton.setBounds (sampleBox.withTrimmedLeft (sampleBox.getWidth() - 170).reduced (8, 8));
    activity.setBounds (r.removeFromBottom (36).withSizeKeepingCentre (30, 30));

    const int w = r.getWidth() / 4, size = juce::jmin (w, r.getHeight()) - 4;
    for (auto* k : { &subKnob, &kickKnob, &decayKnob, &clipKnob }) k->setBounds (r.removeFromLeft (w).withSizeKeepingCentre (size, size));
}
