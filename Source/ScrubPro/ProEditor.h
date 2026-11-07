#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ProProcessor.h"
#include "FatboardLookAndFeel.h"
#include "FatboardWidgets.h"

class ProEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ProEditor (ProProcessor&);
    ~ProEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void modeClicked (int index);
    void refreshModeButtons();

    ProProcessor& proc;
    FatboardLookAndFeel lnf;

    FatboardHeader header { "Scrub Pro" };
    BigKnob cleanupKnob { "Cleanup" };
    BigKnob modeKnob { "Mode" };
    juce::TextButton modeButtons[3];
    ActivityMark activity;
    FatboardFooter footer { JucePlugin_VersionString };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> cleanupAtt, modeAmountAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> activeAtt;
    juce::ParameterAttachment modeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProEditor)
};
