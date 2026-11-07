#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "CleanupProcessor.h"
#include "FatboardLookAndFeel.h"
#include "FatboardWidgets.h"

class CleanupEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CleanupEditor (CleanupProcessor&);
    ~CleanupEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    CleanupProcessor& proc;
    FatboardLookAndFeel lnf;

    FatboardHeader header { "Cleanup" };
    BigKnob knob { "Amount" };
    ActivityMark activity;
    FatboardFooter footer { JucePlugin_VersionString };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> activeAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CleanupEditor)
};
