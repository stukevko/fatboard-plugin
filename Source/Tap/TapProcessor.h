#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>

/** Tap – private tool: finger drumming on the computer keyboard.
    Q = kick, W = snare, E = hat, R = open hat (the hat chokes the open hat).
    Built-in sounds so it works at once; drop your own WAV on a pad to replace one.
    With Rec on and FL playing, what you tap is written into a loop of 1/2/4 bars that plays back in time.
    Drag the grid out: it leaves as a MIDI file (kick 36, snare 38, hat 42, open hat 46 – the standard drum notes). */
class TapProcessor : public juce::AudioProcessor
{
public:
    static constexpr int numPads = 4, maxEvents = 512, maxVoices = 16;
    struct Event { double beat = 0; int pad = 0; float vel = 1.0f; int pass = 0; };
    struct Sample { juce::AudioBuffer<float> audio; double sr = 48000.0; juce::String name; juce::String path; };

    TapProcessor();
    ~TapProcessor() override = default;
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
    void hit (int pad, float velocity = 1.0f);                 // a key or a click
    juce::String loadSample (int pad, const juce::File&);      // error text, empty when fine
    void resetSample (int pad);
    juce::String getPadName (int pad) const;
    void clearLoop()            { clearReq.store (true); }
    void clearPad (int pad)     { clearPadReq.store (pad); }
    std::vector<Event> getEvents() const;
    int  getEventCount() const  { return eventStamp.load(); }
    float getFlash (int pad)    { return flash[(size_t) pad].exchange (0.0f); }
    double getPlayBeat() const  { return playBeat.load(); }
    double getLoopBeats() const { return (double) barsNow() * 4.0; }
    double getHostBpm() const   { return hostBpm.load(); }
    bool isHostPlaying() const  { return hostPlaying.load(); }
    juce::File writeMidi();

    static constexpr const char* kBars = "bars";
    static constexpr const char* kQuant = "quantize";
    static constexpr const char* kRec = "rec";
    static constexpr const char* kLevel = "level";
    static constexpr int midiNote[numPads] = { 36, 38, 42, 46 };

private:
    int barsNow() const;
    void makeDefault (int pad);
    void trigger (int pad, float vel, int delay);

    double sr = 48000.0;
    std::atomic<double> hostBpm { 120.0 }, playBeat { -1.0 };
    std::atomic<bool> hostPlaying { false };

    // pads: message thread swaps, audio thread picks up
    mutable juce::SpinLock sampleLock;
    std::shared_ptr<const Sample> samples[numPads], audioSamples[numPads];
    std::vector<std::shared_ptr<const Sample>> retired;
    std::atomic<bool> samplesChanged { false };

    // hits from the UI
    struct Hit { int pad; float vel; double timeMs; };
    juce::AbstractFifo hitFifo { 64 }; std::array<Hit, 64> hitData {};

    // the loop (audio thread writes; the UI copies under the lock)
    mutable juce::SpinLock eventLock;
    std::array<Event, maxEvents> events {}; int numEvents = 0;
    std::atomic<int> eventStamp { 0 }, clearPadReq { -1 };
    std::atomic<bool> clearReq { false };
    int pass = 0; double lastStart = -1.0;
    std::vector<Event> restored; std::atomic<bool> restoreReq { false };

    struct Voice { bool on = false; int pad = 0; double pos = 0, step = 1; float gain = 1; int delay = 0, fade = -1; };
    Voice voices[maxVoices];
    std::array<std::atomic<float>, numPads> flash {};
    int fileCounter = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapProcessor)
};
