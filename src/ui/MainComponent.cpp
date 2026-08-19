#include "ui/MainComponent.h"

#include "edit/MidiExport.h"

#include <atomic>

namespace tuner
{
namespace
{
    using Palette = PanelLookAndFeel::Palette;
}

MainComponent::MainComponent()
{
    setOpaque (true);
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);

    formats.registerBasicFormats();
    devices.initialiseWithDefaultDevices (0, 2);

    // Rendering keeps every core busy, so the device is asked for a block long enough that the
    // audio thread can miss a scheduling slot without the output breaking up.
    if (auto setup = devices.getAudioDeviceSetup(); setup.bufferSize < 1024)
    {
        setup.bufferSize = 1024;
        devices.setAudioDeviceSetup (setup, true);
    }

    document.addListener (this);
    pipeline.addListener (this);
    scheduler.addListener (this);

    addAndMakeVisible (toolBar);
    addAndMakeVisible (editor);
    addAndMakeVisible (inspector);
    addAndMakeVisible (transport);

    toolBar.onOpen = [this] { chooseAndOpen(); };
    toolBar.onExport = [this] { chooseAndExport(); };
    toolBar.onAnalyse = [this] { analyse(); };
    toolBar.onToolChosen = [this] (NoteGrid::Tool tool) { editor.getGrid().setTool (tool); };

    editor.getGrid().onPositionClicked = [this] (double seconds)
    {
        player.setPosition (seconds);
        scheduler.setPriorityFrame (document.getFrameForTime (seconds));
        editor.setPlayheadPosition (seconds, false);
    };

    editor.getGrid().onLoopDragged = [this] (double first, double last)
    {
        player.setLoop (first, last, true);
        editor.setLoopRange (first, last, true);
    };

    transport.onKeepUneditedChanged = [this] (bool shouldKeep)
    {
        scheduler.setKeepingUneditedAudio (shouldKeep);
    };

    transport.onPositionChanged = [this] (double seconds)
    {
        editor.setPlayheadPosition (seconds, player.isPlaying());

        if (player.isPlaying())
            scheduler.setPriorityFrame (document.getFrameForTime (seconds));
    };

    editor.setCaption ("Open a vocal take to begin.\n\n"
                       "Its melody is heard note by note, corrected the way you ask, and sung back "
                       "by a mel vocoder, which keeps whichever voice was recorded and needs no "
                       "model of it.",
                       false);

    status = "no recording open";
    setSize (1360, 820);
}

MainComponent::~MainComponent()
{
    exportShouldAbort.store (true);

    if (exportThread != nullptr && exportThread->joinable())
        exportThread->join();

    scheduler.removeListener (this);
    pipeline.removeListener (this);
    document.removeListener (this);

    pipeline.cancel();
    scheduler.clear();

    setLookAndFeel (nullptr);
}

void MainComponent::chooseAndOpen()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a vocal take",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   formats.getWildcardForAllFormats());

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& browser)
                          {
                              const auto file = browser.getResult();

                              if (file.existsAsFile())
                                  open (file);
                          });
}

void MainComponent::open (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (file) };

    if (reader == nullptr)
    {
        status = "cannot read " + file.getFileName();
        isStatusAlert = true;
        updateStatus();
        return;
    }

    const auto numSamples = static_cast<int> (reader->lengthInSamples);

    juce::AudioBuffer<float> audio { static_cast<int> (reader->numChannels), numSamples };
    reader->read (&audio, 0, numSamples, 0, true, true);

    player.setRecording (nullptr, reader->sampleRate, nullptr);
    scheduler.clear();

    document.setRecording (std::move (audio), reader->sampleRate, file);

    player.setRecording (&document.getRecording(), document.getSampleRate(), &scheduler);
    player.setPosition (0.0);

    transport.setLength (document.getSeconds());

    analyse();
}

void MainComponent::analyse()
{
    if (! document.hasRecording())
        return;

    isBusy = true;
    isStatusAlert = false;
    toolBar.setBusy (true);

    scheduler.clear();
    editor.setCaption ("Listening to " + document.getFile().getFileName() + "...", false);

    pipeline.start (document.getRecording(), document.getSampleRate(), toolBar.getEngineOptions());
}

void MainComponent::pipelineProgressed (float fraction, const juce::String& stage)
{
    status = stage + " " + juce::String (juce::roundToInt (100.0f * fraction)) + "%";
    updateStatus();
}

void MainComponent::melodyEstimated (PitchTrack melody, std::vector<Note> notes)
{
    document.setMelody (std::move (melody), std::move (notes));

    editor.setCaption ({}, false);
    editor.scrollToSinging();
}

void MainComponent::pipelineFinished (std::shared_ptr<Synthesiser> synthesiser, const juce::String& error)
{
    isBusy = false;
    toolBar.setBusy (false);

    if (synthesiser == nullptr)
    {
        status = error.isNotEmpty() ? error : "nothing was prepared";
        isStatusAlert = true;

        if (! document.hasMelody())
            editor.setCaption (status, true);

        updateStatus();
        return;
    }

    editor.setCaption ({}, false);

    scheduler.setRecording (&document.getRecording());
    scheduler.setSungMelody (document.getSungMelody().fundamentalFrequencyHz);
    scheduler.setSynthesiser (std::move (synthesiser),
                              document.getRecording().getNumSamples(),
                              document.getSampleRate());

    melodyChanged();
}

void MainComponent::melodyChanged()
{
    if (! document.hasMelody())
        return;

    scheduler.setMelody (document.getCorrectedMelody().fundamentalFrequencyHz,
                         document.getCorrectedMelody().gain);
    updateStatus();
}

void MainComponent::renderChanged()
{
    updateStatus();
}

void MainComponent::updateStatus()
{
    const auto rendererError = scheduler.getError();

    auto message = status;
    auto isAlert = isStatusAlert;

    if (rendererError.isNotEmpty())
    {
        message = rendererError;
        isAlert = true;
    }
    else if (! isBusy && document.hasMelody())
    {
        const auto fraction = scheduler.getFractionReady();

        message = scheduler.isRendering()
                    ? "rendering " + juce::String (juce::roundToInt (100.0 * fraction)) + "%"
                    : (fraction >= 1.0 ? juce::String (document.getNotes().size()) + " notes"
                                       : "waiting to render");
    }

    transport.setRenderState (scheduler.getFractionReady(), message, isAlert);
}

void MainComponent::chooseAndExport()
{
    if (! document.hasMelody())
        return;

    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeel);
    menu.addItem (1, "Corrected audio...");
    menu.addItem (2, "Melody as MIDI...");

    menu.showMenuAsync (juce::PopupMenu::Options {}, [this] (int result)
    {
        if (result == 1)
            chooseAndExportAudio();
        else if (result == 2)
            chooseAndExportMidi();
    });
}

void MainComponent::chooseAndExportMidi()
{
    const auto suggested = document.getFile().getParentDirectory()
                               .getChildFile (document.getFile().getFileNameWithoutExtension() + ".mid");

    chooser = std::make_unique<juce::FileChooser> ("Export the melody", suggested, "*.mid");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& browser)
                          {
                              const auto file = browser.getResult();

                              if (file == juce::File {})
                                  return;

                              const auto written = writeMidiFile (file, document.getNotes(),
                                                                  document.getFrameRate());

                              status = written ? "wrote " + file.getFileName()
                                               : "could not write " + file.getFileName();
                              isStatusAlert = ! written;
                              updateStatus();
                          });
}

void MainComponent::chooseAndExportAudio()
{
    const auto suggested = document.getFile().getParentDirectory()
                               .getChildFile (document.getFile().getFileNameWithoutExtension() + " tuned.wav");

    chooser = std::make_unique<juce::FileChooser> ("Export the corrected take", suggested, "*.wav");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& browser)
                          {
                              const auto file = browser.getResult();

                              if (file != juce::File {})
                                  exportTo (file);
                          });
}

void MainComponent::exportTo (const juce::File& file)
{
    isBusy = true;
    isStatusAlert = false;
    status = "rendering the whole take";
    toolBar.setBusy (true);
    updateStatus();

    exportThread = std::make_unique<std::thread> ([this, file]
    {
        const auto written = writeExport (file);

        juce::MessageManager::callAsync ([this, file, written]
        {
            isBusy = false;
            isStatusAlert = ! written;
            status = written ? "wrote " + file.getFileName()
                             : "the export did not finish: " + scheduler.getError();

            toolBar.setBusy (false);
            updateStatus();
        });
    });
}

bool MainComponent::writeExport (const juce::File& file)
{
    if (! scheduler.renderEverything (exportShouldAbort))
        return false;

    const auto numSamples = document.getRecording().getNumSamples();
    const auto numChannels = std::max (1, document.getRecording().getNumChannels());

    juce::AudioBuffer<float> rendered { numChannels, numSamples };
    rendered.clear();
    scheduler.read (rendered, 0, 0, numSamples);

    file.deleteFile();

    std::unique_ptr<juce::OutputStream> stream { file.createOutputStream() };
    juce::WavAudioFormat wav;

    auto writer = wav.createWriterFor (stream,
                                       juce::AudioFormatWriterOptions {}
                                           .withSampleRate (document.getSampleRate())
                                           .withNumChannels (static_cast<unsigned int> (numChannels))
                                           .withBitsPerSample (24));

    if (writer == nullptr)
        return false;

    writer->writeFromAudioSampleBuffer (rendered, 0, numSamples);
    writer.reset();

    return true;
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        if (player.isPlaying())
            player.stop();
        else
            player.start();

        return true;
    }

    if (key.getTextCharacter() == 'z' && key.getModifiers().isCommandDown())
    {
        if (key.getModifiers().isShiftDown())
            document.getUndoManager().redo();
        else
            document.getUndoManager().undo();

        return true;
    }

    return false;
}

void MainComponent::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Palette::ground);
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();

    toolBar.setBounds (bounds.removeFromTop (ToolBar::preferredHeight));
    transport.setBounds (bounds.removeFromBottom (TransportBar::preferredHeight));
    inspector.setBounds (bounds.removeFromBottom (Inspector::preferredHeight));
    editor.setBounds (bounds);
}
}
