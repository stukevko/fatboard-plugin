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
    void refreshFineLabels();

    ProProcessor& proc;
    FatboardLookAndFeel lnf;

    FatboardHeader header { "Scrub Pro" };
    CurveView curveView;
    BigKnob scrubKnob { "Scrub" };
    BigKnob airKnob { "Air" };
    BigKnob fine[4] { BigKnob { "Low Cut" }, BigKnob { "Mud" }, BigKnob { "Harsh" }, BigKnob { "Top" } };
    ActivityMark activity;
    FatboardFooter footer { JucePlugin_VersionString };

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAtt> scrubAtt, airAtt, fineAtt[4];
    std::unique_ptr<ButtonAtt> activeAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProEditor)
};
