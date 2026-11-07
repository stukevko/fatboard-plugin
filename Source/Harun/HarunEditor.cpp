#include "HarunEditor.h"
#include "HarmonyLogic.h"

using namespace FatboardColours;

HarunEditor::HarunEditor (HarunProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (header);
    addAndMakeVisible (blendKnob);
    addAndMakeVisible (humanKnob);
    addAndMakeVisible (activity);
    addAndMakeVisible (footer);

    // key
    if (auto* keyParam = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (HarunProcessor::kKey)))
        keyBox.addItemList (keyParam->choices, 1);
    addAndMakeVisible (keyBox);
    keyAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, HarunProcessor::kKey, keyBox);

    scaleGroup = std::make_unique<PillGroup> (*proc.apvts.getParameter (HarunProcessor::kScale), "Scale");
    voiceGroup = std::make_unique<PillGroup> (*proc.apvts.getParameter (HarunProcessor::kVoice), "Voice");
    modeGroup  = std::make_unique<PillGroup> (*proc.apvts.getParameter (HarunProcessor::kMode),  "Mode");
    addAndMakeVisible (*scaleGroup); addAndMakeVisible (*voiceGroup); addAndMakeVisible (*modeGroup);

    blendAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, HarunProcessor::kBlend, blendKnob.slider);
    humanAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, HarunProcessor::kHuman, humanKnob.slider);
    activeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, HarunProcessor::kActive, header.power);

    blendKnob.slider.setDoubleClickReturnValue (true, 0.6);
    humanKnob.slider.setDoubleClickReturnValue (true, 0.6);

    setSize (640, 620);
    startTimerHz (20);
}

HarunEditor::~HarunEditor() { setLookAndFeel (nullptr); }

void HarunEditor::timerCallback()
{
    const auto& e = proc.getEngine();
    activity.setActivity (e.isVoiced() ? e.getActivity() : e.getActivity() * 0.3f);
    juce::String n = e.isVoiced() ? juce::String (Harmony::noteName (e.getNoteMidi())) : juce::String ("-");
    juce::String h;
    if (e.isVoiced())
    {
        const float s0 = e.getVoiceShift (0);
        h = juce::String (Harmony::noteName (e.getNoteMidi() + s0));
        const bool both = (int) proc.apvts.getRawParameterValue (HarunProcessor::kVoice)->load() == 2;
        if (both) h += " + " + juce::String (Harmony::noteName (e.getNoteMidi() + e.getVoiceShift (1)));
    }
    if (n != noteText || h != harmonyText) { noteText = n; harmonyText = h; repaint(); }
}

void HarunEditor::paint (juce::Graphics& g)
{
    g.fillAll (black);
    auto r = getLocalBounds().reduced (28);
    r.removeFromTop (56);
    r.removeFromBottom (28);

    // claim left, live notes right
    auto claim = r.removeFromTop (74);
    auto left = claim.removeFromLeft (claim.getWidth() / 2);
    g.setColour (bone);
    g.setFont (FatboardLookAndFeel::display (30.0f));
    g.drawText ("SING ONCE.", left.removeFromTop (37), juce::Justification::centredLeft, false);
    g.setColour (yellow);
    g.drawText ("HEAR TWO.", left, juce::Justification::centredLeft, false);

    // notes: lead → harmony
    g.setColour (grey);
    g.setFont (FatboardLookAndFeel::displayBold (10.0f));
    g.drawText ("YOU          HARUN", claim.removeFromTop (14), juce::Justification::centredRight, false);
    auto notes = claim;
    g.setFont (FatboardLookAndFeel::display (34.0f));
    auto right = notes.removeFromRight (150);
    g.setColour (yellow);
    g.drawText (harmonyText.isEmpty() ? juce::String ("-") : harmonyText, right, juce::Justification::centredRight, false);
    g.setColour (bone);
    g.drawText (noteText, notes.removeFromRight (70), juce::Justification::centredRight, false);

    g.setColour (grey);
    g.setFont (FatboardLookAndFeel::displayBold (10.0f));
    g.drawText ("KEY", keyLabelArea, juce::Justification::centredLeft, false);

    g.setColour (grey);
    g.setFont (FatboardLookAndFeel::body (12.5f));
    g.drawText ("Pick the key. Harun sings the third. Blend sets how loud, Human how real.", r.removeFromBottom (24), juce::Justification::centred, false);
}

void HarunEditor::resized()
{
    auto r = getLocalBounds().reduced (28);
    header.setBounds (r.removeFromTop (56));
    footer.setBounds (r.removeFromBottom (28));
    r.removeFromTop (74);        // claim
    r.removeFromBottom (24);     // hint

    // row 1: key + scale
    auto row1 = r.removeFromTop (62);
    auto keyArea = row1.removeFromLeft (150);
    keyLabelArea = keyArea.removeFromTop (14);
    keyBox.setBounds (keyArea.withTrimmedTop (4).withHeight (38));
    row1.removeFromLeft (16);
    scaleGroup->setBounds (row1.removeFromLeft (220));
    r.removeFromTop (12);

    // row 2: voice + mode
    auto row2 = r.removeFromTop (62);
    voiceGroup->setBounds (row2.removeFromLeft (300));
    row2.removeFromLeft (16);
    modeGroup->setBounds (row2.removeFromLeft (220));
    r.removeFromTop (8);

    auto act = r.removeFromBottom (44);
    activity.setBounds (act.withSizeKeepingCentre (36, 36));

    auto leftArea = r.removeFromLeft (r.getWidth() / 2);
    const int size = juce::jmin (leftArea.getWidth(), leftArea.getHeight()) - 8;
    blendKnob.setBounds (leftArea.withSizeKeepingCentre (size, size));
    humanKnob.setBounds (r.withSizeKeepingCentre (size, size));
}
