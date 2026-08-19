#include "dsp/SincResampler.h"
#include "edit/CorrectionCurve.h"
#include "edit/NoteSegmenter.h"
#include "model/FcpeDetector.h"
#include "model/MelSynthesiser.h"
#include "model/RmvpeDetector.h"
#include "model/VoiceSynthesiser.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <cmath>
#include <iostream>

using namespace rvctuner;

namespace
{
struct Arguments
{
    juce::File input;
    juce::File output;
    juce::File modelDirectory { RVCTUNER_DEVELOPMENT_MODEL_PATH };
    juce::String detector { "rmvpe" };
    juce::String engine { "mel" };
    juce::String voice { "female1" };
    juce::String key { "C" };
    juce::String scale { "chromatic" };
    double shiftSemitones { 0.0 };
    CorrectionSettings correction;
    SegmenterSettings segmenter;
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
        else if (argument == "--engine")
            arguments.engine = next().toLowerCase();
        else if (argument == "--voice")
            arguments.voice = next();
        else if (argument == "--models")
            arguments.modelDirectory = juce::File::getCurrentWorkingDirectory().getChildFile (next());
        else if (argument == "--shift")
            arguments.shiftSemitones = next().getDoubleValue();
        else if (argument == "--key")
            arguments.key = next();
        else if (argument == "--scale")
            arguments.scale = next().toLowerCase();
        else if (argument == "--correction")
            arguments.correction.correction = static_cast<float> (next().getDoubleValue());
        else if (argument == "--vibrato")
            arguments.correction.vibrato = static_cast<float> (next().getDoubleValue());
        else if (argument == "--drift")
            arguments.correction.drift = static_cast<float> (next().getDoubleValue());
        else if (argument == "--min-note")
            arguments.segmenter.minimumNoteMilliseconds = next().getDoubleValue();
        else if (argument == "--split")
            arguments.segmenter.splitSemitones = next().getDoubleValue();
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
        report ("usage: rvctuner-tune <input.wav> <output.wav> [--detector rmvpe|fcpe]");
        report ("                     [--engine mel|voice] [--voice <name>] [--models <dir>]");
        report ("                     [--key C] [--scale chromatic|major|minor|...]");
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

    if (arguments->detector == "fcpe")
    {
        detector = FcpeDetector::load (arguments->modelDirectory.getChildFile ("pitchnet/fcpe.onnx"),
                                       {}, 0, error);
    }
    else
    {
        RmvpeDetector::Configuration configuration;
        detector = RmvpeDetector::load (arguments->modelDirectory
                                            .getChildFile (arguments->voice)
                                            .getChildFile ("pitch_estimator.onnx"),
                                        configuration, {}, 0, error);
    }

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

    auto notes = segmentNotes (melody, scale, arguments->segmenter);

    for (auto& note : notes)
        note.targetNote += juce::roundToInt (arguments->shiftSemitones);

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

    std::unique_ptr<Synthesiser> synthesiser;
    std::unique_ptr<VoiceModel> voiceModel;

    if (arguments->engine == "voice")
    {
        voiceModel = VoiceModel::load (arguments->modelDirectory.getChildFile (arguments->voice), 0, error);

        if (voiceModel == nullptr)
        {
            report ("voice model: " + error);
            return 1;
        }

        synthesiser = std::make_unique<VoiceSynthesiser> (*voiceModel, ContentSettings {});
    }
    else
    {
        synthesiser = MelSynthesiser::load (arguments->modelDirectory
                                                .getChildFile ("pitchnet/pc_nsf_hifigan.onnx"),
                                            {}, 0, error);

        if (synthesiser == nullptr)
        {
            report ("vocoder: " + error);
            return 1;
        }
    }

    const auto preparedAt = juce::Time::getMillisecondCounterHiRes();
    const std::atomic<bool> keepGoing { false };

    if (! synthesiser->prepare (mono.data(), numSamples, sampleRate, melody,
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

    juce::AudioBuffer<float> rendered { 1, numSamples };
    rendered.clear();

    for (auto firstFrame = 0; firstFrame < numFrames; firstFrame += spanFrames)
    {
        const auto numSpanFrames = std::min (spanFrames, numFrames - firstFrame);

        const auto span = synthesiser->render (edited.data(), firstFrame, numSpanFrames, error);

        if (span.empty())
        {
            report ("render: " + error);
            return 1;
        }

        const auto firstSample = static_cast<int> (std::llround (static_cast<double> (firstFrame)
                                                                 * sampleRate
                                                                 / synthesiser->getFrameRate()));
        const auto numToCopy = std::min (static_cast<int> (span.size()), numSamples - firstSample);

        if (numToCopy > 0)
            rendered.copyFrom (0, firstSample, span.data(), numToCopy);

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
                                                   .withNumChannels (1)
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
