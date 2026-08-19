#include "edit/RenderScheduler.h"

#include <algorithm>
#include <cmath>

namespace rvctuner
{
namespace
{
    std::uint64_t mixInto (std::uint64_t hash, float value) noexcept
    {
        const auto quantised = static_cast<std::uint32_t> (std::llround (static_cast<double> (value) * 64.0));

        for (int byteIndex = 0; byteIndex < 4; ++byteIndex)
        {
            hash ^= (quantised >> (8 * byteIndex)) & 0xffU;
            hash *= 0x100000001b3ULL;
        }

        return hash;
    }
}

RenderScheduler::RenderScheduler()
    : juce::Thread ("RVCTuner render")
{
}

RenderScheduler::~RenderScheduler()
{
    stopThread (4000);
}

void RenderScheduler::addListener (Listener* listener) { listeners.add (listener); }
void RenderScheduler::removeListener (Listener* listener) { listeners.remove (listener); }

void RenderScheduler::clear()
{
    stopThread (4000);

    synthesiser.reset();
    spans.clear();
    rendered.setSize (0, 0);
    melody.clear();
    numSourceSamples = 0;

    const juce::ScopedLock lock { errorLock };
    error.clear();
}

void RenderScheduler::setSynthesiser (std::shared_ptr<Synthesiser> newSynthesiser,
                                      int numSamples,
                                      double newSampleRate)
{
    clear();

    if (newSynthesiser == nullptr || ! newSynthesiser->isPrepared())
        return;

    synthesiser = std::move (newSynthesiser);
    sampleRate = newSampleRate;
    numSourceSamples = numSamples;
    frameRate = synthesiser->getFrameRate();

    rendered.setSize (1, numSamples);
    rendered.clear();

    planSpans();
}

int RenderScheduler::getFirstSample (int frameIndex) const
{
    return static_cast<int> (std::llround (static_cast<double> (frameIndex) * sampleRate
                                           / static_cast<double> (frameRate)));
}

void RenderScheduler::planSpans()
{
    spans.clear();

    if (synthesiser == nullptr)
        return;

    const auto numFrames = synthesiser->getNumFrames();

    frameLevel.assign (static_cast<std::size_t> (numFrames), 0.0f);

    if (recording != nullptr)
    {
        for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
        {
            const auto first = getFirstSample (frameIndex);
            const auto last = std::min (getFirstSample (frameIndex + 1), recording->getNumSamples());

            auto peak = 0.0f;

            for (int channel = 0; channel < recording->getNumChannels(); ++channel)
                for (int sampleIndex = first; sampleIndex < last; ++sampleIndex)
                    peak = std::max (peak, std::abs (recording->getSample (channel, sampleIndex)));

            frameLevel[static_cast<std::size_t> (frameIndex)] = peak;
        }
    }

    const auto quietestNear = [this, numFrames] (int frameIndex)
    {
        if (frameLevel.empty())
            return frameIndex;

        const auto first = std::max (1, frameIndex - boundarySearchFrames);
        const auto last = std::min (numFrames - 1, frameIndex + boundarySearchFrames);

        auto quietest = frameIndex;

        for (int candidate = first; candidate < last; ++candidate)
            if (frameLevel[static_cast<std::size_t> (candidate)] < frameLevel[static_cast<std::size_t> (quietest)])
                quietest = candidate;

        return quietest;
    };

    auto firstFrame = 0;

    while (firstFrame < numFrames)
    {
        const auto nominal = firstFrame + spanFrames;
        const auto lastFrame = nominal + boundarySearchFrames >= numFrames ? numFrames
                                                                           : quietestNear (nominal);

        auto span = std::make_unique<Span>();
        span->firstFrame = firstFrame;
        span->lastFrame = std::max (lastFrame, firstFrame + 1);

        firstFrame = span->lastFrame;
        spans.push_back (std::move (span));
    }
}

int RenderScheduler::getSpanForSample (int sampleIndex) const
{
    for (int spanIndex = 0; spanIndex < static_cast<int> (spans.size()); ++spanIndex)
        if (sampleIndex < getFirstSample (spans[static_cast<std::size_t> (spanIndex)]->lastFrame))
            return spanIndex;

    return -1;
}

void RenderScheduler::setSungMelody (std::vector<float> melodyHz)
{
    const juce::ScopedLock lock { melodyLock };
    sung = std::move (melodyHz);
}

void RenderScheduler::setRecording (const juce::AudioBuffer<float>* newRecording)
{
    recording = newRecording;
}

void RenderScheduler::setKeepingUneditedAudio (bool shouldKeep)
{
    if (keepUnedited.exchange (shouldKeep) == shouldKeep)
        return;

    for (auto& span : spans)
        span->renderedFrom.store (0, std::memory_order_release);

    melodyIsDirty.store (true, std::memory_order_release);

    if (synthesiser != nullptr && ! spans.empty() && ! isThreadRunning())
        startThread (Priority::low);
}

void RenderScheduler::setPriorityFrame (int frameIndex)
{
    priorityFrame.store (frameIndex, std::memory_order_release);
}

void RenderScheduler::setMelody (std::vector<float> melodyHz, std::vector<float> gainPerFrame)
{
    {
        const juce::ScopedLock lock { melodyLock };
        melody = std::move (melodyHz);
        gain = std::move (gainPerFrame);
    }

    melodyIsDirty.store (true, std::memory_order_release);

    if (synthesiser != nullptr && ! spans.empty() && ! isThreadRunning())
        startThread (Priority::low);
}

std::uint64_t RenderScheduler::hashMelody (int spanIndex) const
{
    if (synthesiser == nullptr)
        return 0;

    const auto& span = *spans[static_cast<std::size_t> (spanIndex)];
    const auto dependency = synthesiser->getDependencyFrames();
    const auto numFrames = static_cast<int> (renderMelody.size());

    const auto first = std::max (0, span.firstFrame - dependency);
    const auto last = std::min (numFrames, span.lastFrame + dependency);

    auto hash = 0xcbf29ce484222325ULL;

    for (int frameIndex = first; frameIndex < last; ++frameIndex)
    {
        hash = mixInto (hash, renderMelody[static_cast<std::size_t> (frameIndex)]);

        if (frameIndex < static_cast<int> (renderGain.size()))
            hash = mixInto (hash, renderGain[static_cast<std::size_t> (frameIndex)]);
    }

    return hash;
}

void RenderScheduler::rescheduleSpans()
{
    if (synthesiser == nullptr)
        return;

    {
        const juce::ScopedLock lock { melodyLock };
        renderMelody = melody;
        renderSung = sung;
        renderGain = gain;
    }

    if (static_cast<int> (renderMelody.size()) < synthesiser->getNumFrames())
        renderMelody.resize (static_cast<std::size_t> (synthesiser->getNumFrames()), 0.0f);

    for (int spanIndex = 0; spanIndex < static_cast<int> (spans.size()); ++spanIndex)
        spans[static_cast<std::size_t> (spanIndex)]->wanted.store (hashMelody (spanIndex),
                                                                   std::memory_order_release);
}

int RenderScheduler::chooseSpan() const
{
    const auto priorityFrameIndex = priorityFrame.load (std::memory_order_acquire);

    auto prioritySpan = 0;

    for (int spanIndex = 0; spanIndex < static_cast<int> (spans.size()); ++spanIndex)
        if (priorityFrameIndex >= spans[static_cast<std::size_t> (spanIndex)]->firstFrame)
            prioritySpan = spanIndex;

    auto best = -1;
    auto bestDistance = std::numeric_limits<int>::max();

    for (int spanIndex = 0; spanIndex < static_cast<int> (spans.size()); ++spanIndex)
    {
        const auto& span = *spans[static_cast<std::size_t> (spanIndex)];

        const auto wanted = span.wanted.load (std::memory_order_acquire);

        if (wanted != 0 && span.renderedFrom.load (std::memory_order_acquire) == wanted)
            continue;

        // Ahead of the playhead first, since that is what will be heard next.
        const auto distance = spanIndex >= prioritySpan ? spanIndex - prioritySpan
                                                        : (prioritySpan - spanIndex) * 4;

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = spanIndex;
        }
    }

    return best;
}

bool RenderScheduler::isUnedited (int spanIndex) const
{
    if (! keepUnedited.load (std::memory_order_acquire) || recording == nullptr)
        return false;

    if (renderSung.size() != renderMelody.size() || renderSung.empty())
        return false;

    const auto& span = *spans[static_cast<std::size_t> (spanIndex)];
    const auto dependency = synthesiser->getDependencyFrames();
    const auto numFrames = static_cast<int> (renderMelody.size());

    const auto first = std::max (0, span.firstFrame - dependency);
    const auto last = std::min (numFrames, span.lastFrame + dependency);

    for (int frameIndex = first; frameIndex < last; ++frameIndex)
    {
        const auto index = static_cast<std::size_t> (frameIndex);

        if (! renderGain.empty() && std::abs (renderGain[index] - 1.0f) > 1.0e-3f)
            return false;

        const auto sungHz = renderSung[index];
        const auto editedHz = renderMelody[index];

        if (sungHz <= 0.0f || editedHz <= 0.0f)
        {
            if (sungHz != editedHz)
                return false;

            continue;
        }

        // A hundredth of a semitone is far below what anyone hears, and well below what the
        // detector resolves; anything larger is an edit.
        if (std::abs (1200.0f * std::log2 (editedHz / sungHz)) > 1.0f)
            return false;
    }

    return true;
}

void RenderScheduler::passThrough (int firstSample, int numSamples)
{
    auto* destination = rendered.getWritePointer (0, firstSample);
    const auto numChannels = recording->getNumChannels();

    std::fill (destination, destination + numSamples, 0.0f);

    for (int channel = 0; channel < numChannels; ++channel)
    {
        const auto* source = recording->getReadPointer (channel, firstSample);

        for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
            destination[sampleIndex] += source[sampleIndex] / static_cast<float> (numChannels);
    }
}

bool RenderScheduler::renderSpan (int spanIndex)
{
    auto& span = *spans[static_cast<std::size_t> (spanIndex)];

    const auto wanted = span.wanted.load (std::memory_order_acquire);
    const auto firstFrame = span.firstFrame;
    const auto numFrames = std::min (span.lastFrame, synthesiser->getNumFrames()) - firstFrame;

    if (numFrames <= 0)
        return false;

    const auto firstSample = getFirstSample (firstFrame);

    const auto untouched = isUnedited (spanIndex);

    std::vector<float> audio;

    if (! untouched)
    {
        juce::String renderError;
        audio = synthesiser->render (renderMelody.data(), firstFrame, numFrames, renderError);

        if (audio.empty())
        {
            const juce::ScopedLock lock { errorLock };
            error = renderError;
            return false;
        }
    }

    const auto numSamples = std::min (untouched ? getFirstSample (firstFrame + numFrames) - firstSample
                                                : static_cast<int> (audio.size()),
                                      numSourceSamples - firstSample);

    if (numSamples <= 0)
        return false;

    span.sequence.fetch_add (1, std::memory_order_acq_rel);

    if (untouched)
        passThrough (firstSample, numSamples);
    else
        rendered.copyFrom (0, firstSample, audio.data(), numSamples);

    if (! renderGain.empty())
    {
        auto* samples = rendered.getWritePointer (0, firstSample);

        for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
        {
            const auto frameIndex = std::min (static_cast<int> (renderGain.size()) - 1,
                                              firstFrame + sampleIndex * frameRate
                                                  / static_cast<int> (sampleRate));

            samples[sampleIndex] *= renderGain[static_cast<std::size_t> (frameIndex)];
        }
    }

    span.renderedFrom.store (wanted, std::memory_order_release);
    span.sequence.fetch_add (1, std::memory_order_acq_rel);

    return true;
}

void RenderScheduler::run()
{
    rendering.store (true, std::memory_order_release);

    while (! threadShouldExit())
    {
        // The copy the renders read belongs to this thread, so an edit only sets a flag.
        if (melodyIsDirty.exchange (false, std::memory_order_acq_rel))
            rescheduleSpans();

        const auto spanIndex = chooseSpan();

        if (spanIndex < 0)
        {
            if (melodyIsDirty.load (std::memory_order_acquire))
                continue;

            break;
        }

        if (! renderSpan (spanIndex))
            break;

        triggerAsyncUpdate();
    }

    rendering.store (false, std::memory_order_release);
    triggerAsyncUpdate();
}

bool RenderScheduler::renderEverything (const std::atomic<bool>& shouldAbort)
{
    if (synthesiser == nullptr)
        return false;

    stopThread (4000);
    melodyIsDirty.store (false, std::memory_order_release);
    rescheduleSpans();

    for (;;)
    {
        if (shouldAbort.load())
            return false;

        const auto spanIndex = chooseSpan();

        if (spanIndex < 0)
            return true;

        if (! renderSpan (spanIndex))
            return false;
    }
}

void RenderScheduler::handleAsyncUpdate()
{
    listeners.call ([] (Listener& listener) { listener.renderChanged(); });
}

int RenderScheduler::read (float* destination, int firstSample, int numSamples) const
{
    if (destination == nullptr || rendered.getNumSamples() == 0 || numSamples <= 0)
        return 0;

    auto numWritten = 0;

    while (numWritten < numSamples)
    {
        const auto sampleIndex = firstSample + numWritten;

        if (sampleIndex < 0 || sampleIndex >= rendered.getNumSamples())
            break;

        const auto spanIndex = getSpanForSample (sampleIndex);

        if (spanIndex < 0)
            break;

        const auto& span = *spans[static_cast<std::size_t> (spanIndex)];

        const auto spanEnd = std::min (getFirstSample (span.lastFrame), rendered.getNumSamples());
        const auto numFromSpan = std::min (numSamples - numWritten, spanEnd - sampleIndex);

        if (numFromSpan <= 0)
            break;

        const auto before = span.sequence.load (std::memory_order_acquire);

        if ((before & 1U) != 0 || span.renderedFrom.load (std::memory_order_acquire) == 0)
            break;

        std::copy (rendered.getReadPointer (0, sampleIndex),
                   rendered.getReadPointer (0, sampleIndex) + numFromSpan,
                   destination + numWritten);

        if (span.sequence.load (std::memory_order_acquire) != before)
            break;

        numWritten += numFromSpan;
    }

    return numWritten;
}

bool RenderScheduler::isReady (int firstSample, int numSamples) const
{
    if (spans.empty() || numSamples <= 0)
        return false;

    const auto firstSpan = getSpanForSample (firstSample);
    const auto lastSpan = getSpanForSample (firstSample + numSamples - 1);

    if (firstSpan < 0)
        return false;

    const auto last = lastSpan < 0 ? static_cast<int> (spans.size()) - 1 : lastSpan;

    for (int spanIndex = firstSpan; spanIndex <= last; ++spanIndex)
    {
        const auto& span = *spans[static_cast<std::size_t> (spanIndex)];

        if (span.renderedFrom.load (std::memory_order_acquire) != span.wanted.load (std::memory_order_acquire))
            return false;
    }

    return true;
}

double RenderScheduler::getFractionReady() const
{
    if (spans.empty())
        return 0.0;

    auto numReady = 0;

    for (const auto& span : spans)
    {
        const auto wanted = span->wanted.load (std::memory_order_acquire);

        if (wanted != 0 && span->renderedFrom.load (std::memory_order_acquire) == wanted)
            ++numReady;
    }

    return static_cast<double> (numReady) / static_cast<double> (spans.size());
}

juce::String RenderScheduler::getError() const
{
    const juce::ScopedLock lock { errorLock };
    return error;
}
}
