#include "common/Pipeline.h"

#include "dsp/SincResampler.h"
#include "common/NoteSegmenter.h"
#include "nn/GameSegmenter.h"
#include "nn/MelSynthesiser.h"
#include "nn/FcpeDetector.h"

#include <algorithm>

namespace multiplyandreplenish
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
             + ", or set MULTIPLYANDREPLENISH_MODEL_PATH. Looked in "
             + juce::String (static_cast<int> (models.getSearchPaths().size())) + " places.";
    }
}

Pipeline::Pipeline()
    : juce::Thread ("Multiply and Replenish analysis")
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

    const auto modelFile = models.find ("fcpe/fcpe.onnx");

    if (! modelFile.existsAsFile())
    {
        fail (describeMissingModel ("pitch model", models));
        return;
    }

    auto detector = FcpeDetector::load (modelFile, {}, chooseNumThreads (options.numThreads), loadError);

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

    notes.clear();

    if (const auto gameDirectory = models.find ("GAME/encoder.onnx").getParentDirectory();
        gameDirectory.isDirectory())
    {
        report (0.2f, "finding the notes");

        juce::String segmenterError;

        if (auto game = GameSegmenter::load (gameDirectory, {}, chooseNumThreads (options.numThreads),
                                             segmenterError))
        {
            const auto segments = game->segment (mono.data(), static_cast<int> (mono.size()),
                                                 sampleRate, melody.frameRate, segmenterError);

            std::vector<int> firstFrames;
            std::vector<int> lastFrames;

            for (const auto& segment : segments)
            {
                if (segment.isRest)
                    continue;

                firstFrames.push_back (segment.firstFrame);
                lastFrames.push_back (segment.lastFrame);
            }

            notes = notesFromSegments (firstFrames, lastFrames, melody, {});
        }
    }

    {
        const juce::ScopedLock lock { stateLock };
        hasMelody = true;
    }

    report (0.3f, "reading the recording");

    if (threadShouldExit())
        return;

    const auto vocoderFile = models.find ("hifigan/pc_nsf_hifigan.onnx");

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
        auto found = notes;

        listeners.call ([&heard, &found] (Listener& listener)
        {
            listener.melodyEstimated (heard, found);
        });
    }

    if (finished)
        listeners.call ([this, &failure] (Listener& listener) { listener.pipelineFinished (synthesiser, failure); });
}
}
