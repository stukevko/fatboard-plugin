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
    double getTailLengthSeconds() const override { return 0.05; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    const HarunEngine& getEngine() const { return engine; }

    static constexpr const char* kMode   = "mode";
    static constexpr const char* kAmount = "amount";
    static constexpr const char* kWidth  = "width";
    static constexpr const char* kTuck   = "tuck";
    static constexpr const char* kActive = "active";

private:
    HarunEngine engine;
    std::atomic<float>* pMode = nullptr; std::atomic<float>* pAmount = nullptr; std::atomic<float>* pWidth = nullptr;
    std::atomic<float>* pTuck = nullptr; std::atomic<float>* pActive = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarunProcessor)
};
