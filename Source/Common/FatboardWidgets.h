#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "FatboardLookAndFeel.h"

// ============================================================================
//  Small reusable pieces shared by Cleanup and Pro editors
// ============================================================================

/** Mark + wordmark + product name on the left, bypass toggle on the right. */
class FatboardHeader : public juce::Component
{
public:
    explicit FatboardHeader (juce::String productName) : product (std::move (productName))
    {
        power.setButtonText ("IN");
        power.setClickingTogglesState (true);
        power.setToggleState (true, juce::dontSendNotification);
        power.setTooltip ("Bypass");
        addAndMakeVisible (power);
    }

    juce::TextButton power;

    void resized() override
    {
        power.setBounds (getLocalBounds().removeFromRight (64).withSizeKeepingCentre (56, 32));
    }

    void paint (juce::Graphics& g) override
    {
        using namespace FatboardColours;
        auto r = getLocalBounds().toFloat();
        const float markSize = 30.0f;
        auto markArea = juce::Rectangle<float> (markSize, markSize).withCentre ({ r.getX() + markSize * 0.5f, r.getCentreY() });
        FatboardLookAndFeel::drawMark (g, markArea, yellow, 1.0f);

        auto text = r.withTrimmedLeft (markSize + 12.0f).withTrimmedRight (72.0f);

        // brand small on top, product name big underneath
        g.setColour (grey);
        g.setFont (FatboardLookAndFeel::displayBold (10.0f));
        g.drawText ("FATBOARD", text.removeFromTop (r.getHeight() * 0.40f), juce::Justification::bottomLeft, false);

        g.setColour (bone);
        g.setFont (FatboardLookAndFeel::display (22.0f));
        g.drawText (product.toUpperCase(), text, juce::Justification::topLeft, false);
    }

private:
    juce::String product;
};

/** The big knob with a label underneath. */
class BigKnob : public juce::Component
{
public:
    explicit BigKnob (juce::String labelText) : label (std::move (labelText))
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                    juce::MathConstants<float>::pi * 2.75f, true);
        slider.setDoubleClickReturnValue (true, 0.5);
        slider.setMouseDragSensitivity (220);
        addAndMakeVisible (slider);
    }

    juce::Slider slider;

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromBottom (34);
        slider.setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace FatboardColours;
        auto r = getLocalBounds();
        auto l = r.removeFromBottom (34);
        g.setColour (grey);
        g.setFont (FatboardLookAndFeel::displayBold (12.0f));
        g.drawText (label.toUpperCase(), l, juce::Justification::centredTop, false);
    }

private:
    juce::String label;
};

/** The mark, lit up by how much the engine is working. */
class ActivityMark : public juce::Component
{
public:
    void setActivity (float a)
    {
        a = juce::jlimit (0.0f, 1.0f, a);
        if (std::abs (a - activity) > 0.005f) { activity = a; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const float s = juce::jmin (r.getWidth(), r.getHeight());
        FatboardLookAndFeel::drawMark (g, juce::Rectangle<float> (s, s).withCentre (r.getCentre()), FatboardColours::yellow, activity);
    }

private:
    float activity = 0.0f;
};

/** Footer line: claim on the left, url + version on the right. */
class FatboardFooter : public juce::Component
{
public:
    explicit FatboardFooter (juce::String versionText) : version (std::move (versionText)) {}

    void paint (juce::Graphics& g) override
    {
        using namespace FatboardColours;
        auto r = getLocalBounds().toFloat();
        g.setColour (line);
        g.fillRect (r.removeFromTop (1.0f));
        g.setColour (yellow);
        g.setFont (FatboardLookAndFeel::displayBold (11.0f));
        g.drawText ("FETT. BRETT.", r, juce::Justification::centredLeft, false);
        g.setColour (grey);
        g.setFont (FatboardLookAndFeel::body (11.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("fatboard.com  \xc2\xb7  v")) + version, r, juce::Justification::centredRight, false);
    }

private:
    juce::String version;
};
