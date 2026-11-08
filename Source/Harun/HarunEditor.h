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
