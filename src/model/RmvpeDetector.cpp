#include "model/RmvpeDetector.h"

#include "dsp/MelFilterBank.h"
#include "dsp/PitchConversions.h"

#include <algorithm>
#include <cmath>

namespace tuner
{
RmvpeDetector::RmvpeDetector (const Configuration& configuration, std::vector<float> filterBank)
    : config (configuration)
{
    if (filterBank.empty())
        filterBank = makeMelFilterBank (MelScale::htk,
                                        static_cast<double> (config.sampleRate),
                                        config.mel.fftSize,
                                        config.mel.numMelBins,
                                        config.melMinimumHz,
                                        config.melMaximumHz);

    melSpectrogram = std::make_unique<MelSpectrogram> (config.mel, std::move (filterBank));

    centsPerClass.resize (static_cast<std::size_t> (config.numPitchBins));

    for (int binIndex = 0; binIndex < config.numPitchBins; ++binIndex)
        centsPerClass[static_cast<std::size_t> (binIndex)] =
            config.centsOrigin + config.centsPerBin * static_cast<double> (binIndex);
}

RmvpeDetector::RmvpeDetector (const Configuration& configuration,
                              std::vector<float> filterBank,
                              const OnnxSession& sharedNetwork)
    : RmvpeDetector (configuration, std::move (filterBank))
{
    network = &sharedNetwork;
}

std::unique_ptr<RmvpeDetector> RmvpeDetector::load (const juce::File& modelFile,
                                                    const Configuration& configuration,
                                                    std::vector<float> filterBank,
                                                    int numThreads,
                                                    juce::String& error)
{
    std::unique_ptr<RmvpeDetector> detector { new RmvpeDetector (configuration, std::move (filterBank)) };

    detector->ownedNetwork = std::make_unique<OnnxSession>();

    if (! detector->ownedNetwork->load (modelFile, numThreads))
    {
        error = detector->ownedNetwork->getError();
        return nullptr;
    }

    detector->network = detector->ownedNetwork.get();
    return detector;
}

double RmvpeDetector::decodeFrame (const float* salienceFrame, float& peakSalience) const
{
    const auto numBins = config.numPitchBins;
    const auto radius = config.localAverageRadius;

    auto peakIndex = 0;
    auto peakValue = salienceFrame[0];

    for (int binIndex = 1; binIndex < numBins; ++binIndex)
    {
        if (salienceFrame[binIndex] > peakValue)
        {
            peakValue = salienceFrame[binIndex];
            peakIndex = binIndex;
        }
    }

    peakSalience = peakValue;

    if (peakValue <= config.salienceThreshold)
        return 0.0;

    const auto firstBin = std::max (peakIndex - radius, 0);
    const auto lastBin = std::min (peakIndex + radius, numBins - 1);

    double weightedSum = 0.0;
    double weightSum = 0.0;

    for (int binIndex = firstBin; binIndex <= lastBin; ++binIndex)
    {
        const auto weight = static_cast<double> (salienceFrame[binIndex]);
        weightedSum += weight * centsPerClass[static_cast<std::size_t> (binIndex)];
        weightSum += weight;
    }

    if (weightSum <= 0.0)
        return 0.0;

    const auto frequencyHz = centsToHz (weightedSum / weightSum, config.centsReferenceHz);

    return frequencyHz >= config.minimumFrequencyHz && frequencyHz <= config.maximumFrequencyHz
         ? frequencyHz
         : 0.0;
}

PitchTrack RmvpeDetector::estimate (const float* samples, int numSamples, juce::String& error) const
{
    PitchTrack track;
    track.frameRate = config.sampleRate / config.mel.hopSizeInSamples;

    if (network == nullptr || ! network->isLoaded())
    {
        error = "the pitch network is not loaded";
        return track;
    }

    std::vector<float> logMel;
    const auto numFrames = melSpectrogram->process (samples, numSamples, logMel);

    if (numFrames <= 0)
    {
        error = "the recording is shorter than one analysis window";
        return track;
    }

    const auto multiple = std::max (config.frameCountMultiple, 1);
    const auto paddedNumFrames = ((numFrames + multiple - 1) / multiple) * multiple;
    const auto numMelBins = config.mel.numMelBins;

    std::vector<float> padded;

    if (paddedNumFrames != numFrames)
    {
        padded.assign (static_cast<std::size_t> (numMelBins) * static_cast<std::size_t> (paddedNumFrames), 0.0f);

        for (int melIndex = 0; melIndex < numMelBins; ++melIndex)
        {
            const auto* source = logMel.data() + static_cast<std::size_t> (melIndex) * static_cast<std::size_t> (numFrames);
            auto* destination = padded.data() + static_cast<std::size_t> (melIndex) * static_cast<std::size_t> (paddedNumFrames);
            std::copy (source, source + numFrames, destination);
        }
    }
    else
    {
        padded = std::move (logMel);
    }

    const std::vector<OnnxSession::TensorView> inputs {
        OnnxSession::TensorView::floats (config.inputName.c_str(), padded.data(),
                                        { 1, numMelBins, paddedNumFrames })
    };

    const std::vector<const char*> outputs { config.outputName.c_str() };

    auto returned = network->run (inputs, outputs);

    if (returned.empty())
    {
        error = "pitch estimation failed: " + network->getError();
        return track;
    }

    const auto* salience = returned.front().GetTensorData<float>();

    track.fundamentalFrequencyHz.resize (static_cast<std::size_t> (numFrames));
    track.confidence.resize (static_cast<std::size_t> (numFrames));

    for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
    {
        const auto* frame = salience + static_cast<std::size_t> (frameIndex)
                                           * static_cast<std::size_t> (config.numPitchBins);

        auto peakSalience = 0.0f;
        track.fundamentalFrequencyHz[static_cast<std::size_t> (frameIndex)] =
            static_cast<float> (decodeFrame (frame, peakSalience));
        track.confidence[static_cast<std::size_t> (frameIndex)] = peakSalience;
    }

    return track;
}
}
