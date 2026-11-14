#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// ============================================================================
//  Fatboard look – black, one signal yellow, extremely fat type.
//  Palette and fonts in one place so every plugin matches the website.
// ============================================================================
namespace FatboardColours
{
    const juce::Colour black   { 0xFF0B0B0B };
    const juce::Colour panel   { 0xFF141414 };
    const juce::Colour line    { 0xFF262626 };
    const juce::Colour ring    { 0xFF222222 };
    const juce::Colour yellow  { 0xFFF5E100 };   // Scrub / brand
    const juce::Colour ember   { 0xFFFF5A1F };   // Harun
    const juce::Colour ice     { 0xFF4FD8FF };   // free (was Korrent)
    const juce::Colour lime    { 0xFFC8FF00 };   // free (was Spit)
    const juce::Colour magenta { 0xFFFF3DA6 };   // Smash
    const juce::Colour mint    { 0xFF2EE6A6 };   // Sog (reverse reverb)
    const juce::Colour bone    { 0xFFF3F1EC };
    const juce::Colour grey    { 0xFF8A877F };
    const juce::Colour dim     { 0xFF4A4840 };
}

class FatboardLookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit FatboardLookAndFeel (juce::Colour accentColour = FatboardColours::yellow);
    juce::Colour accent;

    static juce::Font display (float height);   // Unbounded Black
    static juce::Font displayBold (float height); // Unbounded Bold
    static juce::Font body (float height);      // Inter Medium
    static juce::Font bodyBold (float height);  // Inter SemiBold

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOverButton, bool isButtonDown) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon,
                            const juce::Colour* textColour) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    juce::Font getLabelFont (juce::Label&) override;

    /** Draws the three-bar mark into the given bounds. activity 0..1 lights the bars up. */
    static void drawMark (juce::Graphics&, juce::Rectangle<float> bounds, juce::Colour colour, float activity = 1.0f);

    /** Accent of the LookAndFeel a component is using (falls back to brand yellow). */
    static juce::Colour accentOf (const juce::Component& c)
    {
        if (auto* l = dynamic_cast<const FatboardLookAndFeel*> (&c.getLookAndFeel())) return l->accent;
        return FatboardColours::yellow;
    }

private:
    static juce::Typeface::Ptr unboundedBlack, unboundedBold, interMedium, interSemiBold;
    static void loadTypefaces();
};
