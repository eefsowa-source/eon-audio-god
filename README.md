# EON AUDIO — GOD EDITION (rebuilt)

Analog-emulation EQ plugins. Neve 1073 + Avalon 737, rebuilt from the
`EON_AUDIO_GOD` draft with the DSP made honest — see `Docs/REVIEW.md` for what
was wrong with the original.

- **EON 1073 GOD**: Jiles-Atherton Marinair transformers (real ODE, no DC-wander
  memory hack), inductor resonator, Koren triode solved by actual Newton-Raphson
  on the load line, ADAA2 soft clip / ADAA1 tanh interstages.
- **EON 737 GOD**: Class-A asymmetric harmonic stage, AC-coupled dielectric-
  absorption memory, bounded power-sag/thermal/OU drift, 32k "liquid air" path.

## Quality modes — now real

The oversampler actually changes with the mode (original ran 4x regardless):

- Eco 8x — ADAA soft clip, no triode solve
- Liquid 16x — Newton triode, 2 iterations
- GOD 32x — Newton triode, 2 iterations
- GOD+ NR 32x — Newton triode, 4 iterations (warm-started)

Latency differs per mode and is pushed to the host on change.

## Build

JUCE is a git submodule pinned at **8.0.9** — **JUCE 7 does not compile
against the macOS 15 SDK** (`CGWindowListCreateImage` obsoleted); use 8.x.

```bash
git clone --recursive https://github.com/eefsowa-source/eon-audio-god.git
cd eon-audio-god
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

Artifacts land in `build/<name>_artefacts/Release/` and are also copied to
`~/Library/Audio/Plug-Ins/` (VST3 + AU + Standalone).

## Verify

```bash
c++ -std=c++20 -O2 -I Source/Shared Docs/dsp_smoke_test.cpp -o /tmp/dsp_test && /tmp/dsp_test
```

17 checks over the pure-DSP headers (no JUCE needed): ADAA correctness,
Newton residual, JA boundedness/hysteresis, bounded drift, DC rejection.

## Layout

```
Source/Shared/Dsp/   Adaa.h Triode.h Transformer.h Stages.h   (pure DSP, no JUCE dsp)
Source/Shared/       Quality.h (oversampling manager) EonLookAndFeel.h
Source/Neve1073/     plugin processor + editor
Source/Avalon737/    plugin processor + editor
Docs/REVIEW.md       findings on the original draft
```
