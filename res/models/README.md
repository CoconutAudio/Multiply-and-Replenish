# Models

RVCTuner looks for its networks here, and in

- `~/.local/share/RVCTuner/models` (or the platform equivalent),
- `/usr/share/RVCTuner/models`,
- every path in `RVCTUNER_MODEL_PATH`.

## What goes where

```
models/
  pitchnet/
    pc_nsf_hifigan.onnx    the default engine: mel and fundamental in, waveform out
    fcpe.onnx              the lighter pitch detector
  rmvpe/
    rmvpe.onnx             the pitch detector used by default, if exported on its own
  <voice>/                 an RVC voice, exported by RTVoice
    manifest.json
    content_encoder.onnx
    pitch_estimator.onnx
    vocoder.onnx
    retrieval.bin
    mel_filter_bank.bin
```

Nothing here is required all at once. The mel vocoder alone is enough to correct any take; an RVC
voice is needed only for the voice engine, and it also supplies RMVPE when no standalone export of
it is installed.

## Licences

The networks are third-party and carry their own terms, which are not this repository's licence.

- **PC-NSF-HiFiGAN** and **FCPE** are distributed with the PitchNet/HachiTune editor (AGPL-3.0) and
  originate from OpenVPI and CNChTu respectively. The OpenVPI vocoder weights are released for
  non-commercial use; check the terms that came with the files before shipping anything with them.
- **RVC voices** carry whatever terms the voice they were trained on carries. Train on a voice you
  have the right to use.
