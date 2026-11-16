#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "TapProcessor.h"
#include "FatboardLookAndFeel.h"
#include "FatboardWidgets.h"

class PadView : public juce::Component
{
public:
    std::function<void()> onHit, onReset;
    void set (const juce::String& keyLetter, const juce::String& sampleName) { if (keyLetter != key || sampleName != name) { key = keyLetter; name = sampleName; repaint(); } }
    void flash (float v) { if (v > glow) glow = v; else glow *= 0.82f; if (glow > 0.01f || lastGlow > 0.01f) repaint(); lastGlow = glow; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) { if (onReset) onReset(); } else if (onHit) onHit(); }
private:
    juce::String key, name; float glow = 0.0f, lastGlow = 0.0f;
};

/** The recorded loop: one row per pad. Drag it out as MIDI. Right-click a row to wipe that pad. */
class LoopView : public juce::Component
{
public:
    std::function<juce::File()> makeFile; std::function<void (int)> onClearPad;
    void set (std::vector<TapProcessor::Event> e, double beats) { events = std::move (e); loopBeats = beats; repaint(); }
    void setPlayBeat (double b) { if (std::abs (b - playBeat) > 1.0e-3) { playBeat = b; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu() && onClearPad) onClearPad (juce::jlimit (0, 3, (int) ((float) (e.y - 8) / juce::jmax (1.0f, ((float) getHeight() - 16.0f) / 4.0f)))); }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragStarted = false; }
private:
    std::vector<TapProcessor::Event> events; double loopBeats = 8, playBeat = -1; bool dragStarted = false;
};

class TapEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit TapEditor (TapProcessor&);
    ~TapEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
private:
    void timerCallback() override;
    TapProcessor& proc;
    FatboardLookAndFeel lnf { FatboardColours::ice };
    FatboardHeader header { "Tap" };
    PadView pads[4];
    LoopView loopView;
    juce::TextButton recButton { "Rec" }, clearButton { "Clear" };
    std::unique_ptr<PillGroup> bars, quant;
    BigKnob levelKnob { "Level" };
    FatboardFooter footer { JucePlugin_VersionString };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> levelAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> recAtt;
    bool down[4] { false, false, false, false }; int shown = -1; juce::String message;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapEditor)
};
