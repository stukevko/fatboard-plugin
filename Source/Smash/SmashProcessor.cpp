#include "SmashProcessor.h"
#include "SmashEditor.h"

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { SmashProcessor::kStyle, 1 }, "Style", juce::StringArray { "Tight", "Punch", "Dirty" }, 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { SmashProcessor::kSmash, 1 }, "Smash", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, pct));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { SmashProcessor::kFit, 1 }, "Fit", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, pct));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { SmashProcessor::kActive, 1 }, "In", true));
    return layout;
}

SmashProcessor::SmashProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),   // the sample / beat
      apvts (*this, nullptr, "Smash", makeLayout())
{
    pStyle = apvts.getRawParameterValue (kStyle); pSmash = apvts.getRawParameterValue (kSmash);
    pFit = apvts.getRawParameterValue (kFit); pActive = apvts.getRawParameterValue (kActive);
}

bool SmashProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (layouts.getMainInputChannelSet() != out) return false;
    const auto& sc = layouts.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
}

void SmashProcessor::prepareToPlay (double sampleRate, int samplesPerBlock) { engine.prepare (sampleRate, samplesPerBlock, getMainBusNumOutputChannels()); }

void SmashProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, true, 0);
    const bool scOn = getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled() && getBusBuffer (buffer, true, 1).getNumChannels() > 0;
    juce::AudioBuffer<float> sc; if (scOn) sc = getBusBuffer (buffer, true, 1);
    if (pActive->load() < 0.5f) return;
    engine.setParams ((SmashEngine::Style) (int) pStyle->load(), pSmash->load(), pFit->load(), scOn);
    engine.process (main, scOn ? &sc : nullptr);
}

juce::AudioProcessorEditor* SmashProcessor::createEditor() { return new SmashEditor (*this); }
void SmashProcessor::getStateInformation (juce::MemoryBlock& d) { if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d); }
void SmashProcessor::setStateInformation (const void* data, int size) { if (auto xml = getXmlFromBinary (data, size)) if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml)); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SmashProcessor(); }
