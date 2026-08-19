#include "model/FcpeDetector.h"

#include "dsp/MelFilterBank.h"
#include "dsp/PitchConversions.h"

#include <algorithm>
#include <cmath>

namespace rvctuner
{
FcpeDetector::FcpeDetector (const Configuration& configuration)
    : config (configuration)
{
    melSpectrogram = std::make_unique<MelSpectrogram> (config.mel,
                                                       makeMelFilterBank (MelScale::slaney,
                                                                          static_cast<double> (config.sampleRate),
                                                                          config.mel.fftSize,
                                                                          config.mel.numMelBins,
                                                                          config.melMinimumHz,
                                                                          config.melMaximumHz));

    const auto lowestCents = hzToCents (config.minimumFrequencyHz, config.centsReferenceHz);
    const auto highestCents = hzToCents (config.maximumFrequencyHz, config.centsReferenceHz);

    centsPerClass.resize (static_cast<std::size_t> (config.numPitchBins));

    for (int binIndex = 0; binIndex < config.numPitchBins; ++binIndex)
    {
        const auto position = static_cast<double> (binIndex) / static_cast<double> (config.numPitchBins - 1);
        centsPerClass[static_cast<std::size_t> (binIndex)] =
            lowestCents + position * (highestCents - lowestCents);
    }
}

std::unique_ptr<FcpeDetector> FcpeDetector::load (const juce::File& modelFile,
                                                  const Configuration& configuration,
                                                  int numThreads,
                                                  juce::String& error)
{
    std::unique_ptr<FcpeDetector> detector { new FcpeDetector (configuration) };

    if (! detector->network.load (modelFile, numThreads))
    {
        error = detector->network.getError();
        return nullptr;
    }

    const auto inputNames = detector->network.getInputNames();
    const auto outputNames = detector->network.getOutputNames();

    if (inputNames.empty() || outputNames.empty())
    {
        error = modelFile.getFileName() + " declares no inputs or outputs";
        return nullptr;
    }

    detector->inputName = inputNames.front();
    detector->outputName = outputNames.front();

    return detector;
}

PitchTrack FcpeDetector::estimate (const float* samples, int numSamples, juce::String& error) const
{
    PitchTrack track;
    track.frameRate = config.sampleRate / config.mel.hopSizeInSamples;

    std::vector<float> logMel;
    const auto numFrames = melSpectrogram->process (samples, numSamples, logMel);

    if (numFrames <= 0)
    {
        error = "the recording is shorter than one analysis window";
        return track;
    }

    const auto numMelBins = config.mel.numMelBins;

    std::vector<float> framewiseMel (logMel.size());

    for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
        for (int melIndex = 0; melIndex < numMelBins; ++melIndex)
            framewiseMel[static_cast<std::size_t> (frameIndex) * static_cast<std::size_t> (numMelBins)
                         + static_cast<std::size_t> (melIndex)] =
                logMel[static_cast<std::size_t> (melIndex) * static_cast<std::size_t> (numFrames)
                       + static_cast<std::size_t> (frameIndex)];

    const std::vector<OnnxSession::TensorView> inputs {
        OnnxSession::TensorView::floats (inputName.c_str(), framewiseMel.data(),
                                        { 1, numFrames, numMelBins })
    };

    const std::vector<const char*> outputs { outputName.c_str() };

    auto returned = network.run (inputs, outputs);

    if (returned.empty())
    {
        error = "pitch estimation failed: " + network.getError();
        return track;
    }

    const auto shape = returned.front().GetTensorTypeAndShapeInfo().GetShape();
    const auto numReturnedFrames = shape.size() >= 2 ? static_cast<int> (shape[shape.size() - 2]) : 0;
    const auto numBins = shape.empty() ? 0 : static_cast<int> (shape.back());

    if (numBins != config.numPitchBins || numReturnedFrames <= 0)
    {
        error = "the pitch network returned an unexpected shape";
        return track;
    }

    const auto* latent = returned.front().GetTensorData<float>();
    const auto numUsable = std::min (numFrames, numReturnedFrames);

    track.fundamentalFrequencyHz.assign (static_cast<std::size_t> (numFrames), 0.0f);
    track.confidence.assign (static_cast<std::size_t> (numFrames), 0.0f);

    for (int frameIndex = 0; frameIndex < numUsable; ++frameIndex)
    {
        const auto* frame = latent + static_cast<std::size_t> (frameIndex) * static_cast<std::size_t> (numBins);

        auto peakIndex = 0;
        auto peakValue = frame[0];

        for (int binIndex = 1; binIndex < numBins; ++binIndex)
        {
            if (frame[binIndex] > peakValue)
            {
                peakValue = frame[binIndex];
                peakIndex = binIndex;
            }
        }

        track.confidence[static_cast<std::size_t> (frameIndex)] = peakValue;

        if (peakValue <= config.confidenceThreshold)
            continue;

        const auto firstBin = std::max (peakIndex - config.localAverageRadius, 0);
        const auto lastBin = std::min (peakIndex + config.localAverageRadius, numBins - 1);

        double weightedSum = 0.0;
        double weightSum = 0.0;

        for (int binIndex = firstBin; binIndex <= lastBin; ++binIndex)
        {
            const auto weight = static_cast<double> (frame[binIndex]);
            weightedSum += weight * centsPerClass[static_cast<std::size_t> (binIndex)];
            weightSum += weight;
        }

        if (weightSum <= 1.0e-9)
            continue;

        const auto frequencyHz = centsToHz (weightedSum / weightSum, config.centsReferenceHz);

        if (frequencyHz >= config.minimumFrequencyHz && frequencyHz <= config.maximumFrequencyHz)
            track.fundamentalFrequencyHz[static_cast<std::size_t> (frameIndex)] =
                static_cast<float> (frequencyHz);
    }

    return track;
}
}
