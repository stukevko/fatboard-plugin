#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SpitProcessor.h"
#include "FatboardLookAndFeel.h"
#include "FatboardWidgets.h"

/** One drum pad: lights on hit, has a "learn" button underneath. */
class DrumPad : public juce::Component
{
public:
    DrumPad (juce::String name, juce::Colour c) : label (std::move (name)), colour (c)
    {
        learn.setButtonText ("learn");
        addAndMakeVisible (learn);
    }
    juce::TextButton learn;
    void flash (float v) { glow = juce::jmax (glow, v); repaint(); }
    void tick() { if (glow > 0.0f) { glow = juce::jmax (0.0f, glow - 0.08f); repaint(); } }
    void setLearning (bool b, int n) { learning = b; learnN = n; repaint(); }

    void resized() override { learn.setBounds (getLocalBounds().removeFromBottom (34).reduced (10, 2)); }
    void paint (juce::Graphics& g) override
    {
        using namespace FatboardColours;
        auto r = getLocalBounds().toFloat(); r.removeFromBottom (40);
        g.setColour (panel); g.fillRoundedRectangle (r, 22.0f);
        g.setColour (learning ? colour : line); g.drawRoundedRectangle (r.reduced (1.0f), 22.0f, learning ? 3.0f : 1.5f);
        if (glow > 0.0f) { g.setColour (colour.withAlpha (glow * 0.9f)); g.fillRoundedRectangle (r.reduced (6.0f), 18.0f); }
        g.setColour (glow > 0.5f ? black : bone);
        g.setFont (FatboardLookAndFeel::display (22.0f));
        g.drawText (label.toUpperCase(), r, juce::Justification::centred, false);
        if (learning)
        {
            g.setColour (colour); g.setFont (FatboardLookAndFeel::bodyBold (12.0f));
            g.drawText ("do it " + juce::String (3 - learnN) + "x", r.removeFromBottom (26), juce::Justification::centred, false);
        }
    }
private:
    juce::String label; juce::Colour colour; float glow = 0.0f; bool learning = false; int learnN = 0;
};

/** Draggable MIDI chip: drag it into the DAW to drop the take as a .mid */
class MidiChip : public juce::Component
{
public:
    std::function<juce::MemoryBlock()> buildFile;
    std::function<int()> hitCount;
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
private:
    juce::File tempFile;
};

class SpitEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpitEditor (SpitProcessor&);
    ~SpitEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;

    SpitProcessor& proc;
    FatboardLookAndFeel lnf { FatboardColours::lime };

    FatboardHeader header { "Spit" };
    DrumPad kick { "Kick", FatboardColours::lime }, snare { "Snare", FatboardColours::lime }, hat { "Hat", FatboardColours::lime };
    BigKnob sensKnob { "Sens" }, tightKnob { "Tight" };
    std::unique_ptr<PillGroup> gridGroup;
    juce::TextButton recButton { "Record take" }, clearButton { "Clear" };
    MidiChip chip;
    ActivityMark activity;
    FatboardFooter footer { JucePlugin_VersionString };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sensAtt, tightAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> activeAtt;
    int lastCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpitEditor)
};
