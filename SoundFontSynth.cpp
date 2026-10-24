#include "SoundFontSynth.h"
#include "ThirdParty/tsf.h"

namespace pt
{
namespace
{
struct MemStream
{
    const unsigned char* data; size_t size, pos;
};
int memRead (void* d, void* ptr, unsigned int n)
{
    auto* m = (MemStream*) d;
    const size_t left = m->size - m->pos;
    const size_t k = n < left ? n : left;
    if (k > 0) std::memcpy (ptr, m->data + m->pos, k);
    m->pos += k;
    return (int) k;
}
int memSkip (void* d, unsigned int n)
{
    auto* m = (MemStream*) d;
    if (n > m->size - m->pos) return 0;
    m->pos += n;
    return 1;
}

struct FileStream { juce::FileInputStream* in; };
int fileRead (void* d, void* ptr, unsigned int n) { return ((FileStream*) d)->in->read (ptr, (int) n); }
int fileSkip (void* d, unsigned int n)
{
    auto* in = ((FileStream*) d)->in;
    return in->setPosition (in->getPosition() + (juce::int64) n) ? 1 : 0;
}
}

SoundFontSynth::SoundFontSynth() = default;

SoundFontSynth::~SoundFontSynth()
{
    closeFont (font);
}

void SoundFontSynth::closeFont (tsf* f)
{
    if (f != nullptr) tsf_close (f);
}

void SoundFontSynth::prepare (double sr, int maxBlock)
{
    sampleRate = sr;
    scratch.assign ((size_t) juce::jmax (64, maxBlock) * 2, 0.f);
    builtin.prepare (sr);

    const juce::SpinLock::ScopedLockType sl (lock);
    if (font != nullptr)
        tsf_set_output (font, TSF_STEREO_INTERLEAVED, (int) sr, 0.f);
}

//==============================================================================
static bool finishLoad (tsf* f, SoundFontSynth::Loaded& out, const juce::String& name, juce::String& error)
{
    if (f == nullptr) { error = "Not a valid SoundFont (.sf2) file"; return false; }
    const int count = tsf_get_presetcount (f);
    if (count <= 0) { tsf_close (f); error = "SoundFont contains no instruments"; return false; }

    out.font = f;
    out.name = name;
    out.presets.clear();
    for (int i = 0; i < count; ++i)
        out.presets.add (juce::String (tsf_get_presetname (f, i)).trim());
    return true;
}

bool SoundFontSynth::load (const juce::File& file, Loaded& out, juce::String& error) const
{
    if (! file.existsAsFile()) { error = "File not found"; return false; }
    juce::FileInputStream in (file);
    if (! in.openedOk()) { error = "Cannot open file"; return false; }

    FileStream fs { &in };
    tsf_stream stream { &fs, &fileRead, &fileSkip };
    return finishLoad (tsf_load (&stream), out, file.getFileNameWithoutExtension(), error);
}

bool SoundFontSynth::loadMemory (const void* data, size_t size, Loaded& out, juce::String& error) const
{
    MemStream ms { (const unsigned char*) data, size, 0 };
    tsf_stream stream { &ms, &memRead, &memSkip };
    return finishLoad (tsf_load (&stream), out, "Embedded", error);
}

void SoundFontSynth::discard (Loaded& l)
{
    if (l.font != nullptr) tsf_close (l.font);
    l.font = nullptr;
}

void SoundFontSynth::install (Loaded&& l)
{
    tsf* old = nullptr;

    if (l.font != nullptr)
    {
        tsf_set_output (l.font, TSF_STEREO_INTERLEAVED, (int) sampleRate, 0.f);
        tsf_set_max_voices (l.font, 64);
        tsf_channel_set_presetindex (l.font, 0, 0);
    }

    {
        const juce::SpinLock::ScopedLockType sl (lock);
        old = font;
        font = l.font;
        currentPreset = l.font != nullptr ? 0 : -1;
        fontLoaded.store (l.font != nullptr);
    }
    presetNames = l.font != nullptr ? l.presets : juce::StringArray();
    fontName    = l.font != nullptr ? l.name : juce::String();
    l.font = nullptr;

    closeFont (old);                 // outside the lock: tsf_close frees a lot of memory
}

//==============================================================================
void SoundFontSynth::setPreset (int idx)
{
    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked() || font == nullptr || idx == currentPreset) return;
    if (idx < 0 || idx >= tsf_get_presetcount (font)) return;
    tsf_channel_note_off_all (font, 0);
    tsf_channel_set_presetindex (font, 0, idx);
    currentPreset = idx;
}

void SoundFontSynth::handleMidi (const juce::MidiMessage& m)
{
    const juce::SpinLock::ScopedTryLockType sl (lock);
    const bool useFont = sl.isLocked() && font != nullptr && ! forceBuiltin.load();

    if (m.isNoteOn())
    {
        if (useFont) tsf_channel_note_on (font, 0, m.getNoteNumber(), m.getFloatVelocity());
        else         builtin.noteOn (m.getNoteNumber(), m.getVelocity());
    }
    else if (m.isNoteOff())
    {
        if (useFont) tsf_channel_note_off (font, 0, m.getNoteNumber());
        else         builtin.noteOff (m.getNoteNumber());
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber(), v = m.getControllerValue();
        if (cc == 64) builtin.setSustain (v >= 64);
        if (sl.isLocked() && font != nullptr)
            tsf_channel_midi_control (font, 0, cc, v);
    }
    else if (m.isPitchWheel())
    {
        if (sl.isLocked() && font != nullptr) tsf_channel_set_pitchwheel (font, 0, m.getPitchWheelValue());
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        if (sl.isLocked() && font != nullptr) tsf_channel_sounds_off_all (font, 0);
        builtin.allOff();
    }
}

void SoundFontSynth::allNotesOff()
{
    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (sl.isLocked() && font != nullptr) tsf_channel_sounds_off_all (font, 0);
    builtin.allOff();
}

void SoundFontSynth::render (float* left, float* right, int numSamples)
{
    const juce::SpinLock::ScopedTryLockType sl (lock);

    if (sl.isLocked() && font != nullptr && ! forceBuiltin.load())
    {
        const int chunk = (int) (scratch.size() / 2);
        for (int done = 0; done < numSamples; done += chunk)
        {
            const int n = juce::jmin (chunk, numSamples - done);
            tsf_render_float (font, scratch.data(), n, 0);
            for (int i = 0; i < n; ++i)
            {
                left [done + i] += scratch[(size_t) i * 2];
                right[done + i] += scratch[(size_t) i * 2 + 1];
            }
        }
    }
    builtin.render (left, right, numSamples);   // idle voices cost nothing; also covers the fallback
}
} // namespace pt
