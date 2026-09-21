#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <algorithm>

namespace multiplyandreplenish
{
namespace
{
    const juce::Identifier projectType { "MultiplyAndReplenish" };
    const juce::Identifier tabType { "Tab" };
    const juce::Identifier noteType { "Note" };

    /** @brief The fields of a note that an edit changes; everything else comes back from analysis. */
    void writeNote (juce::ValueTree& tree, const Note& note)
    {
        tree.setProperty ("sourceFirstFrame", note.sourceFirstFrame, nullptr);
        tree.setProperty ("firstFrame", note.firstFrame, nullptr);
        tree.setProperty ("lastFrame", note.lastFrame, nullptr);
        tree.setProperty ("semitones", note.semitones, nullptr);
        tree.setProperty ("lastEditedSemitones", note.lastEditedSemitones, nullptr);
        tree.setProperty ("pitchOffset", note.pitchOffset, nullptr);
    }

    void readNote (const juce::ValueTree& tree, Note& note)
    {
        note.firstFrame = tree.getProperty ("firstFrame", note.firstFrame);
        note.lastFrame = tree.getProperty ("lastFrame", note.lastFrame);
        note.semitones = tree.getProperty ("semitones", note.semitones);
        note.lastEditedSemitones = tree.getProperty ("lastEditedSemitones", note.lastEditedSemitones);
        note.pitchOffset = tree.getProperty ("pitchOffset", note.pitchOffset);
    }
}

/** @brief The pipeline, the shared synthesiser and the tabs; the processor is the face of it. */
struct PluginProcessor::Impl final : public Pipeline::Listener,
                                     public RenderScheduler::Listener
#if JucePlugin_Enable_ARA
    , private juce::Timer
#endif
{
    /** @brief A tab, and the two things that need to hear about it. */
    struct Entry final : private EditDocument::Listener
    {
        explicit Entry (Impl& owner) : impl (owner)
        {
            tab.document.addListener (this);
            tab.scheduler.addListener (&impl);
        }

        ~Entry() override
        {
            tab.scheduler.removeListener (&impl);
            tab.document.removeListener (this);
        }

        void melodyChanged() override
        {
            impl.pushMelody (tab);
            impl.processor.sendChangeMessage();
        }

        void scaleChanged() override { impl.processor.sendChangeMessage(); }

        Impl& impl;
        Tab tab;
    };

    explicit Impl (PluginProcessor& owner) : processor (owner)
    {
        pipeline.addListener (this);
        tabs.push_back (std::make_unique<Entry> (*this));
        syncPlayer();
    }

    ~Impl() override
    {
        pipeline.cancel();
        pipeline.removeListener (this);
    }

    [[nodiscard]] Tab& getTab (int index) noexcept
    {
        return tabs[static_cast<std::size_t> (juce::jlimit (0, static_cast<int> (tabs.size()) - 1, index))]->tab;
    }

    /** @brief Tells the player which renders to mix; the first tab is the main one. */
    void syncPlayer()
    {
        std::vector<RenderScheduler*> schedulers;

        for (auto& entry : tabs)
            schedulers.push_back (&entry->tab.scheduler);

        processor.player.setTabs (std::move (schedulers));
    }

    /** @brief Hands a tab to the synthesiser, once there is both a melody and a synthesiser. */
    void prepare (Tab& tab)
    {
        auto& document = tab.document;

        if (synthesiser == nullptr || ! document.hasMelody())
            return;

        tab.scheduler.setRecording (&document.getRecording());
        tab.scheduler.setSungMelody (interpolateThroughUnvoiced (document.getSungMelody()));
        tab.scheduler.setSynthesiser (synthesiser, document.getRecording().getNumSamples(),
                                      document.getSampleRate());
        pushMelody (tab);
    }

    void pushMelody (Tab& tab)
    {
        if (synthesiser == nullptr || ! tab.document.hasMelody())
            return;

        const auto& composed = tab.document.getComposedMelody();

        tab.scheduler.setMelody (composed.fundamentalFrequencyHz, composed.gain);
        tab.scheduler.setVoicing (composed.isVoiced);
    }

    Tab& addTab()
    {
        auto& first = getTab (0).document;

        auto entry = std::make_unique<Entry> (*this);
        auto& tab = entry->tab;

        tab.document.setRecording (first.getRecording(), first.getSampleRate(), first.getFile());
        tab.document.setMelody (melody, notes);
        tab.document.setScale (first.getScale());

        tabs.push_back (std::move (entry));

        prepare (tab);
        syncPlayer();

        return tab;
    }

    /** @brief Drops a tab. The player is pointed away from it before it is destroyed. */
    void removeTab (int index)
    {
        auto removed = std::move (tabs[static_cast<std::size_t> (index)]);
        tabs.erase (tabs.begin() + index);

        if (index == 0)
        {
            // The player reads the first tab's recording, so it has to move to the new first tab.
            // Setting the recording stops playback and rewinds; put both back afterwards.
            const auto wasPlaying = processor.player.isPlaying();
            const auto position = processor.player.getPosition();
            auto& first = getTab (0).document;

            processor.player.setRecording (&first.getRecording(), first.getSampleRate());
            syncPlayer();
            processor.player.setPosition (position);

            if (wasPlaying)
                processor.player.start();
        }
        else
        {
            syncPlayer();
        }

        if (index < processor.activeTab)
            --processor.activeTab;

        processor.activeTab = juce::jlimit (0, static_cast<int> (tabs.size()) - 1, processor.activeTab);

        // The editor points at documents by address, so it has to see the new list before the
        // removed tab is destroyed at the end of this scope.
        processor.sendSynchronousChangeMessage();

#if JucePlugin_Enable_ARA
        if (processor.isAraActive())
            startTimer (400);
#endif
    }

    void reset()
    {
        pipeline.cancel();

        processor.player.setRecording (nullptr, 44100.0);

        tabs.resize (1);
        tabs.front()->tab.scheduler.clear();

        synthesiser.reset();
        melody = {};
        notes.clear();
        pending = {};

        syncPlayer();
    }

    void pipelineProgressed (float, const juce::String&) override {}

    void melodyEstimated (PitchTrack estimated, std::vector<Note> found) override
    {
        melody = estimated;
        notes = found;

        getTab (0).document.setMelody (std::move (estimated), std::move (found));

        if (pending.isValid())
            restoreTabs();

        processor.sendChangeMessage();
    }

    void pipelineFinished (std::shared_ptr<Synthesiser> prepared, const juce::String& message) override
    {
        processor.busy = false;
        processor.error = prepared == nullptr ? (message.isNotEmpty() ? message : "nothing was prepared")
                                              : juce::String();

        if (prepared != nullptr)
        {
            synthesiser = std::move (prepared);

            for (auto& entry : tabs)
                prepare (entry->tab);
        }

        processor.sendChangeMessage();
    }

    void renderChanged() override
    {
#if JucePlugin_Enable_ARA
        if (processor.isAraActive())
            startTimer (400);
#endif
        processor.sendChangeMessage();
    }

#if JucePlugin_Enable_ARA
    /** @brief Renders arrive in bursts, so the host is given the mix once they settle. */
    void timerCallback() override
    {
        stopTimer();
        processor.publishToAra();
    }
#endif

    /** @brief Puts the saved edits onto the freshly analysed take, one tab each. */
    void restoreTabs()
    {
        const auto saved = std::exchange (pending, juce::ValueTree {});

        processor.setScale (Scale (static_cast<Scale::Type> (static_cast<int> (saved.getProperty ("scaleType", 0))),
                                   saved.getProperty ("tonic", 0)));

        for (int index = 0; index < saved.getNumChildren(); ++index)
        {
            const auto tabTree = saved.getChild (index);

            auto& tab = index == 0 ? getTab (0) : addTab();
            auto state = tab.document.getState();

            for (const auto noteTree : tabTree)
                for (auto& note : state.notes)
                    if (note.sourceFirstFrame == static_cast<int> (noteTree.getProperty ("sourceFirstFrame", -1)))
                        readNote (noteTree, note);

            tab.document.setState (std::move (state));
        }

        processor.activeTab = juce::jlimit (0, static_cast<int> (tabs.size()) - 1,
                                            static_cast<int> (saved.getProperty ("activeTab", 0)));
    }

    PluginProcessor& processor;

    Pipeline pipeline;
    std::shared_ptr<Synthesiser> synthesiser;

    /** @brief The take as analysed, which is what a new tab starts from. */
    PitchTrack melody;
    std::vector<Note> notes;

    /** @brief A project waiting for the analysis it was saved from to finish. */
    juce::ValueTree pending;

    std::vector<std::unique_ptr<Entry>> tabs;
};

PluginProcessor::PluginProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      impl (std::make_unique<Impl> (*this))
{
    formats.registerBasicFormats();
}

PluginProcessor::~PluginProcessor()
{
    player.setRecording (nullptr, 44100.0);
}

void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    player.prepare (sampleRate, samplesPerBlock);

#if JucePlugin_Enable_ARA
    prepareToPlayForARA (sampleRate, samplesPerBlock, getMainBusNumOutputChannels(), getProcessingPrecision());
#endif
}

void PluginProcessor::releaseResources()
{
    player.releaseResources();

#if JucePlugin_Enable_ARA
    releaseResourcesForARA();
#endif
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

#if JucePlugin_Enable_ARA
    // Under ARA the host's timeline drives playback, through the ARA playback renderer.
    const auto realtime = isNonRealtime() ? juce::AudioProcessor::Realtime::no
                                          : juce::AudioProcessor::Realtime::yes;

    if (isBoundToARA() && processBlockForARA (buffer, realtime, getPlayHead()))
        return;
#endif

    player.renderBlock (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

// --- The take -----------------------------------------------------------------------------------

void PluginProcessor::openFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (file) };

    if (reader == nullptr)
    {
        error = "cannot read " + file.getFileName();
        sendChangeMessage();
        return;
    }

    const auto numSamples = static_cast<int> (reader->lengthInSamples);

    juce::AudioBuffer<float> audio { static_cast<int> (reader->numChannels), numSamples };
    reader->read (&audio, 0, numSamples, 0, true, true);

    impl->reset();
    activeTab = 0;
    error.clear();

    auto& tab = impl->getTab (0);
    tab.document.setRecording (std::move (audio), reader->sampleRate, file);
    tab.scheduler.setRecording (&tab.document.getRecording());

    player.setRecording (&tab.document.getRecording(), tab.document.getSampleRate());
    impl->syncPlayer();

    busy = true;
    sendChangeMessage();

    impl->pipeline.start (tab.document.getRecording(), tab.document.getSampleRate(), {});
}

bool PluginProcessor::hasTake() const noexcept
{
    return impl->getTab (0).document.hasRecording();
}

juce::String PluginProcessor::getError() const
{
    return error;
}

// --- Tabs ---------------------------------------------------------------------------------------

int PluginProcessor::getNumTabs() const noexcept
{
    return static_cast<int> (impl->tabs.size());
}

void PluginProcessor::setActiveTab (int index)
{
    activeTab = juce::jlimit (0, getNumTabs() - 1, index);
    sendChangeMessage();
}

EditDocument& PluginProcessor::getDocument (int tabIndex) noexcept
{
    return impl->getTab (tabIndex).document;
}

void PluginProcessor::addTab()
{
    if (! hasTake() || ! impl->getTab (0).document.hasMelody())
        return;

    impl->addTab();
    activeTab = getNumTabs() - 1;
    sendChangeMessage();
}

void PluginProcessor::removeTab (int index)
{
    if (getNumTabs() <= 1 || index < 0 || index >= getNumTabs())
        return;

    impl->removeTab (index);
}

// --- Editing ------------------------------------------------------------------------------------

bool PluginProcessor::canUndo() noexcept { return getActiveDocument().getUndoManager().canUndo(); }
bool PluginProcessor::canRedo() noexcept { return getActiveDocument().getUndoManager().canRedo(); }

void PluginProcessor::undo()
{
    getActiveDocument().getUndoManager().undo();
    sendChangeMessage();
}

void PluginProcessor::redo()
{
    getActiveDocument().getUndoManager().redo();
    sendChangeMessage();
}

Scale PluginProcessor::getScale() const
{
    return impl->getTab (0).document.getScale();
}

void PluginProcessor::setScale (const Scale& scale)
{
    for (auto& entry : impl->tabs)
        entry->tab.document.setScale (scale);

    sendChangeMessage();
}

// --- Playback -----------------------------------------------------------------------------------

void PluginProcessor::togglePlayback()
{
    if (player.isPlaying())
    {
        player.stop();
    }
    else
    {
        const auto& document = impl->getTab (0).document;

        if (player.getPosition() >= document.getSeconds() - 0.01)
            player.setPosition (0.0);

        updateRenderPriority();
        player.start();
    }

    sendChangeMessage();
}

void PluginProcessor::setPlayhead (double seconds)
{
    player.setPosition (seconds);
    updateRenderPriority();
}

juce::AudioBuffer<float> PluginProcessor::getMix()
{
    constexpr int chunk = 4096;

    const auto& recording = impl->getTab (0).document.getRecording();
    const auto numSamples = recording.getNumSamples();
    const auto numChannels = std::max (2, recording.getNumChannels());

    juce::AudioBuffer<float> mix { numChannels, numSamples };
    mix.clear();

    juce::AudioBuffer<float> scratch { numChannels, chunk };

    for (int first = 0; first < numSamples; first += chunk)
    {
        const auto count = std::min (chunk, numSamples - first);

        for (std::size_t index = 0; index < impl->tabs.size(); ++index)
        {
            auto& scheduler = impl->tabs[index]->tab.scheduler;
            const auto ready = scheduler.isReady (first, count);

            if (ready)
            {
                scratch.clear();
                scheduler.read (scratch, 0, first, count);
            }
            else if (index == 0)
            {
                for (int channel = 0; channel < numChannels; ++channel)
                    scratch.copyFrom (channel, 0, recording,
                                      std::min (channel, recording.getNumChannels() - 1), first, count);
            }
            else
            {
                continue;
            }

            for (int channel = 0; channel < numChannels; ++channel)
                mix.addFrom (channel, first, scratch, channel, 0, count);
        }
    }

    return mix;
}

void PluginProcessor::updateRenderPriority()
{
    for (auto& entry : impl->tabs)
        entry->tab.scheduler.setPriorityFrame (entry->tab.document.getFrameForTime (player.getPosition()));
}

// --- Projects -----------------------------------------------------------------------------------

namespace
{
    juce::ValueTree makeProject (PluginProcessor& processor)
    {
        juce::ValueTree project { projectType };
        const auto& first = processor.getDocument (0);

        project.setProperty ("file", first.getFile().getFullPathName(), nullptr);
        project.setProperty ("scaleType", static_cast<int> (processor.getScale().getType()), nullptr);
        project.setProperty ("tonic", processor.getScale().getTonic(), nullptr);
        project.setProperty ("activeTab", processor.getActiveTab(), nullptr);

        for (int index = 0; index < processor.getNumTabs(); ++index)
        {
            juce::ValueTree tab { tabType };

            for (const auto& note : processor.getDocument (index).getNotes())
            {
                juce::ValueTree noteTree { noteType };
                writeNote (noteTree, note);
                tab.appendChild (noteTree, nullptr);
            }

            project.appendChild (tab, nullptr);
        }

        return project;
    }
}

bool PluginProcessor::saveProject (const juce::File& file)
{
    if (! hasTake())
        return false;

    const auto xml = makeProject (*this).createXml();
    return xml != nullptr && xml->writeTo (file);
}

bool PluginProcessor::loadProject (const juce::File& file)
{
    const auto xml = juce::parseXML (file);

    if (xml == nullptr)
        return false;

    const auto project = juce::ValueTree::fromXml (*xml);
    const juce::File source { project.getProperty ("file").toString() };

    if (! project.hasType (projectType) || ! source.existsAsFile())
        return false;

    openFile (source);
    impl->pending = project;

    return true;
}

void PluginProcessor::getStateInformation (juce::MemoryBlock& destinationData)
{
    if (const auto xml = makeProject (*this).createXml())
        copyXmlToBinary (*xml, destinationData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    const auto project = juce::ValueTree::fromXml (*xml);
    const juce::File source { project.getProperty ("file").toString() };

    if (project.hasType (projectType) && source.existsAsFile())
    {
        openFile (source);
        impl->pending = project;
    }
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new multiplyandreplenish::PluginProcessor();
}
