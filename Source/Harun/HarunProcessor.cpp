#include "HarunProcessor.h"
#include "HarunEditor.h"

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { HarunProcessor::kKey, 1 }, "Key",
        juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { HarunProcessor::kScale, 1 }, "Scale",
        juce::StringArray { "Major", "Minor" }, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { HarunProcessor::kVoice, 1 }, "Voice",
        juce::StringArray { "Low", "High", "Both" }, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { HarunProcessor::kMode, 1 }, "Mode",
        juce::StringArray { "Harmony", "Double" }, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { HarunProcessor::kBlend, 1 }, "Blend",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, pct));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { HarunProcessor::kHuman, 1 }, "Human",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, pct));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { HarunProcessor::kActive, 1 }, "In", true));
    return layout;
}

HarunProcessor::HarunProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Harun", makeLayout())
{
    pKey = apvts.getRawParameterValue (kKey);     pScale = apvts.getRawParameterValue (kScale);
    pVoice = apvts.getRawParameterValue (kVoice); pMode = apvts.getRawParameterValue (kMode);
    pBlend = apvts.getRawParameterValue (kBlend); pHuman = apvts.getRawParameterValue (kHuman);
    pActive = apvts.getRawParameterValue (kActive);
}

bool HarunProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void HarunProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    setLatencySamples (engine.getLatencySamples());
}

void HarunProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    engine.setParams ((int) pKey->load(), (Harmony::Scale) (int) pScale->load(), (HarunEngine::Voice) (int) pVoice->load(),
                      (HarunEngine::Mode) (int) pMode->load(),
                      pActive->load() < 0.5f ? 0.0f : pBlend->load(), pHuman->load());
    // always run so the latency-compensated lead stays aligned when bypassed
    engine.process (buffer);
}

juce::AudioProcessorEditor* HarunProcessor::createEditor() { return new HarunEditor (*this); }

void HarunProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void HarunProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HarunProcessor(); }
