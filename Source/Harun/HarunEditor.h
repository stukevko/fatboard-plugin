#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "HarunProcessor.h"
#include "FatboardLookAndFeel.h"
#include "FatboardWidgets.h"

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
    FatboardLookAndFeel lnf { FatboardColours::ember };

    FatboardHeader header { "Harun" };
    std::unique_ptr<PillGroup> modeGroup;
    BigKnob amountKnob { "Amount" };
    BigKnob widthKnob { "Width" };
    BigKnob tuckKnob { "Tuck" };
    ActivityMark activity;
    FatboardFooter footer { JucePlugin_VersionString };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAtt, widthAtt, tuckAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> activeAtt;

    bool scShown = false; float rideShown = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarunEditor)
};
