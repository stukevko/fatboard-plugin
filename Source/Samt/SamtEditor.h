#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SamtProcessor.h"
#include "FatboardLookAndFeel.h"
#include "FatboardWidgets.h"
#include "LicenseBar.h"
#include <array>

/** What the cell is doing: the last seconds of gain reduction as a curve hanging from the top, and the number. */
class ReductionView : public juce::Component
{
public:
    void push (float grDb) { history[(size_t) head] = grDb; head = (head + 1) % (int) history.size(); now += (grDb - now) * 0.35f; hold = juce::jmax (grDb, hold - 0.08f); repaint(); }
    void paint (juce::Graphics&) override;
private:
    std::array<float, 240> history {}; int head = 0; float now = 0.0f, hold = 0.0f;
};

class SamtEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SamtEditor (SamtProcessor&);
    ~SamtEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    SamtProcessor& proc;
    FatboardLookAndFeel lnf { FatboardColours::violet };
    FatboardHeader header { "Samt" };
    LicenseBar licenseBar { proc.license };
    ReductionView view;
    BigKnob reduceKnob { "Peak Reduction" }, gainKnob { "Gain" }, warmthKnob { "Warmth" }, mixKnob { "Mix" };
    std::unique_ptr<PillGroup> mode;
    FatboardFooter footer { JucePlugin_VersionString };
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAtt> reduceAtt, gainAtt, warmthAtt, mixAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> activeAtt;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamtEditor)
};
