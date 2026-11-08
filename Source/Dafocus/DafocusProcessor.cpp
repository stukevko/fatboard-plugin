#include "DafocusProcessor.h"
#include "DafocusEditor.h"

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { DafocusProcessor::kSpace, 1 }, "Space",
        juce::StringArray { "Tight", "Room", "Far" }, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { DafocusProcessor::kEcho, 1 }, "Echo",
        juce::StringArray { "Off", "1/8", "1/4", "1/8 dot" }, 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { DafocusProcessor::kAmount, 1 }, "Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.4f, pct));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { DafocusProcessor::kDuck, 1 }, "Duck",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.6f, pct));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { DafocusProcessor::kActive, 1 }, "In", true));
    return layout;
}

DafocusProcessor::DafocusProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Dafocus", makeLayout())
{
    pSpace = apvts.getRawParameterValue (kSpace);   pEcho = apvts.getRawParameterValue (kEcho);
    pAmount = apvts.getRawParameterValue (kAmount); pDuck = apvts.getRawParameterValue (kDuck);
    pActive = apvts.getRawParameterValue (kActive);
}

bool DafocusProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void DafocusProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
}

void DafocusProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm()) lastBpm = *bpm;

    engine.setParams ((DafocusEngine::Space) (int) pSpace->load(), (DafocusEngine::Echo) (int) pEcho->load(),
                      pActive->load() < 0.5f ? 0.0f : pAmount->load(), pDuck->load(), lastBpm);
    engine.process (buffer);
}

juce::AudioProcessorEditor* DafocusProcessor::createEditor() { return new DafocusEditor (*this); }

void DafocusProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void DafocusProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DafocusProcessor(); }
