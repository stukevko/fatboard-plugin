#include "SamtEditor.h"
using namespace FatboardColours;

void ReductionView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const auto accent = FatboardLookAndFeel::accentOf (*this);
    g.setColour (panel); g.fillRoundedRectangle (r, 12.0f);
    auto in = r.reduced (16.0f, 12.0f);
    auto side = in.removeFromRight (150.0f);
    const float range = 20.0f;                                                   // dB shown
    auto yOf = [&] (float db) { return in.getY() + juce::jlimit (0.0f, 1.0f, db / range) * in.getHeight(); };
    g.setFont (FatboardLookAndFeel::body (10.0f));
    for (float db : { 0.0f, 5.0f, 10.0f, 15.0f, 20.0f })
    {
        g.setColour (line); g.fillRect (juce::Rectangle<float> (in.getX(), yOf (db), in.getWidth(), 1.0f));
        g.setColour (grey); g.drawText (db == 0.0f ? "0" : "-" + juce::String ((int) db), juce::Rectangle<float> (in.getX() + 3.0f, yOf (db) + 1.0f, 30.0f, 11.0f), juce::Justification::centredLeft, false);
    }
    juce::Path p; const int n = (int) history.size(); const float w = in.getWidth() / (float) (n - 1);
    p.startNewSubPath (in.getX(), in.getY());
    for (int i = 0; i < n; ++i) p.lineTo (in.getX() + (float) i * w, yOf (history[(size_t) ((head + i) % n)]));
    p.lineTo (in.getRight(), in.getY()); p.closeSubPath();
    g.setColour (accent.withAlpha (0.28f)); g.fillPath (p);
    juce::Path l; l.startNewSubPath (in.getX(), yOf (history[(size_t) head]));
    for (int i = 1; i < n; ++i) l.lineTo (in.getX() + (float) i * w, yOf (history[(size_t) ((head + i) % n)]));
    g.setColour (accent); g.strokePath (l, juce::PathStrokeType (2.0f));

    g.setColour (now > 0.3f ? bone : grey); g.setFont (FatboardLookAndFeel::display (30.0f));
    g.drawText (now > 0.05f ? "-" + juce::String (now, 1) : juce::String ("0.0"), side.removeFromTop (side.getHeight() * 0.55f), juce::Justification::centredBottom, false);
    g.setColour (grey); g.setFont (FatboardLookAndFeel::body (11.0f));
    g.drawText ("dB held back", side.removeFromTop (16.0f), juce::Justification::centred, false);
    if (hold > 0.05f) g.drawText ("most: -" + juce::String (hold, 1), side.removeFromTop (16.0f), juce::Justification::centred, false);
}

SamtEditor::SamtEditor (SamtProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    addChildComponent (licenseBar); licenseBar.setVisible (proc.license.isEnabled());
    for (auto* c : std::initializer_list<juce::Component*> { &header, &view, &reduceKnob, &gainKnob, &warmthKnob, &mixKnob, &footer }) addAndMakeVisible (c);
    mode = std::make_unique<PillGroup> (*proc.apvts.getParameter (SamtProcessor::kMode), "Mode");
    addAndMakeVisible (*mode);
    reduceAtt = std::make_unique<SliderAtt> (proc.apvts, SamtProcessor::kReduce, reduceKnob.slider);
    gainAtt   = std::make_unique<SliderAtt> (proc.apvts, SamtProcessor::kGain, gainKnob.slider);
    warmthAtt = std::make_unique<SliderAtt> (proc.apvts, SamtProcessor::kWarmth, warmthKnob.slider);
    mixAtt    = std::make_unique<SliderAtt> (proc.apvts, SamtProcessor::kMix, mixKnob.slider);
    activeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, SamtProcessor::kActive, header.power);
    reduceKnob.slider.setDoubleClickReturnValue (true, 0.0); gainKnob.slider.setDoubleClickReturnValue (true, 0.0);
    warmthKnob.slider.setDoubleClickReturnValue (true, 0.2); mixKnob.slider.setDoubleClickReturnValue (true, 1.0);
    setSize (640, 700);
    startTimerHz (30);
}
SamtEditor::~SamtEditor() { setLookAndFeel (nullptr); }

void SamtEditor::timerCallback()
{
    const bool on = proc.apvts.getRawParameterValue (SamtProcessor::kActive)->load() > 0.5f;
    view.push (on ? proc.getEngine().getReductionDb() : 0.0f);
    auto val = [this] (const char* id) { return proc.apvts.getParameter (id)->getCurrentValueAsText(); };
    reduceKnob.setLabel ("Peak Reduction " + val (SamtProcessor::kReduce)); gainKnob.setLabel ("Gain " + val (SamtProcessor::kGain));
    warmthKnob.setLabel ("Warmth " + val (SamtProcessor::kWarmth)); mixKnob.setLabel ("Mix " + val (SamtProcessor::kMix));
}

void SamtEditor::paint (juce::Graphics& g)
{
    g.fillAll (black);
    auto r = getLocalBounds().reduced (28);
    r.removeFromTop (56); r.removeFromBottom (28);
    auto claim = r.removeFromTop (74);
    auto left = claim.removeFromLeft (claim.getWidth() / 2);
    g.setColour (bone); g.setFont (FatboardLookAndFeel::display (30.0f));
    g.drawText ("LOUD DOWN.", left.removeFromTop (37), juce::Justification::centredLeft, false);
    g.setColour (lnf.accent); g.drawText ("VOICE UP.", left, juce::Justification::centredLeft, false);
    g.setColour (grey); g.setFont (FatboardLookAndFeel::body (12.5f));
    g.drawFittedText ("Turn Peak Reduction until the curve dips 3 to 7 dB on the loud words. Then bring the level back with Gain.", claim.reduced (0, 6), juce::Justification::centredRight, 3);
    g.drawText ("Soft like a hand on the fader: quick to catch, slow to let go.", r.removeFromBottom (24), juce::Justification::centred, false);
}

void SamtEditor::resized()
{
    licenseBar.setBounds (28, 3, getWidth() - 56, 24);
    auto r = getLocalBounds().reduced (28);
    header.setBounds (r.removeFromTop (56));
    footer.setBounds (r.removeFromBottom (28));
    r.removeFromTop (74);
    r.removeFromBottom (24);
    view.setBounds (r.removeFromTop (140));
    r.removeFromTop (8);
    auto low = r.removeFromBottom (112);
    const int bw = r.getWidth() / 2, bs = juce::jmin (bw, r.getHeight());
    reduceKnob.setBounds (r.removeFromLeft (bw).withSizeKeepingCentre (bs, bs));
    gainKnob.setBounds (r.withSizeKeepingCentre (bs, bs));
    mode->setBounds (low.removeFromLeft (low.getWidth() / 2 - 10).withSizeKeepingCentre (low.getWidth() / 1 - 0, 62).withWidth (getWidth() / 2 - 38).withX (28));
    low.removeFromLeft (20);
    const int sw = low.getWidth() / 2;
    warmthKnob.setBounds (low.removeFromLeft (sw).withSizeKeepingCentre (108, 108));
    mixKnob.setBounds (low.withSizeKeepingCentre (108, 108));
}
