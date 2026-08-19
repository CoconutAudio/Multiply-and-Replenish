#include "model/MelSynthesiser.h"

#include "dsp/MelFilterBank.h"
#include "dsp/SincResampler.h"

#include <algorithm>
#include <cmath>

namespace rvctuner
{
MelSynthesiser::MelSynthesiser (const Configuration& configuration)
    : config (configuration)
{
    const MelSpectrogram::Configuration melConfiguration {
        config.fftSize,
        config.fftSize,
        config.hopSizeInSamples,
        config.numMelBins,
        config.fftSize / 2 + 1,
        config.magnitudeFloor,
        true
    };

    melSpectrogram = std::make_unique<MelSpectrogram> (melConfiguration,
                                                       makeMelFilterBank (MelScale::slaney,
                                                                          static_cast<double> (config.sampleRate),
                                                                          config.fftSize,
                                                                          config.numMelBins,
                                                                          config.melMinimumHz,
                                                                          config.melMaximumHz));
}

std::unique_ptr<MelSynthesiser> MelSynthesiser::load (const juce::File& modelFile,
                                                      const Configuration& configuration,
                                                      int numThreads,
                                                      juce::String& error)
{
    std::unique_ptr<MelSynthesiser> synthesiser { new MelSynthesiser (configuration) };

    if (! synthesiser->network.load (modelFile, numThreads))
    {
        error = synthesiser->network.getError();
        return nullptr;
    }

    const auto inputNames = synthesiser->network.getInputNames();
    const auto outputNames = synthesiser->network.getOutputNames();

    if (inputNames.size() < 2 || outputNames.empty())
    {
        error = modelFile.getFileName() + " does not take a mel spectrogram and a fundamental";
        return nullptr;
    }

    synthesiser->melInputName = inputNames[0];
    synthesiser->pitchInputName = inputNames[1];
    synthesiser->outputName = outputNames.front();

    return synthesiser;
}

int MelSynthesiser::getDependencyFrames() const noexcept
{
    const auto melFrameRate = static_cast<double> (config.sampleRate)
                            / static_cast<double> (config.hopSizeInSamples);

    return 1 + static_cast<int> (std::ceil (static_cast<double> (config.contextFrames)
                                            * static_cast<double> (frameRate) / melFrameRate));
}

bool MelSynthesiser::prepare (const float* samples,
                              int numSamples,
                              double sampleRate,
                              const PitchTrack& melody,
                              const ProgressCallback& onProgress,
                              const std::atomic<bool>& shouldAbort,
                              juce::String& error)
{
    mel.clear();
    numMelFrames = 0;

    if (samples == nullptr || numSamples <= 0)
    {
        error = "there is nothing to prepare";
        return false;
    }

    sourceSampleRate = sampleRate;
    numSourceSamples = numSamples;
    frameRate = melody.frameRate > 0 ? melody.frameRate : 100;
    numFrames = melody.getNumFrames();

    if (onProgress)
        onProgress (0.1f);

    const SincResampler toVocoderRate { sampleRate, static_cast<double> (config.sampleRate) };
    const auto resampled = toVocoderRate.process (samples, numSamples);

    if (resampled.empty())
    {
        error = "resampling to the vocoder's rate produced nothing";
        return false;
    }

    if (shouldAbort.load())
        return false;

    if (onProgress)
        onProgress (0.4f);

    numMelFrames = melSpectrogram->process (resampled.data(), static_cast<int> (resampled.size()), mel);

    if (numMelFrames <= 0)
    {
        error = "the recording is shorter than one vocoder frame";
        return false;
    }

    if (onProgress)
        onProgress (1.0f);

    return true;
}

std::vector<float> MelSynthesiser::resampleMelody (const float* melodyHz,
                                                   int firstMelFrame,
                                                   int numSpanMelFrames) const
{
    std::vector<float> resampled (static_cast<std::size_t> (numSpanMelFrames), 0.0f);

    const auto melFrameRate = static_cast<double> (config.sampleRate)
                            / static_cast<double> (config.hopSizeInSamples);

    for (int spanIndex = 0; spanIndex < numSpanMelFrames; ++spanIndex)
    {
        const auto seconds = static_cast<double> (firstMelFrame + spanIndex) / melFrameRate;
        const auto position = seconds * static_cast<double> (frameRate);

        const auto lower = std::clamp (static_cast<int> (std::floor (position)), 0, numFrames - 1);
        const auto upper = std::min (lower + 1, numFrames - 1);

        const auto below = melodyHz[lower];
        const auto above = melodyHz[upper];

        auto value = 0.0f;

        if (below > 0.0f && above > 0.0f)
        {
            const auto fraction = static_cast<float> (position - static_cast<double> (lower));
            value = below + fraction * (above - below);
        }
        else if (below > 0.0f)
        {
            value = position - static_cast<double> (lower) < 0.5 ? below : 0.0f;
        }
        else if (above > 0.0f)
        {
            value = position - static_cast<double> (lower) < 0.5 ? 0.0f : above;
        }

        resampled[static_cast<std::size_t> (spanIndex)] =
            value > 0.0f ? std::clamp (value, config.minimumFrequencyHz, config.maximumFrequencyHz)
                         : 0.0f;
    }

    return resampled;
}

std::vector<float> MelSynthesiser::render (const float* melodyHz,
                                           int firstFrame,
                                           int numSpanFrames,
                                           juce::String& error) const
{
    if (! isPrepared() || melodyHz == nullptr || numSpanFrames <= 0)
    {
        error = "nothing to render";
        return {};
    }

    const auto melFrameRate = static_cast<double> (config.sampleRate)
                            / static_cast<double> (config.hopSizeInSamples);

    const auto firstSecond = static_cast<double> (firstFrame) / static_cast<double> (frameRate);
    const auto lastSecond = static_cast<double> (firstFrame + numSpanFrames) / static_cast<double> (frameRate);

    const auto firstSourceSample = static_cast<int> (std::llround (firstSecond * sourceSampleRate));
    const auto lastSourceSample = static_cast<int> (std::llround (lastSecond * sourceSampleRate));
    const auto numWanted = std::max (0, lastSourceSample - firstSourceSample);

    if (numWanted <= 0)
    {
        error = "the span is empty";
        return {};
    }

    const auto firstMelFrame = std::max (0, static_cast<int> (std::floor (firstSecond * melFrameRate))
                                                - config.contextFrames);
    const auto lastMelFrame = std::min (numMelFrames,
                                        static_cast<int> (std::ceil (lastSecond * melFrameRate))
                                            + config.contextFrames);
    const auto numSpanMelFrames = lastMelFrame - firstMelFrame;

    if (numSpanMelFrames <= 0)
    {
        error = "the span lies outside the recording";
        return {};
    }

    const auto numMelBins = config.numMelBins;

    std::vector<float> melSpan (static_cast<std::size_t> (numMelBins) * static_cast<std::size_t> (numSpanMelFrames));

    for (int melIndex = 0; melIndex < numMelBins; ++melIndex)
    {
        const auto* source = mel.data() + static_cast<std::size_t> (melIndex) * static_cast<std::size_t> (numMelFrames)
                           + static_cast<std::size_t> (firstMelFrame);
        auto* destination = melSpan.data() + static_cast<std::size_t> (melIndex) * static_cast<std::size_t> (numSpanMelFrames);

        for (int frameIndex = 0; frameIndex < numSpanMelFrames; ++frameIndex)
            destination[frameIndex] = std::clamp (source[frameIndex], config.melFloor, config.melCeiling);
    }

    const auto pitchSpan = resampleMelody (melodyHz, firstMelFrame, numSpanMelFrames);

    const std::vector<OnnxSession::TensorView> inputs {
        OnnxSession::TensorView::floats (melInputName.c_str(), melSpan.data(),
                                        { 1, numMelBins, numSpanMelFrames }),
        OnnxSession::TensorView::floats (pitchInputName.c_str(), pitchSpan.data(),
                                        { 1, numSpanMelFrames })
    };

    const std::vector<const char*> outputs { outputName.c_str() };

    auto returned = network.run (inputs, outputs);

    if (returned.empty())
    {
        error = "vocoder failed: " + network.getError();
        return {};
    }

    const auto shape = returned.front().GetTensorTypeAndShapeInfo().GetShape();
    const auto numRendered = static_cast<int> (shape.back());
    const auto* rendered = returned.front().GetTensorData<float>();

    const SincResampler toSourceRate { static_cast<double> (config.sampleRate), sourceSampleRate };
    const auto atSourceRate = toSourceRate.isPassThrough()
                            ? std::vector<float> (rendered, rendered + numRendered)
                            : toSourceRate.process (rendered, numRendered);

    const auto spanFirstSourceSample = static_cast<int> (std::llround (
        static_cast<double> (firstMelFrame) * static_cast<double> (config.hopSizeInSamples)
        * sourceSampleRate / static_cast<double> (config.sampleRate)));

    const auto offset = firstSourceSample - spanFirstSourceSample;

    std::vector<float> span (static_cast<std::size_t> (numWanted), 0.0f);

    const auto numAvailable = std::min (numWanted, static_cast<int> (atSourceRate.size()) - offset);

    if (offset < 0 || numAvailable <= 0)
    {
        error = "the vocoder returned less audio than the span covers";
        return {};
    }

    std::copy (atSourceRate.begin() + offset,
               atSourceRate.begin() + offset + numAvailable,
               span.begin());

    return span;
}
}
