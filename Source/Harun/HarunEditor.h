#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "HarunProcessor.h"
#include "FatboardLookAndFeel.h"
#include "FatboardWidgets.h"

/** A row of pill buttons bound to an AudioParameterChoice. */
class PillGroup : public juce::Component
{
public:
    PillGroup (juce::RangedAudioParameter& param, juce::String labelText)
        : label (std::move (labelText)),
          attachment (param, [this] (float v) { select ((int) v, false); }, nullptr)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&param))
        {
            for (int i = 0; i < choice->choices.size(); ++i)
            {
                auto* b = buttons.add (new juce::TextButton (choice->choices[i]));
                b->setClickingTogglesState (false);
                b->onClick = [this, i] { select (i, true); };
                addAndMakeVisible (b);
            }
        }
        attachment.sendInitialUpdate();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (18);
        const int gap = 8;
        const int w = (r.getWidth() - gap * (buttons.size() - 1)) / juce::jmax (1, buttons.size());
        for (auto* b : buttons) { b->setBounds (r.removeFromLeft (w).withHeight (38)); r.removeFromLeft (gap); }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (FatboardColours::grey);
        g.setFont (FatboardLookAndFeel::displayBold (10.0f));
        g.drawText (label.toUpperCase(), getLocalBounds().removeFromTop (14), juce::Justification::centredLeft, false);
    }

private:
    void select (int index, bool fromUser)
    {
        for (int i = 0; i < buttons.size(); ++i) buttons[i]->setToggleState (i == index, juce::dontSendNotification);
        if (fromUser) attachment.setValueAsCompleteGesture ((float) index);
    }

    juce::String label;
    juce::OwnedArray<juce::TextButton> buttons;
    juce::ParameterAttachment attachment;
};

class HarunEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HarunEditor (HarunProcessor&);
    ~HarunEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    HarunProcessor& proc;
    FatboardLookAndFeel lnf;

    FatboardHeader header { "Harun" };
    juce::ComboBox keyBox;
    std::unique_ptr<PillGroup> scaleGroup, voiceGroup, modeGroup;
    BigKnob blendKnob { "Blend" };
    BigKnob humanKnob { "Human" };
    ActivityMark activity;
    FatboardFooter footer { JucePlugin_VersionString };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> blendAtt, humanAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> activeAtt;

    juce::String noteText = "-";
    juce::Rectangle<int> keyLabelArea;
    juce::String harmonyText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarunEditor)
};
