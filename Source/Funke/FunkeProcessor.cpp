#include "FunkeProcessor.h"
#include "FunkeEditor.h"

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });
    using Choice = juce::AudioParameterChoice; using Float = juce::AudioParameterFloat;
    layout.add (std::make_unique<Choice> (juce::ParameterID { FunkeProcessor::kVibe, 1 }, "Vibe", Funke::vibeNames(), 0));
    layout.add (std::make_unique<Choice> (juce::ParameterID { FunkeProcessor::kKey, 1 }, "Key", juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 5));
    layout.add (std::make_unique<Choice> (juce::ParameterID { FunkeProcessor::kBars, 1 }, "Length", juce::StringArray { "4 bars", "8 bars" }, 0));
    layout.add (std::make_unique<Float> (juce::ParameterID { FunkeProcessor::kMotion, 1 }, "Motion", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f, pct));
    layout.add (std::make_unique<Float> (juce::ParameterID { FunkeProcessor::kDensity, 1 }, "Density", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.4f, pct));
    layout.add (std::make_unique<Choice> (juce::ParameterID { FunkeProcessor::kDrumMode, 1 }, "Drums", juce::StringArray { "Free", "With the kick", "In the gaps" }, 1));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { FunkeProcessor::kListen, 1 }, "Listen", true));
    layout.add (std::make_unique<Float> (juce::ParameterID { FunkeProcessor::kLevel, 1 }, "Level", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.6f, pct));
    return layout;
}

FunkeProcessor::FunkeProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Funke", makeLayout())
{
    juce::Random r; for (auto& s : seeds) s = (uint32_t) r.nextInt (1000000) + 1;
    startTimerHz (15);
    timerCallback();
    // dev hook: FUNKE_DRUMS=/path/to/loop.wav loads it right away (screenshots, tests)
    const auto d = juce::SystemStats::getEnvironmentVariable ("FUNKE_DRUMS", {});
    if (d.isNotEmpty()) loadDrums (juce::File (d));
}

void FunkeProcessor::prepareToPlay (double sampleRate, int) { sr = sampleRate; for (auto& v : voices) v.on = false; lastBeat = -1.0; }

Funke::Settings FunkeProcessor::settingsNow() const
{
    Funke::Settings s;
    s.vibe = (int) apvts.getRawParameterValue (kVibe)->load(); s.key = (int) apvts.getRawParameterValue (kKey)->load();
    s.bars = apvts.getRawParameterValue (kBars)->load() > 0.5f ? 8 : 4;
    s.motion = apvts.getRawParameterValue (kMotion)->load(); s.density = apvts.getRawParameterValue (kDensity)->load();
    s.drumMode = (int) apvts.getRawParameterValue (kDrumMode)->load();
    s.seedChords = seeds[0]; s.seedMelody = seeds[1]; s.seedBass = seeds[2]; s.drums = drums;
    return s;
}

void FunkeProcessor::roll (Layer which)
{
    juce::Random r;
    for (int i = 0; i < 3; ++i) if (which == All || which == (Layer) i) seeds[i] = (uint32_t) r.nextInt (100000000) + 1;
    timerCallback();
}

void FunkeProcessor::timerCallback()
{
    const auto s = settingsNow();
    juce::String key; key << s.vibe << "|" << s.key << "|" << s.bars << "|" << juce::String (s.motion, 2) << "|" << juce::String (s.density, 2) << "|" << s.drumMode << "|"
                          << (juce::int64) s.seedChords << "|" << (juce::int64) s.seedMelody << "|" << (juce::int64) s.seedBass << "|" << (drums.valid ? drumName : juce::String());
    if (key == lastKey) return;
    lastKey = key;
    auto p = std::make_shared<Funke::Pattern> (Funke::generate (s));
    {
        const juce::SpinLock::ScopedLockType l (lock);
        if (pattern != nullptr) retired.push_back (pattern);
        if (retired.size() > 8) retired.erase (retired.begin());
        pattern = p;
    }
    changed.store (true); patternCount.fetch_add (1);
}

juce::String FunkeProcessor::loadDrums (const juce::File& f)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples < 2000) return "Cannot read " + f.getFileName() + ".";
    const int n = (int) juce::jmin ((juce::int64) (reader->sampleRate * 40.0), reader->lengthInSamples);
    juce::AudioBuffer<float> b (juce::jlimit (1, 2, (int) reader->numChannels), n);
    reader->read (&b, 0, n, 0, true, true);
    const auto g = Funke::analyseDrums (b, reader->sampleRate, hostBpm.load());
    if (! g.valid) return "No clear hits found in " + f.getFileName() + ". It should be a drum loop at the project tempo.";
    drums = g; drumName = f.getFileNameWithoutExtension();
    timerCallback();
    return {};
}
void FunkeProcessor::clearDrums() { drums = {}; drumName.clear(); timerCallback(); }

juce::File FunkeProcessor::writeMidi (Layer which)
{
    const auto p = getPattern(); if (p == nullptr) return {};
    std::vector<const std::vector<Funke::Note>*> layers;
    if (which == Chords || which == All) layers.push_back (&p->chords);
    if (which == Melody || which == All) layers.push_back (&p->melody);
    if (which == Bass   || which == All) layers.push_back (&p->bass);
    static const char* names[4] = { "chords", "melody", "bass", "idea" };
    static const char* keys[12] = { "C", "Cs", "D", "Ds", "E", "F", "Fs", "G", "Gs", "A", "As", "B" };
    const auto s = settingsNow();
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Fatboard Funke"); dir.createDirectory();
    const auto file = dir.getChildFile (juce::String ("Funke ") + Funke::vibe (s.vibe).name + " " + keys[s.key % 12] + " " + names[(int) which] + " " + juce::String (++fileCounter) + ".mid");
    file.deleteFile();
    if (auto out = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
        if (Funke::toMidi (layers, hostBpm.load(), p->beats()).writeTo (*out)) { out->flush(); return file; }
    return {};
}

// ---------------------------------------------------------------- the built-in sound
void FunkeProcessor::trigger (int kind, const Funke::Note& n, int delaySamples, double samplesPerBeat)
{
    Voice* v = nullptr;
    for (auto& x : voices) if (! x.on) { v = &x; break; }
    if (v == nullptr) { v = &voices[0]; for (auto& x : voices) if (x.age > v->age) v = &x; }
    v->on = true; v->kind = kind; v->phase = 0.0; v->env = 0.0f; v->age = -delaySamples;
    v->inc = 440.0 * std::pow (2.0, (n.pitch - 69) / 12.0) / sr;
    v->amp = n.vel * (kind == Chords ? 0.10f : kind == Melody ? 0.26f : 0.42f);
    v->hold = juce::jmax (64, (int) (n.length * samplesPerBeat));
}

void FunkeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), nCh = juce::jmin (2, buffer.getNumChannels());
    buffer.clear(); midi.clear();
    bool hostPlaying = false; double ppq = 0.0;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition())
    {
        if (auto b = pos->getBpm()) if (*b > 20.0) hostBpm.store (*b);
        hostPlaying = pos->getIsPlaying();
        if (auto q = pos->getPpqPosition()) ppq = *q;
    }
    if (changed.load()) { const juce::SpinLock::ScopedTryLockType l (lock); if (l.isLocked()) { audioPattern = pattern; changed.store (false); } }
    const bool listen = apvts.getRawParameterValue (kListen)->load() > 0.5f;
    if (audioPattern == nullptr || ! listen) { for (auto& v : voices) v.on = false; playBeat.store (-1.0); lastBeat = -1.0; return; }

    const double bpm = hostBpm.load(), samplesPerBeat = sr * 60.0 / bpm, loop = audioPattern->beats(), blockBeats = n / samplesPerBeat;
    // with the host when it plays, on our own clock when it stands still
    double start = hostPlaying ? std::fmod (std::fmod (ppq, loop) + loop, loop) : freePos;
    if (! hostPlaying) { freePos = std::fmod (freePos + blockBeats, loop); } else freePos = std::fmod (start + blockBeats, loop);
    if (lastBeat >= 0.0 && std::abs (start - lastBeat) > 0.02 && std::abs (std::abs (start - lastBeat) - loop) > 0.02) for (auto& v : voices) if (v.on && v.hold > 0) v.hold = 0;      // the host jumped: let go
    lastBeat = std::fmod (start + blockBeats, loop);
    playBeat.store (start);
    auto fire = [&] (const std::vector<Funke::Note>& layer, int kind)
    {
        for (const auto& note : layer)
        {
            double d = note.start - start; if (d < -1.0e-9) d += loop;
            if (d >= 0.0 && d < blockBeats) trigger (kind, note, (int) (d * samplesPerBeat), samplesPerBeat);
        }
    };
    fire (audioPattern->chords, Chords); fire (audioPattern->melody, Melody); fire (audioPattern->bass, Bass);

    const float level = apvts.getRawParameterValue (kLevel)->load(); const float gain = level * level * 1.6f;
    const float twoPi = juce::MathConstants<float>::twoPi;
    for (auto& v : voices)
    {
        if (! v.on) continue;
        const float attack = v.kind == Chords ? 1.0f / (0.03f * (float) sr) : 1.0f / (0.004f * (float) sr);
        const float release = std::exp (-1.0f / ((v.kind == Chords ? 0.25f : v.kind == Melody ? 0.12f : 0.06f) * (float) sr));
        const float decay = v.kind == Melody ? std::exp (-1.0f / (0.9f * (float) sr)) : 1.0f;       // the melody is a pluck, chords and bass hold
        for (int i = 0; i < n; ++i)
        {
            if (v.age < 0) { ++v.age; continue; }
            if (v.age < v.hold) { v.env = v.env < 1.0f && v.age * attack < 1.0f ? juce::jmin (1.0f, (float) v.age * attack) : v.env * decay; }
            else { v.env *= release; if (v.env < 1.0e-4f) { v.on = false; break; } }
            const float ph = (float) v.phase;
            float s;
            if (v.kind == Bass) s = std::sin (twoPi * ph) + 0.25f * std::sin (twoPi * 2.0f * ph);
            else if (v.kind == Chords) s = 0.7f * std::sin (twoPi * ph) + 0.22f * std::sin (twoPi * 2.0f * ph) + 0.1f * std::sin (twoPi * 3.0f * ph);       // soft keys
            else s = std::sin (twoPi * ph) + 0.4f * std::sin (twoPi * 2.0f * ph) * v.env + 0.15f * std::sin (twoPi * 4.0f * ph) * v.env * v.env;            // a bell-ish pluck
            s *= v.env * v.amp * gain;
            for (int ch = 0; ch < nCh; ++ch) buffer.addSample (ch, i, s);
            v.phase += v.inc; if (v.phase >= 1.0) v.phase -= 1.0;
            ++v.age;
        }
    }
}

juce::AudioProcessorEditor* FunkeProcessor::createEditor() { return new FunkeEditor (*this); }

void FunkeProcessor::getStateInformation (juce::MemoryBlock& d)
{
    auto st = apvts.copyState();
    st.setProperty ("seedChords", (juce::int64) seeds[0], nullptr); st.setProperty ("seedMelody", (juce::int64) seeds[1], nullptr); st.setProperty ("seedBass", (juce::int64) seeds[2], nullptr);
    if (drums.valid)
    {
        juce::String k, h; for (int i = 0; i < 32; ++i) { k << juce::String (drums.kick[i], 2) << " "; h << juce::String (drums.hit[i], 2) << " "; }
        st.setProperty ("drumKick", k, nullptr); st.setProperty ("drumHit", h, nullptr); st.setProperty ("drumName", drumName, nullptr);
    }
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, d);
}

void FunkeProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto st = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (st);
            seeds[0] = (uint32_t) (juce::int64) st.getProperty ("seedChords", (juce::int64) seeds[0]); seeds[1] = (uint32_t) (juce::int64) st.getProperty ("seedMelody", (juce::int64) seeds[1]); seeds[2] = (uint32_t) (juce::int64) st.getProperty ("seedBass", (juce::int64) seeds[2]);
            drums = {}; drumName = st.getProperty ("drumName").toString();
            juce::StringArray k, h; k.addTokens (st.getProperty ("drumKick").toString(), " ", ""); h.addTokens (st.getProperty ("drumHit").toString(), " ", ""); k.removeEmptyStrings(); h.removeEmptyStrings();
            if (k.size() == 32 && h.size() == 32) { for (int i = 0; i < 32; ++i) { drums.kick[i] = k[i].getFloatValue(); drums.hit[i] = h[i].getFloatValue(); } drums.valid = true; }
            lastKey.clear(); timerCallback();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FunkeProcessor(); }
