#include "DigProcessor.h"
#include "DigEditor.h"

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    using Choice = juce::AudioParameterChoice; using Float = juce::AudioParameterFloat;
    layout.add (std::make_unique<Choice> (juce::ParameterID { DigProcessor::kLength, 1 }, "Length", juce::StringArray { "1 bar", "2 bars", "4 bars", "8 bars" }, 1));
    layout.add (std::make_unique<Choice> (juce::ParameterID { DigProcessor::kMode, 1 }, "Fit", juce::StringArray { "Original", "Repitch", "Stretch" }, 1));
    layout.add (std::make_unique<Choice> (juce::ParameterID { DigProcessor::kChops, 1 }, "Chops", juce::StringArray { "8", "16", "Beats", "Hits" }, 1));
    layout.add (std::make_unique<Float> (juce::ParameterID { DigProcessor::kPitch, 1 }, "Pitch", juce::NormalisableRange<float> (-12.0f, 12.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st"; })));
    layout.add (std::make_unique<Float> (juce::ParameterID { DigProcessor::kNudge, 1 }, "Nudge", juce::NormalisableRange<float> (-60.0f, 60.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " ms"; })));
    layout.add (std::make_unique<Float> (juce::ParameterID { DigProcessor::kLevel, 1 }, "Level", juce::NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; })));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { DigProcessor::kGate, 1 }, "Gate", false));
    return layout;
}

DigProcessor::DigProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("Dig loader"),
      apvts (*this, nullptr, "Dig", makeLayout())
{
    startTimerHz (12);
    // dev hook: DIG_OPEN=/path/to/song opens it right away (used for screenshots and tests)
    const auto open = juce::SystemStats::getEnvironmentVariable ("DIG_OPEN", {});
    if (open.isNotEmpty()) loadFile (juce::File (open));
}

DigProcessor::~DigProcessor() { stopTimer(); stopThread (8000); }

void DigProcessor::prepareToPlay (double sampleRate, int) { sr = sampleRate; lastKey.clear(); for (auto& v : voices) v.on = false; }

// ---------------------------------------------------------------- loading (own thread)
void DigProcessor::loadFile (const juce::File& f, bool keepSettings)
{
    if (! f.existsAsFile()) { errorText = "File not found: " + f.getFileName(); state.store (State::Failed); return; }
    { const juce::ScopedLock l (pendingLock); pendingFile = f; pendingKeep = keepSettings; hasPending = true; }
    loadingName = f.getFileName();
    state.store (State::Loading);
    if (! isThreadRunning()) startThread();
}

void DigProcessor::run()
{
    for (;;)
    {
        juce::File f; bool keep = false;
        { const juce::ScopedLock l (pendingLock); if (! hasPending) return; f = pendingFile; keep = pendingKeep; hasPending = false; }
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
        if (reader == nullptr || reader->lengthInSamples < 1000 || reader->sampleRate < 8000.0)
        {
            juce::MessageManager::callAsync ([this, n = f.getFileName()] { errorText = "Cannot read " + n + " (WAV, AIFF, FLAC, OGG and MP3 work)."; state.store (State::Failed); });
            continue;
        }
        auto s = std::make_shared<Song>();
        s->sr = reader->sampleRate; s->file = f;
        const int n = (int) juce::jmin ((juce::int64) (maxSongSeconds * s->sr), reader->lengthInSamples);
        s->audio.setSize (juce::jlimit (1, 2, (int) reader->numChannels), n);
        reader->read (&s->audio, 0, n, 0, true, true);
        if (threadShouldExit()) return;
        s->an = Dig::analyse (s->audio, s->sr);
        const int cols = 900; s->peaks.resize ((size_t) cols);
        for (int c = 0; c < cols; ++c)
        {
            const int a = (int) ((juce::int64) c * n / cols), z = juce::jmax (a + 1, (int) ((juce::int64) (c + 1) * n / cols));
            float p = 0.0f; for (int ch = 0; ch < s->audio.getNumChannels(); ++ch) p = juce::jmax (p, s->audio.getMagnitude (ch, a, juce::jmin (n, z) - a));
            s->peaks[(size_t) c] = p;
        }
        if (threadShouldExit()) return;
        juce::MessageManager::callAsync ([this, s, keep]
        {
            { const juce::SpinLock::ScopedLockType l (songLock); song = s; }
            if (keep && restoreSettings && restoreBpm > 20.0) { songBpm = restoreBpm; firstBeat = restoreFirst; regionBeat = restoreBeat; }
            else
            {
                songBpm = s->an.bpm > 0.0 ? s->an.bpm : 100.0; firstBeat = s->an.firstBeat; regionBeat = 0;
                loopCandidates.clear(); loopIndex = -1; loopKey.clear();
                findLoop();                                   // start on the best spot instead of the first beat (often a quiet intro)
            }
            restoreSettings = false; lastKey.clear();
            songCount.fetch_add (1);
            state.store (State::Ready);
        });
    }
}

// ---------------------------------------------------------------- region
int DigProcessor::beatsNow() const { static const int b[4] = { 4, 8, 16, 32 }; return b[juce::jlimit (0, 3, (int) apvts.getRawParameterValue (kLength)->load())]; }
double DigProcessor::startSecNow() const { return firstBeat + regionBeat * 60.0 / juce::jmax (20.0, songBpm) + apvts.getRawParameterValue (kNudge)->load() * 0.001; }

void DigProcessor::setSongBpm (double bpm)
{
    bpm = juce::jlimit (40.0, 240.0, bpm);
    if (std::abs (bpm - songBpm) < 0.0005) return;
    const double t = firstBeat + regionBeat * 60.0 / juce::jmax (20.0, songBpm);
    songBpm = bpm;
    regionBeat = (int) std::round ((t - firstBeat) * songBpm / 60.0);
    loopKey.clear();
}

void DigProcessor::setRegionTime (double seconds)
{
    const auto s = getSong(); if (s == nullptr || songBpm < 20.0) return;
    const int maxBeat = (int) ((s->an.seconds - firstBeat) * songBpm / 60.0) - beatsNow();
    regionBeat = juce::jlimit ((int) std::floor (-firstBeat * songBpm / 60.0), juce::jmax (0, maxBeat), (int) std::round ((seconds - firstBeat) * songBpm / 60.0));
}

void DigProcessor::shiftRegion (int d) { setRegionTime (firstBeat + (regionBeat + d) * 60.0 / juce::jmax (20.0, songBpm)); }

void DigProcessor::findLoop()
{
    const auto s = getSong(); if (s == nullptr || songBpm < 20.0) return;
    const juce::String key = juce::String (songBpm, 2) + "|" + juce::String (beatsNow()) + "|" + juce::String (songCount.load());
    if (key != loopKey) { loopCandidates = Dig::findLoops (s->an, songBpm, firstBeat, beatsNow()); loopIndex = -1; loopKey = key; }
    if (loopCandidates.empty()) return;
    loopIndex = (loopIndex + 1) % (int) loopCandidates.size();
    regionBeat = loopCandidates[(size_t) loopIndex];
}

DigProcessor::Info DigProcessor::getInfo() const
{
    Info i; i.state = state.load(); i.hostBpm = hostBpm.load(); i.beats = beatsNow();
    i.mode = (int) apvts.getRawParameterValue (kMode)->load(); i.chops = (int) apvts.getRawParameterValue (kChops)->load();
    i.error = errorText; i.name = loadingName;
    if (const auto s = getSong())
    {
        if (i.state == State::Ready) i.name = s->file.getFileName();
        i.key = Dig::keyName (s->an.key); i.seconds = s->an.seconds; i.detectedBpm = s->an.bpm;
        i.songBpm = songBpm; i.firstBeat = firstBeat; i.startSec = startSecNow(); i.lenSec = i.beats * 60.0 / juce::jmax (20.0, songBpm);
    }
    return i;
}

// message thread: render again whenever the song, the region, a setting or the project tempo changed
void DigProcessor::timerCallback()
{
    if (state.load() != State::Ready) return;
    const auto s = getSong(); if (s == nullptr) return;
    Dig::RenderSettings rs;
    rs.startSec = startSecNow(); rs.songBpm = songBpm; rs.hostBpm = hostBpm.load(); rs.hostSr = sr; rs.beats = beatsNow();
    rs.mode = (int) apvts.getRawParameterValue (kMode)->load(); rs.chops = (int) apvts.getRawParameterValue (kChops)->load();
    rs.semitones = juce::roundToInt (apvts.getRawParameterValue (kPitch)->load()); rs.gainDb = apvts.getRawParameterValue (kLevel)->load();
    const juce::String key = juce::String (songCount.load()) + "|" + juce::String (rs.startSec, 4) + "|" + juce::String (rs.songBpm, 3) + "|" + juce::String (rs.hostBpm, 2) + "|" + juce::String (rs.hostSr, 0) + "|"
                           + juce::String (rs.beats) + "|" + juce::String (rs.mode) + "|" + juce::String (rs.chops) + "|" + juce::String (rs.semitones) + "|" + juce::String (rs.gainDb, 1);
    if (key == lastKey) return;
    lastKey = key;
    auto out = std::make_shared<Dig::Clip> (Dig::render (s->audio, s->sr, rs));
    {
        const juce::SpinLock::ScopedLockType l (clipLock);
        if (clip != nullptr) retired.push_back (clip);
        if (retired.size() > 6) retired.erase (retired.begin());
        clip = out;
    }
    clipChanged.store (true);
    renderCount.fetch_add (1);
}

// ---------------------------------------------------------------- files
juce::File DigProcessor::writeWav (const juce::AudioBuffer<float>& buf, int from, int to, const juce::String& name)
{
    if (to - from < 32) return {};
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Fatboard Dig");
    dir.createDirectory();
    const auto file = dir.getChildFile (juce::File::createLegalFileName (name) + ".wav");
    file.deleteFile();
    juce::WavAudioFormat wav;
    if (auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
        if (auto* writer = wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0))
        {
            stream.release();
            std::unique_ptr<juce::AudioFormatWriter> w (writer);
            w->writeFromAudioSampleBuffer (buf, from, to - from);
            w.reset();
            return file;
        }
    return {};
}

juce::File DigProcessor::writeLoopWav()
{
    const auto c = getClip(); const auto s = getSong(); if (c == nullptr || s == nullptr) return {};
    static const char* mode[3] = { "original", "repitch", "stretch" };
    const int m = juce::jlimit (0, 2, (int) apvts.getRawParameterValue (kMode)->load());
    const double bpm = m == Dig::Original ? songBpm : hostBpm.load();
    return writeWav (c->audio, 0, c->audio.getNumSamples(),
                     "Dig " + s->file.getFileNameWithoutExtension().substring (0, 40) + " " + juce::String (bpm, 1) + " bpm " + juce::String (beatsNow() / 4) + " bars " + mode[m] + " " + juce::String (++wavCounter));
}

juce::File DigProcessor::writeSliceWav (int i)
{
    const auto c = getClip(); const auto s = getSong(); if (c == nullptr || s == nullptr || i < 0 || i >= (int) c->slices.size()) return {};
    return writeWav (c->audio, c->slices[(size_t) i], c->sliceEnd (i), "Dig " + s->file.getFileNameWithoutExtension().substring (0, 40) + " chop " + juce::String (i + 1) + " " + juce::String (++wavCounter));
}

// ---------------------------------------------------------------- audio
void DigProcessor::startVoice (int slice, float gain, int delay, int note)
{
    if (audioClip == nullptr || slice < 0 || slice >= (int) audioClip->slices.size()) return;
    Voice* v = nullptr;
    for (auto& x : voices) if (! x.on) { v = &x; break; }
    if (v == nullptr) { v = &voices[0]; for (auto& x : voices) if (x.pos - x.delay > v->pos - v->delay) v = &x; }      // steal the one that has played longest
    v->on = true; v->slice = slice; v->pos = audioClip->slices[(size_t) slice]; v->end = audioClip->sliceEnd (slice);
    v->delay = delay; v->gain = gain; v->note = note; v->release = -1;
    playingSlice.store (slice);
}

void DigProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), nCh = juce::jmin (2, buffer.getNumChannels());
    buffer.clear();
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) if (auto b = pos->getBpm()) if (*b > 20.0) hostBpm.store (*b);

    if (clipChanged.load())
    {
        const juce::SpinLock::ScopedTryLockType l (clipLock);
        if (l.isLocked()) { audioClip = clip; clipChanged.store (false); for (auto& v : voices) v.on = false; loopPos = 0; }
    }
    const int lr = loopReq.exchange (-1);
    if (lr >= 0) { looping.store (lr == 1); loopPos = 0; }
    if (audioClip == nullptr || audioClip->audio.getNumSamples() < 64) { midi.clear(); return; }
    const auto& a = audioClip->audio; const int len = a.getNumSamples();
    const bool gate = apvts.getRawParameterValue (kGate)->load() > 0.5f;
    const int fadeOut = juce::jmax (8, (int) (0.003 * sr)), fadeIn = juce::jmax (4, (int) (0.001 * sr)), rel = juce::jmax (8, (int) (0.010 * sr));

    const int aud = auditionReq.exchange (-1);
    if (aud >= 0) startVoice (aud, 1.0f, 0, -1);
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int note = m.getNoteNumber(), slice = note >= 60 ? note - 60 : note - 36;       // C3 upwards, or a pad controller from C1
        if (m.isNoteOn()) startVoice (slice, 0.25f + 0.75f * m.getFloatVelocity(), juce::jlimit (0, n - 1, meta.samplePosition), note);
        else if (m.isNoteOff() && gate) { for (auto& v : voices) if (v.on && v.note == note && v.release < 0) v.release = rel; }
    }
    midi.clear();

    if (looping.load())
        for (int i = 0; i < n; ++i) { for (int ch = 0; ch < nCh; ++ch) buffer.addSample (ch, i, a.getSample (ch, loopPos)); if (++loopPos >= len) loopPos = 0; }

    for (auto& v : voices)
    {
        if (! v.on) continue;
        const int start = audioClip->slices[(size_t) juce::jlimit (0, (int) audioClip->slices.size() - 1, v.slice)];
        for (int i = 0; i < n; ++i)
        {
            if (v.delay > 0) { --v.delay; continue; }
            if (v.pos >= v.end || v.pos >= len) { v.on = false; break; }
            float g = v.gain;
            if (v.pos - start < fadeIn) g *= (float) (v.pos - start) / (float) fadeIn;
            if (v.end - v.pos < fadeOut) g *= (float) (v.end - v.pos) / (float) fadeOut;
            if (v.release >= 0) { g *= (float) v.release / (float) rel; if (--v.release <= 0) { v.on = false; break; } }
            for (int ch = 0; ch < nCh; ++ch) buffer.addSample (ch, i, a.getSample (ch, v.pos) * g);
            ++v.pos;
        }
    }
    bool any = false; for (auto& v : voices) any = any || v.on;
    if (! any) playingSlice.store (-1);
}

juce::AudioProcessorEditor* DigProcessor::createEditor() { return new DigEditor (*this); }

void DigProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto st = apvts.copyState();
    if (const auto s = getSong())
    {
        st.setProperty ("file", s->file.getFullPathName(), nullptr);
        st.setProperty ("songBpm", songBpm, nullptr); st.setProperty ("firstBeat", firstBeat, nullptr); st.setProperty ("regionBeat", regionBeat, nullptr);
    }
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, destData);
}

void DigProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto st = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (st);
            const juce::String path = st.getProperty ("file").toString();
            if (path.isNotEmpty())
            {
                restoreSettings = true; restoreBpm = (double) st.getProperty ("songBpm", 0.0); restoreFirst = (double) st.getProperty ("firstBeat", 0.0); restoreBeat = (int) st.getProperty ("regionBeat", 0);
                loadFile (juce::File (path), true);
            }
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DigProcessor(); }
