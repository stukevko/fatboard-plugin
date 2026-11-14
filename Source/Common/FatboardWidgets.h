#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "FatboardLookAndFeel.h"
#include <array>
#include <cmath>

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
        FatboardLookAndFeel::drawMark (g, markArea, FatboardLookAndFeel::accentOf (*this), 1.0f);

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

    void setLabel (const juce::String& text) { if (text != label) { label = text; repaint(); } }

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
    float getActivity() const { return activity; }
    void setActivity (float a)
    {
        a = juce::jlimit (0.0f, 1.0f, a);
        if (std::abs (a - activity) > 0.005f) { activity = a; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const float s = juce::jmin (r.getWidth(), r.getHeight());
        FatboardLookAndFeel::drawMark (g, juce::Rectangle<float> (s, s).withCentre (r.getCentre()), FatboardLookAndFeel::accentOf (*this), activity);
    }

private:
    float activity = 0.0f;
};

/** The picture of what the plugin is doing right now: one line over 20 Hz – 20 kHz.
    Below the middle = taken out, above = added. Not an analyzer: it shows the plugin, not the audio. */
class CurveView : public juce::Component
{
public:
    static constexpr int points = 128;
    static constexpr float minHz = 30.0f, maxHz = 20000.0f, topDb = 9.0f, bottomDb = -15.0f;

    static float hzAt (int i) { return minHz * std::pow (maxHz / minHz, (float) i / (float) (points - 1)); }

    /** fn (hz) → dB. Repaints only when the picture changed. */
    template <typename Fn>
    void update (Fn&& fn)
    {
        bool changed = false;
        for (int i = 0; i < points; ++i)
        {
            const float v = juce::jlimit (bottomDb, topDb, fn (hzAt (i)));
            if (std::abs (v - db[(size_t) i]) > 0.03f) { db[(size_t) i] = v; changed = true; }
        }
        if (changed) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace FatboardColours;
        auto r = getLocalBounds().toFloat();
        g.setColour (panel);
        g.fillRoundedRectangle (r, 10.0f);
        auto in = r.reduced (12.0f, 10.0f);
        auto yOf = [in] (float d) { return in.getY() + in.getHeight() * (topDb - d) / (topDb - bottomDb); };
        auto xOf = [in] (float hz) { return in.getX() + in.getWidth() * std::log (hz / minHz) / std::log (maxHz / minHz); };

        // frequency marks
        g.setFont (FatboardLookAndFeel::body (9.5f));
        for (auto [hz, name] : { std::pair<float, const char*> { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } })
        {
            g.setColour (line);
            g.fillRect (juce::Rectangle<float> (xOf (hz), in.getY(), 1.0f, in.getHeight()));
            g.setColour (grey.withAlpha (0.7f));
            g.drawText (name, juce::Rectangle<float> (xOf (hz) + 4.0f, in.getBottom() - 11.0f, 30.0f, 11.0f), juce::Justification::centredLeft, false);
        }
        g.setColour (line);
        g.fillRect (juce::Rectangle<float> (in.getX(), yOf (0.0f), in.getWidth(), 1.0f));

        juce::Path curve, fill;
        const float zero = yOf (0.0f);
        for (int i = 0; i < points; ++i)
        {
            const float x = in.getX() + in.getWidth() * (float) i / (float) (points - 1), y = yOf (db[(size_t) i]);
            if (i == 0) { curve.startNewSubPath (x, y); fill.startNewSubPath (x, zero); fill.lineTo (x, y); }
            else        { curve.lineTo (x, y); fill.lineTo (x, y); }
        }
        fill.lineTo (in.getRight(), zero); fill.closeSubPath();
        const auto accent = FatboardLookAndFeel::accentOf (*this);
        g.setColour (accent.withAlpha (0.16f));
        g.fillPath (fill);
        g.setColour (accent);
        g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    std::array<float, points> db {};
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
        g.setColour (FatboardLookAndFeel::accentOf (*this));
        g.setFont (FatboardLookAndFeel::displayBold (11.0f));
        g.drawText ("FETT. BRETT.", r, juce::Justification::centredLeft, false);
        g.setColour (grey);
        g.setFont (FatboardLookAndFeel::body (11.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("fat-board.com  \xc2\xb7  v")) + version, r, juce::Justification::centredRight, false);
    }

private:
    juce::String version;
};

/** A row of pill buttons bound to an AudioParameterChoice. */
class PillGroup : public juce::Component
{
public:
    PillGroup (juce::RangedAudioParameter& param, juce::String labelText)
        : label (std::move (labelText)),
          attachment (param, [this] (float v) { select ((int) v, false); }, nullptr)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&param))
        {
            for (int i = 0; i < choice->choices.size(); ++i)
            {
                auto* b = buttons.add (new juce::TextButton (choice->choices[i]));
                b->setClickingTogglesState (false);
                b->onClick = [this, i] { select (i, true); };
                addAndMakeVisible (b);
            }
        }
        attachment.sendInitialUpdate();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (18);
        const int gap = 8;
        const int w = (r.getWidth() - gap * (buttons.size() - 1)) / juce::jmax (1, buttons.size());
        for (auto* b : buttons) { b->setBounds (r.removeFromLeft (w).withHeight (38)); r.removeFromLeft (gap); }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (FatboardColours::grey);
        g.setFont (FatboardLookAndFeel::displayBold (10.0f));
        g.drawText (label.toUpperCase(), getLocalBounds().removeFromTop (14), juce::Justification::centredLeft, false);
    }

private:
    void select (int index, bool fromUser)
    {
        for (int i = 0; i < buttons.size(); ++i) buttons[i]->setToggleState (i == index, juce::dontSendNotification);
        if (fromUser) attachment.setValueAsCompleteGesture ((float) index);
    }

    juce::String label;
    juce::OwnedArray<juce::TextButton> buttons;
    juce::ParameterAttachment attachment;
};
