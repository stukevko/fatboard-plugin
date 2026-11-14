#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "LicenseClient.h"
#include "SmashEngine.h"

class SmashProcessor : public juce::AudioProcessor
{
public:
    SmashProcessor();
    ~SmashProcessor() override = default;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
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
    LicenseClient license { "smash", JucePlugin_VersionString };   // plugin <-> fat-board.com account
    const SmashEngine& getEngine() const { return engine; }
    static constexpr const char* kStyle = "style";
    static constexpr const char* kSmash = "smash";
    static constexpr const char* kFit = "fit";
    static constexpr const char* kActive = "active";
private:
    LicenseGate licenseGate;
    SmashEngine engine;
    std::atomic<float>* pStyle = nullptr; std::atomic<float>* pSmash = nullptr; std::atomic<float>* pFit = nullptr; std::atomic<float>* pActive = nullptr;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmashProcessor)
};
