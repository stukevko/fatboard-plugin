#include "HarunProcessor.h"
#include "HarunEditor.h"

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { HarunProcessor::kMode, 1 }, "Mode",
        juce::StringArray { "Double", "Harmony" }, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { HarunProcessor::kAmount, 1 }, "Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f, pct));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { HarunProcessor::kWidth, 1 }, "Width",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.8f, pct));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { HarunProcessor::kTuck, 1 }, "Tuck",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, pct));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { HarunProcessor::kActive, 1 }, "In", true));
    return layout;
}

HarunProcessor::HarunProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),   // the lead vocal, optional
      apvts (*this, nullptr, "Harun", makeLayout())
{
    pMode = apvts.getRawParameterValue (kMode);     pAmount = apvts.getRawParameterValue (kAmount);
    pWidth = apvts.getRawParameterValue (kWidth);   pTuck = apvts.getRawParameterValue (kTuck);
    pActive = apvts.getRawParameterValue (kActive);
}

bool HarunProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (layouts.getMainInputChannelSet() != out) return false;
    const auto& sc = layouts.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
}

void HarunProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, getMainBusNumOutputChannels());
}

void HarunProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, true, 0);
    const bool scOn = getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled() && getBusBuffer (buffer, true, 1).getNumChannels() > 0;
    juce::AudioBuffer<float> sc;
    if (scOn) sc = getBusBuffer (buffer, true, 1);

    engine.setParams ((HarunEngine::Mode) (int) pMode->load(),
                      pActive->load() < 0.5f ? 0.0f : pAmount->load(), pWidth->load(), pTuck->load(), scOn);
    engine.process (main, scOn ? &sc : nullptr);
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
