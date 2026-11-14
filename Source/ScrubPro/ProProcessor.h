#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "LicenseClient.h"
#include "CleanupEngine.h"
#include "VoiceCurve.h"
#include "AirEngine.h"

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
    LicenseClient license { "scrub-pro", JucePlugin_VersionString };   // plugin <-> fatboard.com account
    const CleanupEngine& getEngine() const { return engine; }
    const AirEngine& getAir() const { return air; }

    static constexpr const char* kCleanup = "cleanup";
    static constexpr const char* kAir     = "air";
    static constexpr const char* kLowCut  = "lowCut";
    static constexpr const char* kMud     = "mud";
    static constexpr const char* kHarsh   = "harsh";
    static constexpr const char* kTop     = "top";

    FatboardCurve::Shape getShape() const
    {
        FatboardCurve::Shape s;
        s.lowCutHz = lowCutParam->load(); s.mud = mudParam->load(); s.harsh = harshParam->load(); s.topHz = topParam->load();
        return s;
    }
    static constexpr const char* kActive  = "active";

private:
    LicenseGate licenseGate;
    CleanupEngine engine;
    VoiceCurve curve;
    AirEngine air;
    std::atomic<float>* cleanupParam = nullptr;
    std::atomic<float>* airParam = nullptr;
    std::atomic<float>* lowCutParam = nullptr;
    std::atomic<float>* mudParam = nullptr;
    std::atomic<float>* harshParam = nullptr;
    std::atomic<float>* topParam = nullptr;
    std::atomic<float>* activeParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProProcessor)
};
