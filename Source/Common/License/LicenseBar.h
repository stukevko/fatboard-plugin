#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "LicenseClient.h"
#include "FatboardLookAndFeel.h"

/** The thin strip at the top of a paid plugin: "Demo · Log in", the code while waiting, or who it is licensed to.
    Invisible in builds without licensing. */
class LicenseBar : public juce::Component, private juce::Timer
{
public:
    explicit LicenseBar (LicenseClient& c) : client (c)
    {
        setVisible (client.isEnabled());
        if (! client.isEnabled()) return;
        addAndMakeVisible (button);
        button.onClick = [this]
        {
            const auto s = client.getStatus();
            if (s.state == LicenseClient::State::Licensed) client.logout(); else client.login();
            refresh();
        };
        startTimerHz (3);
        refresh();
    }

    void resized() override { button.setBounds (getLocalBounds().removeFromRight (96).reduced (0, 1)); }

    void paint (juce::Graphics& g) override
    {
        using namespace FatboardColours;
        const auto accent = FatboardLookAndFeel::accentOf (*this);
        auto r = getLocalBounds().withTrimmedRight (104);
        g.setFont (FatboardLookAndFeel::body (12.0f));
        g.setColour (shown.state == LicenseClient::State::Licensed ? grey : accent);
        g.drawText (text, r, juce::Justification::centredLeft, true);
    }

private:
    void timerCallback() override { refresh(); }
    void refresh()
    {
        shown = client.getStatus();
        juce::String t, b = "Log in";
        switch (shown.state)
        {
            case LicenseClient::State::Open:     break;
            case LicenseClient::State::Demo:     t = "DEMO: sound drops out every 40 seconds.  " + (shown.message.isNotEmpty() ? shown.message : juce::String ("Log in to your Fatboard account to unlock.")); break;
            case LicenseClient::State::Waiting:  t = shown.userCode.isNotEmpty() ? "Confirm in your browser. Code: " + shown.userCode : shown.message; b = "Waiting"; break;
            case LicenseClient::State::Licensed: t = "Licensed to " + shown.email; b = "Log out"; break;
        }
        button.setEnabled (shown.state != LicenseClient::State::Waiting);
        if (b != button.getButtonText()) button.setButtonText (b);
        if (t != text) { text = t; repaint(); }
    }

    LicenseClient& client;
    LicenseClient::Status shown;
    juce::String text;
    juce::TextButton button { "Log in" };
};
