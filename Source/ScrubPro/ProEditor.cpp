#include "ProEditor.h"

using namespace FatboardColours;

static const char* modeNames[4] = { "Crisp", "Clear", "Soft", "Room" };
static const char* modeHints[4] = {
    "Bites without the ice pick. For vocals that need to cut through a loud beat.",
    "Takes the blanket off. The voice sits in front of the beat.",
    "Rounds the edges, keeps the body. For R&B and anything close.",
    "Put me on the BEAT, send the vocal to my sidechain. The beat steps aside while you rap."
};

ProEditor::ProEditor (ProProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      modeAttachment (*proc.apvts.getParameter (ProProcessor::kMode),
                      [this] (float) { refreshModeButtons(); }, nullptr)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (header);
    addAndMakeVisible (cleanupKnob);
    addAndMakeVisible (modeKnob);
    addAndMakeVisible (activity);
    addAndMakeVisible (footer);

    for (int i = 0; i < 4; ++i)
    {
        modeButtons[i].setButtonText (modeNames[i]);
        modeButtons[i].setClickingTogglesState (false);
        modeButtons[i].setRadioGroupId (0);
        modeButtons[i].onClick = [this, i] { modeClicked (i); };
        addAndMakeVisible (modeButtons[i]);
    }

    cleanupAtt    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, ProProcessor::kCleanup, cleanupKnob.slider);
    modeAmountAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, ProProcessor::kAmount, modeKnob.slider);
    activeAtt     = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ProProcessor::kActive, header.power);

    modeAttachment.sendInitialUpdate();
    setSize (640, 540);
    startTimerHz (30);
}

ProEditor::~ProEditor()
{
    setLookAndFeel (nullptr);
}

void ProEditor::modeClicked (int index)
{
    if (auto* param = proc.apvts.getParameter (ProProcessor::kMode))
    {
        const float norm = param->convertTo0to1 ((float) index);
        modeAttachment.setValueAsCompleteGesture (param->convertFrom0to1 (norm));
    }
}

void ProEditor::refreshModeButtons()
{
    int current = 1;
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (ProProcessor::kMode)))
        current = param->getIndex();
    for (int i = 0; i < 4; ++i)
        modeButtons[i].setToggleState (i == current, juce::dontSendNotification);
    repaint();
}

void ProEditor::timerCallback()
{
    const bool roomMode = (int) proc.apvts.getRawParameterValue (ProProcessor::kMode)->load() == ProProcessor::roomModeIndex;
    const float total = proc.getEngine().getTotalActivity() + (roomMode ? proc.getRoom().getTotalCutDb() * 1.5f : proc.getChain().getDeEssDb());
    activity.setActivity (juce::jlimit (0.0f, 1.0f, total / 18.0f));
    const bool l = roomMode && proc.getRoom().isLinked(), v = roomMode && proc.getRoom().vocalPresent();
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

    // current mode hint, right of the claim
    int current = 1;
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (ProProcessor::kMode)))
        current = param->getIndex();
    g.setColour (grey);
    g.setFont (FatboardLookAndFeel::body (12.5f));
    g.drawFittedText (modeHints[current], claim.reduced (0, 8), juce::Justification::centredLeft, 3);

    // divider between the two knobs
    auto knobs = r.withTrimmedTop (56).withTrimmedBottom (44 + 24);
    g.setColour (line);
    g.fillRect (juce::Rectangle<int> (1, knobs.getHeight() - 20).withCentre (knobs.getCentre()));

    g.setColour (grey);
    g.setFont (FatboardLookAndFeel::body (12.5f));
    juce::String hint = "Cleanup runs first. Then the mode. Double-click resets.";
    if (current == ProProcessor::roomModeIndex)
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

    // mode pills
    auto pills = r.removeFromTop (56).withSizeKeepingCentre (4 * 110 + 3 * 10, 40);
    for (int i = 0; i < 4; ++i)
    {
        modeButtons[i].setBounds (pills.removeFromLeft (110));
        pills.removeFromLeft (10);
    }

    auto act = r.removeFromBottom (44);
    activity.setBounds (act.withSizeKeepingCentre (36, 36));

    auto leftArea  = r.removeFromLeft (r.getWidth() / 2);
    auto rightArea = r;
    const int size = juce::jmin (leftArea.getWidth(), leftArea.getHeight()) - 8;
    cleanupKnob.setBounds (leftArea.withSizeKeepingCentre (size, size));
    modeKnob.setBounds (rightArea.withSizeKeepingCentre (size, size));
}
