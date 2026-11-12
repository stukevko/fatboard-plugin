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
    void refreshRoom();

    ProProcessor& proc;
    FatboardLookAndFeel lnf;

    FatboardHeader header { "Scrub Pro" };
    BigKnob scrubKnob { "Scrub" };
    BigKnob airKnob { "Air" };
    juce::TextButton roomButton { "Room" };
    ActivityMark activity;
    FatboardFooter footer { JucePlugin_VersionString };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> scrubAtt, airAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> activeAtt, roomAtt;

    bool roomOn = false, roomLinked = false, roomVocal = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProEditor)
};
