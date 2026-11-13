#include "KorrentProcessor.h"
#include "KorrentEditor.h"

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { KorrentProcessor::kLoud, 1 }, "Loud",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.7f, pct));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { KorrentProcessor::kStyle, 1 }, "Style",
        juce::StringArray { "Clean", "Warm", "Hard" }, 1));
    // CHECK is a listening aid, not part of the sound: not automatable, and never active while rendering
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { KorrentProcessor::kCheck, 1 }, "Check",
        juce::StringArray { "Off", "Phone", "Car", "Buds" }, 0, juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { KorrentProcessor::kActive, 1 }, "In", true));
    return layout;
}

KorrentProcessor::KorrentProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Korrent", makeLayout())
{
    pLoud = apvts.getRawParameterValue (kLoud);   pStyle = apvts.getRawParameterValue (kStyle);
    pCheck = apvts.getRawParameterValue (kCheck); pActive = apvts.getRawParameterValue (kActive);
}

bool KorrentProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void KorrentProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    setLatencySamples (engine.getLatencySamples());
    bypassDelay.setSize (2, engine.getLatencySamples() + 1);
    bypassDelay.clear(); bypassPos = 0;
}

void KorrentProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch) buffer.clear (ch, 0, buffer.getNumSamples());

    if (relearnFlag.exchange (false)) engine.reset();

    if (pActive->load() < 0.5f)
    {
        // switched off: same delay as when on, so nothing jumps in time when you compare
        const int len = bypassDelay.getNumSamples(), nCh = juce::jmin (2, buffer.getNumChannels());
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            for (int ch = 0; ch < nCh; ++ch)
            {
                const float in = buffer.getSample (ch, i);
                buffer.setSample (ch, i, bypassDelay.getSample (ch, (bypassPos + 1) % len));
                bypassDelay.setSample (ch, bypassPos, in);
            }
            bypassPos = (bypassPos + 1) % len;
        }
        return;
    }

    engine.setParams (pLoud->load(), (int) pStyle->load(), (int) pCheck->load(), isNonRealtime());
    engine.process (buffer);
}

juce::AudioProcessorEditor* KorrentProcessor::createEditor() { return new KorrentEditor (*this); }

void KorrentProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (kCheck, 0, nullptr);
    if (auto xml = state.createXml())
    {
        // CHECK is never saved as "on": a reopened project always plays the real master
        for (auto* c : xml->getChildIterator()) if (c->getStringAttribute ("id") == kCheck) c->setAttribute ("value", 0.0);
        copyXmlToBinary (*xml, destData);
    }
}

void KorrentProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            for (auto* c : xml->getChildIterator()) if (c->getStringAttribute ("id") == kCheck) c->setAttribute ("value", 0.0);
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KorrentProcessor(); }
