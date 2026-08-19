#pragma once

#include "model/Synthesiser.h"
#include "model/VoiceModel.h"

namespace rvctuner
{
/** @brief What the content pass reads from the voice, and which editing never revisits. */
struct ContentSettings
{
    float retrievalRatio { 0.75f };

    float consonantProtection { 0.33f };

    [[nodiscard]] bool operator== (const ContentSettings& other) const noexcept
    {
        return retrievalRatio == other.retrievalRatio
            && consonantProtection == other.consonantProtection;
    }
};

/** @brief An RVC voice run as a re-synthesiser: the same performance, sung at a different pitch.

    The content encoder strips the pitch out of the recording and keeps the words and the delivery;
    the vocoder puts a melody back in. Because the vocoder's weights are the voice, this engine
    sings in the voice the model was trained on, and is only a pitch corrector when that voice is
    the one in the recording.
*/
class VoiceSynthesiser final : public Synthesiser
{
public:
    VoiceSynthesiser (const VoiceModel& model, const ContentSettings& settings);

    [[nodiscard]] juce::String getName() const override;

    bool prepare (const float* samples,
                  int numSamples,
                  double sampleRate,
                  const PitchTrack& melody,
                  const ProgressCallback& onProgress,
                  const std::atomic<bool>& shouldAbort,
                  juce::String& error) override;

    [[nodiscard]] bool isPrepared() const noexcept override { return numFrames > 0 && ! conditioning.empty(); }

    [[nodiscard]] std::vector<float> render (const float* melodyHz,
                                             int firstFrame,
                                             int numFrames,
                                             juce::String& error) const override;

    [[nodiscard]] int getDependencyFrames() const noexcept override { return contextFrames; }

    [[nodiscard]] int getFrameRate() const noexcept override { return frameRate; }

    [[nodiscard]] int getNumFrames() const noexcept override { return numFrames; }

    /** @brief The seed the vocoder's latent noise is drawn from, which fixes its every detail. */
    void setLatentNoiseSeed (std::int32_t seed) noexcept { latentNoiseSeed = seed; }

private:
    [[nodiscard]] std::vector<float> encodeContent (const float* samples,
                                                    int numSamples,
                                                    juce::String& error) const;

    const VoiceModel& voiceModel;
    const ModelManifest& manifest;
    ContentSettings settings;

    std::vector<float> conditioning;

    double sourceSampleRate { 44100.0 };
    int numSourceSamples { 0 };
    int frameRate { 100 };
    int numFrames { 0 };
    int contextFrames { 50 };

    std::int32_t latentNoiseSeed { 1 };
};
}
