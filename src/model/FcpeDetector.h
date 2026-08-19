#pragma once

#include "dsp/MelSpectrogram.h"
#include "model/OnnxSession.h"
#include "model/PitchDetector.h"

#include <memory>

namespace rvctuner
{
/** @brief FCPE: a transformer that reads a log-mel spectrogram and reports a cent distribution.

    Lighter than RMVPE and steadier on breathy singing; it decodes the same way, over a cent table
    spanning C1 to B6 in 360 steps.
*/
class FcpeDetector final : public PitchDetector
{
public:
    /** @brief The decoder constants the exported network was trained with. */
    struct Configuration
    {
        int sampleRate { 16000 };
        int numPitchBins { 360 };
        double minimumFrequencyHz { 32.7 };
        double maximumFrequencyHz { 1975.5 };
        double centsReferenceHz { 10.0 };
        int localAverageRadius { 4 };
        float confidenceThreshold { 0.006f };

        MelSpectrogram::Configuration mel { 1024, 1024, 160, 128, 513, 1.0e-5f, true };
        double melMinimumHz { 0.0 };
        double melMaximumHz { 8000.0 };
    };

    static std::unique_ptr<FcpeDetector> load (const juce::File& modelFile,
                                               const Configuration& configuration,
                                               int numThreads,
                                               juce::String& error);

    [[nodiscard]] juce::String getName() const override { return "FCPE"; }

    [[nodiscard]] int getSampleRate() const noexcept override { return config.sampleRate; }

    [[nodiscard]] PitchTrack estimate (const float* samples,
                                       int numSamples,
                                       juce::String& error) const override;

private:
    explicit FcpeDetector (const Configuration& configuration);

    Configuration config;
    std::vector<double> centsPerClass;

    OnnxSession network;
    std::unique_ptr<MelSpectrogram> melSpectrogram;

    std::string inputName;
    std::string outputName;
};
}
