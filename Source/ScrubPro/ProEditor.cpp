#include "ProEditor.h"

using namespace FatboardColours;

static const char* fineIds[4]   = { ProProcessor::kLowCut, ProProcessor::kMud, ProProcessor::kHarsh, ProProcessor::kTop };
static const char* fineNames[4] = { "Low Cut", "Mud", "Harsh", "Top" };

ProEditor::ProEditor (ProProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (header);
    addAndMakeVisible (curveView);
    addAndMakeVisible (scrubKnob);
    addAndMakeVisible (airKnob);
    addAndMakeVisible (roomButton);
    addAndMakeVisible (activity);
    addAndMakeVisible (footer);

    roomButton.setClickingTogglesState (true);
    roomButton.setTooltip ("Put Scrub Pro on the BEAT and send the vocal to its sidechain. The beat steps aside while you rap.");

    scrubAtt  = std::make_unique<SliderAtt> (proc.apvts, ProProcessor::kCleanup, scrubKnob.slider);
    airAtt    = std::make_unique<SliderAtt> (proc.apvts, ProProcessor::kAir, airKnob.slider);
    activeAtt = std::make_unique<ButtonAtt> (proc.apvts, ProProcessor::kActive, header.power);
    roomAtt   = std::make_unique<ButtonAtt> (proc.apvts, ProProcessor::kRoom, roomButton);
    airKnob.slider.setDoubleClickReturnValue (true, 0.35);

    for (int i = 0; i < 4; ++i)
    {
        addAndMakeVisible (fine[i]);
        fineAtt[i] = std::make_unique<SliderAtt> (proc.apvts, fineIds[i], fine[i].slider);
        // double-click = back to the preset value
        if (auto* prm = proc.apvts.getParameter (fineIds[i]))
            fine[i].slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
        fine[i].slider.onValueChange = [this] { refreshFineLabels(); };
    }

    setSize (640, 760);
    refreshRoom();
    refreshFineLabels();
    startTimerHz (30);
}

ProEditor::~ProEditor()
{
    setLookAndFeel (nullptr);
}

void ProEditor::refreshFineLabels()
{
    for (int i = 0; i < 4; ++i)
        if (auto* prm = proc.apvts.getParameter (fineIds[i]))
            fine[i].setLabel (juce::String (fineNames[i]) + "  " + prm->getCurrentValueAsText());
}

void ProEditor::refreshRoom()
{
    roomOn = proc.apvts.getRawParameterValue (ProProcessor::kRoom)->load() > 0.5f;
    scrubKnob.setLabel (roomOn ? "Room" : "Scrub");
    airKnob.setEnabled (! roomOn);
    airKnob.setAlpha (roomOn ? 0.3f : 1.0f);
    for (auto& k : fine) { k.setEnabled (! roomOn); k.setAlpha (roomOn ? 0.3f : 1.0f); }
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

    // ---- the picture of what is happening right now ----
    const bool on = proc.apvts.getRawParameterValue (ProProcessor::kActive)->load() > 0.5f;
    const float knob = proc.apvts.getRawParameterValue (ProProcessor::kCleanup)->load();
    if (! on)
        curveView.update ([] (float) { return 0.0f; });
    else if (roomOn)
    {
        const auto& room = proc.getRoom();
        curveView.update ([&] (float hz)
        {
            float d = 0.0f;
            for (int b = 0; b < RoomCarver::numBands; ++b) d += VoiceCurve::bellDb (hz, room.settings.bandHz[b], room.settings.bandQ, -room.getCutDb (b));
            return d;
        });
    }
    else
    {
        const auto shape = proc.getShape();
        const float a = FatboardCurve::fromKnob (knob);
        const float air = proc.apvts.getRawParameterValue (ProProcessor::kAir)->load();
        const float guard = proc.getAir().getGuardDb();
        const auto& e = proc.getEngine();
        curveView.update ([&] (float hz)
        {
            float d = VoiceCurve::responseDb (hz, a, shape) + AirEngine::responseDb (hz, air, guard);
            for (int i = 0; i < CleanupEngine::numBands; ++i) d += VoiceCurve::bellDb (hz, e.getBandHz (i), e.settings.bandQ, -e.getBandReductionDb (i));
            const float rr = hz / e.settings.shelfHz; d += e.getShelfDb() * rr * rr / (1.0f + rr * rr);
            return d;
        });
    }
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
                             : "The line is what I do to your voice right now. Below the middle: taken out. Above: added.",
                      claim.reduced (0, 8), juce::Justification::centredLeft, 3);

    juce::String hint = "Scrub does the job. The four small ones are for fine tuning. Double-click resets.";
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

    curveView.setBounds (r.removeFromTop (118));
    r.removeFromTop (10);

    // fine tuning row at the bottom
    auto fineRow = r.removeFromBottom (116);
    const int fw = fineRow.getWidth() / 4;
    for (int i = 0; i < 4; ++i) fine[i].setBounds (fineRow.removeFromLeft (fw).withSizeKeepingCentre (juce::jmin (fw - 8, 124), 116));
    r.removeFromBottom (6);

    // big Scrub knob on the left, Air + Room stacked on the right
    auto rightCol = r.removeFromRight (190);
    r.removeFromRight (16);
    const int big = juce::jmin (r.getWidth(), r.getHeight());
    scrubKnob.setBounds (r.withSizeKeepingCentre (big, big));

    auto stack = rightCol.withSizeKeepingCentre (190, 150 + 12 + 38 + 14 + 30);
    airKnob.setBounds (stack.removeFromTop (150).withSizeKeepingCentre (150, 150));
    stack.removeFromTop (12);
    roomButton.setBounds (stack.removeFromTop (38).withSizeKeepingCentre (130, 38));
    stack.removeFromTop (14);
    activity.setBounds (stack.removeFromTop (30).withSizeKeepingCentre (30, 30));
}
