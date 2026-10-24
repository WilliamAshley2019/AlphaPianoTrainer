#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "Lesson.h"
#include "LessonLibrary.h"
#include "PracticeEngine.h"
#include "SoundFontSynth.h"
#include "ProgressStore.h"
#include "Coach.h"

namespace pt
{
struct LessonEntry
{
    juce::String id, title, category, blurb;
    int  level = 1;
    bool user = false;
};

class PianoTrainerProcessor final : public juce::AudioProcessor,
                                    public juce::ChangeBroadcaster,
                                    private juce::Timer,
                                    private juce::AsyncUpdater,
                                    private juce::AudioProcessorValueTreeState::Listener
{
public:
    PianoTrainerProcessor();
    ~PianoTrainerProcessor() override;

    // AudioProcessor ------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Lessons (message thread) ----------------------------------------------------
    std::vector<LessonEntry> getLessonList() const;
    bool selectLesson (const juce::String& id);
    const juce::String& getLessonId() const noexcept { return currentLessonId; }
    const Session* getSession() const noexcept { return sessions.empty() ? nullptr : sessions.back().s.get(); }
    bool addMidiFile (const juce::File& f, bool select = true);
    int  addMidiFolder (const juce::File& dir);
    void removeUserLesson (const juce::String& id);
    void setLoop (double startBeat, double endBeat);
    double getLoopStart() const noexcept { return loopStart.load(); }
    double getLoopEnd()   const noexcept { return loopEnd.load(); }
    double getEffectiveBpm() const noexcept { return effectiveBpm.load(); }

    // Sound -----------------------------------------------------------------------
    void loadSoundFont (const juce::File& f);
    void useBuiltinSound();
    juce::File getSoundFontFile() const { return sfFile; }
    juce::Array<juce::File> findSoundFonts() const;
    SoundFontSynth& getSynth() noexcept { return synth; }

    // On-screen keyboard
    void virtualNote (int pitch, int velocity, bool on);

    // Results / status
    const RunResult& getLastResult() const noexcept { return lastResult; }
    int getResultSerial() const noexcept { return resultSerial; }
    juce::String getStatusText() const { return statusText; }
    void postStatus (const juce::String& s) { statusText = s; sendChangeMessage(); }

    juce::AudioProcessorValueTreeState apvts;
    PracticeEngine engine;
    ProgressStore  progress;

    std::atomic<float> peakL { 0.f }, peakR { 0.f };
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<bool>   hostPlaying { false };

private:
    struct Slot { std::unique_ptr<Session> s; juce::uint32 supersededAt = 0; };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void publishSession();
    void commitRun();
    void timerCallback() override;
    void handleAsyncUpdate() override;
    void parameterChanged (const juce::String& id, float) override;
    void applyState (const juce::ValueTree& vt);
    void renderClicks (float* l, float* r, int n);

    // parameter pointers
    std::atomic<float> *pSpeed, *pFollow, *pSync, *pMode, *pHand, *pLoop, *pMetro, *pCountIn, *pTranspose,
                       *pBacking, *pVolume, *pMetroVol, *pTiming, *pBuiltin, *pPreset, *pAutoSpeed;

    // lesson / session (message thread)
    Lesson baseLesson;
    juce::String currentLessonId;
    std::vector<Slot> sessions;
    juce::Array<juce::File> userFiles;
    juce::StringArray userFolders;
    std::atomic<double> loopStart { 0.0 }, loopEnd { 0.0 };
    std::atomic<double> effectiveBpm { 100.0 };

    // audio <-> message
    std::atomic<Session*> pending { nullptr }, activeSession { nullptr };
    Session* attached = nullptr;
    bool lastHostPlaying = false;
    float lastGain = 1.f;

    juce::AbstractFifo vFifo { 256 };
    struct VKey { std::uint8_t pitch, vel; bool on; } vBuf[256];

    juce::MidiBuffer userMidi, merged;
    SoundFontSynth synth;
    juce::File sfFile;
    juce::ThreadPool loader { 1 };

    struct ClickVoice { double phase = 0, inc = 0, env = 0, amp = 0; } click;
    double sr = 44100.0;
    double clickDecay = 0.998;

    RunResult lastResult;
    int resultSerial = 0, lastFinished = 0;
    double lastTickMs = 0.0;
    juce::String statusText;

    JUCE_DECLARE_WEAK_REFERENCEABLE (PianoTrainerProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoTrainerProcessor)
};
} // namespace pt
