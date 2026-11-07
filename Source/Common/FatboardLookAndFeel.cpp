#include "FatboardLookAndFeel.h"
#include "BinaryData.h"

using namespace FatboardColours;

juce::Typeface::Ptr FatboardLookAndFeel::unboundedBlack;
juce::Typeface::Ptr FatboardLookAndFeel::unboundedBold;
juce::Typeface::Ptr FatboardLookAndFeel::interMedium;
juce::Typeface::Ptr FatboardLookAndFeel::interSemiBold;

void FatboardLookAndFeel::loadTypefaces()
{
    if (unboundedBlack != nullptr) return;
    unboundedBlack = juce::Typeface::createSystemTypefaceFor (FatboardAssets::UnboundedBlack_ttf, FatboardAssets::UnboundedBlack_ttfSize);
    unboundedBold  = juce::Typeface::createSystemTypefaceFor (FatboardAssets::UnboundedBold_ttf,  FatboardAssets::UnboundedBold_ttfSize);
    interMedium    = juce::Typeface::createSystemTypefaceFor (FatboardAssets::InterMedium_ttf,    FatboardAssets::InterMedium_ttfSize);
    interSemiBold  = juce::Typeface::createSystemTypefaceFor (FatboardAssets::InterSemiBold_ttf,  FatboardAssets::InterSemiBold_ttfSize);
}

static juce::Font fontFor (juce::Typeface::Ptr tf, float height)
{
    if (tf == nullptr) return juce::Font (juce::FontOptions (height));
    return juce::Font (juce::FontOptions (tf).withHeight (height));
}

juce::Font FatboardLookAndFeel::display (float h)     { loadTypefaces(); return fontFor (unboundedBlack, h); }
juce::Font FatboardLookAndFeel::displayBold (float h) { loadTypefaces(); return fontFor (unboundedBold, h); }
juce::Font FatboardLookAndFeel::body (float h)        { loadTypefaces(); return fontFor (interMedium, h); }
juce::Font FatboardLookAndFeel::bodyBold (float h)    { loadTypefaces(); return fontFor (interSemiBold, h); }

FatboardLookAndFeel::FatboardLookAndFeel()
{
    loadTypefaces();
    setColour (juce::ResizableWindow::backgroundColourId, black);
    setColour (juce::Slider::rotarySliderFillColourId, yellow);
    setColour (juce::Slider::rotarySliderOutlineColourId, ring);
    setColour (juce::Slider::thumbColourId, yellow);
    setColour (juce::Label::textColourId, bone);
    setColour (juce::TextButton::buttonColourId, panel);
    setColour (juce::TextButton::buttonOnColourId, yellow);
    setColour (juce::TextButton::textColourOffId, bone);
    setColour (juce::TextButton::textColourOnId, black);
    setColour (juce::TooltipWindow::backgroundColourId, panel);
    setColour (juce::TooltipWindow::textColourId, bone);
}

juce::Font FatboardLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return bodyBold (juce::jmin (15.0f, buttonHeight * 0.45f));
}

juce::Font FatboardLookAndFeel::getLabelFont (juce::Label& l)
{
    return l.getFont();
}

void FatboardLookAndFeel::drawMark (juce::Graphics& g, juce::Rectangle<float> b, juce::Colour colour, float activity)
{
    // three bars: thin, medium, fat – proportions from the brand mark (36-unit grid)
    const float u = b.getHeight() / 36.0f;
    const float x = b.getX() + 4.0f * u;
    const float w = 28.0f * u;
    const float heights[3] = { 4.0f, 7.0f, 10.0f };
    const float ys[3]      = { 5.0f, 12.0f, 22.0f };
    for (int i = 0; i < 3; ++i)
    {
        // activity lights bars progressively: thin bar lights first, fat bar last
        const float threshold = (float) i / 3.0f;
        const float lit = juce::jlimit (0.0f, 1.0f, (activity - threshold) * 3.0f);
        g.setColour (dim.interpolatedWith (colour, lit));
        g.fillRoundedRectangle (x, b.getY() + ys[i] * u, w, heights[i] * u, heights[i] * u * 0.5f);
    }
}

void FatboardLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                                            float startAngle, float endAngle, juce::Slider& s)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    auto circle = juce::Rectangle<float> (size, size).withCentre (bounds.getCentre());
    const float ringW = juce::jmax (6.0f, size * 0.055f);
    const float radius = size * 0.5f - ringW * 0.5f;
    const auto centre = circle.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    // body
    g.setColour (black);
    g.fillEllipse (circle);

    // outer ring (track)
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (ring);
    g.strokePath (track, juce::PathStrokeType (ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    if (sliderPos > 0.001f)
    {
        juce::Path val;
        val.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
        g.setColour (s.isEnabled() ? yellow : dim);
        g.strokePath (val, juce::PathStrokeType (ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // inner face
    auto face = circle.reduced (ringW * 2.2f);
    g.setColour (panel);
    g.fillEllipse (face);
    g.setColour (line);
    g.drawEllipse (face, 1.0f);

    // pointer
    const float pLen = face.getWidth() * 0.34f;
    const float pW   = juce::jmax (4.0f, face.getWidth() * 0.06f);
    juce::Path p;
    p.addRoundedRectangle (-pW * 0.5f, -face.getWidth() * 0.46f, pW, pLen, pW * 0.5f);
    p.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));
    g.setColour (s.isEnabled() ? yellow : dim);
    g.fillPath (p);
}

void FatboardLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                                bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const float radius = r.getHeight() * 0.5f;
    const bool on = b.getToggleState();

    if (on)
    {
        g.setColour (down ? yellow.darker (0.15f) : yellow);
        g.fillRoundedRectangle (r, radius);
    }
    else
    {
        g.setColour (down ? line : (over ? panel.brighter (0.08f) : panel));
        g.fillRoundedRectangle (r, radius);
        g.setColour (over ? grey : line);
        g.drawRoundedRectangle (r, radius, 1.5f);
    }
}

void FatboardLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.getToggleState() ? black : (b.isEnabled() ? bone : grey));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (8, 0), juce::Justification::centred, 1);
}
