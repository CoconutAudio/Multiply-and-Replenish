#include "model/VoiceSynthesiser.h"

#include "dsp/PitchConversions.h"
#include "dsp/Smoothing.h"
#include "dsp/SincResampler.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rvctuner
{
namespace
{
    constexpr double contentChunkSeconds = 30.0;

    int reflectIndex (int index, int numSamples) noexcept
    {
        if (numSamples <= 1)
            return 0;

        const auto period = 2 * (numSamples - 1);

        auto wrapped = index % period;
        if (wrapped < 0)
            wrapped += period;

        return wrapped < numSamples ? wrapped : period - wrapped;
    }

    std::vector<float> takeReflected (const float* samples, int numSamples, int first, int count)
    {
        std::vector<float> taken (static_cast<std::size_t> (count));

        for (int index = 0; index < count; ++index)
            taken[static_cast<std::size_t> (index)] = samples[reflectIndex (first + index, numSamples)];

        return taken;
    }

    std::uint64_t mixBits (std::uint64_t state) noexcept
    {
        state += 0x9e3779b97f4a7c15ULL;
        state = (state ^ (state >> 30)) * 0xbf58476d1ce4e5b9ULL;
        state = (state ^ (state >> 27)) * 0x94d049bb133111ebULL;
        return state ^ (state >> 31);
    }

    /** @brief A standard normal drawn from the seed and the coordinates alone, so that spans agree
               wherever they overlap, however the recording is divided up.
    */
    float normalAt (std::int32_t seed, int dimension, int frameIndex) noexcept
    {
        const auto key = mixBits (static_cast<std::uint64_t> (static_cast<std::uint32_t> (seed))
                                  ^ (static_cast<std::uint64_t> (static_cast<std::uint32_t> (dimension)) << 32)
                                  ^ mixBits (static_cast<std::uint64_t> (static_cast<std::uint32_t> (frameIndex))));

        const auto toUnitInterval = [] (std::uint64_t bits)
        {
            return (static_cast<double> (bits >> 11) + 0.5) * (1.0 / 9007199254740992.0);
        };

        const auto first = toUnitInterval (key);
        const auto second = toUnitInterval (mixBits (key));

        return static_cast<float> (std::sqrt (-2.0 * std::log (first))
                                   * std::cos (2.0 * std::numbers::pi * second));
    }
}

VoiceSynthesiser::VoiceSynthesiser (const VoiceModel& model, const ContentSettings& contentSettings)
    : voiceModel (model),
      manifest (model.getManifest()),
      settings (contentSettings)
{
    latentNoiseSeed = manifest.defaultLatentNoiseSeed;
}

juce::String VoiceSynthesiser::getName() const
{
    return "RVC voice (" + voiceModel.getName() + ")";
}

std::vector<float> VoiceSynthesiser::encodeContent (const float* samples,
                                                    int numSamples,
                                                    juce::String& error) const
{
    const std::vector<OnnxSession::TensorView> inputs {
        OnnxSession::TensorView::floats (manifest.contentEncoderInput.c_str(), samples, { 1, numSamples })
    };

    const std::vector<const char*> outputs { manifest.contentEncoderOutput.c_str() };

    auto returned = voiceModel.getContentEncoder().run (inputs, outputs);

    if (returned.empty())
    {
        error = "content encoder failed: " + voiceModel.getContentEncoder().getError();
        return {};
    }

    const auto shape = returned.front().GetTensorTypeAndShapeInfo().GetShape();

    if (shape.size() != 3)
    {
        error = "content encoder returned an unexpected rank";
        return {};
    }

    const auto numElements = static_cast<std::size_t> (shape[1] * shape[2]);
    const auto* data = returned.front().GetTensorData<float>();

    return { data, data + numElements };
}

namespace
{
    /** @brief The loudness of every frame of the recording, for the render to be held to. */
    std::vector<float> measureLevel (const float* samples, int numSamples, double sampleRate,
                                     int frameRate, int numFrames)
    {
        std::vector<float> level (static_cast<std::size_t> (numFrames), 0.0f);

        const auto samplesPerFrame = sampleRate / static_cast<double> (frameRate);

        for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
        {
            const auto first = static_cast<int> (static_cast<double> (frameIndex) * samplesPerFrame);
            const auto last = std::min (static_cast<int> (static_cast<double> (frameIndex + 1) * samplesPerFrame),
                                        numSamples);

            auto sumOfSquares = 0.0;

            for (int sampleIndex = std::max (first, 0); sampleIndex < last; ++sampleIndex)
                sumOfSquares += static_cast<double> (samples[sampleIndex]) * static_cast<double> (samples[sampleIndex]);

            level[static_cast<std::size_t> (frameIndex)] =
                static_cast<float> (std::sqrt (sumOfSquares / std::max (1, last - first)));
        }

        return level;
    }
}

bool VoiceSynthesiser::encodeChannel (const float* samples,
                                      int numSamples,
                                      const PitchTrack& melody,
                                      std::vector<float>& destination,
                                      const ProgressCallback& onProgress,
                                      float progressFrom,
                                      float progressTo,
                                      const std::atomic<bool>& shouldAbort,
                                      juce::String& error)
{
    const auto analysisSampleRate = manifest.contentSampleRate;
    const auto hopSize = manifest.melConfiguration.hopSizeInSamples;

    const SincResampler toAnalysisRate { sourceSampleRate, static_cast<double> (analysisSampleRate) };
    auto analysis = toAnalysisRate.process (samples, numSamples);

    if (analysis.empty())
    {
        error = "resampling to the analysis rate produced nothing";
        return false;
    }

    voiceModel.getInputFilter().process (analysis.data(), static_cast<int> (analysis.size()));

    const auto numAnalysisSamples = static_cast<int> (analysis.size());

    const auto contextPadding = static_cast<int> (manifest.contextPaddingSeconds
                                                  * static_cast<double> (analysisSampleRate));
    contextFrames = contextPadding / hopSize;

    const auto featureDim = manifest.featureDim;
    const auto upsampleFactor = manifest.getFeatureUpsampleFactor();
    const auto samplesPerFeature = analysisSampleRate / manifest.contentFrameRate;
    const auto numFeatureFrames = (numFrames + upsampleFactor - 1) / upsampleFactor;
    const auto paddingFeatureFrames = contextPadding / samplesPerFeature;
    const auto chunkFeatureFrames = static_cast<int> (contentChunkSeconds * manifest.contentFrameRate);

    destination.assign (static_cast<std::size_t> (numFrames) * static_cast<std::size_t> (featureDim), 0.0f);

    const auto* retriever = voiceModel.getRetriever();

    for (auto firstFeature = 0; firstFeature < numFeatureFrames; firstFeature += chunkFeatureFrames)
    {
        if (shouldAbort.load())
            return false;

        const auto lastFeature = std::min (firstFeature + chunkFeatureFrames, numFeatureFrames);
        const auto numChunkFeatures = lastFeature - firstFeature;

        const auto chunk = takeReflected (analysis.data(),
                                          numAnalysisSamples,
                                          (firstFeature - paddingFeatureFrames) * samplesPerFeature,
                                          (numChunkFeatures + 2 * paddingFeatureFrames) * samplesPerFeature);

        auto features = encodeContent (chunk.data(), static_cast<int> (chunk.size()), error);

        if (features.empty())
            return false;

        const auto numReturned = static_cast<int> (features.size() / static_cast<std::size_t> (featureDim));
        const auto numUsable = std::min (numChunkFeatures, numReturned - paddingFeatureFrames);

        if (numUsable <= 0)
        {
            error = "the content encoder returned fewer frames than the chunk covers";
            return false;
        }

        std::vector<float> kept (static_cast<std::size_t> (numUsable) * static_cast<std::size_t> (featureDim));
        std::copy (features.begin() + static_cast<std::ptrdiff_t> (paddingFeatureFrames) * featureDim,
                   features.begin() + static_cast<std::ptrdiff_t> (paddingFeatureFrames + numUsable) * featureDim,
                   kept.begin());

        std::vector<float> asSung;

        if (settings.consonantProtection < 1.0f)
            asSung = kept;

        if (retriever != nullptr && settings.retrievalRatio > 0.0f)
            retriever->blend (kept.data(), numUsable, settings.retrievalRatio);

        for (int featureIndex = 0; featureIndex < numUsable; ++featureIndex)
        {
            const auto* retrieved = kept.data() + static_cast<std::size_t> (featureIndex)
                                                      * static_cast<std::size_t> (featureDim);
            const auto* sung = asSung.empty()
                             ? nullptr
                             : asSung.data() + static_cast<std::size_t> (featureIndex)
                                                   * static_cast<std::size_t> (featureDim);

            for (int repeat = 0; repeat < upsampleFactor; ++repeat)
            {
                const auto frameIndex = (firstFeature + featureIndex) * upsampleFactor + repeat;

                if (frameIndex >= numFrames)
                    break;

                auto* target = destination.data() + static_cast<std::size_t> (frameIndex)
                                                        * static_cast<std::size_t> (featureDim);

                if (sung == nullptr || melody.isVoiced (frameIndex))
                {
                    std::copy (retrieved, retrieved + featureDim, target);
                }
                else
                {
                    const auto protection = settings.consonantProtection;

                    for (int dimension = 0; dimension < featureDim; ++dimension)
                        target[dimension] = protection * retrieved[dimension]
                                          + (1.0f - protection) * sung[dimension];
                }
            }
        }

        if (onProgress)
            onProgress (progressFrom + (progressTo - progressFrom)
                                           * static_cast<float> (lastFeature)
                                           / static_cast<float> (numFeatureFrames));
    }

    return true;
}

bool VoiceSynthesiser::prepare (const juce::AudioBuffer<float>& recording,
                                double newSampleRate,
                                const PitchTrack& melody,
                                const ProgressCallback& onProgress,
                                const std::atomic<bool>& shouldAbort,
                                juce::String& error)
{
    conditioning.clear();
    sourceLevel.clear();
    numFrames = 0;

    const auto numSamples = recording.getNumSamples();

    if (numSamples <= 0 || recording.getNumChannels() <= 0 || melody.getNumFrames() <= 0)
    {
        error = "there is nothing to prepare";
        return false;
    }

    sourceSampleRate = newSampleRate;
    numSourceSamples = numSamples;
    numSourceChannels = recording.getNumChannels();
    frameRate = melody.frameRate;

    if (frameRate != manifest.getPitchFrameRate())
    {
        error = "the melody is framed at " + juce::String (frameRate) + " Hz, the voice expects "
              + juce::String (manifest.getPitchFrameRate()) + " Hz";
        return false;
    }

    const auto hopSize = manifest.melConfiguration.hopSizeInSamples;
    const auto numAnalysisSamples = static_cast<int> (std::llround (static_cast<double> (numSamples)
                                                                   * manifest.contentSampleRate
                                                                   / sourceSampleRate));

    numFrames = std::min (melody.getNumFrames(), numAnalysisSamples / hopSize);

    if (numFrames <= 0)
    {
        error = "the recording is shorter than one analysis frame";
        return false;
    }

    // Two channels holding the same signal are one channel's work, and one vocoder pass.
    auto numAnalysed = 1;

    for (int channel = 1; channel < numSourceChannels; ++channel)
    {
        const auto* first = recording.getReadPointer (0);
        const auto* other = recording.getReadPointer (channel);

        if (! std::equal (first, first + numSamples, other))
        {
            numAnalysed = numSourceChannels;
            break;
        }
    }

    conditioning.resize (static_cast<std::size_t> (numAnalysed));
    sourceLevel.resize (static_cast<std::size_t> (numAnalysed));

    for (int channel = 0; channel < numAnalysed; ++channel)
    {
        const auto from = static_cast<float> (channel) / static_cast<float> (numAnalysed);
        const auto to = static_cast<float> (channel + 1) / static_cast<float> (numAnalysed);

        sourceLevel[static_cast<std::size_t> (channel)] =
            measureLevel (recording.getReadPointer (channel), numSamples, sourceSampleRate,
                          frameRate, numFrames);

        if (! encodeChannel (recording.getReadPointer (channel), numSamples, melody,
                             conditioning[static_cast<std::size_t> (channel)],
                             onProgress, from, to, shouldAbort, error))
        {
            conditioning.clear();
            return false;
        }
    }

    if (onProgress)
        onProgress (1.0f);

    return true;
}

void VoiceSynthesiser::followRecording (std::vector<float>& span,
                                        int channel,
                                        int firstFrame,
                                        int numSpanFrames) const
{
    if (envelopeFollow <= 0.0f || sourceLevel.empty() || span.empty())
        return;

    const auto& level = sourceLevel[static_cast<std::size_t> (std::min (channel,
                                                                        static_cast<int> (sourceLevel.size()) - 1))];

    const auto samplesPerFrame = sourceSampleRate / static_cast<double> (frameRate);

    std::vector<float> gain (static_cast<std::size_t> (numSpanFrames), 1.0f);

    for (int spanFrame = 0; spanFrame < numSpanFrames; ++spanFrame)
    {
        const auto frameIndex = std::min (firstFrame + spanFrame, static_cast<int> (level.size()) - 1);

        const auto first = static_cast<int> (static_cast<double> (spanFrame) * samplesPerFrame);
        const auto last = std::min (static_cast<int> (static_cast<double> (spanFrame + 1) * samplesPerFrame),
                                    static_cast<int> (span.size()));

        auto sumOfSquares = 0.0;

        for (int sampleIndex = std::max (first, 0); sampleIndex < last; ++sampleIndex)
            sumOfSquares += static_cast<double> (span[static_cast<std::size_t> (sampleIndex)])
                          * static_cast<double> (span[static_cast<std::size_t> (sampleIndex)]);

        const auto rendered = std::sqrt (sumOfSquares / std::max (1, last - first));
        const auto wanted = static_cast<double> (level[static_cast<std::size_t> (frameIndex)]);

        const auto ratio = rendered > 1.0e-6 ? wanted / rendered : 1.0;

        gain[static_cast<std::size_t> (spanFrame)] =
            static_cast<float> (std::clamp (std::pow (ratio, static_cast<double> (envelopeFollow)), 0.25, 4.0));
    }

    const auto smoothed = gaussianFilter (gain, 4.0);

    for (std::size_t sampleIndex = 0; sampleIndex < span.size(); ++sampleIndex)
    {
        const auto position = static_cast<double> (sampleIndex) / samplesPerFrame;
        const auto lower = std::min (static_cast<int> (position), numSpanFrames - 1);
        const auto upper = std::min (lower + 1, numSpanFrames - 1);
        const auto fraction = static_cast<float> (position - static_cast<double> (lower));

        const auto factor = smoothed[static_cast<std::size_t> (lower)] * (1.0f - fraction)
                          + smoothed[static_cast<std::size_t> (upper)] * fraction;

        span[sampleIndex] *= factor;
    }
}

std::vector<float> VoiceSynthesiser::renderChannel (int channel,
                                                    const float* melodyHz,
                                                    int paddedFirst,
                                                    int numPaddedFrames,
                                                    juce::String& error) const
{
    const auto featureDim = manifest.featureDim;
    const auto latentDim = manifest.latentDim;

    const CoarsePitchQuantiser quantiser { manifest.pitchMinimumHz,
                                           manifest.pitchMaximumHz,
                                           manifest.numCoarsePitchBins };

    const auto* melody = melodyHz + paddedFirst;

    std::vector<std::int64_t> coarsePitch (static_cast<std::size_t> (numPaddedFrames));

    for (int frameIndex = 0; frameIndex < numPaddedFrames; ++frameIndex)
        coarsePitch[static_cast<std::size_t> (frameIndex)] =
            quantiser.toBin (static_cast<double> (melody[frameIndex]));

    std::vector<float> latentNoise (static_cast<std::size_t> (latentDim)
                                    * static_cast<std::size_t> (numPaddedFrames));

    for (int dimension = 0; dimension < latentDim; ++dimension)
        for (int frameIndex = 0; frameIndex < numPaddedFrames; ++frameIndex)
            latentNoise[static_cast<std::size_t> (dimension) * static_cast<std::size_t> (numPaddedFrames)
                        + static_cast<std::size_t> (frameIndex)] =
                normalAt (latentNoiseSeed, dimension, paddedFirst + frameIndex);

    const std::int64_t numFramesValue = numPaddedFrames;
    const std::int64_t speakerIdValue = manifest.speakerId;

    const auto& names = manifest.vocoderInputs;
    const auto& features = conditioning[static_cast<std::size_t> (channel)];

    const std::vector<OnnxSession::TensorView> inputs {
        OnnxSession::TensorView::floats (names[0].c_str(),
                                        features.data() + static_cast<std::size_t> (paddedFirst)
                                                              * static_cast<std::size_t> (featureDim),
                                        { 1, numPaddedFrames, featureDim }),
        OnnxSession::TensorView::integers (names[1].c_str(), &numFramesValue, { 1 }),
        OnnxSession::TensorView::integers (names[2].c_str(), coarsePitch.data(), { 1, numPaddedFrames }),
        OnnxSession::TensorView::floats (names[3].c_str(), melody, { 1, numPaddedFrames }),
        OnnxSession::TensorView::integers (names[4].c_str(), &speakerIdValue, { 1 }),
        OnnxSession::TensorView::floats (names[5].c_str(), latentNoise.data(), { 1, latentDim, numPaddedFrames }),
    };

    const std::vector<const char*> outputs { manifest.vocoderOutput.c_str() };

    auto returned = voiceModel.getVocoder().run (inputs, outputs);

    if (returned.empty())
    {
        error = "vocoder failed: " + voiceModel.getVocoder().getError();
        return {};
    }

    const auto shape = returned.front().GetTensorTypeAndShapeInfo().GetShape();
    const auto numRendered = static_cast<int> (shape.back());
    const auto* rendered = returned.front().GetTensorData<float>();

    const SincResampler toSourceRate { static_cast<double> (manifest.modelSampleRate), sourceSampleRate };

    return toSourceRate.isPassThrough() ? std::vector<float> (rendered, rendered + numRendered)
                                        : toSourceRate.process (rendered, numRendered);
}

bool VoiceSynthesiser::render (const float* melodyHz,
                               int firstFrame,
                               int numSpanFrames,
                               juce::AudioBuffer<float>& destination,
                               juce::String& error) const
{
    if (! isPrepared() || melodyHz == nullptr || numSpanFrames <= 0)
    {
        error = "nothing to render";
        return false;
    }

    const auto paddedFirst = std::max (firstFrame - contextFrames, 0);
    const auto paddedLast = std::min (firstFrame + numSpanFrames + contextFrames, numFrames);
    const auto numPaddedFrames = paddedLast - paddedFirst;

    if (numPaddedFrames <= 0)
    {
        error = "the span lies outside the recording";
        return false;
    }

    const auto toSourceSample = [this] (int frameIndex)
    {
        return static_cast<int> (std::llround (static_cast<double> (frameIndex) * sourceSampleRate
                                               / static_cast<double> (frameRate)));
    };

    const auto firstSourceSample = toSourceSample (firstFrame);
    const auto numWanted = toSourceSample (firstFrame + numSpanFrames) - firstSourceSample;
    const auto offset = firstSourceSample - toSourceSample (paddedFirst);

    if (numWanted <= 0 || offset < 0)
    {
        error = "the span is empty";
        return false;
    }

    destination.setSize (numSourceChannels, numWanted, false, false, true);
    destination.clear();

    for (int channel = 0; channel < numSourceChannels; ++channel)
    {
        const auto analysed = std::min (channel, static_cast<int> (conditioning.size()) - 1);

        if (channel > 0 && analysed == std::min (channel - 1, static_cast<int> (conditioning.size()) - 1))
        {
            destination.copyFrom (channel, 0, destination, channel - 1, 0, numWanted);
            continue;
        }

        auto atSourceRate = renderChannel (analysed, melodyHz, paddedFirst, numPaddedFrames, error);

        const auto numAvailable = std::min (numWanted, static_cast<int> (atSourceRate.size()) - offset);

        if (atSourceRate.empty() || numAvailable <= 0)
        {
            if (error.isEmpty())
                error = "the vocoder returned less audio than the span covers";

            return false;
        }

        std::vector<float> span (atSourceRate.begin() + offset,
                                 atSourceRate.begin() + offset + numAvailable);

        followRecording (span, analysed, firstFrame, numSpanFrames);

        destination.copyFrom (channel, 0, span.data(), numAvailable);
    }

    return true;
}
}
