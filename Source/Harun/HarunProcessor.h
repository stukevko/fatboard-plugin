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
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    const HarunEngine& getEngine() const { return engine; }

    /** Writes the template's own Width / Level / Space / Space type into the knobs. */
    void loadTemplate (int index);

    static constexpr const char* kTemplate  = "template";
    static constexpr const char* kWidth     = "width";
    static constexpr const char* kLevel     = "level";
    static constexpr const char* kSpace     = "space";
    static constexpr const char* kSpaceType = "spaceType";
    static constexpr const char* kActive    = "active";

private:
    HarunEngine engine;
    std::atomic<float>* pTemplate = nullptr; std::atomic<float>* pWidth = nullptr; std::atomic<float>* pLevel = nullptr;
    std::atomic<float>* pSpace = nullptr; std::atomic<float>* pSpaceType = nullptr; std::atomic<float>* pActive = nullptr;
    bool wasActive = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarunProcessor)
};
