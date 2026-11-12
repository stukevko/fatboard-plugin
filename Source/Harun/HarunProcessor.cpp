#include "HarunProcessor.h"
#include "HarunEditor.h"

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });
    auto dB = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; });

    juce::StringArray names; for (int i = 0; i < HarunTemplates::count; ++i) names.add (HarunTemplates::all[i].name);
    const auto& t = HarunTemplates::get (0);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { HarunProcessor::kTemplate, 1 }, "Template", names, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { HarunProcessor::kWidth, 1 }, "Width",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), t.width, pct));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { HarunProcessor::kLevel, 1 }, "Level",
        juce::NormalisableRange<float> (-18.0f, 3.0f, 0.1f), t.levelDb, dB));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { HarunProcessor::kSpace, 1 }, "Space",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), t.space, pct));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { HarunProcessor::kSpaceType, 1 }, "Space Type",
        juce::StringArray { "Hall", "Echo", "Both" }, t.spaceType));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { HarunProcessor::kActive, 1 }, "In", true));
    return layout;
}

HarunProcessor::HarunProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Harun", makeLayout())
{
    pTemplate = apvts.getRawParameterValue (kTemplate);   pWidth = apvts.getRawParameterValue (kWidth);
    pLevel = apvts.getRawParameterValue (kLevel);         pSpace = apvts.getRawParameterValue (kSpace);
    pSpaceType = apvts.getRawParameterValue (kSpaceType); pActive = apvts.getRawParameterValue (kActive);
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
}

void HarunProcessor::loadTemplate (int index)
{
    const auto& t = HarunTemplates::get (index);
    auto set = [this] (const char* id, float value)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    };
    set (kTemplate, (float) juce::jlimit (0, HarunTemplates::count - 1, index));
    set (kWidth, t.width); set (kLevel, t.levelDb); set (kSpace, t.space); set (kSpaceType, (float) t.spaceType);
}

void HarunProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch) buffer.clear (ch, 0, buffer.getNumSamples());

    const bool active = pActive->load() > 0.5f;
    if (! active) { wasActive = false; return; }
    if (! wasActive) { engine.reset(); wasActive = true; }

    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm = *b;

    engine.setParams ((int) pTemplate->load(), pWidth->load(), pLevel->load(), pSpace->load(), (int) pSpaceType->load(), bpm);
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
