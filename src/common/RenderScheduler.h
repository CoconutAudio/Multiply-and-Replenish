#pragma once

#include "nn/Synthesiser.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <memory>
#include <vector>

namespace multiplyandreplenish
{
/** @brief Keeps the rendered audio in step with the melody, a span at a time, off the message thread.

    The recording is divided into spans of a couple of seconds. Each span remembers the melody it
    was rendered from, so an edit re-renders only the spans it touched, and the span under the
    playhead is rendered first. The audio thread reads finished spans without waiting for anything.
*/
class RenderScheduler final : private juce::Thread,
                              private juce::AsyncUpdater
{
public:
    RenderScheduler();
    ~RenderScheduler() override;

    /** @brief Notified on the message thread whenever more of the recording is ready. */
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void renderChanged() = 0;
    };

    void addListener (Listener* listener);
    void removeListener (Listener* listener);

    /** @brief Takes a prepared synthesiser and the length of the recording it was prepared with. */
    void setSynthesiser (std::shared_ptr<Synthesiser> synthesiser, int numSamples, double sampleRate);

    /** @brief Forgets the synthesiser and everything rendered with it. */
    void clear();

    /** @brief Sets what to render, and schedules whichever spans it changed.
        @param melodyHz  The edited melody, one entry per frame.
        @param gain      The level per frame, or empty to render everything as it comes out.
    */
    void setMelody (std::vector<float> melodyHz, std::vector<float> gain = {});

    /** @brief The melody as sung, against which a span counts as untouched. */
    void setSungMelody (std::vector<float> melodyHz);

    /** @brief Where the take was voiced, one entry per frame.

        The engine is given a pitch everywhere, including where nothing was sung, because that is
        what it was trained on and because handing it a hole makes it invent a way across. What it
        renders over a long silence is not wanted though, so the recording is faded back in there.
    */
    void setVoicing (std::vector<bool> voiced);

    /** @brief The recording to pass through, which must outlive the scheduler.

        Stretches where the melody is unchanged play as recorded rather than re-synthesised: there
        is nothing for the engine to improve on, and an edit then costs only the spans it touched.
    */
    void setRecording (const juce::AudioBuffer<float>* recording);

    /** @brief Renders around this frame first, which is where the playhead is. */
    void setPriorityFrame (int frameIndex);

    /** @brief Copies rendered audio out for playback.

        Channels are written one for one, and a destination with more channels than the render has
        is filled by repeating the last one, so a mono render reaches both ears.

        @param destination       Where to write; untouched wherever nothing is rendered yet.
        @param destinationStart  First sample of @p destination to write.
        @param firstSample       First sample wanted, counted from the start of the recording.
        @param numSamples        How many samples to write.
        @return The number of samples actually written, which is zero when nothing is ready.
    */
    int read (juce::AudioBuffer<float>& destination,
              int destinationStart,
              int firstSample,
              int numSamples) const;

    /** @brief Whether every sample of a stretch has been rendered. */
    [[nodiscard]] bool isReady (int firstSample, int numSamples) const;

    [[nodiscard]] double getFractionReady() const;

    [[nodiscard]] bool isRendering() const noexcept { return rendering.load (std::memory_order_acquire); }

    [[nodiscard]] juce::String getError() const;

private:
    /** @brief One span of the recording, and the melody the audio in it came from.

        @c sequence is odd while the span is being written, so that a reader can tell whether what
        it copied is whole without taking a lock the audio thread cannot afford.
    */
    struct Span
    {
        int firstFrame { 0 };
        int lastFrame { 0 };

        std::atomic<std::uint64_t> sequence { 0 };
        std::atomic<std::uint64_t> renderedFrom { 0 };
        std::atomic<std::uint64_t> wanted { 0 };
    };

    void run() override;
    void handleAsyncUpdate() override;

    /** @brief The melody a span was rendered from, hashed; only the render thread reads the copy. */
    [[nodiscard]] std::uint64_t hashMelody (int spanIndex) const;
    [[nodiscard]] int chooseSpan() const;
    bool renderSpan (int spanIndex);

    /** @brief Whether a span and its context were left exactly as sung. */
    [[nodiscard]] bool isUnedited (int spanIndex) const;

    /** @brief Copies the recording into a span, channel for channel. */
    void passThrough (int firstSample, int numSamples);

    /** @brief Fades the recording back in over the stretches where nothing was sung for long.

        Short gaps between notes are left to the engine: they carry the end of one note into the
        start of the next, and splicing the recording into them puts a seam where a singer had
        none. Only a silence long enough to be a breath or a rest is worth keeping as recorded.
    */
    void restoreSilences (int firstFrame, int numFrames, int firstSample, int numSamples);

    /** @brief How long a stretch of nothing has to be before the recording is put back, in ms. */
    static constexpr double keepRecordingMilliseconds = 280.0;

    /** @brief How long the crossfade into and out of that stretch is, in milliseconds. */
    static constexpr double crossfadeMilliseconds = 12.0;
    void rescheduleSpans();

    /** @brief The nominal length of a span, before it is moved to the nearest quiet frame. */
    static constexpr int spanFrames = 200;

    /** @brief How far a boundary may move to find somewhere quiet to fall. */
    static constexpr int boundarySearchFrames = 25;

    /** @brief Puts the span boundaries where the recording is quietest, so that the joins between
               one span and the next fall where nothing is being sung.
    */
    void planSpans();

    [[nodiscard]] int getFirstSample (int frameIndex) const;

    [[nodiscard]] int getSpanForSample (int sampleIndex) const;

    std::shared_ptr<Synthesiser> synthesiser;

    juce::CriticalSection melodyLock;
    std::vector<float> melody;
    std::vector<float> sung;
    std::vector<float> gain;
    std::vector<bool> voicing;
    std::vector<float> renderMelody;
    std::vector<float> renderSung;
    std::vector<float> renderGain;
    std::vector<bool> renderVoicing;

    const juce::AudioBuffer<float>* recording { nullptr };
    std::vector<float> frameLevel;

    juce::AudioBuffer<float> rendered;
    juce::AudioBuffer<float> spanScratch;
    std::vector<std::unique_ptr<Span>> spans;

    double sampleRate { 44100.0 };
    int numSourceSamples { 0 };
    int frameRate { 100 };

    std::atomic<bool> melodyIsDirty { false };
    std::atomic<int> priorityFrame { 0 };
    std::atomic<bool> rendering { false };

    mutable juce::CriticalSection errorLock;
    juce::String error;

    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RenderScheduler)
};
}
