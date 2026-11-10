#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "CleanupEngine.h"
#include "VocalModes.h"
#include "RoomCarver.h"

class ProProcessor : public juce::AudioProcessor
{
public:
    ProProcessor();
    ~ProProcessor() override = default;

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
    const CleanupEngine& getEngine() const { return engine; }
    const VocalChain& getChain() const { return chain; }
    const RoomCarver& getRoom() const { return room; }
    static constexpr int roomModeIndex = 3;

    static constexpr const char* kCleanup = "cleanup";
    static constexpr const char* kMode    = "mode";
    static constexpr const char* kAmount  = "modeAmount";
    static constexpr const char* kActive  = "active";

private:
    CleanupEngine engine;
    VocalChain chain;
    RoomCarver room;
    std::atomic<float>* cleanupParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* activeParam = nullptr;
    float lastAmount = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProProcessor)
};
