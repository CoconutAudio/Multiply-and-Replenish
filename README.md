# RVCTuner

A vocal pitch editor, in the shape of Newtone: open a take, see the melody as notes on a piano
roll, move them, and hear the take sung back at the pitches you put them on.

```bash
rvctuner take.wav
```

---

## What it does

It hears the melody, cuts it into notes, and re-sings the recording from an edited copy of that
melody. The words, the delivery, the breath and the timbre all come from the recording; only the
tune is yours to change.

```
recording
    │
    ├─ pitch detection ── a fundamental every 10 ms          (RMVPE or FCPE)
    ├─ segmentation ───── one note per sung pitch
    ├─ editing ────────── the piano roll: move, split, join, draw, retune
    ├─ correction ─────── a shift per frame, note by note
    └─ synthesis ──────── the take, re-sung                  (PC-NSF-HiFiGAN or an RVC voice)
    │
    ▼
corrected audio, and the melody as MIDI
```

Editing does not re-read the recording. Only the two seconds around an edit are rendered again,
nearest the playhead first, so an edit is audible about as fast as you can make the next one.

## The two engines

**PC-NSF-HiFiGAN** is the default. It is a vocoder conditioned on a mel spectrogram and a
fundamental *separately*: the mel carries the timbre and the words, the fundamental carries the
tune, and it was trained on pairs where the two disagree. It re-sings whoever was recorded and
needs no model of them, which is the whole point for a corrector — the voice that comes out is the
voice that went in.

**An RVC voice** is the other option. Its content encoder strips the pitch out of the recording and
keeps everything else; its vocoder puts the edited melody back in. The vocoder's weights *are* a
voice, so this engine is a pitch corrector only when the voice it was trained on is the voice in
the recording — one of your own, trained with the sibling [RTVoice](https://github.com/vivekvjyn/RTVoice)
project. Point it at anyone else's model and it is a voice converter, not a tuner.

Both engines are offline and run on the CPU. Neither is usable for live monitoring: the detectors
see the future, and the correction is defined over whole notes.

## Why a corrected note still sounds sung

Retuning a note by moving its whole pitch curve is what makes correction sound synthetic. Here each
note is split into four parts and only one of them is touched:

| Part | What it is | What correction does |
| --- | --- | --- |
| Centre | where the note sat | moved, by however much the note is corrected |
| Drift | the slow wander across the note | scaled, kept at 100% by default |
| Vibrato | the 4–9 Hz shake | scaled, kept at 100% by default |
| Detail | the jitter under all of it | **always kept** |

The shift that results is smoothed across note boundaries and nowhere else, so tuning arrives and
leaves the way a slide does rather than as a step. Turn `Transition` up for a lazier approach into
each note, down for a hard snap.

On top of that, stretches you did not edit are not re-synthesised at all: with **Keep unedited** on,
they play back as recorded, and the joins between rendered and untouched audio are placed at the
quietest frame nearby, where a join cannot be heard.

## Editing

| Tool | What it does |
| --- | --- |
| Select | click a note to select it, drag it up or down to retune it, shift-click to add |
| Draw | draw a pitch curve freehand over the take |
| Split | click inside a note to cut it in two |
| Join | click a note to join it to the next |

Double-click retunes a note to the scale. Right-click for the rest: leave a note as sung, correct
it fully, forget it entirely, clear what was drawn, find the notes again. Up and down arrows move
the selection by a semitone, delete forgets it, space plays, ⌘Z and ⇧⌘Z undo and redo everything.
Alt-drag marks a stretch to play on repeat.

The dials along the top act on every note at once; the ones along the bottom act on the selection.
`Shortest note` decides how readily the melody is cut up — raise it on a legato take with a wide
vibrato, lower it on something fast.

## Requirements

- Linux, macOS or Windows
- CMake 3.24, a C++20 compiler
- ONNX Runtime (Fedora: `onnxruntime-devel`, macOS: `brew install onnxruntime`)

## Setup

```bash
git clone --recursive https://github.com/vivekvjyn/RVCTuner.git
cd RVCTuner
git submodule update --init --depth 1 libs/JUCE libs/googletest libs/hnswlib

cmake -B build
cmake --build build
```

If ONNX Runtime is somewhere CMake cannot see:

```bash
cmake -B build -DRVCTUNER_ONNXRUNTIME_ROOT=/path/to/onnxruntime
```

Then put the networks where the app looks for them — see [res/models/README.md](res/models/README.md).
The mel vocoder and one detector are enough to correct anything; an RVC voice is optional.

## Command line

The same engine without the editor, for batch work and for hearing what a setting does:

```bash
rvctuner-tune take.wav tuned.wav                       # correct to the chromatic scale
rvctuner-tune take.wav tuned.wav --key A --scale minor
rvctuner-tune take.wav tuned.wav --correction 0.6 --vibrato 1.2 --transition 80
rvctuner-tune take.wav tuned.wav --engine voice --voice female1
rvctuner-tune take.wav tuned.wav --detector fcpe
```

It prints how many notes it found and how far out of tune they were, which is the quickest way to
check a take before opening it.

## Layout

```
CMakeLists.txt        one target for the engine, one for the app, one for the tests
src/
  Main.cpp            the application
  common/             the matrix format the exporters write
  dsp/                resampling, filtering, mel spectra, smoothing, pitch maths
  model/              the detectors and the engines, behind two interfaces
  edit/               the document, the notes, the correction, the renderer, the pipeline
  audio/              playback
  ui/                 the editor and its panels
tools/TuneCli.cpp     the command line
tests/                the GoogleTest suite
res/models/           where the networks go, and what they are
docs/                 Doxygen
```

`src/model/PitchDetector.h` and `src/model/Synthesiser.h` are the two interfaces everything else is
written against, which is what makes the algorithms a choice rather than a rewrite.

## What is measured

Running the correction over a 20-second operatic take and re-analysing the result:

| | Before | After |
| --- | --- | --- |
| Notes within 15 cents of their target | 38 of 127 | 81 of 120 |
| Average error | 26.2 cents | 13.3 cents |

Shifting a take two semitones moves the measured pitch by 2.000 semitones and the spectral centroid
by 17 Hz, which is the point of a mel vocoder: the formants stay where the singer put them.

Rendering runs at about 4× real time on a six-core CPU, so a two-second span takes about half a
second to come back.

## Known limitations

- **Mono.** A stereo file is mixed down on load and exported as mono. Vocal takes generally are.
- **No time editing.** Notes can be retuned, split, joined and forgotten, but not moved or
  stretched in time; the timing you sang is the timing you get.
- **No formant control.** The mel engine holds the formants where they were sung, which is right
  for correction and wrong if you wanted to change the character of the voice.
- **Nothing is saved but the audio.** Closing the editor loses the edits; re-analysis takes a few
  seconds, so this is an annoyance rather than a loss.
- **The detectors are monophonic.** Two voices at once, or a vocal over a backing track, will not
  segment sensibly. Separate them first.

## Development

```bash
cmake -B build -DRVCTUNER_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
cmake --build build --target rvctuner_docs
```

The suite covers what the bugs were actually in: the filter banks against the exported ones, the
resampler and the zero-phase filter against SciPy, note segmentation against melodies with a
vibrato deep enough to be mistaken for a run of notes, the correction curve's separation of centre
from shake, and every edit the document exposes, including undo.

## Provenance

The DSP and model layers carry over from the author's [RVCARA](https://github.com/vivekvjyn/RVCARA)
plug-in and, through it, from [RVC](https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI).
The idea of running PC-NSF-HiFiGAN as a pitch editor's engine, and the two networks themselves, come
from [PitchNet](https://github.com/SessionLoops/PitchNet) and [HachiTune](https://github.com/KCKT0112/HachiTune),
which are AGPL-3.0; no code from either is used here, and their weights carry their own terms.

## Licence

MIT — see [LICENSE](LICENSE). JUCE is dual-licensed and imposes its own terms on a binary you
distribute; the networks carry theirs.
