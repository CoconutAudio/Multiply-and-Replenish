#include "nn/GameSegmenter.h"

#include "dsp/SincResampler.h"

#include <algorithm>
#include <cmath>

namespace multiplyandreplenish
{
namespace
{
    /** @brief Boundaries are proposed from nothing and then refined, so the first pass is told the
               melody is at the start of the schedule and each later one how far along it is.
    */
    float scheduleAt (int step, int numSteps) noexcept
    {
        return numSteps > 0 ? static_cast<float> (step) / static_cast<float> (numSteps) : 0.0f;
    }
}

GameSegmenter::GameSegmenter (const Configuration& configuration)
    : config (configuration)
{
}

std::unique_ptr<GameSegmenter> GameSegmenter::load (const juce::File& directory,
                                                    const Configuration& configuration,
                                                    int numThreads,
                                                    juce::String& error)
{
    std::unique_ptr<GameSegmenter> segmenter { new GameSegmenter (configuration) };

    const std::pair<OnnxSession*, const char*> networks[] = {
        { &segmenter->encoder, "encoder.onnx" },
        { &segmenter->boundaryModel, "segmenter.onnx" },
        { &segmenter->durationModel, "bd2dur.onnx" },
        { &segmenter->estimator, "estimator.onnx" }
    };

    for (const auto& [session, name] : networks)
    {
        if (! session->load (directory.getChildFile (name), numThreads))
        {
            error = session->getError();
            return nullptr;
        }
    }

    return segmenter;
}

std::vector<GameSegmenter::Segment> GameSegmenter::segmentChunk (const float* samples,
                                                                 int numSamples,
                                                                 int frameOffset,
                                                                 int frameRate,
                                                                 juce::String& error) const
{
    const std::int64_t numSamplesValue = numSamples;
    const auto durationSeconds = static_cast<float> (numSamples) / static_cast<float> (config.sampleRate);

    juce::ignoreUnused (numSamplesValue);

    auto encoded = encoder.run ({ OnnxSession::TensorView::floats ("waveform", samples, { 1, numSamples }),
                                  OnnxSession::TensorView::floats ("duration", &durationSeconds, { 1 }) },
                                { "x_seg", "x_est", "maskT" });

    if (encoded.size() < 3)
    {
        error = "the note encoder failed: " + encoder.getError();
        return {};
    }

    const auto shape = encoded[0].GetTensorTypeAndShapeInfo().GetShape();

    if (shape.size() != 3)
    {
        error = "the note encoder returned an unexpected rank";
        return {};
    }

    const auto numEncoderFrames = static_cast<int> (shape[1]);
    const auto embeddingDim = static_cast<int> (shape[2]);

    const auto* segmentFeatures = encoded[0].GetTensorData<float>();
    const auto* estimateFeatures = encoded[1].GetTensorData<float>();
    const auto* frameMask = encoded[2].GetTensorData<bool>();

    std::vector<bool> known (static_cast<std::size_t> (numEncoderFrames), false);
    std::vector<bool> boundaries (static_cast<std::size_t> (numEncoderFrames), false);

    const std::int64_t language = 0;
    const std::int64_t radius = config.boundaryRadius;

    for (int step = 0; step < std::max (1, config.numRefinementSteps); ++step)
    {
        const auto position = scheduleAt (step, config.numRefinementSteps);

        const std::vector<char> knownBytes (known.begin(), known.end());
        const std::vector<char> previousBytes (boundaries.begin(), boundaries.end());

        auto refined = boundaryModel.run (
            { OnnxSession::TensorView::floats ("x_seg", segmentFeatures, { 1, numEncoderFrames, embeddingDim }),
              OnnxSession::TensorView::integers ("language", &language, { 1 }),
              OnnxSession::TensorView::booleans ("known_boundaries",
                                                 reinterpret_cast<const bool*> (knownBytes.data()),
                                                 { 1, numEncoderFrames }),
              OnnxSession::TensorView::booleans ("prev_boundaries",
                                                 reinterpret_cast<const bool*> (previousBytes.data()),
                                                 { 1, numEncoderFrames }),
              OnnxSession::TensorView::floats ("t", &position, { 1 }),
              OnnxSession::TensorView::booleans ("maskT", frameMask, { 1, numEncoderFrames }),
              OnnxSession::TensorView::scalar ("threshold", &config.boundaryThreshold),
              OnnxSession::TensorView::integers ("radius", &radius, {}) },
            { "boundaries" });

        if (refined.empty())
        {
            error = "the note segmenter failed: " + boundaryModel.getError();
            return {};
        }

        const auto* found = refined.front().GetTensorData<bool>();
        std::copy (found, found + numEncoderFrames, boundaries.begin());
    }

    const std::vector<char> boundaryBytes (boundaries.begin(), boundaries.end());

    auto durations = durationModel.run (
        { OnnxSession::TensorView::booleans ("boundaries",
                                             reinterpret_cast<const bool*> (boundaryBytes.data()),
                                             { 1, numEncoderFrames }),
          OnnxSession::TensorView::booleans ("maskT", frameMask, { 1, numEncoderFrames }) },
        { "durations", "maskN" });

    if (durations.size() < 2)
    {
        error = "the note durations could not be read: " + durationModel.getError();
        return {};
    }

    const auto numSegments = static_cast<int> (durations[0].GetTensorTypeAndShapeInfo().GetShape().back());
    const auto* durationSeconds2 = durations[0].GetTensorData<float>();
    const auto* segmentMask = durations[1].GetTensorData<bool>();

    auto judged = estimator.run (
        { OnnxSession::TensorView::floats ("x_est", estimateFeatures, { 1, numEncoderFrames, embeddingDim }),
          OnnxSession::TensorView::booleans ("boundaries",
                                             reinterpret_cast<const bool*> (boundaryBytes.data()),
                                             { 1, numEncoderFrames }),
          OnnxSession::TensorView::booleans ("maskT", frameMask, { 1, numEncoderFrames }),
          OnnxSession::TensorView::booleans ("maskN", segmentMask, { 1, numSegments }),
          OnnxSession::TensorView::scalar ("threshold", &config.presenceThreshold) },
        { "presence", "scores" });

    if (judged.size() < 2)
    {
        error = "the note estimator failed: " + estimator.getError();
        return {};
    }

    const auto* presence = judged[0].GetTensorData<bool>();
    const auto* pitches = judged[1].GetTensorData<float>();

    std::vector<Segment> found;
    found.reserve (static_cast<std::size_t> (numSegments));

    auto elapsedSeconds = 0.0;

    for (int index = 0; index < numSegments; ++index)
    {
        if (! segmentMask[index])
            break;

        const auto startSeconds = elapsedSeconds;
        elapsedSeconds += static_cast<double> (durationSeconds2[index]);

        Segment segment;
        segment.firstFrame = frameOffset + static_cast<int> (std::llround (startSeconds * frameRate));
        segment.lastFrame = frameOffset + static_cast<int> (std::llround (elapsedSeconds * frameRate));
        segment.midiNote = pitches[index];
        segment.isRest = ! presence[index];

        if (segment.lastFrame <= segment.firstFrame)
            segment.lastFrame = segment.firstFrame + 1;

        found.push_back (segment);
    }

    return found;
}

std::vector<GameSegmenter::Segment> GameSegmenter::segment (const float* samples,
                                                            int numSamples,
                                                            double sampleRate,
                                                            int frameRate,
                                                            juce::String& error) const
{
    if (samples == nullptr || numSamples <= 0)
    {
        error = "there is nothing to segment";
        return {};
    }

    const SincResampler toModelRate { sampleRate, static_cast<double> (config.sampleRate) };
    const auto atModelRate = toModelRate.isPassThrough()
                           ? std::vector<float> (samples, samples + numSamples)
                           : toModelRate.process (samples, numSamples);

    const auto samplesPerFrame = static_cast<int> (std::llround (config.timestep * config.sampleRate));
    const auto samplesPerChunk = config.maximumFramesPerChunk * samplesPerFrame;

    std::vector<Segment> found;

    for (auto firstSample = 0; firstSample < static_cast<int> (atModelRate.size()); firstSample += samplesPerChunk)
    {
        const auto numChunkSamples = std::min (samplesPerChunk,
                                               static_cast<int> (atModelRate.size()) - firstSample);

        if (numChunkSamples < samplesPerFrame)
            break;

        const auto frameOffset = static_cast<int> (std::llround (static_cast<double> (firstSample)
                                                                 / config.sampleRate * frameRate));

        auto chunk = segmentChunk (atModelRate.data() + firstSample, numChunkSamples,
                                   frameOffset, frameRate, error);

        if (chunk.empty() && error.isNotEmpty())
            return {};

        found.insert (found.end(), chunk.begin(), chunk.end());
    }

    return found;
}
}
