#include "dsp/SincResampler.h"
#include "edit/CorrectionCurve.h"
#include "edit/NoteSegmenter.h"
#include "model/FcpeDetector.h"
#include "model/MelSynthesiser.h"
#include "model/GameSegmenter.h"
#include "model/ModelLibrary.h"
#include "model/RmvpeDetector.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <cmath>
#include <iostream>

using namespace tuner;

namespace
{
struct Arguments
{
    juce::File input;
    juce::File output;
    juce::File modelDirectory { TUNER_DEVELOPMENT_MODEL_PATH };
    juce::String detector { "rmvpe" };
    juce::String key { "C" };
    juce::String scale { "chromatic" };
    juce::String segmenter { "dsp" };
    double shiftSemitones { 0.0 };
    CorrectionSettings correction;
    SegmenterSettings segmenterSettings;
};

void report (const juce::String& message)
{
    std::cout << message.toStdString() << std::endl;
}

std::optional<Arguments> parse (int argc, char** argv)
{
    Arguments arguments;

    juce::StringArray positional;

    for (int index = 1; index < argc; ++index)
    {
        const juce::String argument { argv[index] };

        const auto next = [&] { return index + 1 < argc ? juce::String { argv[++index] } : juce::String {}; };

        if (argument == "--detector")
            arguments.detector = next().toLowerCase();
        else if (argument == "--models")
            arguments.modelDirectory = juce::File::getCurrentWorkingDirectory().getChildFile (next());
        else if (argument == "--shift")
            arguments.shiftSemitones = next().getDoubleValue();
        else if (argument == "--key")
            arguments.key = next();
        else if (argument == "--segmenter")
            arguments.segmenter = next().toLowerCase();
        else if (argument == "--scale")
            arguments.scale = next().toLowerCase();
        else if (argument == "--correction")
            arguments.correction.correction = static_cast<float> (next().getDoubleValue());
        else if (argument == "--vibrato")
            arguments.correction.vibrato = static_cast<float> (next().getDoubleValue());
        else if (argument == "--drift")
            arguments.correction.drift = static_cast<float> (next().getDoubleValue());
        else if (argument == "--min-note")
            arguments.segmenterSettings.minimumNoteMilliseconds = next().getDoubleValue();
        else if (argument == "--split")
            arguments.segmenterSettings.splitSemitones = next().getDoubleValue();
        else if (argument == "--transition")
            arguments.correction.transitionMilliseconds = static_cast<float> (next().getDoubleValue());
        else if (argument.startsWith ("--"))
            return std::nullopt;
        else
            positional.add (argument);
    }

    if (positional.size() != 2)
        return std::nullopt;

    arguments.input = juce::File::getCurrentWorkingDirectory().getChildFile (positional[0]);
    arguments.output = juce::File::getCurrentWorkingDirectory().getChildFile (positional[1]);

    return arguments;
}
}

int main (int argc, char** argv)
{
    const auto arguments = parse (argc, argv);

    if (! arguments.has_value())
    {
        report ("usage: tuner-tune <input.wav> <output.wav> [--detector fcpe|rmvpe] [--models <dir>]");
        report ("                     [--key C] [--scale chromatic|major|minor|...]");
        report ("                     [--segmenter dsp|game]");
        report ("                     [--correction 0..1] [--vibrato 0..2] [--drift 0..2]");
        report ("                     [--transition <ms>] [--shift <semitones>]");
        return 1;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (arguments->input) };

    if (reader == nullptr)
    {
        report ("cannot read " + arguments->input.getFullPathName());
        return 1;
    }

    const auto numSamples = static_cast<int> (reader->lengthInSamples);
    const auto sampleRate = reader->sampleRate;

    juce::AudioBuffer<float> source { static_cast<int> (reader->numChannels), numSamples };
    reader->read (&source, 0, numSamples, 0, true, true);

    std::vector<float> mono (static_cast<std::size_t> (numSamples), 0.0f);

    for (int channel = 0; channel < source.getNumChannels(); ++channel)
        for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
            mono[static_cast<std::size_t> (sampleIndex)] +=
                source.getSample (channel, sampleIndex) / static_cast<float> (source.getNumChannels());

    report ("read " + arguments->input.getFileName() + ": " + juce::String (numSamples / sampleRate, 2)
            + " s at " + juce::String (sampleRate, 0) + " Hz");

    juce::String error;
    std::unique_ptr<PitchDetector> detector;

    const auto start = juce::Time::getMillisecondCounterHiRes();

    if (arguments->detector == "rmvpe")
        detector = RmvpeDetector::load (arguments->modelDirectory.getChildFile ("rmvpe/rmvpe.onnx"),
                                        {}, {}, 0, error);
    else
        detector = FcpeDetector::load (arguments->modelDirectory.getChildFile ("pitchnet/fcpe.onnx"),
                                       {}, 0, error);

    if (detector == nullptr)
    {
        report ("pitch detector: " + error);
        return 1;
    }

    const SincResampler toDetectorRate { sampleRate, static_cast<double> (detector->getSampleRate()) };
    const auto atDetectorRate = toDetectorRate.process (mono.data(), numSamples);

    auto melody = detector->estimate (atDetectorRate.data(), static_cast<int> (atDetectorRate.size()), error);

    if (melody.getNumFrames() == 0)
    {
        report ("pitch estimation: " + error);
        return 1;
    }

    auto numVoiced = 0;
    for (const auto frequencyHz : melody.fundamentalFrequencyHz)
        numVoiced += frequencyHz > 0.0f ? 1 : 0;

    report (detector->getName() + ": " + juce::String (melody.getNumFrames()) + " frames, "
            + juce::String (100.0 * numVoiced / melody.getNumFrames(), 1) + "% voiced, "
            + juce::String (juce::Time::getMillisecondCounterHiRes() - start, 0) + " ms");

    auto scaleType = Scale::Type::chromatic;

    {
        const auto names = Scale::getTypeNames();

        for (int index = 0; index < names.size(); ++index)
            if (names[index].toLowerCase().removeCharacters (" ").startsWith (arguments->scale.removeCharacters (" ")))
                scaleType = static_cast<Scale::Type> (index);
    }

    const Scale scale { scaleType, std::max (0, Scale::getPitchClassNames().indexOf (arguments->key)) };

    std::vector<Note> notes;

    if (arguments->segmenter == "game")
    {
        auto game = GameSegmenter::load (arguments->modelDirectory.getChildFile ("pitchnet/GAME"),
                                         {}, 0, error);

        if (game == nullptr)
        {
            report ("note segmenter: " + error);
            return 1;
        }

        const auto segments = game->segment (mono.data(), numSamples, sampleRate,
                                             melody.frameRate, error);

        if (segments.empty())
        {
            report ("note segmenter: " + (error.isNotEmpty() ? error : juce::String ("no notes found")));
            return 1;
        }

        std::vector<int> firstFrames;
        std::vector<int> lastFrames;

        for (const auto& segment : segments)
        {
            if (segment.isRest)
                continue;

            firstFrames.push_back (segment.firstFrame);
            lastFrames.push_back (segment.lastFrame);
        }

        report ("GAME: " + juce::String (segments.size()) + " segments, "
                + juce::String (firstFrames.size()) + " of them notes");

        notes = notesFromSegments (firstFrames, lastFrames, melody, scale, arguments->segmenterSettings);
    }
    else
    {
        notes = segmentNotes (melody, scale, arguments->segmenterSettings);
    }

    for (auto& note : notes)
    {
        note.targetNote += juce::roundToInt (arguments->shiftSemitones);

        // A note opens as it was played; correcting a whole file is what this tool is for.
        note.correction = 1.0f;
    }

    auto inTune = 0;
    auto totalError = 0.0;

    for (const auto& note : notes)
    {
        totalError += std::abs (note.getError());
        inTune += std::abs (note.getError()) < 0.15 ? 1 : 0;
    }

    report (juce::String (notes.size()) + " notes, " + juce::String (inTune) + " already in tune, "
            + juce::String (100.0 * totalError / std::max (std::size_t (1), notes.size()), 0)
            + " cents off on average");

    const auto corrected = correctMelody (melody, notes, {}, arguments->correction);
    const auto& edited = corrected.fundamentalFrequencyHz;

    auto synthesiser = MelSynthesiser::load (arguments->modelDirectory
                                                .getChildFile ("pitchnet/pc_nsf_hifigan.onnx"),
                                            {}, 0, error);

    if (synthesiser == nullptr)
    {
        report ("vocoder: " + error);
        return 1;
    }

    const auto preparedAt = juce::Time::getMillisecondCounterHiRes();
    const std::atomic<bool> keepGoing { false };

    if (! synthesiser->prepare (source, sampleRate, melody,
                                [] (float fraction) { std::cout << "\rprepare " << juce::roundToInt (100.0f * fraction) << "%   " << std::flush; },
                                keepGoing, error))
    {
        report ("\nprepare: " + error);
        return 1;
    }

    report ("\n" + synthesiser->getName() + " prepared in "
            + juce::String (juce::Time::getMillisecondCounterHiRes() - preparedAt, 0) + " ms");

    const auto renderStart = juce::Time::getMillisecondCounterHiRes();
    const auto numFrames = synthesiser->getNumFrames();
    const auto spanFrames = 200;

    juce::AudioBuffer<float> rendered { synthesiser->getNumChannels(), numSamples };
    rendered.clear();

    juce::AudioBuffer<float> spanBuffer;

    for (auto firstFrame = 0; firstFrame < numFrames; firstFrame += spanFrames)
    {
        const auto numSpanFrames = std::min (spanFrames, numFrames - firstFrame);

        if (! synthesiser->render (edited.data(), firstFrame, numSpanFrames, spanBuffer, error))
        {
            report ("render: " + error);
            return 1;
        }

        const auto firstSample = static_cast<int> (std::llround (static_cast<double> (firstFrame)
                                                                 * sampleRate
                                                                 / synthesiser->getFrameRate()));
        const auto numToCopy = std::min (spanBuffer.getNumSamples(), numSamples - firstSample);

        for (int channel = 0; channel < rendered.getNumChannels() && numToCopy > 0; ++channel)
            rendered.copyFrom (channel, firstSample, spanBuffer,
                               std::min (channel, spanBuffer.getNumChannels() - 1), 0, numToCopy);

        std::cout << "\rrender " << juce::roundToInt (100.0 * (firstFrame + numSpanFrames) / numFrames)
                  << "%   " << std::flush;
    }

    const auto renderMilliseconds = juce::Time::getMillisecondCounterHiRes() - renderStart;

    report ("\nrendered in " + juce::String (renderMilliseconds, 0) + " ms ("
            + juce::String (numSamples / sampleRate * 1000.0 / renderMilliseconds, 1) + "x real time)");

    arguments->output.deleteFile();

    std::unique_ptr<juce::OutputStream> stream { arguments->output.createOutputStream() };
    juce::WavAudioFormat wav;

    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                   .withSampleRate (sampleRate)
                                                   .withNumChannels (static_cast<unsigned int> (rendered.getNumChannels()))
                                                   .withBitsPerSample (24));

    if (writer == nullptr)
    {
        report ("cannot write " + arguments->output.getFullPathName());
        return 1;
    }

    writer->writeFromAudioSampleBuffer (rendered, 0, numSamples);
    writer.reset();

    report ("wrote " + arguments->output.getFullPathName());
    return 0;
}
