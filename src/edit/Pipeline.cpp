#include "edit/Pipeline.h"

#include "dsp/SincResampler.h"
#include "model/FcpeDetector.h"
#include "model/MelSynthesiser.h"
#include "model/RmvpeDetector.h"

#include <algorithm>

namespace tuner
{
namespace
{
    /** @brief Threads to give the networks, keeping two cores for playback and the editor. */
    int chooseNumThreads (int requested)
    {
        if (requested > 0)
            return requested;

        return std::max (1, juce::SystemStats::getNumPhysicalCpus() - 2);
    }

    juce::String describeMissingModel (const juce::String& file, const ModelLibrary& models)
    {
        return "no " + file + " found. Install the models under "
             + ModelLibrary::getUserModelDirectory().getFullPathName()
             + ", or set TUNER_MODEL_PATH. Looked in "
             + juce::String (static_cast<int> (models.getSearchPaths().size())) + " places.";
    }
}

juce::StringArray EngineOptions::getDetectorNames()
{
    return { "FCPE", "RMVPE" };
}

Pipeline::Pipeline()
    : juce::Thread ("Tuner analysis")
{
}

Pipeline::~Pipeline()
{
    cancel();
}

void Pipeline::addListener (Listener* listener) { listeners.add (listener); }
void Pipeline::removeListener (Listener* listener) { listeners.remove (listener); }

void Pipeline::cancel()
{
    shouldAbort.store (true);
    stopThread (8000);
    shouldAbort.store (false);
}

void Pipeline::start (const juce::AudioBuffer<float>& recording,
                      double newSampleRate,
                      const EngineOptions& newOptions)
{
    cancel();

    options = newOptions;
    sampleRate = newSampleRate;

    const auto numSamples = recording.getNumSamples();
    const auto numChannels = std::max (1, recording.getNumChannels());

    source.makeCopyOf (recording);

    // The melody is one melody however many channels carry it, so the detector hears the sum.
    mono.assign (static_cast<std::size_t> (numSamples), 0.0f);

    for (int channel = 0; channel < recording.getNumChannels(); ++channel)
    {
        const auto* channelData = recording.getReadPointer (channel);

        for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
            mono[static_cast<std::size_t> (sampleIndex)] +=
                channelData[sampleIndex] / static_cast<float> (numChannels);
    }

    {
        const juce::ScopedLock lock { stateLock };
        error.clear();
        hasMelody = false;
        hasFinished = false;
        progressFraction = 0.0f;
        progressStage = "loading";
    }

    synthesiser.reset();
    melody = {};

    startThread (Priority::normal);
}

void Pipeline::report (float fraction, const juce::String& stage)
{
    {
        const juce::ScopedLock lock { stateLock };
        progressFraction = fraction;
        progressStage = stage;
    }

    triggerAsyncUpdate();
}

void Pipeline::run()
{
    const auto fail = [this] (const juce::String& message)
    {
        const juce::ScopedLock lock { stateLock };
        error = message;
        hasFinished = true;
        triggerAsyncUpdate();
    };

    juce::String loadError;

    report (0.02f, "loading the pitch model");

    std::unique_ptr<PitchDetector> detector;

    if (options.detector == EngineOptions::Detector::rmvpe)
    {
        const auto modelFile = models.find ("rmvpe/rmvpe.onnx");

        if (modelFile.existsAsFile())
            detector = RmvpeDetector::load (modelFile, {}, {}, chooseNumThreads (options.numThreads), loadError);
    }

    if (detector == nullptr)
    {
        const auto modelFile = models.find ("pitchnet/fcpe.onnx");

        if (! modelFile.existsAsFile())
        {
            fail (loadError.isNotEmpty() ? loadError : describeMissingModel ("pitch model", models));
            return;
        }

        detector = FcpeDetector::load (modelFile, {}, chooseNumThreads (options.numThreads), loadError);
    }

    if (detector == nullptr)
    {
        fail (loadError.isNotEmpty() ? loadError : "no pitch model could be loaded");
        return;
    }

    if (threadShouldExit())
        return;

    report (0.1f, "hearing the melody");

    const SincResampler toDetectorRate { sampleRate, static_cast<double> (detector->getSampleRate()) };
    const auto atDetectorRate = toDetectorRate.process (mono.data(), static_cast<int> (mono.size()));

    melody = detector->estimate (atDetectorRate.data(), static_cast<int> (atDetectorRate.size()), loadError);

    if (melody.getNumFrames() == 0)
    {
        fail (loadError.isNotEmpty() ? loadError : "the melody could not be heard");
        return;
    }

    {
        const juce::ScopedLock lock { stateLock };
        hasMelody = true;
    }

    report (0.3f, "reading the recording");

    if (threadShouldExit())
        return;

    const auto vocoderFile = models.find ("pitchnet/pc_nsf_hifigan.onnx");

    if (! vocoderFile.existsAsFile())
    {
        fail (describeMissingModel ("vocoder", models));
        return;
    }

    auto engine = MelSynthesiser::load (vocoderFile, {}, chooseNumThreads (options.numThreads), loadError);

    if (engine == nullptr)
    {
        fail (loadError);
        return;
    }

    const auto prepared = engine->prepare (source,
                                           sampleRate,
                                           melody,
                                           [this] (float fraction)
                                           {
                                               report (0.3f + 0.7f * fraction, "reading the recording");
                                           },
                                           shouldAbort,
                                           loadError);

    if (threadShouldExit())
        return;

    if (! prepared)
    {
        fail (loadError);
        return;
    }

    synthesiser = std::move (engine);

    {
        const juce::ScopedLock lock { stateLock };
        hasFinished = true;
        progressFraction = 1.0f;
        progressStage = "ready";
    }

    triggerAsyncUpdate();
}

void Pipeline::handleAsyncUpdate()
{
    auto fraction = 0.0f;
    juce::String stage;
    juce::String failure;
    auto melodyIsNew = false;
    auto finished = false;

    {
        const juce::ScopedLock lock { stateLock };

        fraction = progressFraction;
        stage = progressStage;
        failure = error;

        melodyIsNew = hasMelody;
        hasMelody = false;

        finished = hasFinished;
        hasFinished = false;
    }

    listeners.call ([fraction, &stage] (Listener& listener) { listener.pipelineProgressed (fraction, stage); });

    if (melodyIsNew)
    {
        auto heard = melody;
        listeners.call ([&heard] (Listener& listener) { listener.melodyEstimated (heard); });
    }

    if (finished)
        listeners.call ([this, &failure] (Listener& listener) { listener.pipelineFinished (synthesiser, failure); });
}
}
