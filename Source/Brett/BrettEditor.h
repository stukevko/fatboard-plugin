#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "BrettProcessor.h"
#include "FatboardLookAndFeel.h"
#include "FatboardWidgets.h"

/** Where the song sits: quiet … streaming … loud … too much. */
class LoudnessBar : public juce::Component
{
public:
    void set (float lufs, bool valid) { if (std::abs (lufs - value) > 0.1f || valid != on) { value = lufs; on = valid; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        using namespace FatboardColours;
        auto r = getLocalBounds().toFloat();
        auto bar = r.removeFromTop (14.0f);
        g.setColour (panel); g.fillRoundedRectangle (bar, 7.0f);
        const float lo = -24.0f, hi = -5.0f;
        auto xOf = [&] (float l) { return bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, (l - lo) / (hi - lo)); };
        const auto accent = FatboardLookAndFeel::accentOf (*this);
        // the good zone
        g.setColour (accent.withAlpha (0.22f));
        g.fillRoundedRectangle (juce::Rectangle<float> (xOf (-14.0f), bar.getY(), xOf (-8.0f) - xOf (-14.0f), bar.getHeight()), 7.0f);
        if (on)
        {
            g.setColour (value > -7.0f ? juce::Colour (0xFFFF4A3D) : accent);
            g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ xOf (value), bar.getCentreY() }));
        }
        g.setColour (grey); g.setFont (FatboardLookAndFeel::body (10.5f));
        auto labels = r.reduced (0.0f, 3.0f);
        g.drawText ("QUIET", labels, juce::Justification::centredLeft, false);
        g.drawText ("RELEASE LOUD", juce::Rectangle<float> (xOf (-14.0f), labels.getY(), xOf (-8.0f) - xOf (-14.0f), labels.getHeight()), juce::Justification::centred, false);
        g.drawText ("TOO MUCH", labels, juce::Justification::centredRight, false);
    }
private:
    float value = -100.0f; bool on = false;
};

class BrettEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BrettEditor (BrettProcessor&);
    ~BrettEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    BrettProcessor& proc;
    FatboardLookAndFeel lnf { FatboardColours::ice };

    FatboardHeader header { "Brett" };
    BigKnob loudKnob { "Loud" };
    LoudnessBar bar;
    std::unique_ptr<PillGroup> style, check;
    juce::TextButton relearn { "Listen again" };
    ActivityMark activity;
    FatboardFooter footer { JucePlugin_VersionString };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> loudAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> activeAtt;
    bool checkOn = false, heard = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrettEditor)
};
