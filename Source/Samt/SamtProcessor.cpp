#include "SamtProcessor.h"
#include "SamtEditor.h"

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });
    auto db  = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB"; });
    using Float = juce::AudioParameterFloat;
    layout.add (std::make_unique<Float> (juce::ParameterID { SamtProcessor::kReduce, 1 }, "Peak Reduction", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f, pct));
    layout.add (std::make_unique<Float> (juce::ParameterID { SamtProcessor::kGain, 1 }, "Gain", juce::NormalisableRange<float> (-6.0f, 24.0f, 0.1f), 0.0f, db));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { SamtProcessor::kMode, 1 }, "Mode", juce::StringArray { "Compress", "Limit" }, 0));
    layout.add (std::make_unique<Float> (juce::ParameterID { SamtProcessor::kWarmth, 1 }, "Warmth", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.2f, pct));
    layout.add (std::make_unique<Float> (juce::ParameterID { SamtProcessor::kMix, 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f, pct));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { SamtProcessor::kActive, 1 }, "In", true));
    return layout;
}

SamtProcessor::SamtProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Samt", makeLayout())
{
    pReduce = apvts.getRawParameterValue (kReduce); pGain = apvts.getRawParameterValue (kGain); pMode = apvts.getRawParameterValue (kMode);
    pWarmth = apvts.getRawParameterValue (kWarmth); pMix = apvts.getRawParameterValue (kMix); pActive = apvts.getRawParameterValue (kActive);
}

bool SamtProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void SamtProcessor::prepareToPlay (double sampleRate, int samplesPerBlock) { engine.prepare (sampleRate, samplesPerBlock, getMainBusNumOutputChannels()); licenseGate.prepare (sampleRate); }

void SamtProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (pActive->load() < 0.5f) return;
    engine.setParams (pReduce->load(), pGain->load(), pMode->load() > 0.5f ? SamtEngine::Limit : SamtEngine::Compress, pWarmth->load(), pMix->load());
    engine.process (buffer);
    licenseGate.process (buffer, license.isLicensed());
}

juce::AudioProcessorEditor* SamtProcessor::createEditor() { return new SamtEditor (*this); }
void SamtProcessor::getStateInformation (juce::MemoryBlock& d) { if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d); }
void SamtProcessor::setStateInformation (const void* data, int size) { if (auto xml = getXmlFromBinary (data, size)) if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml)); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SamtProcessor(); }
