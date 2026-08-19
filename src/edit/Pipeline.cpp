#include "edit/Pipeline.h"

#include "dsp/SincResampler.h"
#include "model/FcpeDetector.h"
#include "model/MelSynthesiser.h"
#include "model/RmvpeDetector.h"
#include "model/VoiceSynthesiser.h"

#include <algorithm>

namespace rvctuner
{
juce::StringArray EngineOptions::getDetectorNames()
{
    return { "FCPE", "RMVPE" };
}

juce::StringArray EngineOptions::getEngineNames()
{
    return { "PC-NSF-HiFiGAN", "RVC voice" };
}

Pipeline::Pipeline()
    : juce::Thread ("RVCTuner analysis")
{
    voices.rescan();
}

Pipeline::~Pipeline()
{
    cancel();
}

void Pipeline::addListener (Listener* listener) { listeners.add (listener); }
void Pipeline::removeListener (Listener* listener) { listeners.remove (listener); }

juce::File Pipeline::findModelFile (const juce::String& relativePath) const
{
    for (const auto& searchPath : voices.getSearchPaths())
    {
        const auto candidate = searchPath.getChildFile (relativePath);

        if (candidate.existsAsFile())
            return candidate;
    }

    return {};
}

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
    voiceModel.reset();
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

    std::unique_ptr<PitchDetector> ownedDetector;
    const PitchDetector* detector = nullptr;
    std::unique_ptr<VoiceModel> loadedVoice;

    if (options.engine == EngineOptions::Engine::voiceModel)
    {
        const auto* entry = voices.findByName (options.voiceName);

        if (entry == nullptr && ! voices.getEntries().empty())
            entry = &voices.getEntries().front();

        if (entry == nullptr)
        {
            fail ("no RVC voice is installed under "
                  + VoiceModelLibrary::getUserModelDirectory().getFullPathName());
            return;
        }

        loadedVoice = VoiceModel::load (entry->directory, options.numThreads, loadError);

        if (loadedVoice == nullptr)
        {
            fail (loadError);
            return;
        }
    }

    if (options.detector == EngineOptions::Detector::rmvpe)
    {
        if (const auto standalone = findModelFile ("rmvpe/rmvpe.onnx"); standalone.existsAsFile())
        {
            ownedDetector = RmvpeDetector::load (standalone, {}, {}, options.numThreads, loadError);
            detector = ownedDetector.get();
        }
        else if (loadedVoice != nullptr)
        {
            detector = &loadedVoice->getPitchDetector();
        }
        else if (const auto* entry = voices.findByName (options.voiceName);
                 entry != nullptr || ! voices.getEntries().empty())
        {
            const auto& chosen = entry != nullptr ? *entry : voices.getEntries().front();
            loadedVoice = VoiceModel::load (chosen.directory, options.numThreads, loadError);

            if (loadedVoice != nullptr)
                detector = &loadedVoice->getPitchDetector();
        }
    }

    if (detector == nullptr)
    {
        const auto modelFile = findModelFile ("pitchnet/fcpe.onnx");

        if (! modelFile.existsAsFile())
        {
            fail (loadError.isNotEmpty()
                      ? loadError
                      : "no pitch model found: install pitchnet/fcpe.onnx, or an RVC voice, under "
                            + VoiceModelLibrary::getUserModelDirectory().getFullPathName());
            return;
        }

        ownedDetector = FcpeDetector::load (modelFile, {}, options.numThreads, loadError);
        detector = ownedDetector.get();
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

    std::shared_ptr<Synthesiser> engine;

    if (options.engine == EngineOptions::Engine::voiceModel)
    {
        if (loadedVoice == nullptr)
        {
            fail (loadError.isNotEmpty() ? loadError : "no voice model could be loaded");
            return;
        }

        ContentSettings content;
        content.retrievalRatio = options.retrievalRatio;
        content.consonantProtection = options.consonantProtection;

        engine = std::make_shared<VoiceSynthesiser> (*loadedVoice, content);
    }
    else
    {
        const auto modelFile = findModelFile ("pitchnet/pc_nsf_hifigan.onnx");

        if (! modelFile.existsAsFile())
        {
            fail ("no vocoder found: install pitchnet/pc_nsf_hifigan.onnx under "
                  + VoiceModelLibrary::getUserModelDirectory().getFullPathName());
            return;
        }

        auto melEngine = MelSynthesiser::load (modelFile, {}, options.numThreads, loadError);

        if (melEngine == nullptr)
        {
            fail (loadError);
            return;
        }

        engine = std::move (melEngine);
    }

    const auto prepared = engine->prepare (mono.data(),
                                           static_cast<int> (mono.size()),
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

    voiceModel = std::move (loadedVoice);
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
