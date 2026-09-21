#pragma once

#include "nn/OnnxSession.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>
#include <vector>

namespace multiplyandreplenish
{
/** @brief GAME: the note segmenter PitchNet uses, four networks and a short diffusion loop.

    An encoder reads the waveform into two sequences, one for cutting the take up and one for
    judging what the pieces are. A discrete diffusion model proposes note boundaries and refines
    them over a couple of passes. The boundaries become durations, and an estimator says which of
    the resulting segments are notes rather than rests, and what pitch each one is.

    It hears notes where a rule about pitch excursions cannot: a repeated note re-attacked at the
    same pitch is two notes here and one to a threshold.
*/
class GameSegmenter
{
public:
    /** @brief One segment the model found, on the caller's frame grid. */
    struct Segment
    {
        int firstFrame { 0 };
        int lastFrame { 0 };

        /** @brief The pitch the estimator read, in semitones on the MIDI scale. */
        float midiNote { 60.0f };

        /** @brief True when the estimator called this a rest rather than a note. */
        bool isRest { false };
    };

    /** @brief The constants the exported networks were sampled with. */
    struct Configuration
    {
        int sampleRate { 44100 };

        /** @brief Seconds per encoder frame, so 441 samples at 44.1 kHz. */
        double timestep { 0.01 };

        /** @brief Passes of the diffusion loop; each one sees the last one's boundaries. */
        int numRefinementSteps { 2 };

        float boundaryThreshold { 0.2f };
        int boundaryRadius { 2 };
        float presenceThreshold { 0.2f };

        /** @brief The encoder's own limit, which is what forces a long take to be read in pieces. */
        int maximumFramesPerChunk { 5000 };
    };

    /** @brief Loads the four networks from a directory holding GAME's exports. */
    static std::unique_ptr<GameSegmenter> load (const juce::File& directory,
                                                const Configuration& configuration,
                                                int numThreads,
                                                juce::String& error);

    /** @brief Finds the notes in a recording.
        @param samples     Mono audio.
        @param numSamples  Length of @p samples.
        @param sampleRate  Rate @p samples arrived at; resampled internally if it differs.
        @param frameRate   The grid the returned segments are counted in.
        @param error       Set when segmentation fails.
        @return The segments, in time order, rests included.
    */
    [[nodiscard]] std::vector<Segment> segment (const float* samples,
                                                int numSamples,
                                                double sampleRate,
                                                int frameRate,
                                                juce::String& error) const;

private:
    explicit GameSegmenter (const Configuration& configuration);

    /** @brief Runs the four networks over one stretch short enough for the encoder. */
    [[nodiscard]] std::vector<Segment> segmentChunk (const float* samples,
                                                     int numSamples,
                                                     int frameOffset,
                                                     int frameRate,
                                                     juce::String& error) const;

    Configuration config;

    OnnxSession encoder;
    OnnxSession boundaryModel;
    OnnxSession durationModel;
    OnnxSession estimator;
};
}
