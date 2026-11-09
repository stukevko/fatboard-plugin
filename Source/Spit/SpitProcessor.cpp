#include "SpitProcessor.h"
#include "SpitEditor.h"

constexpr int SpitProcessor::noteFor[3];

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { SpitProcessor::kSens, 1 }, "Sens",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, pct));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { SpitProcessor::kTight, 1 }, "Tight",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.7f, pct));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { SpitProcessor::kGrid, 1 }, "Grid",
        juce::StringArray { "1/8", "1/16", "1/16 T", "1/32" }, 1));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { SpitProcessor::kActive, 1 }, "In", true));
    return layout;
}

SpitProcessor::SpitProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Spit", makeLayout())
{
    pSens = apvts.getRawParameterValue (kSens); pTight = apvts.getRawParameterValue (kTight);
    pGrid = apvts.getRawParameterValue (kGrid); pActive = apvts.getRawParameterValue (kActive);
}

bool SpitProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void SpitProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    mono.setSize (1, samplesPerBlock);
    hits.reserve (32);
    setLatencySamples (engine.latencySamples());
}

void SpitProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    midi.clear();

    // transport
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm.store (*b);
            if (auto ppq = pos->getPpqPosition()) playheadBeat = *ppq;
            playing = pos->getIsPlaying();
        }

    // pending note-offs from last block
    for (int d = 0; d < 3; ++d)
        if (pendingNoteOff[d] >= 0) { midi.addEvent (juce::MidiMessage::noteOff (10, noteFor[d]), 0); pendingNoteOff[d] = -1; }

    // mono analysis
    auto* m = mono.getWritePointer (0);
    const int nCh = buffer.getNumChannels();
    juce::FloatVectorOperations::copy (m, buffer.getReadPointer (0), n);
    if (nCh > 1) { juce::FloatVectorOperations::add (m, buffer.getReadPointer (1), n); juce::FloatVectorOperations::multiply (m, 0.5f, n); }

    engine.settings.sensitivity = pSens->load();
    engine.process (m, n, hits);

    const bool active = pActive->load() > 0.5f;
    const double sr = getSampleRate();
    for (const auto& h : hits)
    {
        if (engine.isLearning()) { engine.feedLearn (engine.lastFeatures); continue; }
        if (! active) continue;
        const int at = juce::jlimit (0, n - 1, h.sampleOffset);
        const int note = noteFor[(int) h.drum];
        const int vel = juce::jlimit (1, 127, (int) std::lround (h.velocity * 127.0f));
        midi.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) vel), at);
        pendingNoteOff[(int) h.drum] = note;
        lastHitDrum.store ((int) h.drum); lastHitVelocity.store (h.velocity); hitCounter.fetch_add (1);

        if (recording.load() && playing)
        {
            // beat position of the hit, compensating analysis latency
            const double beat = playheadBeat + ((double) at - (double) engine.latencySamples()) / sr * bpm.load() / 60.0;
            const juce::ScopedLock sl (recLock);
            if (rec.size() < 4096) rec.push_back ({ h.drum, h.velocity, beat });
        }
    }
    // audio passes through untouched (so you still hear yourself if you want)
}

void SpitProcessor::startRecording() { { const juce::ScopedLock sl (recLock); rec.clear(); } recording.store (true); }
void SpitProcessor::stopRecording()  { recording.store (false); }
void SpitProcessor::clearRecording() { const juce::ScopedLock sl (recLock); rec.clear(); }
std::vector<SpitProcessor::RecordedHit> SpitProcessor::getRecordedHits() const { const juce::ScopedLock sl (recLock); return rec; }

juce::MemoryBlock SpitProcessor::buildMidiFile() const
{
    const auto hitsCopy = getRecordedHits();
    const float tight = pTight->load();
    const int gridIdx = (int) pGrid->load();
    const double gridBeats = gridIdx == 0 ? 0.5 : gridIdx == 1 ? 0.25 : gridIdx == 2 ? (1.0 / 6.0) : 0.125;

    const int ppq = 960;
    juce::MidiMessageSequence seq;
    double first = hitsCopy.empty() ? 0.0 : std::floor (hitsCopy.front().beat / 4.0) * 4.0;   // start at the bar
    for (const auto& h : hitsCopy)
    {
        const double raw = h.beat - first;
        const double q = std::round (raw / gridBeats) * gridBeats;
        const double beat = raw + (q - raw) * tight;
        const double tick = beat * ppq;
        const int note = noteFor[(int) h.drum];
        seq.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) juce::jlimit (1, 127, (int) std::lround (h.velocity * 127))), tick);
        seq.addEvent (juce::MidiMessage::noteOff (10, note), tick + ppq / 8);
    }
    seq.updateMatchedPairs();
    juce::MidiMessageSequence tempo;
    tempo.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / juce::jmax (20.0, bpm.load()))), 0);
    juce::MidiFile file; file.setTicksPerQuarterNote (ppq);
    file.addTrack (tempo); file.addTrack (seq);
    juce::MemoryBlock mb; juce::MemoryOutputStream os (mb, false); file.writeTo (os);
    return mb;
}

juce::AudioProcessorEditor* SpitProcessor::createEditor() { return new SpitEditor (*this); }

void SpitProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    // persist learned prototypes
    for (int d = 0; d < 3; ++d)
    {
        const auto& p = engine.prototype ((SpitEngine::Drum) d);
        juce::ValueTree pt ("proto"); pt.setProperty ("d", d, nullptr);
        pt.setProperty ("low", p.low, nullptr); pt.setProperty ("cen", p.centroid, nullptr);
        pt.setProperty ("high", p.high, nullptr); pt.setProperty ("dec", p.decay, nullptr); pt.setProperty ("mid", p.mid, nullptr);
        state.appendChild (pt, nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void SpitProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            for (int i = state.getNumChildren(); --i >= 0;)
            {
                auto c = state.getChild (i);
                if (c.hasType ("proto"))
                {
                    const int d = c.getProperty ("d");
                    if (d >= 0 && d < 3)
                        const_cast<SpitEngine::Features&> (engine.prototype ((SpitEngine::Drum) d)) =
                            { c.getProperty ("low"), c.getProperty ("cen"), c.getProperty ("high"), c.getProperty ("dec"), 0.0f, c.getProperty ("mid") };
                    state.removeChild (i, nullptr);
                }
            }
            apvts.replaceState (state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SpitProcessor(); }
