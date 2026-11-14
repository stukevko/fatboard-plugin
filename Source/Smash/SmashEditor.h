#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SmashProcessor.h"
#include "FatboardLookAndFeel.h"
#include "FatboardWidgets.h"
#include "LicenseBar.h"

class SmashEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SmashEditor (SmashProcessor&);
    ~SmashEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    SmashProcessor& proc;
    FatboardLookAndFeel lnf { FatboardColours::magenta };
    FatboardHeader header { "Smash" };
    LicenseBar licenseBar { proc.license };
    std::unique_ptr<PillGroup> styleGroup;
    BigKnob smashKnob { "Smash" }, fitKnob { "Fit" };
    ActivityMark activity;
    FatboardFooter footer { JucePlugin_VersionString };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smashAtt, fitAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> activeAtt;
    float gr = 0, mud = 0, hi = 0; bool sc = false; std::array<float, 3> fit { 0, 0, 0 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmashEditor)
};
