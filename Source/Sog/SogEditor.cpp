#include "SogEditor.h"

using namespace FatboardColours;

// ---------------------------------------------------------------- SwellView
void SwellView::setSwell (const juce::AudioBuffer<float>& b, bool startsOnWord)
{
    peaks.clear(); after = startsOnWord;
    const int n = b.getNumSamples(), cols = 220;
    if (n >= cols)
        for (int c = 0; c < cols; ++c)
        {
            const int a = (int) ((juce::int64) c * n / cols), z = (int) ((juce::int64) (c + 1) * n / cols);
            float p = 0.0f; for (int ch = 0; ch < b.getNumChannels(); ++ch) p = juce::jmax (p, b.getMagnitude (ch, a, z - a));
            peaks.push_back (p);
        }
    repaint();
}

void SwellView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const auto accent = FatboardLookAndFeel::accentOf (*this);
    g.setColour (panel); g.fillRoundedRectangle (r, 12.0f);
    auto in = r.reduced (16.0f, 14.0f);
    auto text = in.removeFromBottom (22.0f);
    if (peaks.empty())
    {
        // input meter while waiting, so you see that sound is arriving
        g.setColour (line); g.fillRoundedRectangle (in.withSizeKeepingCentre (in.getWidth() * 0.6f, 6.0f), 3.0f);
        g.setColour (accent); g.fillRoundedRectangle (in.withSizeKeepingCentre (in.getWidth() * 0.6f, 6.0f).withWidth (in.getWidth() * 0.6f * juce::jlimit (0.0f, 1.0f, meter * 1.5f)), 3.0f);
    }
    else
    {
        const float w = in.getWidth() / (float) peaks.size(), mid = in.getCentreY();
        float mx = 1.0e-4f; for (float p : peaks) mx = juce::jmax (mx, p);
        for (size_t i = 0; i < peaks.size(); ++i)
        {
            const float h = juce::jmax (1.5f, in.getHeight() * 0.5f * std::sqrt (peaks[i] / mx));
            const float prog = (float) i / (float) peaks.size();
            g.setColour (accent.withAlpha (0.35f + 0.65f * (after ? 1.0f - prog : prog)));
            g.fillRect (juce::Rectangle<float> (in.getX() + (float) i * w, mid - h, juce::jmax (1.0f, w - 1.0f), h * 2.0f));
        }
        // where the word lands
        g.setColour (bone); g.fillRect (juce::Rectangle<float> (after ? in.getX() : in.getRight() - 2.0f, in.getY(), 2.0f, in.getHeight()));
    }
    g.setColour (peaks.empty() ? grey : bone.withAlpha (0.9f));
    g.setFont (FatboardLookAndFeel::body (12.5f));
    g.drawText (status, text, juce::Justification::centred, false);
}

void SwellView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || peaks.empty() || ! makeFile || e.getDistanceFromDragStart() < 8) return;
    dragStarted = true;
    const auto f = makeFile();
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

// ---------------------------------------------------------------- SogEditor
SogEditor::SogEditor (SogProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    addChildComponent (licenseBar); licenseBar.setVisible (proc.license.isEnabled());
    header.power.setVisible (false);                 // the track always passes through
    for (auto* c : std::initializer_list<juce::Component*> { &header, &view, &captureButton, &previewButton, &lowCutButton, &sizeKnob, &toneKnob, &widthKnob, &tremKnob, &driveKnob, &levelKnob, &footer })
        addAndMakeVisible (c);
    length  = std::make_unique<PillGroup> (*proc.apvts.getParameter (SogProcessor::kLength), "Length");
    tremDiv = std::make_unique<PillGroup> (*proc.apvts.getParameter (SogProcessor::kTremDiv), "Rate");
    type    = std::make_unique<PillGroup> (*proc.apvts.getParameter (SogProcessor::kType), "Move");
    dir     = std::make_unique<PillGroup> (*proc.apvts.getParameter (SogProcessor::kDir), "Where");
    for (auto* g : { type.get(), dir.get(), length.get(), tremDiv.get() }) addAndMakeVisible (*g);

    sizeAtt  = std::make_unique<SliderAtt> (proc.apvts, SogProcessor::kSize, sizeKnob.slider);
    toneAtt  = std::make_unique<SliderAtt> (proc.apvts, SogProcessor::kTone, toneKnob.slider);
    widthAtt = std::make_unique<SliderAtt> (proc.apvts, SogProcessor::kWidth, widthKnob.slider);
    tremAtt  = std::make_unique<SliderAtt> (proc.apvts, SogProcessor::kTremolo, tremKnob.slider);
    driveAtt = std::make_unique<SliderAtt> (proc.apvts, SogProcessor::kDrive, driveKnob.slider);
    levelAtt = std::make_unique<SliderAtt> (proc.apvts, SogProcessor::kLevel, levelKnob.slider);
    lowCutButton.setClickingTogglesState (true);
    lowCutAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, SogProcessor::kLowCut, lowCutButton);

    captureButton.onClick = [this]
    {
        const auto s = proc.getState();
        if (s == SogProcessor::State::Armed || s == SogProcessor::State::Recording) proc.cancel(); else proc.arm();
    };
    previewButton.onClick = [this] { proc.preview(); };
    view.makeFile = [this] { return proc.writeWav(); };

    setSize (680, 770);
    startTimerHz (20);
    timerCallback();
}

SogEditor::~SogEditor() { setLookAndFeel (nullptr); }

void SogEditor::timerCallback()
{
    const auto s = proc.getState();
    if (proc.getRenderCount() != shownRender && s == SogProcessor::State::Ready) { shownRender = proc.getRenderCount(); view.setSwell (proc.getSwell(), proc.isAfter()); }
    if (s != SogProcessor::State::Ready && shownState == SogProcessor::State::Ready) view.setSwell ({});
    shownState = s;
    juce::String status;
    switch (s)
    {
        case SogProcessor::State::Empty:     status = "Press Capture, then play the word or the hit."; break;
        case SogProcessor::State::Armed:     status = "Listening. Play it now."; break;
        case SogProcessor::State::Recording: status = "Got the start, still recording..."; break;
        case SogProcessor::State::Ready:     status = ! proc.license.isLicensed() ? juce::String ("Demo: Preview works. Log in to drag the swell into your project.") : juce::String (proc.replacesWord() ? "Drag this into the playlist. Put its START on the word and mute the word there.   "
                                                                : proc.isAfter() ? "Drag this into the playlist. Put its START on the word.   "
                                                                                 : "Drag this into the playlist. Put its END on the word.   ") + juce::String (juce::roundToInt (proc.getBpm())) + " bpm"; break;
    }
    view.setStatus (status, proc.getInputLevel());
    captureButton.setButtonText (s == SogProcessor::State::Armed || s == SogProcessor::State::Recording ? "Cancel" : (s == SogProcessor::State::Ready ? "Capture again" : "Capture"));
    captureButton.setToggleState (s == SogProcessor::State::Armed || s == SogProcessor::State::Recording, juce::dontSendNotification);
    previewButton.setEnabled (s == SogProcessor::State::Ready);
    const int moveNow = (int) proc.apvts.getRawParameterValue (SogProcessor::kType)->load();
    const bool trem = proc.apvts.getRawParameterValue (SogProcessor::kTremDiv)->load() > 0.5f
                   && (moveNow == SogRenderer::Reverb || moveNow == SogRenderer::Spin);          // Echo and Stutter use Rate as their time
    tremKnob.setAlpha (trem ? 1.0f : 0.35f);
}

void SogEditor::paint (juce::Graphics& g)
{
    g.fillAll (black);
    auto r = getLocalBounds().reduced (28);
    r.removeFromTop (56);
    r.removeFromBottom (28);
    auto claim = r.removeFromTop (74);
    auto left = claim.removeFromLeft (claim.getWidth() / 2);
    g.setColour (bone);
    g.setFont (FatboardLookAndFeel::display (30.0f));
    g.drawText ("PULLS YOU", left.removeFromTop (37), juce::Justification::centredLeft, false);
    g.setColour (lnf.accent);
    g.drawText ("INTO THE LINE.", left, juce::Justification::centredLeft, false);
    g.setColour (grey);
    g.setFont (FatboardLookAndFeel::body (12.5f));
    g.drawFittedText ("I catch your word and build the move in the song tempo: reverb swell, echo, stutter or spin. Before the word or after it. You drag it in.",
                      claim.reduced (0, 8), juce::Justification::centredLeft, 3);
    g.drawText ("Preview plays the move together with your word. Change anything and it is rebuilt.", r.removeFromBottom (24), juce::Justification::centred, false);
}

void SogEditor::resized()
{
    licenseBar.setBounds (28, 3, getWidth() - 56, 24);
    auto r = getLocalBounds().reduced (28);
    header.setBounds (r.removeFromTop (56));
    footer.setBounds (r.removeFromBottom (28));
    r.removeFromTop (74);
    r.removeFromBottom (24);

    view.setBounds (r.removeFromTop (130));
    r.removeFromTop (10);
    auto buttons = r.removeFromTop (42);
    captureButton.setBounds (buttons.removeFromLeft (200).reduced (0, 1)); buttons.removeFromLeft (10);
    previewButton.setBounds (buttons.removeFromLeft (150).reduced (0, 1));
    lowCutButton.setBounds (buttons.removeFromRight (120).reduced (0, 1));
    r.removeFromTop (10);
    auto moves = r.removeFromTop (62);
    type->setBounds (moves.removeFromLeft (moves.getWidth() * 2 / 3 - 6)); moves.removeFromLeft (12);
    dir->setBounds (moves);
    r.removeFromTop (6);
    auto pills = r.removeFromTop (62);
    length->setBounds (pills.removeFromLeft (pills.getWidth() / 2 - 6)); pills.removeFromLeft (12);
    tremDiv->setBounds (pills);
    r.removeFromTop (6);
    const int w = r.getWidth() / 6, size = juce::jmin (w, r.getHeight()) - 2;
    for (auto* k : { &sizeKnob, &toneKnob, &widthKnob, &tremKnob, &driveKnob, &levelKnob }) k->setBounds (r.removeFromLeft (w).withSizeKeepingCentre (size, size));
}
