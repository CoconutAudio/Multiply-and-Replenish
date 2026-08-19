#pragma once

#include "model/Synthesiser.h"
#include "model/VoiceModel.h"
#include "model/VoiceModelLibrary.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <memory>

namespace rvctuner
{
/** @brief Which algorithms a run uses. */
struct EngineOptions
{
    /** @brief The networks that can hear the melody, in the order they are offered.

        FCPE comes first because it stands on its own; RMVPE is steadier on a noisy take but,
        unless it has been exported by itself, it arrives inside an RVC voice.
    */
    enum class Detector
    {
        fcpe,
        rmvpe
    };

    /** @brief The engines that can sing it back. */
    enum class Engine
    {
        /** @brief A mel vocoder, which keeps whichever voice was recorded and needs no model of it. */
        melVocoder,

        /** @brief An RVC voice, which sings in the voice its weights were trained on. */
        voiceModel
    };

    Detector detector { Detector::fcpe };
    Engine engine { Engine::melVocoder };

    juce::String voiceName;

    float retrievalRatio { 0.75f };
    float consonantProtection { 0.33f };

    /** @brief Threads the networks may use. Zero leaves a couple of cores for the audio thread,
               which is what stops a render from making playback stutter.
    */
    int numThreads { 0 };

    [[nodiscard]] static juce::StringArray getDetectorNames();
    [[nodiscard]] static juce::StringArray getEngineNames();
};

/** @brief Runs everything slow: loading the networks, hearing the melody, reading the recording.

    One run at a time, on its own thread, reporting back on the message thread. The melody arrives
    before the engine has finished reading the recording, so the editor can be drawn and edited
    while the rest is still loading.
*/
class Pipeline final : private juce::Thread,
                       private juce::AsyncUpdater
{
public:
    Pipeline();
    ~Pipeline() override;

    /** @brief Notified on the message thread. */
    struct Listener
    {
        virtual ~Listener() = default;

        /** @brief Progress through the current stage, named for the user. */
        virtual void pipelineProgressed (float fraction, const juce::String& stage) = 0;

        /** @brief The melody has been heard, and can be edited while the engine loads. */
        virtual void melodyEstimated (PitchTrack melody) = 0;

        /** @brief The engine is ready to render, or the run failed. */
        virtual void pipelineFinished (std::shared_ptr<Synthesiser> synthesiser, const juce::String& error) = 0;
    };

    void addListener (Listener* listener);
    void removeListener (Listener* listener);

    /** @brief Starts a run, cancelling whatever was running. */
    void start (const juce::AudioBuffer<float>& recording, double sampleRate, const EngineOptions& options);

    /** @brief Stops the run and waits for the thread to notice. */
    void cancel();

    [[nodiscard]] bool isRunning() const { return isThreadRunning(); }

    /** @brief The voices installed on this machine, for the engine that needs one. */
    [[nodiscard]] VoiceModelLibrary& getVoiceLibrary() noexcept { return voices; }

    /** @brief Where the mel vocoder and the detectors are looked for. */
    [[nodiscard]] juce::File findModelFile (const juce::String& relativePath) const;

private:
    void run() override;
    void handleAsyncUpdate() override;

    void report (float fraction, const juce::String& stage);

    EngineOptions options;

    juce::AudioBuffer<float> source;
    std::vector<float> mono;
    double sampleRate { 44100.0 };

    std::unique_ptr<VoiceModel> voiceModel;
    std::shared_ptr<Synthesiser> synthesiser;
    PitchTrack melody;

    VoiceModelLibrary voices;

    juce::CriticalSection stateLock;
    float progressFraction { 0.0f };
    juce::String progressStage;
    juce::String error;
    bool hasMelody { false };
    bool hasFinished { false };

    std::atomic<bool> shouldAbort { false };

    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Pipeline)
};
}
