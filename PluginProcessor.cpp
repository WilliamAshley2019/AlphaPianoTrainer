#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Theory.h"

namespace pt
{
//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout PianoTrainerProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using B = juce::AudioParameterBool;
    using C = juce::AudioParameterChoice;
    using I = juce::AudioParameterInt;

    juce::AudioProcessorValueTreeState::ParameterLayout l;

    auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; };
    auto pct01 = [] (float v, int) { return juce::String (juce::roundToInt (v * 100.f)) + "%"; };
    auto db = [] (float v, int) { return juce::String (v, 1) + " dB"; };
    l.add (std::make_unique<F> (pid::speed, "Speed", juce::NormalisableRange<float> (25.f, 150.f, 1.f), 100.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<B> (pid::followHost, "Follow DAW Tempo", false));
    l.add (std::make_unique<B> (pid::syncTransport, "Follow DAW Transport", false));
    l.add (std::make_unique<C> (pid::mode, "Mode", juce::StringArray { "Play", "Wait", "Watch" }, 0));
    l.add (std::make_unique<C> (pid::hand, "Hands", juce::StringArray { "Both", "Right", "Left" }, 0));
    l.add (std::make_unique<B> (pid::loop, "Loop", false));
    l.add (std::make_unique<B> (pid::metronome, "Metronome", true));
    l.add (std::make_unique<I> (pid::countIn, "Count-in Bars", 0, 4, 1));
    l.add (std::make_unique<I> (pid::transpose, "Transpose", -12, 12, 0));
    l.add (std::make_unique<F> (pid::backing, "Backing Level", juce::NormalisableRange<float> (0.f, 1.f), 0.6f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct01)));
    l.add (std::make_unique<F> (pid::volume, "Volume", juce::NormalisableRange<float> (-24.f, 6.f, 0.1f), 0.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    l.add (std::make_unique<F> (pid::metroVol, "Click Level", juce::NormalisableRange<float> (0.f, 1.f), 0.5f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct01)));
    l.add (std::make_unique<C> (pid::timing, "Timing Strictness", juce::StringArray { "Relaxed", "Normal", "Strict" }, 1));

    juce::StringArray scales;
    for (auto* const* n = theory::scaleTypeNames(); *n != nullptr; ++n) scales.add (*n);
    l.add (std::make_unique<C> (pid::scaleType, "Scale", scales, 0));

    juce::StringArray roots { "Auto" };
    for (int i = 0; i < 12; ++i) roots.add (theory::pcName (i));
    l.add (std::make_unique<C> (pid::scaleRoot, "Scale Root", roots, 0));
    l.add (std::make_unique<B> (pid::showScale, "Show Scale", false));
    l.add (std::make_unique<B> (pid::builtinOnly, "Built-in Piano Only", false));
    l.add (std::make_unique<I> (pid::preset, "Instrument", 0, 1023, 0));
    l.add (std::make_unique<F> (pid::lookahead, "Roll Window", juce::NormalisableRange<float> (2.f, 12.f, 0.5f), 5.f,
                                juce::AudioParameterFloatAttributes().withLabel ("beats")));
    l.add (std::make_unique<B> (pid::labels, "Note Names", true));
    l.add (std::make_unique<B> (pid::fingers, "Fingering", true));
    l.add (std::make_unique<B> (pid::autoSpeed, "Auto Speed-up", false));
    return l;
}

//==============================================================================
PianoTrainerProcessor::PianoTrainerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PianoTrainer", createLayout())
{
    auto raw = [this] (const juce::ParameterID& p) { return apvts.getRawParameterValue (p.getParamID()); };
    pSpeed = raw (pid::speed); pFollow = raw (pid::followHost); pSync = raw (pid::syncTransport);
    pMode = raw (pid::mode); pHand = raw (pid::hand); pLoop = raw (pid::loop); pMetro = raw (pid::metronome);
    pCountIn = raw (pid::countIn); pTranspose = raw (pid::transpose); pBacking = raw (pid::backing);
    pVolume = raw (pid::volume); pMetroVol = raw (pid::metroVol); pTiming = raw (pid::timing);
    pBuiltin = raw (pid::builtinOnly); pPreset = raw (pid::preset); pAutoSpeed = raw (pid::autoSpeed);

    apvts.addParameterListener (pid::transpose.getParamID(), this);

    userMidi.ensureSize (16384);
    merged.ensureSize (32768);

    selectLesson ("b:fs.rh");
    startTimerHz (20);
    lastTickMs = juce::Time::getMillisecondCounterHiRes();
}

PianoTrainerProcessor::~PianoTrainerProcessor()
{
    stopTimer();
    cancelPendingUpdate();
    apvts.removeParameterListener (pid::transpose.getParamID(), this);
    loader.removeAllJobs (true, 20000);
    progress.save();
}

juce::AudioProcessorEditor* PianoTrainerProcessor::createEditor() { return new PianoTrainerEditor (*this); }

bool PianoTrainerProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PianoTrainerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    synth.prepare (sampleRate, juce::jmax (512, samplesPerBlock));
    engine.prepare (sampleRate);
    clickDecay = std::exp (-1.0 / (0.012 * sampleRate));
    click = {};
    lastGain = 1.f;
}

//==============================================================================
// Audio
//==============================================================================
void PianoTrainerProcessor::renderClicks (float* l, float* r, int n)
{
    const int nc = engine.numClicks();
    const float vol = pMetroVol->load() * 0.45f;
    int ci = 0;
    for (int i = 0; i < n; ++i)
    {
        while (ci < nc && engine.clicks()[ci].offset <= i)
        {
            const bool accent = engine.clicks()[ci].accent;
            click.env = 1.0; click.phase = 0.0;
            click.inc = juce::MathConstants<double>::twoPi * (accent ? 1568.0 : 1046.5) / sr;
            click.amp = accent ? 1.0 : 0.65;
            ++ci;
        }
        if (click.env > 1e-4)
        {
            const float s = (float) (std::sin (click.phase) * click.env * click.amp) * vol;
            l[i] += s; r[i] += s;
            click.phase += click.inc;
            click.env *= clickDecay;
        }
    }
}

void PianoTrainerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (buffer.getNumChannels() < 2) return;

    // --- session hand-over -----------------------------------------------------------
    if (auto* p = pending.load (std::memory_order_acquire); p != attached)
    {
        attached = p;
        activeSession.store (p, std::memory_order_release);
        engine.attach (p);
        synth.allNotesOff();
    }

    // --- host info -----------------------------------------------------------------------
    double hBpm = 0.0;
    bool hPlay = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) hBpm = *b;
            hPlay = pos->getIsPlaying();
        }
    hostBpm.store (hBpm, std::memory_order_relaxed);
    hostPlaying.store (hPlay, std::memory_order_relaxed);

    if (pSync->load() > 0.5f)
    {
        if (hPlay && ! lastHostPlaying) engine.cmdPlay();
        else if (! hPlay && lastHostPlaying) engine.cmdStop();
    }
    lastHostPlaying = hPlay;

    // --- user MIDI (+ on-screen keys) ------------------------------------------------------
    userMidi.clear();
    userMidi.addEvents (midi, 0, n, 0);
    {
        const auto r = vFifo.read (vFifo.getNumReady());
        auto push = [&] (int idx)
        {
            const auto& k = vBuf[idx];
            userMidi.addEvent (k.on ? juce::MidiMessage::noteOn (1, k.pitch, k.vel)
                                    : juce::MidiMessage::noteOff (1, k.pitch), 0);
        };
        for (int i = 0; i < r.blockSize1; ++i) push (r.startIndex1 + i);
        for (int i = 0; i < r.blockSize2; ++i) push (r.startIndex2 + i);
    }
    midi.clear();

    // --- practice engine -----------------------------------------------------------------------
    PracticeParams pp;
    pp.sampleRate = sr;
    const double lessonBpm = attached != nullptr ? attached->lesson.bpm : 100.0;
    const double base = (pFollow->load() > 0.5f && hBpm > 0.0) ? hBpm : lessonBpm;
    pp.bpm = juce::jlimit (20.0, 400.0, base * (double) pSpeed->load() / 100.0);
    effectiveBpm.store (pp.bpm, std::memory_order_relaxed);
    pp.hand = (int) pHand->load();
    pp.mode = (int) pMode->load();
    pp.loop = pLoop->load() > 0.5f;
    pp.loopStart = loopStart.load();
    pp.loopEnd = loopEnd.load();
    static const double perfect[] = { 70.0, 45.0, 30.0 }, good[] = { 160.0, 120.0, 80.0 };
    const int t = juce::jlimit (0, 2, (int) pTiming->load());
    pp.perfectMs = perfect[t]; pp.goodMs = good[t];
    pp.metronome = pMetro->load() > 0.5f;
    pp.countInBars = (int) pCountIn->load();
    pp.backingGain = pBacking->load();
    engine.process (pp, userMidi, n);

    // --- merge everything for the synth -------------------------------------------------------
    merged.clear();
    for (const auto meta : userMidi) merged.addEvent (meta.getMessage(), meta.samplePosition);
    for (int i = 0; i < engine.numEvents(); ++i)
    {
        const auto& e = engine.events()[i];
        merged.addEvent (e.on ? juce::MidiMessage::noteOn (1, (int) e.pitch, e.velocity)
                              : juce::MidiMessage::noteOff (1, (int) e.pitch), e.offset);
    }

    synth.setPreset ((int) pPreset->load());
    synth.setUseBuiltin (pBuiltin->load() > 0.5f);

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    int pos = 0;
    for (const auto meta : merged)
    {
        const int sp = juce::jlimit (pos, n, meta.samplePosition);
        if (sp > pos) synth.render (L + pos, R + pos, sp - pos);
        pos = sp;
        synth.handleMidi (meta.getMessage());
    }
    if (pos < n) synth.render (L + pos, R + pos, n - pos);

    renderClicks (L, R, n);

    const float g = juce::Decibels::decibelsToGain (pVolume->load());
    buffer.applyGainRamp (0, n, lastGain, g);
    lastGain = g;

    peakL.store (juce::jmax (peakL.load() * 0.9f, buffer.getMagnitude (0, 0, n)), std::memory_order_relaxed);
    peakR.store (juce::jmax (peakR.load() * 0.9f, buffer.getMagnitude (1, 0, n)), std::memory_order_relaxed);
}

//==============================================================================
// Lessons
//==============================================================================
std::vector<LessonEntry> PianoTrainerProcessor::getLessonList() const
{
    std::vector<LessonEntry> v;
    for (auto& i : library::all())
        v.push_back ({ i.id, i.title, i.category, i.blurb, i.level, false });
    for (auto& f : userFiles)
        v.push_back ({ "f:" + f.getFullPathName(), f.getFileNameWithoutExtension(), "Your MIDI files",
                       f.getParentDirectory().getFileName() + " / " + f.getFileName(), 2, true });
    return v;
}

void PianoTrainerProcessor::publishSession()
{
    const int tr = (int) pTranspose->load();
    auto s = std::make_unique<Session> (baseLesson.transposed (tr));
    auto* raw = s.get();

    const auto now = juce::Time::getMillisecondCounter();
    if (! sessions.empty()) sessions.back().supersededAt = now;
    sessions.push_back ({ std::move (s), 0 });
    pending.store (raw, std::memory_order_release);
}

bool PianoTrainerProcessor::selectLesson (const juce::String& id)
{
    Lesson l;
    if (id.startsWith ("f:"))
    {
        juce::String err;
        if (! importMidiFile (juce::File (id.substring (2)), l, err))
        {
            postStatus ("Could not open lesson: " + err);
            return false;
        }
    }
    else if (! library::build (id, l))
        return false;

    baseLesson = std::move (l);
    currentLessonId = id;
    loopStart.store (0.0);
    loopEnd.store (baseLesson.lengthBeats);
    publishSession();
    engine.cmdStop();
    statusText = {};
    sendChangeMessage();
    return true;
}

void PianoTrainerProcessor::parameterChanged (const juce::String& id, float)
{
    if (id == pid::transpose.getParamID()) triggerAsyncUpdate();
}

void PianoTrainerProcessor::handleAsyncUpdate()
{
    publishSession();
    engine.cmdStop();
    sendChangeMessage();
}

bool PianoTrainerProcessor::addMidiFile (const juce::File& f, bool select)
{
    Lesson probe; juce::String err;
    if (! importMidiFile (f, probe, err)) { postStatus (f.getFileName() + ": " + err); return false; }
    if (! userFiles.contains (f)) userFiles.add (f);
    if (select) selectLesson ("f:" + f.getFullPathName());
    else sendChangeMessage();
    return true;
}

int PianoTrainerProcessor::addMidiFolder (const juce::File& dir)
{
    int added = 0;
    for (auto& f : dir.findChildFiles (juce::File::findFiles, true, "*.mid;*.midi"))
    {
        if (added >= 500) break;
        if (! userFiles.contains (f)) { userFiles.add (f); ++added; }
    }
    if (! userFolders.contains (dir.getFullPathName())) userFolders.add (dir.getFullPathName());
    sendChangeMessage();
    return added;
}

void PianoTrainerProcessor::removeUserLesson (const juce::String& id)
{
    userFiles.removeFirstMatchingValue (juce::File (id.substring (2)));
    sendChangeMessage();
}

void PianoTrainerProcessor::setLoop (double a, double b)
{
    const double len = baseLesson.lengthBeats;
    a = juce::jlimit (0.0, len, a); b = juce::jlimit (0.0, len, b);
    if (b - a < 0.5) { b = juce::jmin (len, a + 0.5); a = juce::jmax (0.0, b - 0.5); }
    loopStart.store (a); loopEnd.store (b);
}

//==============================================================================
// Sound
//==============================================================================
void PianoTrainerProcessor::loadSoundFont (const juce::File& f)
{
    postStatus ("Loading " + f.getFileName() + "...");
    juce::WeakReference<PianoTrainerProcessor> weak (this);
    loader.addJob ([this, weak, f]
    {
        SoundFontSynth::Loaded l;
        juce::String err;
        const bool ok = synth.load (f, l, err);
        juce::MessageManager::callAsync ([weak, f, l, ok, err]() mutable
        {
            if (weak == nullptr) { SoundFontSynth::discard (l); return; }
            auto* self = weak.get();
            if (ok)
            {
                self->synth.install (std::move (l));
                self->sfFile = f;
                self->postStatus ("SoundFont ready: " + self->synth.getFontName());
            }
            else self->postStatus ("SoundFont failed: " + err);
        });
    });
}

void PianoTrainerProcessor::useBuiltinSound()
{
    synth.install ({});
    sfFile = juce::File();
    postStatus ("Using the built-in piano");
}

juce::Array<juce::File> PianoTrainerProcessor::findSoundFonts() const
{
    juce::Array<juce::File> out;
    const juce::File dirs[] = {
        juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("AlphaAudio/PianoTrainer"),
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("PianoTrainer"),
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("SoundFonts") };
    for (auto& d : dirs)
        out.addArray (d.findChildFiles (juce::File::findFiles, false, "*.sf2"));
    return out;
}

void PianoTrainerProcessor::virtualNote (int pitch, int velocity, bool on)
{
    if (vFifo.getFreeSpace() < 1) return;
    const auto w = vFifo.write (1);
    const VKey k { (std::uint8_t) juce::jlimit (0, 127, pitch), (std::uint8_t) juce::jlimit (1, 127, velocity), on };
    vBuf[w.blockSize1 > 0 ? w.startIndex1 : w.startIndex2] = k;
}

//==============================================================================
// Results, housekeeping
//==============================================================================
void PianoTrainerProcessor::commitRun()
{
    auto* s = activeSession.load (std::memory_order_acquire);
    if (s == nullptr || s->lesson.notes.empty()) return;

    const int mode = (int) pMode->load(), hand = (int) pHand->load();
    const float prevBest = progress.get (currentLessonId).bestAccuracy;
    lastResult = buildResult (*s, engine.stats, mode, hand, effectiveBpm.load(), prevBest);
    ++resultSerial;

    progress.recordRun (currentLessonId, lastResult.accuracy, effectiveBpm.load(), mode == modePlay);

    if (mode == modePlay && pAutoSpeed->load() > 0.5f && lastResult.accuracy >= 0.9f)
        if (auto* param = apvts.getParameter (pid::speed.getParamID()))
            param->setValueNotifyingHost (param->convertTo0to1 (juce::jmin (150.f, pSpeed->load() + 5.f)));

    sendChangeMessage();
}

void PianoTrainerProcessor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (engine.playing.load()) progress.addPracticeSeconds (juce::jlimit (0.0, 0.5, (now - lastTickMs) * 0.001));
    lastTickMs = now;

    if (const int fc = engine.finishedCount.load(); fc != lastFinished)
    {
        lastFinished = fc;
        commitRun();
    }

    // retire sessions the audio thread has long moved on from
    const auto t = juce::Time::getMillisecondCounter();
    auto* act = activeSession.load();
    for (size_t i = 0; i + 1 < sessions.size();)
    {
        auto& sl = sessions[i];
        if (sl.supersededAt != 0 && t - sl.supersededAt > 3000 && sl.s.get() != act)
            sessions.erase (sessions.begin() + (long) i);
        else ++i;
    }
}

//==============================================================================
// State
//==============================================================================
void PianoTrainerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("lesson", currentLessonId, nullptr);
    state.setProperty ("loopStart", loopStart.load(), nullptr);
    state.setProperty ("loopEnd", loopEnd.load(), nullptr);
    state.setProperty ("soundFont", sfFile.getFullPathName(), nullptr);
    juce::StringArray files;
    for (auto& f : userFiles) files.add (f.getFullPathName());
    state.setProperty ("userFiles", files.joinIntoString ("\n"), nullptr);
    state.setProperty ("userFolders", userFolders.joinIntoString ("\n"), nullptr);

    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void PianoTrainerProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    const auto vt = juce::ValueTree::fromXml (*xml);

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        applyState (vt);
    else
    {
        juce::WeakReference<PianoTrainerProcessor> weak (this);
        juce::MessageManager::callAsync ([weak, vt] { if (weak != nullptr) weak->applyState (vt); });
    }
}

void PianoTrainerProcessor::applyState (const juce::ValueTree& vt)
{
    apvts.replaceState (vt);

    userFiles.clear();
    for (auto& p : juce::StringArray::fromLines (vt.getProperty ("userFiles").toString()))
        if (p.isNotEmpty() && juce::File (p).existsAsFile()) userFiles.add (juce::File (p));
    userFolders = juce::StringArray::fromLines (vt.getProperty ("userFolders").toString());

    const auto lesson = vt.getProperty ("lesson").toString();
    if (lesson.isNotEmpty()) selectLesson (lesson);
    if (vt.hasProperty ("loopEnd")) setLoop ((double) vt.getProperty ("loopStart"), (double) vt.getProperty ("loopEnd"));

    const juce::File sf (vt.getProperty ("soundFont").toString());
    if (sf.existsAsFile()) loadSoundFont (sf);
    sendChangeMessage();
}
} // namespace pt

//==============================================================================
// Entry point the plugin wrappers (VST3 / Standalone) call to create the processor.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new pt::PianoTrainerProcessor();
}
