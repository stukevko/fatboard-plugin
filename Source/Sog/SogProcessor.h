#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "LicenseClient.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "SogRenderer.h"

/** Passes the track through untouched. On Capture it waits for the next sound, grabs it,
    renders the reverse swell and can play it back (Preview) or hand it over as a WAV. */
class SogProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    enum class State { Empty, Armed, Recording, Ready };

    SogProcessor();
    ~SogProcessor() override { stopTimer(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
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
    LicenseClient license { "sog", JucePlugin_VersionString };   // plugin <-> fat-board.com account

    // ---- UI (message thread) ----
    void arm()                 { state.store (State::Armed); }
    void cancel()              { if (state.load() == State::Armed || state.load() == State::Recording) state.store (hasCapture ? State::Ready : State::Empty); }
    void preview()             { previewPos.store (0); }
    State getState() const     { return state.load(); }
    double getBpm() const      { return bpm.load(); }
    float getInputLevel() const { return inLevel.load(); }
    int  getRenderCount() const { return renderCount.load(); }
    /** A copy of the current swell (empty if nothing captured yet). */
    juce::AudioBuffer<float> getSwell() const;
    /** Writes the swell as a 24-bit WAV into the temp folder and returns it (invalid file if nothing to write). */
    juce::File writeWav();

    static constexpr const char* kLength  = "length";
    static constexpr const char* kSize    = "size";
    static constexpr const char* kTone    = "tone";
    static constexpr const char* kWidth   = "width";
    static constexpr const char* kTremolo = "tremolo";
    static constexpr const char* kTremDiv = "tremDiv";
    static constexpr const char* kLowCut  = "lowCut";
    static constexpr const char* kLevel   = "level";

    // TUNE
    static constexpr float startThreshold = 0.02f;      // -34 dB: louder than this = "the sound started"
    static constexpr float preRollMs = 8.0f;            // keep a few ms before the trigger so the very start is in
    static constexpr float maxCaptureSec = 2.5f;

private:
    void timerCallback() override;
    SogParams currentParams() const;

    double sr = 48000.0;
    std::atomic<State> state { State::Empty };
    std::atomic<double> bpm { 120.0 };
    std::atomic<float> inLevel { 0.0f };

    // capture (audio thread writes while Armed/Recording; message thread reads once it is Ready)
    juce::AudioBuffer<float> pre, capture;
    int prePos = 0, capLen = 0, quietRun = 0;
    std::atomic<int> capturedSamples { 0 };
    std::atomic<bool> captureFresh { false };
    bool hasCapture = false;

    // swell: rendered on the message thread, played by the audio thread
    mutable juce::SpinLock swellLock;
    std::shared_ptr<const juce::AudioBuffer<float>> swell, audioSwell;
    std::vector<std::shared_ptr<const juce::AudioBuffer<float>>> retired;
    std::atomic<bool> swellChanged { false };
    std::atomic<int> previewPos { -1 }, renderCount { 0 };
    int playPos = -1;
    juce::String lastKey;
    int wavCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SogProcessor)
};
