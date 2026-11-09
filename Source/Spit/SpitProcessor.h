#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SpitEngine.h"

class SpitProcessor : public juce::AudioProcessor
{
public:
    SpitProcessor();
    ~SpitProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return true; }
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
    SpitEngine& getEngine() { return engine; }

    // ---- take recording (UI thread reads, audio thread writes) -------------
    struct RecordedHit { SpitEngine::Drum drum; float velocity; double beat; };
    void startRecording();
    void stopRecording();
    bool isRecording() const { return recording.load(); }
    std::vector<RecordedHit> getRecordedHits() const;       // copy, lock-free-ish (mutex, short)
    void clearRecording();
    double getBpm() const { return bpm.load(); }
    /** Builds a Standard MIDI File of the take, quantised with the current Tight. */
    juce::MemoryBlock buildMidiFile() const;

    // ---- UI pulses -----------------------------------------------------------
    std::atomic<int> lastHitDrum { 3 };
    std::atomic<float> lastHitVelocity { 0.0f };
    std::atomic<int> hitCounter { 0 };

    static constexpr const char* kSens   = "sens";
    static constexpr const char* kTight  = "tight";
    static constexpr const char* kGrid   = "grid";
    static constexpr const char* kActive = "active";
    static constexpr int noteFor[3] = { 36, 38, 42 };      // C1 kick, D1 snare, F#1 closed hat (GM)

private:
    SpitEngine engine;
    juce::AudioBuffer<float> mono;
    std::vector<SpitEngine::Hit> hits;
    std::atomic<float>* pSens = nullptr; std::atomic<float>* pTight = nullptr; std::atomic<float>* pGrid = nullptr; std::atomic<float>* pActive = nullptr;

    std::atomic<bool> recording { false };
    std::atomic<double> bpm { 120.0 };
    double playheadBeat = 0.0; bool playing = false;
    mutable juce::CriticalSection recLock;
    std::vector<RecordedHit> rec;
    int pendingNoteOff[3] = { -1, -1, -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpitProcessor)
};
