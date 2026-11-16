#include "TapProcessor.h"
#include "TapEditor.h"

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { TapProcessor::kBars, 1 }, "Loop", juce::StringArray { "1 bar", "2 bars", "4 bars" }, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { TapProcessor::kQuant, 1 }, "Snap", juce::StringArray { "Off", "1/16", "1/32" }, 1));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { TapProcessor::kRec, 1 }, "Rec", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { TapProcessor::kLevel, 1 }, "Level", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.8f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })));
    return layout;
}

TapProcessor::TapProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Tap", makeLayout())
{
    for (int p = 0; p < numPads; ++p) makeDefault (p);
}

int TapProcessor::barsNow() const { static const int b[3] = { 1, 2, 4 }; return b[juce::jlimit (0, 2, (int) apvts.getRawParameterValue (kBars)->load())]; }

// built-in sounds: plain synthesis, good enough to play right away
void TapProcessor::makeDefault (int pad)
{
    auto s = std::make_shared<Sample>(); s->sr = 48000.0;
    static const char* names[numPads] = { "Kick", "Snare", "Hat", "Open hat" };
    const double secs[numPads] = { 0.45, 0.22, 0.06, 0.38 };
    const int n = (int) (secs[pad] * s->sr); s->audio.setSize (2, n); s->audio.clear(); s->name = names[pad];
    juce::Random rng (17 + pad); float hp = 0.0f, hp2 = 0.0f; double ph = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / s->sr; float v = 0.0f;
        const float noise = rng.nextFloat() * 2.0f - 1.0f;
        if (pad == 0) { ph += (48.0 + 150.0 * std::exp (-t * 38.0)) / s->sr; v = (float) (std::sin (juce::MathConstants<double>::twoPi * ph) * std::exp (-t * 7.5)) * 0.95f + noise * 0.25f * (float) std::exp (-t * 400.0); }
        else if (pad == 1) { ph += (185.0 + 90.0 * std::exp (-t * 60.0)) / s->sr; hp += (noise - hp) * 0.35f; v = (float) (std::sin (juce::MathConstants<double>::twoPi * ph) * std::exp (-t * 28.0)) * 0.55f + (noise - hp) * 0.75f * (float) std::exp (-t * 22.0); }
        else { hp += (noise - hp) * 0.55f; const float x = noise - hp; hp2 += (x - hp2) * 0.5f; v = (x - hp2) * (pad == 2 ? 0.9f * (float) std::exp (-t * 95.0) : 0.7f * (float) std::exp (-t * 11.0)); }
        v = std::tanh (v * 1.4f) * 0.8f;
        s->audio.setSample (0, i, v); s->audio.setSample (1, i, v);
    }
    s->audio.applyGainRamp (n - juce::jmin (n, 200), juce::jmin (n, 200), 1.0f, 0.0f);
    { const juce::SpinLock::ScopedLockType l (sampleLock); if (samples[pad] != nullptr) retired.push_back (samples[pad]); samples[pad] = s; }
    samplesChanged.store (true);
}
void TapProcessor::resetSample (int pad) { if (pad >= 0 && pad < numPads) makeDefault (pad); }

juce::String TapProcessor::loadSample (int pad, const juce::File& f)
{
    if (pad < 0 || pad >= numPads) return {};
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples < 16) return "Cannot read " + f.getFileName() + ".";
    auto s = std::make_shared<Sample>(); s->sr = reader->sampleRate; s->name = f.getFileNameWithoutExtension(); s->path = f.getFullPathName();
    const int n = (int) juce::jmin ((juce::int64) (reader->sampleRate * 10.0), reader->lengthInSamples);       // a one-shot, not a song
    s->audio.setSize (2, n); reader->read (&s->audio, 0, n, 0, true, true);
    if (reader->numChannels == 1) s->audio.copyFrom (1, 0, s->audio, 0, 0, n);
    { const juce::SpinLock::ScopedLockType l (sampleLock); if (samples[pad] != nullptr) retired.push_back (samples[pad]); if (retired.size() > 16) retired.erase (retired.begin()); samples[pad] = s; }
    samplesChanged.store (true);
    return {};
}
juce::String TapProcessor::getPadName (int pad) const { const juce::SpinLock::ScopedLockType l (sampleLock); return samples[pad] != nullptr ? samples[pad]->name : juce::String(); }

void TapProcessor::hit (int pad, float velocity)
{
    if (pad < 0 || pad >= numPads) return;
    int s1, n1, s2, n2; hitFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) { hitData[(size_t) s1] = { pad, velocity, juce::Time::getMillisecondCounterHiRes() }; hitFifo.finishedWrite (1); }
}

std::vector<TapProcessor::Event> TapProcessor::getEvents() const
{
    const juce::SpinLock::ScopedLockType l (eventLock);
    return std::vector<Event> (events.begin(), events.begin() + numEvents);
}

void TapProcessor::prepareToPlay (double sampleRate, int) { sr = sampleRate; for (auto& v : voices) v.on = false; lastStart = -1.0; }

void TapProcessor::trigger (int pad, float vel, int delay)
{
    if (audioSamples[pad] == nullptr) return;
    if (pad == 2) for (auto& v : voices) if (v.on && v.pad == 3 && v.fade < 0) v.fade = (int) (0.012 * sr);      // the closed hat chokes the open one
    Voice* v = nullptr;
    for (auto& x : voices) if (! x.on) { v = &x; break; }
    if (v == nullptr) { v = &voices[0]; for (auto& x : voices) if (x.pos > v->pos) v = &x; }
    v->on = true; v->pad = pad; v->pos = 0.0; v->step = audioSamples[pad]->sr / sr; v->gain = 0.35f + 0.65f * vel; v->delay = delay; v->fade = -1;
    flash[(size_t) pad].store (1.0f);
}

void TapProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), nCh = juce::jmin (2, buffer.getNumChannels());
    buffer.clear();
    bool playing = false; double ppq = 0.0;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition())
    {
        if (auto b = pos->getBpm()) if (*b > 20.0) hostBpm.store (*b);
        playing = pos->getIsPlaying(); if (auto q = pos->getPpqPosition()) ppq = *q;
    }
    hostPlaying.store (playing);
    if (samplesChanged.load()) { const juce::SpinLock::ScopedTryLockType l (sampleLock); if (l.isLocked()) { for (int p = 0; p < numPads; ++p) audioSamples[p] = samples[p]; samplesChanged.store (false); for (auto& v : voices) v.on = false; } }

    const double bpm = hostBpm.load(), samplesPerBeat = sr * 60.0 / bpm, loop = barsNow() * 4.0, blockBeats = n / samplesPerBeat;
    const double start = playing ? std::fmod (std::fmod (ppq, loop) + loop, loop) : -1.0;
    const bool rec = apvts.getRawParameterValue (kRec)->load() > 0.5f;
    const int quant = (int) apvts.getRawParameterValue (kQuant)->load();
    bool eventsTouched = false;
    {
        const juce::SpinLock::ScopedTryLockType l (eventLock);
        if (l.isLocked())
        {
            if (restoreReq.exchange (false)) { numEvents = juce::jmin ((int) restored.size(), (int) maxEvents); for (int i = 0; i < numEvents; ++i) events[(size_t) i] = restored[(size_t) i]; eventsTouched = true; }
            if (clearReq.exchange (false)) { numEvents = 0; eventsTouched = true; }
            const int cp = clearPadReq.exchange (-1);
            if (cp >= 0) { int k = 0; for (int i = 0; i < numEvents; ++i) if (events[(size_t) i].pad != cp) events[(size_t) k++] = events[(size_t) i]; numEvents = k; eventsTouched = true; }
            if (playing && lastStart >= 0.0 && start < lastStart - 1.0e-6) ++pass;                          // the loop came round
            for (int i = numEvents - 1; i >= 0; --i) if (events[(size_t) i].beat >= loop) { events[(size_t) i] = events[(size_t) --numEvents]; eventsTouched = true; }      // loop got shorter

            auto record = [&] (int pad, float vel, double beat)
            {
                if (! (rec && playing) || numEvents >= maxEvents) return;
                if (quant > 0) { const double q = quant == 1 ? 0.25 : 0.125; beat = std::round (beat / q) * q; }
                beat = std::fmod (beat + loop, loop);
                for (int i = 0; i < numEvents; ++i) if (events[(size_t) i].pad == pad && std::abs (events[(size_t) i].beat - beat) < 0.02) return;      // already there
                events[(size_t) numEvents++] = { beat, pad, vel, pass + (beat > start + blockBeats ? -1 : 0) }; eventsTouched = true;
            };
            // taps from the keyboard: placed where the key went down, not where this block happens to be
            const double nowMs = juce::Time::getMillisecondCounterHiRes();
            int s1, n1, s2, n2; hitFifo.prepareToRead (hitFifo.getNumReady(), s1, n1, s2, n2);
            for (int part = 0; part < 2; ++part) for (int i = 0; i < (part == 0 ? n1 : n2); ++i)
            {
                const auto& h = hitData[(size_t) ((part == 0 ? s1 : s2) + i)];
                trigger (h.pad, h.vel, 0);
                if (playing) record (h.pad, h.vel, start - juce::jlimit (0.0, 0.25, (nowMs - h.timeMs) * 0.001) * bpm / 60.0);
            }
            hitFifo.finishedRead (n1 + n2);
            for (const auto meta : midi)
            {
                const auto m = meta.getMessage(); if (! m.isNoteOn()) continue;
                int pad = -1; for (int p = 0; p < numPads; ++p) if (m.getNoteNumber() == midiNote[p] || m.getNoteNumber() == 60 + p) pad = p;
                if (pad < 0) continue;
                trigger (pad, m.getFloatVelocity(), meta.samplePosition);
                if (playing) record (pad, m.getFloatVelocity(), start + meta.samplePosition / samplesPerBeat);
            }
            // play the loop back (not what was tapped during this very pass: that was already heard)
            if (playing)
                for (int i = 0; i < numEvents; ++i)
                {
                    const auto& e = events[(size_t) i]; if (e.pass >= pass) continue;
                    double d = e.beat - start; if (d < -1.0e-9) d += loop;
                    if (d >= 0.0 && d < blockBeats) trigger (e.pad, e.vel, (int) (d * samplesPerBeat));
                }
        }
    }
    midi.clear();
    if (eventsTouched) eventStamp.fetch_add (1);
    lastStart = start; playBeat.store (start);

    const float level = apvts.getRawParameterValue (kLevel)->load(); const float gain = level * level * 1.25f;
    for (auto& v : voices)
    {
        if (! v.on || audioSamples[v.pad] == nullptr) { v.on = false; continue; }
        const auto& a = audioSamples[v.pad]->audio; const int len = a.getNumSamples(); const int fadeLen = juce::jmax (1, (int) (0.012 * sr));
        for (int i = 0; i < n; ++i)
        {
            if (v.delay > 0) { --v.delay; continue; }
            const int k = (int) v.pos; if (k >= len - 1) { v.on = false; break; }
            const float f = (float) (v.pos - k); float g = v.gain * gain;
            if (v.fade >= 0) { g *= (float) v.fade / (float) fadeLen; if (--v.fade <= 0) { v.on = false; break; } }
            for (int ch = 0; ch < nCh; ++ch) buffer.addSample (ch, i, (a.getSample (ch, k) + (a.getSample (ch, k + 1) - a.getSample (ch, k)) * f) * g);
            v.pos += v.step;
        }
    }
}

juce::File TapProcessor::writeMidi()
{
    const auto ev = getEvents(); if (ev.empty()) return {};
    juce::MidiFile f; const int tpq = 960; f.setTicksPerQuarterNote (tpq); const double beats = getLoopBeats();
    juce::MidiMessageSequence tempo;
    tempo.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / juce::jlimit (20.0, 400.0, hostBpm.load()))), 0.0);
    tempo.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0); tempo.addEvent (juce::MidiMessage::endOfTrack(), beats * tpq); f.addTrack (tempo);
    juce::MidiMessageSequence seq;
    for (const auto& e : ev)
    {
        seq.addEvent (juce::MidiMessage::noteOn (10, midiNote[e.pad], juce::jlimit (0.05f, 1.0f, e.vel)), e.beat * tpq);
        seq.addEvent (juce::MidiMessage::noteOff (10, midiNote[e.pad]), juce::jmin (beats, e.beat + 0.2) * tpq);
    }
    seq.sort(); seq.updateMatchedPairs(); seq.addEvent (juce::MidiMessage::endOfTrack(), beats * tpq); f.addTrack (seq);
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Fatboard Tap"); dir.createDirectory();
    const auto file = dir.getChildFile ("Tap drums " + juce::String (hostBpm.load(), 0) + " bpm " + juce::String (++fileCounter) + ".mid"); file.deleteFile();
    if (auto out = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream())) if (f.writeTo (*out)) { out->flush(); return file; }
    return {};
}

juce::AudioProcessorEditor* TapProcessor::createEditor() { return new TapEditor (*this); }

void TapProcessor::getStateInformation (juce::MemoryBlock& d)
{
    auto st = apvts.copyState();
    juce::String ev; for (const auto& e : getEvents()) ev << juce::String (e.beat, 4) << "," << e.pad << "," << juce::String (e.vel, 2) << ";";
    st.setProperty ("events", ev, nullptr);
    { const juce::SpinLock::ScopedLockType l (sampleLock); for (int p = 0; p < numPads; ++p) st.setProperty ("sample" + juce::String (p), samples[p] != nullptr ? samples[p]->path : juce::String(), nullptr); }
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, d);
}

void TapProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto st = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (st);
            if (auto* rec = apvts.getParameter (kRec)) rec->setValueNotifyingHost (0.0f);          // never come back armed
            for (int p = 0; p < numPads; ++p) { const auto path = st.getProperty ("sample" + juce::String (p)).toString(); if (path.isNotEmpty() && juce::File (path).existsAsFile()) loadSample (p, juce::File (path)); }
            std::vector<Event> ev; juce::StringArray items; items.addTokens (st.getProperty ("events").toString(), ";", "");
            for (const auto& it : items) { juce::StringArray f; f.addTokens (it, ",", ""); if (f.size() == 3) ev.push_back ({ f[0].getDoubleValue(), juce::jlimit (0, numPads - 1, f[1].getIntValue()), f[2].getFloatValue(), -1000000 }); }
            { const juce::SpinLock::ScopedLockType l (eventLock); restored = ev; }
            restoreReq.store (true);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TapProcessor(); }
