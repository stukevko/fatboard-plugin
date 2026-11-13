#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "BrettEngine.h"

class BrettProcessor : public juce::AudioProcessor
{
public:
    BrettProcessor();
    ~BrettProcessor() override = default;

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
    const BrettEngine& getEngine() const { return engine; }
    /** Forget the loudest part heard so far (e.g. after big mix changes). */
    void relearn() { relearnFlag.store (true); }

    static constexpr const char* kLoud   = "loud";
    static constexpr const char* kStyle  = "style";
    static constexpr const char* kCheck  = "check";
    static constexpr const char* kActive = "active";

private:
    BrettEngine engine;
    std::atomic<float>* pLoud = nullptr; std::atomic<float>* pStyle = nullptr; std::atomic<float>* pCheck = nullptr; std::atomic<float>* pActive = nullptr;
    std::atomic<bool> relearnFlag { false };
    juce::AudioBuffer<float> bypassDelay; int bypassPos = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrettProcessor)
};
