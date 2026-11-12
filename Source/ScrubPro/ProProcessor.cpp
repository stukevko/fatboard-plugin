#include "ProProcessor.h"
#include "ProEditor.h"

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ProProcessor::kCleanup, 1 }, "Scrub",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, pct));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ProProcessor::kAir, 1 }, "Air",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f, pct));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ProProcessor::kRoom, 1 }, "Room", false));

    // fine tuning: the four parts of the voice curve. Defaults = the preset.
    auto hzAttr = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " k" : juce::String (juce::roundToInt (v)) + " Hz"; });
    juce::NormalisableRange<float> lowRange (20.0f, 250.0f, 1.0f);  lowRange.setSkewForCentre (FatboardCurve::hpHz);
    juce::NormalisableRange<float> topRange (5000.0f, 20000.0f, 10.0f); topRange.setSkewForCentre (FatboardCurve::lpHz);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ProProcessor::kLowCut, 1 }, "Low Cut", lowRange, FatboardCurve::hpHz, hzAttr));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ProProcessor::kMud, 1 }, "Mud",
        juce::NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f, pct));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ProProcessor::kHarsh, 1 }, "Harsh",
        juce::NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f, pct));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ProProcessor::kTop, 1 }, "Top", topRange, FatboardCurve::lpHz, hzAttr));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ProProcessor::kActive, 1 }, "In", true));
    return layout;
}

ProProcessor::ProProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),   // Room mode: the vocal
      apvts (*this, nullptr, "ScrubPro", makeLayout())
{
    cleanupParam = apvts.getRawParameterValue (kCleanup);
    airParam     = apvts.getRawParameterValue (kAir);
    roomParam    = apvts.getRawParameterValue (kRoom);
    lowCutParam  = apvts.getRawParameterValue (kLowCut);
    mudParam     = apvts.getRawParameterValue (kMud);
    harshParam   = apvts.getRawParameterValue (kHarsh);
    topParam     = apvts.getRawParameterValue (kTop);
    activeParam  = apvts.getRawParameterValue (kActive);
}

bool ProProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (layouts.getMainInputChannelSet() != out) return false;
    const auto& sc = layouts.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
}

void ProProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int ch = getMainBusNumOutputChannels();
    engine.prepare (sampleRate, samplesPerBlock, ch);
    curve.prepare (sampleRate, ch);
    air.prepare (sampleRate, ch);
    room.prepare (sampleRate, ch);
    engine.setAmount (cleanupParam->load());
}

void ProProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, true, 0);
    const bool scOn = getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled() && getBusBuffer (buffer, true, 1).getNumChannels() > 0;
    juce::AudioBuffer<float> sc; if (scOn) sc = getBusBuffer (buffer, true, 1);

    if (activeParam->load() < 0.5f) return;

    const float knob = cleanupParam->load();
    if (roomParam->load() > 0.5f)
    {
        // ROOM: the plugin sits on the BEAT. No voice curve, no cleanup, no air: the beat only
        // steps aside for the vocal on the sidechain. The big knob is how much room it makes.
        room.process (main, scOn ? &sc : nullptr, knob);
        wasRoom = true;
        return;
    }
    if (wasRoom) { curve.reset(); air.reset(); engine.reset(); wasRoom = false; }

    // VOICE: curve (high-pass + cuts) → automatic cleanup → air → curve low-pass
    curve.setShape (getShape());
    curve.setAmount (FatboardCurve::fromKnob (knob), main.getNumSamples());
    curve.processPre (main);
    engine.setAmount (knob);
    engine.process (main);
    air.process (main, airParam->load());
    curve.processLowPass (main);
}

juce::AudioProcessorEditor* ProProcessor::createEditor() { return new ProEditor (*this); }

void ProProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void ProProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ProProcessor(); }
