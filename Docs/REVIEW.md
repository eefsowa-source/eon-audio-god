# Code review — `~/Downloads/EON_AUDIO_GOD` (original draft)

Reviewed 2026-09-18. The draft does not compile, and its headline claims
(32x oversampling, Newton-Raphson triode, Jiles-Atherton, WDF) are largely
marketing over approximations. Findings, worst first.

## Build-breaking

1. **`CMakeLists.txt:61`** — stray `j56|endfunction()` garbage line after the
   function body → CMake parse error.
2. **`CpuOptimizer.h:80-83`** — stray `bool pool[i] = 0.f;` and closing braces
   orphaned inside `LargeBufferPool` → syntax error.
3. **Headers and implementations are from different versions.** Every `.cpp`
   references members its `.h` never declares:
   - `Neve1073.cpp` uses `adaaTanh`, `triodeGod`, `air`, `cpuOpt`,
     `lowGainSmooth`, `midGainSmooth`, `highGainSmooth`, `outputSmooth` — the
     header declares none of them (it has `MarinairTransformer`, `InductorSat`,
     `lowGainSmoothed` — old names, unused).
   - `Avalon737.cpp` uses `classA[ch]` (array) but the header declares scalar
     `BastardDSP::AvalonClassA classA` — and `AvalonClassA` doesn't exist; the
     stage in `AnalogStages.h` is named `AvalonGod`.
   - `AnalogStages.h` defines `MarinairGod`, `WDFInductorGod`, `DCBlockerGod`,
     `NewtonTriodeGod`, `ADAATanh2`, `AnalogAirGod` — the headers' names
     (`MarinairTransformer`, `InductorSat`, `DCBlocker`) don't exist anywhere.
4. **Editor classes never declared.** `PluginEditor.h` files contain only
   `// Using Generic Editor for max compatibility`; the `.cpp`s define
   `Neve1073Editor`/`Avalon737Editor` bodies with no class declaration →
   compile error. `createEditor()` returns an undeclared type.
5. **No input bus.** `BusesProperties().withOutput("Output", ...)` only — an EQ
   plugin with no input bus. Hosts may hand it an empty buffer.
6. Duplicate `shared/AnalogStages.h` at repo root (62 lines, divergent copy);
   `Source/shared/AnalogStages.cpp` is an empty `namespace BastardDSP {}`.

## The 32x claim is false — and it breaks every filter

7. `oversampler {2, 2, ...}` — constructor arg is the **exponent**: 2^2 = **4x**,
   not 32x. All EQ coefficients are designed at `sr*32` while the DSP actually
   runs at `sr*4` → every shelf/peak/HPF lands **8x too high** (a 1.6 kHz mid
   peak sits at ~12.8 kHz). The plugin's frequency response is wrong
   everywhere, not subtly.
8. The `quality` parameter (Eco 8x / Liquid 16x / GOD 32x) changes nothing
   about oversampling — it's always 4x. Only the `qualityIdx==3` branch swaps
   one interstage. Eco/Liquid/GOD labels lie about CPU and fidelity.

## DSP: fabricated math

9. **`NewtonTriodeGod.solveNewton` solves nothing.** `f = Ip - Vp*0.005` is not
   a circuit equation (no supply, no load line) and `dIp_dVp = Ip*0.01` is a
   made-up derivative. It's a fixed-point wobble labelled Newton-Raphson.
   Vp restarts at 150 V every sample (no warm start). The 256x256 LUT
   (~256 KB/instance) is built in `reset()` and **never read**.
10. **ADAA2 recursion is wrong.** It mixes antiderivative orders: `a1` uses
    F1 differences, `a0` divides an F1-minus-F2 difference — dimensionally
    meaningless. Real ADAA2: `y = 2*(D01 - D12)/(x0 - x2)` on F2 divided
    differences.
11. **tanh F2 is discontinuous.** The piecewise "second antiderivative" jumps
    at |v|=2 (≈1.333 vs ≈1.114) → audible click on loud peaks. tanh has no
    elementary F2 anyway — don't fake it.
12. **ADAA epsilon branch divides by <1e-4** → potential output spikes.
13. **`MarinairGod` isn't JA.** Real JA: `dMirr/dH = (Man-Mirr)/(kδ - α(Man-Mirr))`,
    `M = Mirr + c(Man-Mirr)`. The draft multiplies by an arbitrary 0.12, then
    adds a free-running `longMemory` accumulator (`*0.99998`) into M — a slow
    random DC wander, not iron physics.
14. **`AvalonGod.drift` is an unbounded random walk** (`+= rand*2e-7`/sample)
    → output DC wanders without limit over minutes. Also uses
    `getSystemRandom()` per-sample per-instance (contention, correlated).
15. **Avalon DA caps feed raw integrator output into audio** — three leaky
    integrators summed into `da` → guaranteed slow DC buildup.
16. **`WDFInductorGod` is not a WDF** — it's tanh + three leaky integrators.
    Fine stage, dishonest name.
17. **HPF double-processing:** `x = hp(x); x += hp(x)*0.025` runs the filter
    twice per sample → doubles its order and scrambles phase. Avalon does
    `hp(hpOut)` similarly.
18. **`processSIMDBlock`** is a scalar loop in a trench coat — and unused.
19. **`firPool.prepare(16M floats)`** allocates 64 MB per plugin instance,
    never touched again. Same for `EON_HAS_64GB_RAM` — no effect.
20. `juce::Random` Barkhausen noise is white noise, not activity-correlated
    jumps.

## Process correctness

21. **Smoothers consumed once per block.** `lowGainSmooth.getNextValue()` etc.
    are called once and baked into coefficients — the "smoothing" does nothing;
    gain changes still step. Output gain same (single `getNextValue()` then a
    constant `applyGain`).
22. **Silence skip drops latency consistency.** `buffer.clear(); return;`
    bypasses the oversampler, so a silent stretch emits at 0 latency while the
    host compensates for the reported latency — timing glitch, plus it throws
    away the filter states' natural decay.
23. `buffer.getRMSLevel(0, ...)` reads only channel 0.
24. `getName()` still says "Bastard"; editor draws "1073 BASTARD ... 2x OS".
25. No `isBusesLayoutSupported` — mono gets stereo treatment or vice versa.
26. `midPeak` declared but `air`/`cpuOpt`/`triodeGod` used undeclared (see #3).

## What the rebuild changes (this repo)

- Real 8x/16x/32x oversampling selected by quality mode; latency re-reported
  to the host on change (`Quality.h`).
- Correct ADAA1 (tanh, exact F1) and ADAA2 (cubic soft clip, exact F1/F2,
  C1-continuous) (`Dsp/Adaa.h`).
- Koren triode with analytic dIp/dVp, real load-line Newton, warm start,
  quiescent solve at reset (`Dsp/Triode.h`). No LUT — it was dead weight;
  Eco mode honestly uses the cheaper clip instead.
- Textbook Jiles-Atherton with Mirr state; Barkhausen noise scaled by
  |dMirr/dH| (`Dsp/Transformer.h`).
- Bounded OU drift, sag/thermal followers; DA memory AC-coupled through
  DC-servo means (`Dsp/Stages.h`).
- Single-pass HPF; per-block-consumed, per-channel-shared drive ramp; silence
  skip that keeps the oversampler delay line (`PluginProcessor.cpp`).
- Input bus declared; buses layout check; consistent GOD naming.
