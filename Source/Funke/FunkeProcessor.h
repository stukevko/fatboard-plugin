#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "FunkeGen.h"

/** Funke – ideas as MIDI. Pick a vibe and a key, roll, listen, drag chords / melody / bass into the piano roll.
    Drop a drum loop (WAV at the project tempo, starting on the 1) and the notes can follow the kick or stay out of its way.
    An instrument: it plays the idea with a small built-in sound so you can judge it before you drag it out. */
class FunkeProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    enum Layer { Chords = 0, Melody, Bass, All };

    FunkeProcessor();
    ~FunkeProcessor() override { stopTimer(); }
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
    void roll (Layer which);                                   // new dice for one layer or all
    std::shared_ptr<const Funke::Pattern> getPattern() const { const juce::SpinLock::ScopedLockType l (lock); return pattern; }
    int  getPatternCount() const { return patternCount.load(); }
    double getPlayBeat() const   { return playBeat.load(); }      // -1 when silent
    double getHostBpm() const    { return hostBpm.load(); }
    juce::String loadDrums (const juce::File&);                   // returns an error text, empty when fine
    void clearDrums();
    Funke::DrumGrid getDrums() const { return drums; }
    juce::String getDrumName() const { return drumName; }
    juce::File writeMidi (Layer which);

    static constexpr const char* kVibe = "vibe";
    static constexpr const char* kKey = "key";
    static constexpr const char* kBars = "bars";
    static constexpr const char* kMotion = "motion";
    static constexpr const char* kDensity = "density";
    static constexpr const char* kDrumMode = "drumMode";
    static constexpr const char* kListen = "listen";
    static constexpr const char* kLevel = "level";

private:
    void timerCallback() override;
    Funke::Settings settingsNow() const;

    double sr = 48000.0;
    std::atomic<double> hostBpm { 120.0 }, playBeat { -1.0 };
    uint32_t seeds[3] { 11, 22, 33 };
    Funke::DrumGrid drums; juce::String drumName;

    mutable juce::SpinLock lock;
    std::shared_ptr<const Funke::Pattern> pattern, audioPattern;
    std::vector<std::shared_ptr<const Funke::Pattern>> retired;
    std::atomic<bool> changed { false };
    std::atomic<int> patternCount { 0 };
    juce::String lastKey; int fileCounter = 0;

    // the little built-in sound
    struct Voice { bool on = false; int kind = 0; double phase = 0, inc = 0; float amp = 0, env = 0; int age = 0, hold = 0; };
    Voice voices[32];
    double freePos = 0.0, lastBeat = -1.0;
    void trigger (int kind, const Funke::Note& n, int delaySamples, double samplesPerBeat);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FunkeProcessor)
};
