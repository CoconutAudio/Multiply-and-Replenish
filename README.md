# Tuner

[![build](https://github.com/vivekvjyn/Tuner/actions/workflows/build.yml/badge.svg)](https://github.com/vivekvjyn/Tuner/actions/workflows/build.yml)

A vocal pitch editor, in the shape of Newtone: open a take, see the melody as notes on a piano
roll, move them, and hear the take sung back at the pitches you put them on.

A standalone app, plus a command line for batch work.

```bash
Tuner take.wav
```

---

## What it does

It hears the melody, cuts it into notes, and re-sings the recording from an edited copy of that
melody. The words, the delivery, the breath and the timbre all come from the recording; only the
tune is yours to change. The melody is heard from the channels summed, since there is only one of
it, but each channel is rendered on its own, so a stereo take comes back stereo.

```
recording
    │
    ├─ pitch detection ── a fundamental every 10 ms          (FCPE, or RMVPE)
    ├─ segmentation ───── one note per sung pitch            (GAME, or by rule)
    ├─ editing ────────── the piano roll: move, split, join, draw, retune
    ├─ correction ─────── a shift per frame, note by note
    └─ synthesis ──────── the take, re-sung                  (PC-NSF-HiFiGAN)
    │
    ▼
corrected audio, and the melody as MIDI
```

Editing does not re-read the recording. Only the two seconds around an edit are rendered again,
nearest the playhead first, so an edit is audible about as fast as you can make the next one.

## The engine

**PC-NSF-HiFiGAN** is a vocoder conditioned on a mel spectrogram and a fundamental *separately*:
the mel carries the timbre and the words, the fundamental carries the tune, and it was trained on
pairs where the two disagree. It re-sings whoever was recorded and needs no model of them, which is
the whole point for a corrector — the voice that comes out is the voice that went in.

It is offline and runs on the CPU, leaving two cores free so that rendering cannot stutter
playback. It is not usable for live monitoring: the detectors see the future, and the correction is
defined over whole notes.

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

ONNX Runtime is found if it is installed, and downloaded for the platform if it is not.

## Setup

```bash
git clone https://github.com/vivekvjyn/Tuner.git
cd Tuner
git submodule update --init --depth 1 libs/JUCE libs/googletest

cmake -B build
cmake --build build
```

To build against an ONNX Runtime of your own:

```bash
cmake -B build -DTUNER_ONNXRUNTIME_ROOT=/path/to/onnxruntime
```

Then put the networks where the app looks for them — see [res/models/README.md](res/models/README.md).

## Command line

The same engine without the editor, for batch work and for hearing what a setting does:

```bash
tuner-tune take.wav tuned.wav                       # correct to the chromatic scale
tuner-tune take.wav tuned.wav --key A --scale minor
tuner-tune take.wav tuned.wav --correction 0.6 --vibrato 1.2 --transition 80
tuner-tune take.wav tuned.wav --detector rmvpe
```

It prints how many notes it found and how far out of tune they were, which is the quickest way to
check a take before opening it.

## Layout

```
CMakeLists.txt        one target for the engine, one for the app, one for the tests
cmake/                finding or fetching ONNX Runtime
src/
  Main.cpp            the application
  dsp/                resampling, filtering, mel spectra, smoothing, pitch maths
  model/              the detectors and the vocoder, behind two interfaces
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
by 17 Hz: the formants stay where the singer put them. A panned vocal keeps its balance to within a
twentieth of a decibel. Rendering runs at about 4× real time on a six-core CPU, so a two-second span
takes about half a second to come back.

On a synthetic flute the vocoder reconstructs faithfully to C6 and then drifts sharp — +62 cents at
B6, where no singer ever trained it. Instruments above that range need a vocoder trained on them.

## Known limitations

- **Stereo width that lives in phase does not survive.** Channels are read and rendered one by one,
  so a stereo file stays stereo and a panned vocal keeps its balance. But a mel spectrogram holds no
  phase, so width that comes from a delay or a chorus between the channels collapses towards the
  middle: a four millisecond offset measured six times narrower coming out. Correct the dry vocal
  and add the width afterwards. Two channels that arrive identical are read and rendered once, and
  come back bit-identical.
- **No time editing.** Notes can be retuned, split, joined and forgotten, but not moved or stretched
  in time; the timing you sang is the timing you get.
- **No formant control.** The engine holds the formants where they were sung, which is right for
  correction and wrong if you wanted to change the character of the voice.
- **Nothing is saved but the audio.** There is no project format, so re-opening a file means
  re-analysing it — a few seconds, but the edits are gone.
- **The detectors are monophonic.** Two voices at once, or a vocal over a backing track, will not
  segment sensibly. Separate them first.

## Development

```bash
cmake -B build -DTUNER_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
cmake --build build --target tuner_docs
```

The suite covers what the bugs were actually in: the filter banks against the exported ones, the
resampler and the zero-phase filter against SciPy, note segmentation against melodies with a
vibrato deep enough to be mistaken for a run of notes, the correction curve's separation of centre
from shake, and every edit the document exposes, including undo.

CI builds all three platforms and uploads the app and the command line for each.

## Provenance and licences

The DSP layer carries over from the author's [RVCARA](https://github.com/vivekvjyn/RVCARA) plug-in.
The idea of running PC-NSF-HiFiGAN as a pitch editor's engine comes from
[PitchNet](https://github.com/SessionLoops/PitchNet) and [HachiTune](https://github.com/KCKT0112/HachiTune),
which are AGPL-3.0; no code from either is used here.

This repository is MIT. The networks are not, and they are the part that decides what you may ship:

| | Licence | In a paid product |
| --- | --- | --- |
| This code | MIT | yes |
| ONNX Runtime | MIT | yes |
| JUCE | AGPLv3 or a commercial licence | needs the commercial licence |
| PC-NSF-HiFiGAN weights | CC BY-NC-SA 4.0 | **no** |
| FCPE weights | CC BY-NC-SA 4.0 | **no** |
| RMVPE weights | none stated | **no** |

The training code for the vocoder ([openvpi/SingingVocoders](https://github.com/openvpi/SingingVocoders))
is MIT, so weights you train yourself are yours, and drop in unchanged: the configs already use the
44.1 kHz, 128-bin, hop-512 mel this expects. Fine-tuning the released checkpoint does not help — a
derivative of a NonCommercial model stays NonCommercial.

## Licence

MIT — see [LICENSE](LICENSE).
