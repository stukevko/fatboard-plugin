#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SogProcessor.h"
#include "FatboardLookAndFeel.h"
#include "FatboardWidgets.h"
#include "LicenseBar.h"

/** Shows the rendered swell and is the thing you drag into the playlist. */
class SwellView : public juce::Component
{
public:
    std::function<juce::File()> makeFile;
    void setSwell (const juce::AudioBuffer<float>& b);
    void setStatus (const juce::String& s, float level) { if (s != status || std::abs (level - meter) > 0.01f) { status = s; meter = level; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragStarted = false; }
private:
    std::vector<float> peaks; juce::String status; float meter = 0.0f; bool dragStarted = false;
};

class SogEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SogEditor (SogProcessor&);
    ~SogEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;

    SogProcessor& proc;
    FatboardLookAndFeel lnf { FatboardColours::mint };
    FatboardHeader header { "Sog" };
    LicenseBar licenseBar { proc.license };
    SwellView view;
    juce::TextButton captureButton { "Capture" }, previewButton { "Preview" }, lowCutButton { "Low cut" };
    std::unique_ptr<PillGroup> length, tremDiv;
    BigKnob sizeKnob { "Size" }, toneKnob { "Tone" }, widthKnob { "Width" }, tremKnob { "Tremolo" }, levelKnob { "Level" };
    FatboardFooter footer { JucePlugin_VersionString };

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAtt> sizeAtt, toneAtt, widthAtt, tremAtt, levelAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lowCutAtt;
    int shownRender = -1; SogProcessor::State shownState = SogProcessor::State::Empty;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SogEditor)
};
