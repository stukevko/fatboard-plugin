#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "ThumpEngine.h"

class ThumpProcessor : public juce::AudioProcessor
{
public:
    ThumpProcessor();
    ~ThumpProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    const ThumpEngine& getEngine() const { return engine; }

    /** Loads an 808 sample (wav / aiff / flac …), detects its root and switches the tone to "Sample".
        Message thread. Returns an empty string on success, else what went wrong. */
    juce::String loadSample (const juce::File& file, bool selectIt);
    std::shared_ptr<const ThumpSample> getSample() const { const juce::SpinLock::ScopedLockType l (sampleLock); return sample; }

    static constexpr const char* kTone   = "tone";
    static constexpr const char* kSub    = "sub";
    static constexpr const char* kKick   = "kick";
    static constexpr const char* kDecay  = "decay";
    static constexpr const char* kClip   = "clip";

private:
    ThumpEngine engine;
    juce::AudioFormatManager formats;
    mutable juce::SpinLock sampleLock;
    std::shared_ptr<const ThumpSample> sample;          // what the editor / state sees
    std::shared_ptr<const ThumpSample> audioSample;     // what the audio thread plays
    std::vector<std::shared_ptr<const ThumpSample>> retired;   // keeps old samples alive so audio never frees memory
    std::atomic<bool> sampleChanged { false };
    juce::String samplePath;
    std::vector<float> mono;
    std::atomic<float>* pTone = nullptr; std::atomic<float>* pSub = nullptr; std::atomic<float>* pKick = nullptr;
    std::atomic<float>* pDecay = nullptr; std::atomic<float>* pClip = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThumpProcessor)
};
