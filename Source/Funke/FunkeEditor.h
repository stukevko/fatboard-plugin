#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "FunkeProcessor.h"
#include "FatboardLookAndFeel.h"
#include "FatboardWidgets.h"

/** One layer as a small piano roll. Drag it into the piano roll / channel rack: it leaves as a MIDI file. */
class LaneView : public juce::Component
{
public:
    std::function<juce::File()> makeFile;
    void setNotes (const std::vector<Funke::Note>& n, double beats, const juce::String& title, const juce::StringArray& barLabels) { notes = n; loopBeats = beats; name = title; labels = barLabels; repaint(); }
    void setPlayBeat (double b) { if (std::abs (b - playBeat) > 1.0e-3) { playBeat = b; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragStarted = false; }
private:
    std::vector<Funke::Note> notes; double loopBeats = 16, playBeat = -1; juce::String name; juce::StringArray labels; bool dragStarted = false;
};

/** Where the loaded drum loop hits: top row the kick, bottom row everything. Two bars of sixteenths. */
class DrumStrip : public juce::Component
{
public:
    void set (const Funke::DrumGrid& g, const juce::String& text) { grid = g; message = text; repaint(); }
    void paint (juce::Graphics&) override;
private:
    Funke::DrumGrid grid; juce::String message;
};

class FunkeEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit FunkeEditor (FunkeProcessor&);
    ~FunkeEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
private:
    void timerCallback() override;
    FunkeProcessor& proc;
    FatboardLookAndFeel lnf { FatboardColours::lime };
    FatboardHeader header { "Funke" };
    std::unique_ptr<PillGroup> vibe, key, bars, drumMode;
    DrumStrip drumStrip;
    LaneView lanes[3];
    juce::TextButton newButtons[3], rollAll { "New idea" }, listenButton { "Listen" }, allMidi { "Drag all" }, clearDrums { "x" };
    BigKnob motionKnob { "Motion" }, densityKnob { "Density" }, levelKnob { "Level" };
    FatboardFooter footer { JucePlugin_VersionString };
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAtt> motionAtt, densityAtt, levelAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> listenAtt;
    int shown = -1; juce::String drumError; bool allDragStarted = false;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { allDragStarted = false; }
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FunkeEditor)
};
