#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DafocusProcessor.h"
#include "FatboardLookAndFeel.h"
#include "FatboardWidgets.h"

class DafocusEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DafocusEditor (DafocusProcessor&);
    ~DafocusEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    DafocusProcessor& proc;
    FatboardLookAndFeel lnf;

    FatboardHeader header { "Dafocus" };
    std::unique_ptr<PillGroup> spaceGroup, echoGroup;
    BigKnob amountKnob { "Amount" };
    BigKnob duckKnob { "Duck" };
    ActivityMark activity;
    FatboardFooter footer { JucePlugin_VersionString };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAtt, duckAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> activeAtt;

    float duckShown = 1.0f;
    juce::String bpmText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DafocusEditor)
};
