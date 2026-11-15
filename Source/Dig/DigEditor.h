#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DigProcessor.h"
#include "FatboardLookAndFeel.h"
#include "FatboardWidgets.h"

/** The whole song. The bright block is the loop; click or drag to move it (it snaps to the beat). */
class SongView : public juce::Component
{
public:
    std::function<void (double seconds)> onPick;
    void setSong (std::vector<float> p, double secs) { peaks = std::move (p); seconds = secs; repaint(); }
    void setRegion (double start, double len) { if (std::abs (start - regionStart) > 1.0e-4 || std::abs (len - regionLen) > 1.0e-4) { regionStart = start; regionLen = len; repaint(); } }
    void setMessage (const juce::String& m) { if (m != message) { message = m; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent&) override;
private:
    std::vector<float> peaks; double seconds = 0, regionStart = 0, regionLen = 0; juce::String message;
};

/** The loop at the project tempo with its chops. This is what you drag into the playlist. */
class ClipView : public juce::Component
{
public:
    std::function<juce::File()> makeFile;
    void setClip (const Dig::Clip* c);
    void setPlaying (int slice) { if (slice != playing) { playing = slice; repaint(); } }
    void setStatus (const juce::String& s) { if (s != status) { status = s; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragStarted = false; }
private:
    std::vector<float> peaks; std::vector<float> cuts; juce::String status; int playing = -1; bool dragStarted = false;
};

/** One chop: click plays it, drag takes it out as its own WAV. */
class PadButton : public juce::TextButton
{
public:
    std::function<juce::File()> makeFile;
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragStarted && makeFile && isEnabled() && e.getDistanceFromDragStart() > 10)
        {
            dragStarted = true; const auto f = makeFile();
            if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
        }
        juce::TextButton::mouseDrag (e);
    }
    void mouseUp (const juce::MouseEvent& e) override { dragStarted = false; juce::TextButton::mouseUp (e); }
private:
    bool dragStarted = false;
};

class DigEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit DigEditor (DigProcessor&);
    ~DigEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
private:
    void timerCallback() override;

    DigProcessor& proc;
    FatboardLookAndFeel lnf { FatboardColours::ice };
    FatboardHeader header { "Dig" };
    SongView songView;
    ClipView clipView;
    juce::Label info1, info2;
    juce::TextButton loadButton { "Load song" }, findButton { "Find loop" }, backButton { "<" }, fwdButton { ">" }, playButton { "Play loop" },
                     halfButton { "/2" }, doubleButton { "x2" }, gateButton { "Gate" };
    juce::Slider bpmSlider;
    std::unique_ptr<PillGroup> length, mode, chops;
    BigKnob pitchKnob { "Pitch" }, nudgeKnob { "Nudge" }, levelKnob { "Level" };
    juce::OwnedArray<PadButton> pads;
    FatboardFooter footer { JucePlugin_VersionString };
    std::unique_ptr<juce::FileChooser> chooser;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAtt> pitchAtt, nudgeAtt, levelAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> gateAtt;
    int shownRender = -1, shownSong = -1; bool settingBpm = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DigEditor)
};
