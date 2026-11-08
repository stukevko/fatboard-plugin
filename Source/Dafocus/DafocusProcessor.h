#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DafocusEngine.h"

class DafocusProcessor : public juce::AudioProcessor
{
public:
    DafocusProcessor();
    ~DafocusProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    const DafocusEngine& getEngine() const { return engine; }

    static constexpr const char* kSpace  = "space";
    static constexpr const char* kEcho   = "echo";
    static constexpr const char* kAmount = "amount";
    static constexpr const char* kDuck   = "duck";
    static constexpr const char* kActive = "active";

private:
    DafocusEngine engine;
    std::atomic<float>* pSpace = nullptr; std::atomic<float>* pEcho = nullptr;
    std::atomic<float>* pAmount = nullptr; std::atomic<float>* pDuck = nullptr; std::atomic<float>* pActive = nullptr;
    double lastBpm = 120.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DafocusProcessor)
};
