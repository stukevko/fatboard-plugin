#include "ProProcessor.h"
#include "ProEditor.h"

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ProProcessor::kCleanup, 1 }, "Cleanup",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, pct));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ProProcessor::kMode, 1 }, "Mode",
        juce::StringArray { "Crisp", "Clear", "Soft" }, 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ProProcessor::kAmount, 1 }, "Mode Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, pct));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ProProcessor::kActive, 1 }, "In", true));
    return layout;
}

ProProcessor::ProProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ScrubPro", makeLayout())
{
    cleanupParam = apvts.getRawParameterValue (kCleanup);
    modeParam    = apvts.getRawParameterValue (kMode);
    amountParam  = apvts.getRawParameterValue (kAmount);
    activeParam  = apvts.getRawParameterValue (kActive);
}

bool ProProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void ProProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int ch = getTotalNumOutputChannels();
    engine.prepare (sampleRate, samplesPerBlock, ch);
    chain.prepare (sampleRate, samplesPerBlock, ch);
    engine.setAmount (cleanupParam->load());
    chain.setMode ((int) modeParam->load(), true);
    chain.setAmount (amountParam->load());
}

void ProProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (activeParam->load() < 0.5f) return;

    engine.setAmount (cleanupParam->load());
    engine.process (buffer);

    chain.setMode ((int) modeParam->load());
    const float a = amountParam->load();
    if (std::abs (a - lastAmount) > 0.0005f) { chain.setAmount (a); lastAmount = a; }
    chain.process (buffer);
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
