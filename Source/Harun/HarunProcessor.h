#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "HarunEngine.h"

class HarunProcessor : public juce::AudioProcessor
{
public:
    HarunProcessor();
    ~HarunProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 0.2; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    const HarunEngine& getEngine() const { return engine; }

    static constexpr const char* kKey    = "key";
    static constexpr const char* kScale  = "scale";
    static constexpr const char* kVoice  = "voice";
    static constexpr const char* kMode   = "mode";
    static constexpr const char* kBlend  = "blend";
    static constexpr const char* kHuman  = "human";
    static constexpr const char* kActive = "active";

private:
    HarunEngine engine;
    std::atomic<float>* pKey = nullptr; std::atomic<float>* pScale = nullptr; std::atomic<float>* pVoice = nullptr;
    std::atomic<float>* pMode = nullptr; std::atomic<float>* pBlend = nullptr; std::atomic<float>* pHuman = nullptr;
    std::atomic<float>* pActive = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarunProcessor)
};
