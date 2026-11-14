#include "SogProcessor.h"
#include "SogEditor.h"

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });
    auto knob = [&] (const char* id, const char* name, float def)
    { layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), def, pct)); };
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { SogProcessor::kType, 1 }, "Type",
        juce::StringArray { "Reverb", "Echo", "Stutter", "Spin" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { SogProcessor::kDir, 1 }, "Direction",
        juce::StringArray { "Before", "After" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { SogProcessor::kLength, 1 }, "Length",
        juce::StringArray { "1/4 bar", "1/2 bar", "1 bar", "2 bars" }, 2));
    knob (SogProcessor::kSize, "Size", 0.5f);
    knob (SogProcessor::kTone, "Tone", 0.5f);
    knob (SogProcessor::kWidth, "Width", 0.6f);
    knob (SogProcessor::kTremolo, "Tremolo", 0.6f);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { SogProcessor::kTremDiv, 1 }, "Rate",
        juce::StringArray { "Off", "1/8", "1/16", "1/32" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { SogProcessor::kLowCut, 1 }, "Low Cut", true));
    knob (SogProcessor::kDrive, "Drive", 0.0f);
    knob (SogProcessor::kLevel, "Level", 0.7f);
    return layout;
}

SogProcessor::SogProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Sog", makeLayout())
{
    startTimerHz (12);
}

bool SogProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void SogProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    pre.setSize (2, juce::jmax (16, (int) (preRollMs * 0.001 * sr))); pre.clear(); prePos = 0;
    if (capture.getNumSamples() != (int) (maxCaptureSec * sr)) { capture.setSize (2, (int) (maxCaptureSec * sr)); capture.clear(); hasCapture = false; capturedSamples.store (0); if (state.load() == State::Ready) state.store (State::Empty); }
    lastKey.clear();
}

SogParams SogProcessor::currentParams() const
{
    static const float bars[4] = { 0.25f, 0.5f, 1.0f, 2.0f };
    SogParams p;
    p.bpm = bpm.load();
    p.bars = bars[juce::jlimit (0, 3, (int) apvts.getRawParameterValue (kLength)->load())];
    p.size = apvts.getRawParameterValue (kSize)->load();   p.tone = apvts.getRawParameterValue (kTone)->load();
    p.width = apvts.getRawParameterValue (kWidth)->load(); p.tremolo = apvts.getRawParameterValue (kTremolo)->load();
    p.tremDiv = (int) apvts.getRawParameterValue (kTremDiv)->load();
    p.lowCut = apvts.getRawParameterValue (kLowCut)->load() > 0.5f;
    p.level = apvts.getRawParameterValue (kLevel)->load();
    p.type = (int) apvts.getRawParameterValue (kType)->load();
    p.dir = (int) apvts.getRawParameterValue (kDir)->load();
    p.drive = apvts.getRawParameterValue (kDrive)->load();
    return p;
}

// message thread: (re)render whenever there is a new capture or a setting / the tempo changed
void SogProcessor::timerCallback()
{
    if (state.load() != State::Ready) return;
    const int n = capturedSamples.load();
    if (n < 64) return;
    const auto p = currentParams();
    const juce::String key = juce::String (n) + "|" + juce::String (p.bpm, 2) + "|" + juce::String (p.bars) + "|" + juce::String (p.size, 3) + "|" + juce::String (p.tone, 3) + "|"
                           + juce::String (p.width, 3) + "|" + juce::String (p.tremolo, 3) + "|" + juce::String (p.tremDiv) + "|" + juce::String ((int) p.lowCut) + "|" + juce::String (p.level, 3)
                           + "|" + juce::String (p.type) + "|" + juce::String (p.dir) + "|" + juce::String (p.drive, 3);
    if (key == lastKey && ! captureFresh.load()) return;
    captureFresh.store (false); lastKey = key;

    juce::AudioBuffer<float> src (capture.getArrayOfWritePointers(), 2, n);      // audio thread does not write while Ready
    auto out = std::make_shared<juce::AudioBuffer<float>> (SogRenderer::render (src, sr, p));
    {
        const juce::SpinLock::ScopedLockType l (swellLock);
        if (swell != nullptr) retired.push_back (swell);
        if (retired.size() > 6) retired.erase (retired.begin());
        swell = out;
    }
    clipAfter.store (p.dir == SogRenderer::After); clipReplaces.store (SogRenderer::replacesWord (p));
    swellChanged.store (true);
    renderCount.fetch_add (1);
}

juce::AudioBuffer<float> SogProcessor::getSwell() const
{
    std::shared_ptr<const juce::AudioBuffer<float>> s;
    { const juce::SpinLock::ScopedLockType l (swellLock); s = swell; }
    juce::AudioBuffer<float> copy; if (s != nullptr) copy.makeCopyOf (*s);
    return copy;
}

juce::File SogProcessor::writeWav()
{
    if (! license.isLicensed()) return {};          // demo: capture and preview work, the file needs a login
    const auto buf = getSwell();
    if (buf.getNumSamples() < 64) return {};
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Fatboard Sog");
    dir.createDirectory();
    static const char* len[4] = { "quarter bar", "half bar", "1 bar", "2 bars" };
    static const char* kind[4] = { "reverb", "echo", "stutter", "spin" };
    const juce::String what = juce::String (kind[juce::jlimit (0, 3, (int) apvts.getRawParameterValue (kType)->load())]) + (clipAfter.load() ? " after " : " before ");
    const auto file = dir.getChildFile ("Sog " + what + juce::String (juce::roundToInt (bpm.load())) + " bpm " + len[juce::jlimit (0, 3, (int) apvts.getRawParameterValue (kLength)->load())]
                                         + " " + juce::String (++wavCounter) + ".wav");
    file.deleteFile();
    juce::WavAudioFormat wav;
    if (auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
        if (auto* writer = wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0))
        {
            stream.release();
            std::unique_ptr<juce::AudioFormatWriter> w (writer);
            w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
            w.reset();
            return file;
        }
    return {};
}

void SogProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), nCh = juce::jmin (2, buffer.getNumChannels());
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch) buffer.clear (ch, 0, n);
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) if (auto b = pos->getBpm()) if (*b > 20.0) bpm.store (*b);

    // ---- capture: wait for the sound, then take it from a few ms before its start ----
    const State st = state.load();
    float peak = 0.0f;
    const int preLen = pre.getNumSamples(), capMax = capture.getNumSamples();
    for (int i = 0; i < n; ++i)
    {
        const float l = buffer.getSample (0, i), r = nCh > 1 ? buffer.getSample (1, i) : l;
        const float a = juce::jmax (std::abs (l), std::abs (r)); peak = juce::jmax (peak, a);
        if (st == State::Armed && state.load() == State::Armed)
        {
            if (a > startThreshold)
            {
                // the pre-roll becomes the start of the capture
                for (int k = 0; k < preLen; ++k) { const int rp = (prePos + k) % preLen; capture.setSample (0, k, pre.getSample (0, rp)); capture.setSample (1, k, pre.getSample (1, rp)); }
                capLen = preLen; quietRun = 0;
                state.store (State::Recording);
            }
        }
        if (state.load() == State::Recording)
        {
            if (capLen < capMax) { capture.setSample (0, capLen, l); capture.setSample (1, capLen, r); ++capLen; }
            quietRun = a < startThreshold * 0.5f ? quietRun + 1 : 0;
            const bool longEnough = capLen > (int) (0.15 * sr);
            if (capLen >= capMax || (longEnough && quietRun > (int) (0.35 * sr)))
            {
                capturedSamples.store (capLen); hasCapture = true; captureFresh.store (true);
                state.store (State::Ready);
            }
        }
        pre.setSample (0, prePos, l); pre.setSample (1, prePos, r); prePos = (prePos + 1) % preLen;
    }
    inLevel.store (inLevel.load() + (peak - inLevel.load()) * (peak > inLevel.load() ? 0.7f : 0.15f));

    // ---- preview: swell, then the captured sound, on top of the track ----
    if (swellChanged.load())
    {
        const juce::SpinLock::ScopedTryLockType l (swellLock);
        if (l.isLocked()) { audioSwell = swell; swellChanged.store (false); }
    }
    const int req = previewPos.exchange (-1);
    if (req >= 0) playPos = 0;
    if (playPos >= 0 && audioSwell != nullptr && state.load() == State::Ready)
    {
        const int sLen = audioSwell->getNumSamples(), cLen = capturedSamples.load(), skip = preLen;       // the word starts after the pre-roll
        const bool after = clipAfter.load(), replaces = clipReplaces.load();
        for (int i = 0; i < n; ++i)
        {
            if (! after)
            {
                // Before: the clip, then the word
                if (playPos < sLen) { for (int ch = 0; ch < nCh; ++ch) buffer.addSample (ch, i, audioSwell->getSample (ch, playPos)); }
                else if (playPos - sLen + skip < cLen) { for (int ch = 0; ch < nCh; ++ch) buffer.addSample (ch, i, capture.getSample (ch, playPos - sLen + skip)); }
                else { playPos = -1; break; }
            }
            else
            {
                // After: the clip starts with the word (or stands in for it)
                const bool clipOn = playPos < sLen, wordOn = ! replaces && playPos + skip < cLen;
                if (! clipOn && ! wordOn) { playPos = -1; break; }
                if (clipOn) for (int ch = 0; ch < nCh; ++ch) buffer.addSample (ch, i, audioSwell->getSample (ch, playPos));
                if (wordOn) for (int ch = 0; ch < nCh; ++ch) buffer.addSample (ch, i, capture.getSample (ch, playPos + skip));
            }
            ++playPos;
        }
    }
    else if (playPos >= 0 && state.load() != State::Ready) playPos = -1;
}

juce::AudioProcessorEditor* SogProcessor::createEditor() { return new SogEditor (*this); }

void SogProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}
void SogProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SogProcessor(); }
