#include "nn/MelSynthesiser.h"

#include "dsp/MelFilterBank.h"
#include "dsp/SincResampler.h"

#include <algorithm>
#include <cmath>

namespace multiplyandreplenish
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

bool MelSynthesiser::prepare (const juce::AudioBuffer<float>& recording,
                              double sampleRate,
                              const PitchTrack& melody,
                              const ProgressCallback& onProgress,
                              const std::atomic<bool>& shouldAbort,
                              juce::String& error)
{
    mel.clear();
    numMelFrames = 0;

    const auto numSamples = recording.getNumSamples();

    if (numSamples <= 0 || recording.getNumChannels() <= 0)
    {
        error = "there is nothing to prepare";
        return false;
    }

    sourceSampleRate = sampleRate;
    numSourceSamples = numSamples;
    numSourceChannels = recording.getNumChannels();
    frameRate = melody.frameRate > 0 ? melody.frameRate : 100;
    numFrames = melody.getNumFrames();

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

    const SincResampler toVocoderRate { sampleRate, static_cast<double> (config.sampleRate) };

    mel.resize (static_cast<std::size_t> (numAnalysed));

    for (int channel = 0; channel < numAnalysed; ++channel)
    {
        if (shouldAbort.load())
            return false;

        if (onProgress)
            onProgress (static_cast<float> (channel) / static_cast<float> (numAnalysed));

        const auto resampled = toVocoderRate.process (recording.getReadPointer (channel), numSamples);

        if (resampled.empty())
        {
            error = "resampling to the vocoder's rate produced nothing";
            return false;
        }

        const auto numChannelFrames = melSpectrogram->process (resampled.data(),
                                                              static_cast<int> (resampled.size()),
                                                              mel[static_cast<std::size_t> (channel)]);

        if (numChannelFrames <= 0)
        {
            error = "the recording is shorter than one vocoder frame";
            return false;
        }

        numMelFrames = channel == 0 ? numChannelFrames : std::min (numMelFrames, numChannelFrames);
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

std::vector<float> MelSynthesiser::renderChannel (int channel,
                                                  const std::vector<float>& pitchSpan,
                                                  int firstMelFrame,
                                                  int numSpanMelFrames,
                                                  juce::String& error) const
{
    const auto numMelBins = config.numMelBins;
    const auto& source = mel[static_cast<std::size_t> (channel)];

    std::vector<float> melSpan (static_cast<std::size_t> (numMelBins) * static_cast<std::size_t> (numSpanMelFrames));

    for (int melIndex = 0; melIndex < numMelBins; ++melIndex)
    {
        const auto* row = source.data() + static_cast<std::size_t> (melIndex) * static_cast<std::size_t> (numMelFrames)
                        + static_cast<std::size_t> (firstMelFrame);
        auto* destination = melSpan.data() + static_cast<std::size_t> (melIndex) * static_cast<std::size_t> (numSpanMelFrames);

        for (int frameIndex = 0; frameIndex < numSpanMelFrames; ++frameIndex)
            destination[frameIndex] = std::clamp (row[frameIndex], config.melFloor, config.melCeiling);
    }

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

    return toSourceRate.isPassThrough() ? std::vector<float> (rendered, rendered + numRendered)
                                        : toSourceRate.process (rendered, numRendered);
}

bool MelSynthesiser::render (const float* melodyHz,
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
        return false;
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
        return false;
    }

    const auto pitchSpan = resampleMelody (melodyHz, firstMelFrame, numSpanMelFrames);

    const auto spanFirstSourceSample = static_cast<int> (std::llround (
        static_cast<double> (firstMelFrame) * static_cast<double> (config.hopSizeInSamples)
        * sourceSampleRate / static_cast<double> (config.sampleRate)));

    const auto offset = firstSourceSample - spanFirstSourceSample;

    destination.setSize (numSourceChannels, numWanted, false, false, true);
    destination.clear();

    for (int channel = 0; channel < numSourceChannels; ++channel)
    {
        const auto analysed = std::min (channel, static_cast<int> (mel.size()) - 1);

        if (channel > 0 && analysed == std::min (channel - 1, static_cast<int> (mel.size()) - 1))
        {
            destination.copyFrom (channel, 0, destination, channel - 1, 0, numWanted);
            continue;
        }

        const auto atSourceRate = renderChannel (analysed, pitchSpan, firstMelFrame, numSpanMelFrames, error);

        const auto numAvailable = std::min (numWanted, static_cast<int> (atSourceRate.size()) - offset);

        if (atSourceRate.empty() || offset < 0 || numAvailable <= 0)
        {
            if (error.isEmpty())
                error = "the vocoder returned less audio than the span covers";

            return false;
        }

        destination.copyFrom (channel, 0, atSourceRate.data() + offset, numAvailable);
    }

    return true;
}
}
