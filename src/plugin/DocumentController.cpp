#include "plugin/DocumentController.h"

#include "dsp/SincResampler.h"
#include "model/FcpeDetector.h"
#include "model/MelSynthesiser.h"
#include "model/RmvpeDetector.h"
#include "plugin/PlaybackRenderer.h"

#include <algorithm>

namespace tuner
{
/** @brief Reads one region: the melody, the notes, and everything the vocoder needs. */
class DocumentController::AnalysisJob final : public juce::ThreadPoolJob
{
public:
    AnalysisJob (DocumentController& ownerToUse, Modification& modificationToRead)
        : juce::ThreadPoolJob ("analyse region"),
          owner (ownerToUse),
          modification (modificationToRead)
    {
    }

    void abort() { shouldAbort.store (true); }

    JobStatus runJob() override
    {
        auto& document = modification.getDocument();

        modification.setState (Modification::State::analysing);
        modification.setProgress (0.0f);
        modification.setError ({});

        juce::String error;

        auto* audioSource = modification.getAudioSource<juce::ARAAudioSource>();

        if (audioSource == nullptr || ! audioSource->isSampleAccessEnabled())
        {
            finish (Modification::State::failed, "the host is not letting the audio be read");
            return jobHasFinished;
        }

        juce::ARAAudioSourceReader reader { audioSource };

        const auto numSamples = static_cast<int> (std::min (audioSource->getSampleCount(),
                                                            static_cast<ARA::ARASampleCount> (std::numeric_limits<int>::max())));
        const auto numChannels = std::max (1, audioSource->getChannelCount());
        const auto sampleRate = audioSource->getSampleRate();

        if (numSamples <= 0)
        {
            finish (Modification::State::failed, "the region is empty");
            return jobHasFinished;
        }

        juce::AudioBuffer<float> recording { numChannels, numSamples };
        recording.clear();

        if (! reader.read (&recording, 0, numSamples, 0, true, true))
        {
            finish (Modification::State::failed, "the region's audio could not be read");
            return jobHasFinished;
        }

        if (shouldExit())
            return jobHasFinished;

        modification.setProgress (0.1f);

        std::vector<float> mono (static_cast<std::size_t> (numSamples), 0.0f);

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto* channelData = recording.getReadPointer (channel);

            for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
                mono[static_cast<std::size_t> (sampleIndex)] +=
                    channelData[sampleIndex] / static_cast<float> (numChannels);
        }

        const auto detectorFile = owner.getModelLibrary().find ("pitchnet/fcpe.onnx");

        if (! detectorFile.existsAsFile())
        {
            finish (Modification::State::failed,
                    "no pitch model under " + ModelLibrary::getUserModelDirectory().getFullPathName());
            return jobHasFinished;
        }

        auto detector = FcpeDetector::load (detectorFile, {}, 0, error);

        if (detector == nullptr)
        {
            finish (Modification::State::failed, error);
            return jobHasFinished;
        }

        const SincResampler toDetectorRate { sampleRate, static_cast<double> (detector->getSampleRate()) };
        const auto atDetectorRate = toDetectorRate.process (mono.data(), numSamples);

        auto melody = detector->estimate (atDetectorRate.data(), static_cast<int> (atDetectorRate.size()), error);

        if (melody.getNumFrames() == 0 || shouldExit())
        {
            finish (Modification::State::failed, error.isNotEmpty() ? error : "no melody was heard");
            return jobHasFinished;
        }

        modification.setProgress (0.3f);

        const auto vocoderFile = owner.getModelLibrary().find ("pitchnet/pc_nsf_hifigan.onnx");

        if (! vocoderFile.existsAsFile())
        {
            finish (Modification::State::failed,
                    "no vocoder under " + ModelLibrary::getUserModelDirectory().getFullPathName());
            return jobHasFinished;
        }

        auto engine = MelSynthesiser::load (vocoderFile, {}, 0, error);

        if (engine == nullptr)
        {
            finish (Modification::State::failed, error);
            return jobHasFinished;
        }

        if (! engine->prepare (recording, sampleRate, melody,
                               [this] (float fraction) { modification.setProgress (0.3f + 0.6f * fraction); },
                               shouldAbort, error))
        {
            finish (Modification::State::failed, error);
            return jobHasFinished;
        }

        if (shouldExit())
            return jobHasFinished;

        // The document and the scheduler are read on the message and audio threads, so they are
        // handed the finished work rather than being written to while it is being made.
        const auto notes = document.getNotes();

        juce::MessageManager::callAsync (
            [&owner = owner, &modification = modification, recording = std::move (recording),
             melody = std::move (melody), sampleRate, engine = std::shared_ptr<Synthesiser> (std::move (engine)),
             hadNotes = ! notes.empty()] () mutable
            {
                auto& target = modification.getDocument();

                target.setRecording (std::move (recording), sampleRate, {});
                target.setMelody (std::move (melody));

                auto& scheduler = modification.getScheduler();

                scheduler.setRecording (&target.getRecording());
                scheduler.setSungMelody (target.getSungMelody().fundamentalFrequencyHz);
                scheduler.setSynthesiser (engine, target.getRecording().getNumSamples(), sampleRate);
                scheduler.setMelody (target.getCorrectedMelody().fundamentalFrequencyHz,
                                     target.getCorrectedMelody().gain);

                juce::ignoreUnused (hadNotes);

                modification.setProgress (1.0f);
                modification.setState (Modification::State::ready);
                owner.jobFinished (modification);
                owner.notifyStateChanged();
            });

        return jobHasFinished;
    }

private:
    void finish (Modification::State result, const juce::String& message)
    {
        modification.setError (message);
        modification.setState (result);

        juce::MessageManager::callAsync ([&owner = owner, &modification = modification]
        {
            owner.jobFinished (modification);
            owner.notifyStateChanged();
        });
    }

    DocumentController& owner;
    Modification& modification;
    std::atomic<bool> shouldAbort { false };
};

DocumentController::~DocumentController()
{
    pool.removeAllJobs (true, 8000);
}

void DocumentController::addListener (Listener* listener) { listeners.add (listener); }
void DocumentController::removeListener (Listener* listener) { listeners.remove (listener); }

void DocumentController::notifyStateChanged()
{
    listeners.call ([] (Listener& listener) { listener.regionStateChanged(); });
}

juce::ARAAudioModification* DocumentController::doCreateAudioModification (
    juce::ARAAudioSource* audioSource,
    ARA::ARAAudioModificationHostRef hostRef,
    const juce::ARAAudioModification* optionalModificationToClone) noexcept
{
    return new Modification (audioSource, hostRef, optionalModificationToClone);
}

juce::ARAPlaybackRenderer* DocumentController::doCreatePlaybackRenderer() noexcept
{
    return new PlaybackRenderer (getDocumentController());
}

std::vector<Modification*> DocumentController::getModifications()
{
    std::vector<Modification*> found;

    auto* controller = getDocumentController();

    if (controller == nullptr || controller->getDocument() == nullptr)
        return found;

    for (auto* audioSource : controller->getDocument()->getAudioSources<juce::ARAAudioSource>())
        for (auto* modification : audioSource->getAudioModifications<Modification>())
            found.push_back (modification);

    return found;
}

Modification* DocumentController::getModificationToEdit()
{
    const auto found = getModifications();

    for (auto* modification : found)
        if (modification->getState() == Modification::State::ready)
            return modification;

    return found.empty() ? nullptr : found.front();
}

void DocumentController::jobFinished (Modification& modification)
{
    const juce::ScopedLock lock { jobLock };
    activeJobs.erase (&modification);
}

void DocumentController::requestAnalysis (Modification& modification)
{
    const juce::ScopedLock lock { jobLock };

    if (activeJobs.find (&modification) != activeJobs.end())
        return;

    if (modification.getState() == Modification::State::ready)
        return;

    auto* job = new AnalysisJob (*this, modification);
    activeJobs[&modification] = job;

    pool.addJob (job, true);
}

void DocumentController::didEnableAudioSourceSamplesAccess (juce::ARAAudioSource* audioSource,
                                                            bool enable) noexcept
{
    if (! enable || audioSource == nullptr)
        return;

    for (auto* modification : audioSource->getAudioModifications<Modification>())
        requestAnalysis (*modification);
}

bool DocumentController::doStoreObjectsToStream (juce::ARAOutputStream& output,
                                                 const juce::ARAStoreObjectsFilter* filter) noexcept
{
    if (filter == nullptr)
        return false;

    const auto& modifications = filter->getAudioModificationsToStore<Modification>();

    output.writeInt (static_cast<int> (modifications.size()));

    for (const auto* modification : modifications)
    {
        output.writeString (juce::String (modification->getPersistentID()));
        modification->writeToArchive (output);
    }

    return true;
}

bool DocumentController::doRestoreObjectsFromStream (juce::ARAInputStream& input,
                                                     const juce::ARARestoreObjectsFilter* filter) noexcept
{
    const auto numModifications = input.readInt();

    for (int index = 0; index < numModifications; ++index)
    {
        const auto persistentID = input.readString();

        auto* modification = filter != nullptr
                           ? filter->getAudioModificationToRestoreStateWithID<Modification> (
                                 persistentID.toRawUTF8())
                           : nullptr;

        if (modification != nullptr)
            modification->readFromArchive (input);
        else
            Modification::readAndDiscard (input);

        if (input.failed())
            return false;
    }

    return true;
}
}
