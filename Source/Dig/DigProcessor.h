#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "DigAnalysis.h"
#include "DigRender.h"

/** Dig – private sampling tool (not a product, no licence check).
    Load a song → tempo, key and beat grid are read → pick a loop on the grid → it is fitted to the project
    tempo (Repitch or Stretch) → drag it out as a WAV, or play its chops from the keyboard (C3 upwards, or pads from C1). */
class DigProcessor : public juce::AudioProcessor, private juce::Timer, private juce::Thread
{
public:
    enum class State { Empty, Loading, Ready, Failed };
    struct Song
    {
        juce::AudioBuffer<float> audio; double sr = 44100.0;
        Dig::Analysis an; juce::File file; std::vector<float> peaks;
    };
    struct Info
    {
        State state = State::Empty; juce::String name, key, error;
        double seconds = 0, detectedBpm = 0, songBpm = 0, hostBpm = 120, startSec = 0, lenSec = 0, firstBeat = 0;
        int beats = 8, mode = 0, chops = 0;
    };

    DigProcessor();
    ~DigProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override { return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- UI (message thread) ----
    void loadFile (const juce::File&, bool keepSettings = false);
    Info getInfo() const;
    std::shared_ptr<const Song> getSong() const { const juce::SpinLock::ScopedLockType l (songLock); return song; }
    std::shared_ptr<const Dig::Clip> getClip() const { const juce::SpinLock::ScopedLockType l (clipLock); return clip; }
    int  getRenderCount() const { return renderCount.load(); }
    int  getSongCount() const   { return songCount.load(); }
    void setSongBpm (double bpm);
    void setRegionTime (double seconds);          // snaps to the grid
    void shiftRegion (int beatsDelta);
    void findLoop();                              // next best spot for the current length
    void audition (int slice)  { auditionReq.store (slice); }
    void setLooping (bool on)  { loopReq.store (on ? 1 : 0); }
    bool isLooping() const     { return looping.load(); }
    int  getPlayingSlice() const { return playingSlice.load(); }
    juce::File writeLoopWav();
    juce::File writeSliceWav (int slice);

    static constexpr const char* kLength = "length";
    static constexpr const char* kMode   = "mode";
    static constexpr const char* kChops  = "chops";
    static constexpr const char* kPitch  = "pitch";
    static constexpr const char* kNudge  = "nudge";
    static constexpr const char* kLevel  = "level";
    static constexpr const char* kGate   = "gate";

    static constexpr int maxVoices = 8;
    static constexpr double maxSongSeconds = 900.0;

private:
    void timerCallback() override;
    void run() override;
    int beatsNow() const;
    double startSecNow() const;
    juce::File writeWav (const juce::AudioBuffer<float>&, int from, int to, const juce::String& name);

    double sr = 48000.0;
    std::atomic<double> hostBpm { 120.0 };

    // song (loader thread publishes, message thread reads)
    mutable juce::SpinLock songLock;
    std::shared_ptr<const Song> song;
    std::atomic<int> songCount { 0 };
    std::atomic<State> state { State::Empty };
    juce::CriticalSection pendingLock;
    juce::File pendingFile; bool pendingKeep = false, hasPending = false;
    juce::String loadingName, errorText;

    // what the user set (message thread)
    double songBpm = 0.0, firstBeat = 0.0; int regionBeat = 0;
    bool restoreSettings = false; double restoreBpm = 0.0, restoreFirst = 0.0; int restoreBeat = 0;
    std::vector<int> loopCandidates; int loopIndex = -1; juce::String loopKey;

    // clip: rendered on the message thread, played by the audio thread
    mutable juce::SpinLock clipLock;
    std::shared_ptr<const Dig::Clip> clip, audioClip;
    std::vector<std::shared_ptr<const Dig::Clip>> retired;
    std::atomic<bool> clipChanged { false };
    std::atomic<int> renderCount { 0 }, auditionReq { -1 }, loopReq { -1 }, playingSlice { -1 };
    std::atomic<bool> looping { false };
    juce::String lastKey; int wavCounter = 0;

    struct Voice { bool on = false; int pos = 0, end = 0, delay = 0, note = -1, release = -1, slice = 0; float gain = 1.0f; };
    Voice voices[maxVoices];
    int loopPos = 0;
    void startVoice (int slice, float gain, int delay, int note);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DigProcessor)
};
