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
    void refreshLabels();

    HarunProcessor& proc;
    FatboardLookAndFeel lnf { FatboardColours::ember };

    FatboardHeader header { "Harun" };
    juce::TextButton templateButtons[HarunTemplates::count];
    CurveView curveView;
    BigKnob widthKnob { "Width" };
    BigKnob levelKnob { "Level" };
    BigKnob spaceKnob { "Space" };
    std::unique_ptr<PillGroup> spaceType;
    ActivityMark activity;
    FatboardFooter footer { JucePlugin_VersionString };

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAtt> widthAtt, levelAtt, spaceAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> activeAtt;
    int shownTemplate = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarunEditor)
};
