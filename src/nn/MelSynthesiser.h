#pragma once

#include "dsp/MelSpectrogram.h"
#include "nn/OnnxSession.h"
#include "nn/Synthesiser.h"

#include <memory>

namespace multiplyandreplenish
{
/** @brief PC-NSF-HiFiGAN: a vocoder that reads a mel spectrogram and a fundamental separately.

    The mel carries the timbre and the words, the fundamental carries the tune, and the network was
    trained on pairs where the two disagree, so handing it a corrected melody re-sings the recording
    at the new pitch in the voice that was recorded. Nothing about the singer is stored in the
    weights, which is why this engine needs no model of the voice it is correcting.
*/
class MelSynthesiser final : public Synthesiser
{
public:
    /** @brief The rates and shapes the exported network was trained with. */
    struct Configuration
    {
        int sampleRate { 44100 };
        int hopSizeInSamples { 512 };
        int numMelBins { 128 };
        int fftSize { 2048 };
        double melMinimumHz { 40.0 };
        double melMaximumHz { 16000.0 };
        float magnitudeFloor { 1.0e-10f };

        float melFloor { -15.0f };
        float melCeiling { 5.0f };
        float minimumFrequencyHz { 20.0f };

        /** @brief The highest fundamental the network was trained to sing.

            Its release states an output range of E2 to D#7, so anything commanded above this is
            beyond what it was taught rather than merely high. A violin's E7 and a flute's D7 both
            sit above where a singer ever goes, which is why this is not the vocal range.
        */
        float maximumFrequencyHz { 2489.0f };

        /** @brief Frames of the recording either side of a span that the network is given. */
        int contextFrames { 24 };
    };

    static std::unique_ptr<MelSynthesiser> load (const juce::File& modelFile,
                                                 const Configuration& configuration,
                                                 int numThreads,
                                                 juce::String& error);

    [[nodiscard]] juce::String getName() const override { return "PC-NSF-HiFiGAN"; }

    bool prepare (const juce::AudioBuffer<float>& recording,
                  double sampleRate,
                  const PitchTrack& melody,
                  const ProgressCallback& onProgress,
                  const std::atomic<bool>& shouldAbort,
                  juce::String& error) override;

    [[nodiscard]] bool isPrepared() const noexcept override { return numMelFrames > 0; }

    bool render (const float* melodyHz,
                 int firstFrame,
                 int numFrames,
                 juce::AudioBuffer<float>& destination,
                 juce::String& error) const override;

    [[nodiscard]] int getNumChannels() const noexcept override { return numSourceChannels; }

    [[nodiscard]] int getDependencyFrames() const noexcept override;

    [[nodiscard]] int getFrameRate() const noexcept override { return frameRate; }

    [[nodiscard]] int getNumFrames() const noexcept override { return numFrames; }

private:
    explicit MelSynthesiser (const Configuration& configuration);

    /** @brief The recording's melody resampled onto the vocoder's frame grid, unvoiced kept. */
    [[nodiscard]] std::vector<float> resampleMelody (const float* melodyHz,
                                                     int firstMelFrame,
                                                     int numSpanMelFrames) const;

    /** @brief Renders one channel's mel through the vocoder and resamples it back. */
    [[nodiscard]] std::vector<float> renderChannel (int channel,
                                                    const std::vector<float>& pitchSpan,
                                                    int firstMelFrame,
                                                    int numSpanMelFrames,
                                                    juce::String& error) const;

    Configuration config;

    OnnxSession network;
    std::unique_ptr<MelSpectrogram> melSpectrogram;

    std::string melInputName;
    std::string pitchInputName;
    std::string outputName;

    std::vector<std::vector<float>> mel;
    int numMelFrames { 0 };

    int numSourceChannels { 1 };

    double sourceSampleRate { 44100.0 };
    int numSourceSamples { 0 };
    int frameRate { 100 };
    int numFrames { 0 };
};
}
