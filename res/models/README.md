# Models

Tuner looks for its networks here, and in

- `~/.local/share/Tuner/models` (or the platform equivalent),
- `/usr/share/Tuner/models`,
- every path in `TUNER_MODEL_PATH`,
- a `models` folder beside the binary.

## What goes where

```
models/
  pitchnet/
    pc_nsf_hifigan.onnx    the engine: mel and fundamental in, waveform out
    fcpe.onnx              the default pitch detector
    GAME/                  the note segmenter: encoder, segmenter, bd2dur, estimator
      config.json
  rmvpe/
    rmvpe.onnx             the other detector, steadier against noise
```


The vocoder and one detector are enough. RMVPE is optional; without it the app uses FCPE. GAME is
optional too; without it the melody is cut into notes by rule instead, which is close on singing and
differed by one note in three takes measured here.

## Licences

The networks are third-party and carry their own terms, which are not this repository's licence.

- **PC-NSF-HiFiGAN**, **FCPE** and **GAME** are distributed with the PitchNet/HachiTune editor
  (AGPL-3.0) and originate from OpenVPI and CNChTu respectively. The OpenVPI vocoder weights are released for
  non-commercial use; check the terms that came with the files before shipping anything with them.
Weights you train yourself carry your terms, and the vocoder's training code is MIT, so that is the
way out if any of this needs to ship.
