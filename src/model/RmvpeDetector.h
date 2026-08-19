#pragma once

#include "dsp/MelSpectrogram.h"
#include "model/OnnxSession.h"
#include "model/PitchDetector.h"

#include <memory>

namespace tuner
{
/** @brief RMVPE: a deep U-net that reads a log-mel spectrogram and reports pitch salience. */
class RmvpeDetector final : public PitchDetector
{
public:
    /** @brief The decoder constants the exported network was trained with. */
    struct Configuration
    {
        int sampleRate { 16000 };
        int numPitchBins { 360 };
        double centsOrigin { 1997.3794084376191 };
        double centsPerBin { 20.0 };
        double centsReferenceHz { 10.0 };
        int localAverageRadius { 4 };
        float salienceThreshold { 0.03f };
        int frameCountMultiple { 32 };
        double minimumFrequencyHz { 50.0 };
        double maximumFrequencyHz { 2000.0 };

        MelSpectrogram::Configuration mel { 1024, 1024, 160, 128, 513, 1.0e-5f, true };
        double melMinimumHz { 30.0 };
        double melMaximumHz { 8000.0 };

        std::string inputName { "logMelSpectrogram" };
        std::string outputName { "salience" };
    };

    /** @brief Loads the network and builds the front end it was trained against.
        @param modelFile    The exported RMVPE graph.
        @param configuration Decoder and front-end constants.
        @param filterBank   The exported mel filter bank, or empty to generate the trained one.
        @param numThreads   Threads for inference; zero lets the runtime decide.
        @param error        Set when loading fails.
    */
    static std::unique_ptr<RmvpeDetector> load (const juce::File& modelFile,
                                                const Configuration& configuration,
                                                std::vector<float> filterBank,
                                                int numThreads,
                                                juce::String& error);

    [[nodiscard]] juce::String getName() const override { return "RMVPE"; }

    [[nodiscard]] int getSampleRate() const noexcept override { return config.sampleRate; }

    [[nodiscard]] PitchTrack estimate (const float* samples,
                                       int numSamples,
                                       juce::String& error) const override;

    /** @brief Estimates using a network someone else owns, as when a voice model holds it. */
    RmvpeDetector (const Configuration& configuration,
                   std::vector<float> filterBank,
                   const OnnxSession& sharedNetwork);

private:
    RmvpeDetector (const Configuration& configuration, std::vector<float> filterBank);

    [[nodiscard]] double decodeFrame (const float* salienceFrame, float& peakSalience) const;

    Configuration config;
    std::vector<double> centsPerClass;

    std::unique_ptr<OnnxSession> ownedNetwork;
    const OnnxSession* network { nullptr };

    std::unique_ptr<MelSpectrogram> melSpectrogram;
};
}
