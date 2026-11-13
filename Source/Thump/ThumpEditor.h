#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ThumpProcessor.h"
#include "FatboardLookAndFeel.h"
#include "FatboardWidgets.h"

class ThumpEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit ThumpEditor (ThumpProcessor&);
    ~ThumpEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragging = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragging = false; repaint(); }

private:
    void timerCallback() override;
    void load (const juce::File& f);

    ThumpProcessor& proc;
    FatboardLookAndFeel lnf { FatboardColours::violet };

    FatboardHeader header { "Thump" };
    std::unique_ptr<PillGroup> tone;
    BigKnob subKnob { "808" }, kickKnob { "Kick" }, decayKnob { "Decay" }, clipKnob { "Clip" };
    juce::TextButton loadButton { "Load your 808..." };
    ActivityMark activity;
    FatboardFooter footer { JucePlugin_VersionString };
    std::unique_ptr<juce::FileChooser> chooser;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAtt> subAtt, kickAtt, decayAtt, clipAtt;
    juce::Rectangle<int> sampleBox;
    juce::String sampleText, message;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThumpEditor)
};
