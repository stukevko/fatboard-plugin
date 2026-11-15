#include "DigEditor.h"

using namespace FatboardColours;

static juce::String clock (double s) { const int t = (int) s; return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2); }

// ---------------------------------------------------------------- SongView
void SongView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const auto accent = FatboardLookAndFeel::accentOf (*this);
    g.setColour (panel); g.fillRoundedRectangle (r, 10.0f);
    auto in = r.reduced (12.0f, 8.0f);
    if (peaks.empty() || seconds <= 0.0)
    {
        g.setColour (grey); g.setFont (FatboardLookAndFeel::body (13.0f));
        g.drawText (message, in, juce::Justification::centred, false);
        return;
    }
    const float w = in.getWidth() / (float) peaks.size(), mid = in.getCentreY();
    float mx = 1.0e-4f; for (float p : peaks) mx = juce::jmax (mx, p);
    const float x0 = in.getX() + (float) (regionStart / seconds) * in.getWidth(), x1 = in.getX() + (float) ((regionStart + regionLen) / seconds) * in.getWidth();
    for (size_t i = 0; i < peaks.size(); ++i)
    {
        const float x = in.getX() + (float) i * w, h = juce::jmax (1.0f, in.getHeight() * 0.5f * std::sqrt (peaks[i] / mx));
        g.setColour (x >= x0 && x < x1 ? accent : dim);
        g.fillRect (juce::Rectangle<float> (x, mid - h, juce::jmax (1.0f, w), h * 2.0f));
    }
    g.setColour (accent.withAlpha (0.18f)); g.fillRect (juce::Rectangle<float> (x0, in.getY(), juce::jmax (3.0f, x1 - x0), in.getHeight()));
    g.setColour (bone); g.fillRect (juce::Rectangle<float> (x0, in.getY(), 1.5f, in.getHeight()));
}

void SongView::mouseDrag (const juce::MouseEvent& e)
{
    if (peaks.empty() || seconds <= 0.0 || ! onPick) return;
    const auto in = getLocalBounds().toFloat().reduced (12.0f, 8.0f);
    // the block is grabbed in its middle
    onPick (juce::jlimit (0.0, seconds, (double) ((float) e.x - in.getX()) / in.getWidth() * seconds - regionLen * 0.5));
}

// ---------------------------------------------------------------- ClipView
void ClipView::setClip (const Dig::Clip* c)
{
    peaks.clear(); cuts.clear();
    if (c != nullptr)
    {
        const int n = c->audio.getNumSamples(), cols = 260;
        if (n >= cols)
        {
            for (int k = 0; k < cols; ++k)
            {
                const int a = (int) ((juce::int64) k * n / cols), z = (int) ((juce::int64) (k + 1) * n / cols);
                float p = 0.0f; for (int ch = 0; ch < c->audio.getNumChannels(); ++ch) p = juce::jmax (p, c->audio.getMagnitude (ch, a, z - a));
                peaks.push_back (p);
            }
            for (int s : c->slices) cuts.push_back ((float) s / (float) n);
        }
    }
    repaint();
}

void ClipView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const auto accent = FatboardLookAndFeel::accentOf (*this);
    g.setColour (panel); g.fillRoundedRectangle (r, 12.0f);
    auto in = r.reduced (16.0f, 12.0f);
    auto text = in.removeFromBottom (20.0f);
    if (! peaks.empty())
    {
        auto nums = in.removeFromTop (14.0f);
        const float w = in.getWidth() / (float) peaks.size(), mid = in.getCentreY();
        float mx = 1.0e-4f; for (float p : peaks) mx = juce::jmax (mx, p);
        for (size_t c = 0; c < cuts.size(); ++c)
        {
            const float x = in.getX() + cuts[c] * in.getWidth(), xe = in.getX() + (c + 1 < cuts.size() ? cuts[c + 1] : 1.0f) * in.getWidth();
            if ((int) c == playing) { g.setColour (accent.withAlpha (0.22f)); g.fillRect (juce::Rectangle<float> (x, in.getY(), xe - x, in.getHeight())); }
            g.setColour (grey); g.setFont (FatboardLookAndFeel::body (10.0f));
            if (xe - x > 12.0f) g.drawText (juce::String ((int) c + 1), juce::Rectangle<float> (x + 2.0f, nums.getY(), xe - x, nums.getHeight()), juce::Justification::centredLeft, false);
        }
        for (size_t i = 0; i < peaks.size(); ++i)
        {
            const float h = juce::jmax (1.5f, in.getHeight() * 0.5f * std::sqrt (peaks[i] / mx));
            g.setColour (accent.withAlpha (0.9f));
            g.fillRect (juce::Rectangle<float> (in.getX() + (float) i * w, mid - h, juce::jmax (1.0f, w - 1.0f), h * 2.0f));
        }
        g.setColour (bone.withAlpha (0.55f));
        for (float c : cuts) g.fillRect (juce::Rectangle<float> (in.getX() + c * in.getWidth(), in.getY(), 1.0f, in.getHeight()));
    }
    g.setColour (peaks.empty() ? grey : bone.withAlpha (0.9f));
    g.setFont (FatboardLookAndFeel::body (12.5f));
    g.drawText (status, text, juce::Justification::centred, false);
}

void ClipView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || peaks.empty() || ! makeFile || e.getDistanceFromDragStart() < 8) return;
    dragStarted = true;
    const auto f = makeFile();
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

// ---------------------------------------------------------------- DigEditor
DigEditor::DigEditor (DigProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    header.power.setVisible (false);
    for (auto* c : std::initializer_list<juce::Component*> { &header, &songView, &clipView, &info1, &info2, &loadButton, &findButton, &backButton, &fwdButton, &playButton,
                                                             &halfButton, &doubleButton, &gateButton, &bpmSlider, &pitchKnob, &nudgeKnob, &levelKnob, &footer })
        addAndMakeVisible (c);
    length = std::make_unique<PillGroup> (*proc.apvts.getParameter (DigProcessor::kLength), "Loop length");
    mode   = std::make_unique<PillGroup> (*proc.apvts.getParameter (DigProcessor::kMode), "Fit to project tempo");
    chops  = std::make_unique<PillGroup> (*proc.apvts.getParameter (DigProcessor::kChops), "Chops");
    for (auto* g : { length.get(), mode.get(), chops.get() }) addAndMakeVisible (*g);
    for (int i = 0; i < 16; ++i)
    {
        auto* b = pads.add (new PadButton()); b->setButtonText (juce::String (i + 1));
        b->onClick = [this, i] { proc.audition (i); };
        b->makeFile = [this, i] { return proc.writeSliceWav (i); };
        addAndMakeVisible (b);
    }
    for (auto* l : { &info1, &info2 }) { l->setFont (FatboardLookAndFeel::body (13.0f)); l->setJustificationType (juce::Justification::centredLeft); l->setMinimumHorizontalScale (0.7f); }
    info1.setColour (juce::Label::textColourId, bone); info2.setColour (juce::Label::textColourId, grey);

    bpmSlider.setSliderStyle (juce::Slider::LinearBar);
    bpmSlider.setRange (40.0, 240.0, 0.01);
    bpmSlider.setTextValueSuffix (" bpm (song)");
    bpmSlider.setColour (juce::Slider::trackColourId, lnf.accent.withAlpha (0.35f));
    bpmSlider.setColour (juce::Slider::textBoxTextColourId, bone);
    bpmSlider.setColour (juce::Slider::textBoxOutlineColourId, line);
    bpmSlider.setVelocityBasedMode (true); bpmSlider.setVelocityModeParameters (0.6, 1, 0.05, false);
    bpmSlider.onValueChange = [this] { if (! settingBpm) proc.setSongBpm (bpmSlider.getValue()); };

    pitchAtt = std::make_unique<SliderAtt> (proc.apvts, DigProcessor::kPitch, pitchKnob.slider);
    nudgeAtt = std::make_unique<SliderAtt> (proc.apvts, DigProcessor::kNudge, nudgeKnob.slider);
    levelAtt = std::make_unique<SliderAtt> (proc.apvts, DigProcessor::kLevel, levelKnob.slider);
    pitchKnob.slider.setDoubleClickReturnValue (true, 0.0); nudgeKnob.slider.setDoubleClickReturnValue (true, 0.0); levelKnob.slider.setDoubleClickReturnValue (true, 0.0);
    gateButton.setClickingTogglesState (true);
    gateAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, DigProcessor::kGate, gateButton);
    gateButton.setTooltip ("On: a chop stops when you let go of the key. Off: it plays to its end.");

    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Pick a song", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.mp3;*.flac;*.aif;*.aiff;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { const auto f = fc.getResult(); if (f.existsAsFile()) proc.loadFile (f); });
    };
    findButton.onClick = [this] { proc.findLoop(); };
    backButton.onClick = [this] { proc.shiftRegion (-1); };
    fwdButton.onClick  = [this] { proc.shiftRegion (1); };
    halfButton.onClick = [this] { proc.setSongBpm (proc.getInfo().songBpm * 0.5); };
    doubleButton.onClick = [this] { proc.setSongBpm (proc.getInfo().songBpm * 2.0); };
    playButton.setClickingTogglesState (true);
    playButton.onClick = [this] { proc.setLooping (playButton.getToggleState()); };
    playButton.setToggleState (proc.isLooping(), juce::dontSendNotification);
    backButton.setTooltip ("One beat earlier"); fwdButton.setTooltip ("One beat later");
    songView.onPick = [this] (double s) { proc.setRegionTime (s); };
    clipView.makeFile = [this] { return proc.writeLoopWav(); };

    setSize (720, 790);
    startTimerHz (20);
    timerCallback();
}

DigEditor::~DigEditor() { setLookAndFeel (nullptr); }

bool DigEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files) if (juce::File (f).hasFileExtension ("wav;mp3;flac;aif;aiff;ogg")) return true;
    return false;
}
void DigEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files) if (juce::File (f).hasFileExtension ("wav;mp3;flac;aif;aiff;ogg")) { proc.loadFile (juce::File (f)); return; }
}

void DigEditor::timerCallback()
{
    const auto i = proc.getInfo();
    const bool ready = i.state == DigProcessor::State::Ready;
    if (proc.getSongCount() != shownSong && ready)
    {
        shownSong = proc.getSongCount();
        if (const auto s = proc.getSong()) songView.setSong (s->peaks, s->an.seconds);
    }
    if (! ready) { songView.setSong ({}, 0.0); shownSong = -1; }
    songView.setMessage (i.state == DigProcessor::State::Loading ? "Reading " + i.name + " and working out tempo and key..."
                       : i.state == DigProcessor::State::Failed ? i.error : juce::String ("Drop a song here (WAV, MP3, FLAC) or press Load song."));
    songView.setRegion (i.startSec, i.lenSec);
    if (proc.getRenderCount() != shownRender && ready) { shownRender = proc.getRenderCount(); const auto c = proc.getClip(); clipView.setClip (c.get()); }
    if (! ready && shownRender != -1) { clipView.setClip (nullptr); shownRender = -1; }
    clipView.setPlaying (proc.getPlayingSlice());

    const auto c = ready ? proc.getClip() : nullptr;
    const int nChops = c != nullptr ? (int) c->slices.size() : 0;
    for (int k = 0; k < pads.size(); ++k) { pads[k]->setEnabled (k < nChops); pads[k]->setToggleState (k == proc.getPlayingSlice(), juce::dontSendNotification); }
    for (auto* b : { &findButton, &backButton, &fwdButton, &playButton, &halfButton, &doubleButton }) b->setEnabled (ready);
    bpmSlider.setEnabled (ready);
    if (ready && ! bpmSlider.isMouseButtonDown() && std::abs (bpmSlider.getValue() - i.songBpm) > 0.004) { settingBpm = true; bpmSlider.setValue (i.songBpm, juce::dontSendNotification); settingBpm = false; }

    if (ready)
    {
        info1.setText (i.name + "   " + clock (i.seconds) + "   " + juce::String (i.detectedBpm, 1) + " bpm   " + i.key, juce::dontSendNotification);
        const double ratio = i.hostBpm / juce::jmax (20.0, i.songBpm), st = 12.0 * std::log2 (ratio);
        juce::String what;
        if (i.mode == Dig::Original) what = "left at the song's " + juce::String (i.songBpm, 1) + " bpm";
        else if (i.mode == Dig::Repitch) what = "repitched to " + juce::String (i.hostBpm, 1) + " bpm: " + (st >= 0 ? "+" : "") + juce::String (st, 1) + " semitones";
        else what = "stretched to " + juce::String (i.hostBpm, 1) + " bpm: " + (ratio >= 1 ? "+" : "") + juce::String ((ratio - 1.0) * 100.0, 0) + " % speed" + (std::abs (ratio - 1.0) > 0.25 ? "  (a lot: try Repitch or /2, x2)" : "");
        info2.setText ("Loop from " + clock (i.startSec) + ", " + juce::String (i.beats / 4) + (i.beats == 4 ? " bar, " : " bars, ") + what, juce::dontSendNotification);
        clipView.setStatus ("Drag this into the playlist.  Chops: keys from C5 in FL (MIDI 60), or click a pad.");
    }
    else { info1.setText ({}, juce::dontSendNotification); info2.setText ({}, juce::dontSendNotification); clipView.setStatus ("The loop at your project tempo appears here."); }

    auto val = [this] (const char* id) { return proc.apvts.getParameter (id)->getCurrentValueAsText(); };
    pitchKnob.setLabel ("Pitch " + val (DigProcessor::kPitch)); nudgeKnob.setLabel ("Nudge " + val (DigProcessor::kNudge)); levelKnob.setLabel ("Level " + val (DigProcessor::kLevel));
    pitchKnob.setAlpha (i.mode == Dig::Repitch ? 0.35f : 1.0f);       // Repitch: the pitch follows the tempo
}

void DigEditor::paint (juce::Graphics& g) { g.fillAll (black); }

void DigEditor::resized()
{
    auto r = getLocalBounds().reduced (28);
    header.setBounds (r.removeFromTop (56));
    footer.setBounds (r.removeFromBottom (28));
    r.removeFromTop (4);
    songView.setBounds (r.removeFromTop (78));
    r.removeFromTop (6);
    info1.setBounds (r.removeFromTop (20)); info2.setBounds (r.removeFromTop (20));
    r.removeFromTop (6);
    auto row = r.removeFromTop (38);
    loadButton.setBounds (row.removeFromLeft (120).reduced (0, 1)); row.removeFromLeft (8);
    findButton.setBounds (row.removeFromLeft (120).reduced (0, 1)); row.removeFromLeft (8);
    backButton.setBounds (row.removeFromLeft (44).reduced (0, 1)); row.removeFromLeft (4);
    fwdButton.setBounds (row.removeFromLeft (44).reduced (0, 1)); row.removeFromLeft (8);
    doubleButton.setBounds (row.removeFromRight (48).reduced (0, 1)); row.removeFromRight (4);
    halfButton.setBounds (row.removeFromRight (48).reduced (0, 1)); row.removeFromRight (8);
    bpmSlider.setBounds (row.reduced (0, 4));
    r.removeFromTop (8);
    clipView.setBounds (r.removeFromTop (136));
    r.removeFromTop (8);
    auto padRow = r.removeFromTop (36);
    const int pw = padRow.getWidth() / 16;
    for (auto* b : pads) b->setBounds (padRow.removeFromLeft (pw).reduced (2, 0));
    r.removeFromTop (8);
    auto pills = r.removeFromTop (62);
    length->setBounds (pills.removeFromLeft (pills.getWidth() / 2 - 6)); pills.removeFromLeft (12);
    mode->setBounds (pills);
    r.removeFromTop (6);
    auto row2 = r.removeFromTop (62);
    chops->setBounds (row2.removeFromLeft (row2.getWidth() / 2 - 6)); row2.removeFromLeft (12);
    auto btns = row2.withTrimmedTop (20);
    playButton.setBounds (btns.removeFromLeft (btns.getWidth() / 2 - 4).reduced (0, 1)); btns.removeFromLeft (8);
    gateButton.setBounds (btns.reduced (0, 1));
    r.removeFromTop (4);
    const int w = r.getWidth() / 3, size = juce::jmin (w, r.getHeight()) - 2;
    for (auto* k : { &pitchKnob, &nudgeKnob, &levelKnob }) k->setBounds (r.removeFromLeft (w).withSizeKeepingCentre (size, size));
}
