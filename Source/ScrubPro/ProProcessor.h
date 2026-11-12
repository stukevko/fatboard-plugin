#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "CleanupEngine.h"
#include "VoiceCurve.h"
#include "AirEngine.h"
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
    const AirEngine& getAir() const { return air; }
    const RoomCarver& getRoom() const { return room; }

    static constexpr const char* kCleanup = "cleanup";
    static constexpr const char* kAir     = "air";
    static constexpr const char* kRoom    = "room";
    static constexpr const char* kActive  = "active";

private:
    CleanupEngine engine;
    VoiceCurve curve;
    AirEngine air;
    RoomCarver room;
    std::atomic<float>* cleanupParam = nullptr;
    std::atomic<float>* airParam = nullptr;
    std::atomic<float>* roomParam = nullptr;
    std::atomic<float>* activeParam = nullptr;
    bool wasRoom = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProProcessor)
};
