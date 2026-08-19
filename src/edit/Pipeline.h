#pragma once

#include "model/ModelLibrary.h"
#include "model/Synthesiser.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <memory>

namespace tuner
{
/** @brief Which networks a run uses. */
struct EngineOptions
{
    /** @brief The networks that can hear the melody, in the order they are offered.

        FCPE is lighter and steadier on breathy singing; RMVPE holds up better against noise.
    */
    enum class Detector
    {
        fcpe,
        rmvpe
    };

    Detector detector { Detector::fcpe };

    int numThreads { 0 };

    [[nodiscard]] static juce::StringArray getDetectorNames();
};

/** @brief Runs everything slow: loading the networks, hearing the melody, reading the recording.

    One run at a time, on its own thread, reporting back on the message thread. The melody arrives
    before the vocoder has finished reading the recording, so the editor can be drawn and edited
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

        /** @brief The melody has been heard, and can be edited while the vocoder loads. */
        virtual void melodyEstimated (PitchTrack melody) = 0;

        /** @brief The vocoder is ready to render, or the run failed. */
        virtual void pipelineFinished (std::shared_ptr<Synthesiser> synthesiser, const juce::String& error) = 0;
    };

    void addListener (Listener* listener);
    void removeListener (Listener* listener);

    /** @brief Starts a run, cancelling whatever was running. */
    void start (const juce::AudioBuffer<float>& recording, double sampleRate, const EngineOptions& options);

    /** @brief Stops the run and waits for the thread to notice. */
    void cancel();

    [[nodiscard]] bool isRunning() const { return isThreadRunning(); }

    [[nodiscard]] const ModelLibrary& getModelLibrary() const noexcept { return models; }

private:
    void run() override;
    void handleAsyncUpdate() override;

    void report (float fraction, const juce::String& stage);

    EngineOptions options;

    juce::AudioBuffer<float> source;
    std::vector<float> mono;
    double sampleRate { 44100.0 };

    std::shared_ptr<Synthesiser> synthesiser;
    PitchTrack melody;

    ModelLibrary models;

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
