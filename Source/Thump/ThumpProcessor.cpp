#include "ThumpProcessor.h"
#include "ThumpEditor.h"

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });
    juce::StringArray tones; for (int i = 0; i < ThumpTones::count; ++i) tones.add (ThumpTones::all[i].name);
    tones.add ("Sample");
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ThumpProcessor::kTone, 1 }, "808", tones, 0));
    auto knob = [&] (const char* id, const char* name, float def)
    { layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), def, pct)); };
    knob (ThumpProcessor::kSub, "808 Level", 0.8f);
    knob (ThumpProcessor::kKick, "Kick", 0.6f);
    knob (ThumpProcessor::kDecay, "Decay", 0.67f);
    knob (ThumpProcessor::kClip, "Clip", 0.3f);
    return layout;
}

ThumpProcessor::ThumpProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Thump", makeLayout())
{
    formats.registerBasicFormats();
    pTone = apvts.getRawParameterValue (kTone);   pSub = apvts.getRawParameterValue (kSub);
    pKick = apvts.getRawParameterValue (kKick);   pDecay = apvts.getRawParameterValue (kDecay);
    pClip = apvts.getRawParameterValue (kClip);
}

bool ThumpProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void ThumpProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    mono.assign ((size_t) juce::jmax (samplesPerBlock, 8192), 0.0f);
    sampleChanged.store (true);
}

juce::String ThumpProcessor::loadSample (const juce::File& file, bool selectIt)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr) return "Can't read that file. Use wav, aiff or flac.";
    const int n = (int) juce::jmin ((juce::int64) (reader->sampleRate * 12.0), reader->lengthInSamples);     // 12 s is plenty for an 808
    if (n < 2000) return "That file is too short for an 808.";
    juce::AudioBuffer<float> buf ((int) reader->numChannels, n);
    reader->read (&buf, 0, n, 0, true, true);
    auto s = std::make_shared<ThumpSample>();
    s->sampleRate = reader->sampleRate; s->name = file.getFileNameWithoutExtension(); s->data.resize ((size_t) n);
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float v = 0.0f; for (int ch = 0; ch < buf.getNumChannels(); ++ch) v += buf.getSample (ch, i);
        v /= (float) buf.getNumChannels(); s->data[(size_t) i] = v; peak = juce::jmax (peak, std::abs (v));
    }
    if (peak < 1.0e-4f) return "That file is silent.";
    for (auto& v : s->data) v *= 0.8f / peak;                    // same level as the built-in tones
    const int fade = juce::jmin (n / 4, (int) (s->sampleRate * 0.01));   // no click if the file ends abruptly
    for (int i = 0; i < fade; ++i) s->data[(size_t) (n - 1 - i)] *= (float) i / (float) fade;
    s->detectRoot();
    {
        const juce::SpinLock::ScopedLockType l (sampleLock);
        if (sample != nullptr) retired.push_back (sample);
        if (retired.size() > 8) retired.erase (retired.begin());      // only ever on the message thread, long after audio moved on
        sample = s;
    }
    samplePath = file.getFullPathName();
    sampleChanged.store (true);
    if (selectIt)
        if (auto* p = apvts.getParameter (kTone)) { p->beginChangeGesture(); p->setValueNotifyingHost (1.0f); p->endChangeGesture(); }
    return {};
}

void ThumpProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (sampleChanged.load())
    {
        const juce::SpinLock::ScopedTryLockType l (sampleLock);
        if (l.isLocked()) { audioSample = sample; engine.setSample (audioSample); sampleChanged.store (false); }
    }
    engine.setParams ((int) pTone->load(), pSub->load(), pKick->load(), pDecay->load(), pClip->load());
    if ((int) mono.size() < n) { buffer.clear(); return; }

    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > pos) { engine.render (mono.data() + pos, at - pos); pos = at; }
        const auto m = meta.getMessage();
        if (m.isNoteOn()) engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allOff();
        // note-off is ignored on purpose: an 808 rings until the next note cuts it (Decay sets how long)
    }
    if (pos < n) engine.render (mono.data() + pos, n - pos);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) buffer.copyFrom (ch, 0, mono.data(), n);
    midi.clear();
}

juce::AudioProcessorEditor* ThumpProcessor::createEditor() { return new ThumpEditor (*this); }

void ThumpProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("samplePath", samplePath, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void ThumpProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            const juce::String path = tree.getProperty ("samplePath").toString();
            apvts.replaceState (tree);
            if (path.isNotEmpty() && juce::File (path).existsAsFile()) loadSample (juce::File (path), false);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ThumpProcessor(); }
