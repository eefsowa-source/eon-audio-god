# EON DSP Core Correctness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the framework-free `eon_dsp` algorithms deterministic across block sizes, numerically finite over their supported input ranges, sample-rate invariant where coefficients represent time, and directly testable with CMake/CTest.

**Architecture:** The canonical repository `/Users/sungha/Desktop/EON LLM wiki/EON Audio Plugin/eon_dsp` remains a header-only library. Add a small CMake INTERFACE target plus focused framework-free tests, then fix each algorithm behind a failing regression test. Keep JUCE and product integration out of this plan; the companion EON_GOD plan consumes the corrected headers after this plan passes.

**Tech Stack:** C++20, CMake 3.22+, CTest, Apple Clang/GCC/MSVC, ASan/UBSan where supported.

---

## File map

- Create `CMakeLists.txt`: exports `eon_dsp` as an INTERFACE target and owns CTest targets.
- Create `Tests/TestHarness.h`: assertion helpers shared by the framework-free tests.
- Create `Tests/AdaaOversamplingTests.cpp`: ADAA repeated-node and oversampler block-partition regressions.
- Create `Tests/StageTriodeTests.cpp`: DC blocker, Class-A continuity, and triode operating-point regressions.
- Create `Tests/SolverWdfZdfTests.cpp`: Lambert W, WDF leaf/diode, and nonlinear ladder regressions.
- Create `Tests/TestMain.cpp`: runs all three test groups.
- Modify `Dsp/Adaa.h`: correct the second-order repeated-node limit.
- Modify `Dsp/Oversampling.h`: separate interpolation and decimation histories.
- Modify `Dsp/Stages.h`: configure the DC blocker in Hz and make Class-A saturation continuous.
- Modify `Dsp/Triode.h`: converge reset state and make `lastIp` correspond to the returned plate voltage.
- Modify `Dsp/Solvers.h`: keep Lambert W iteration finite for very small log arguments.
- Modify `Dsp/Wdf.h`: correct wave accessors/source behavior and harden nonlinear roots.
- Modify `Dsp/Zdf.h`: solve the saturated ladder feedback equation implicitly.
- Modify `NOTES.md`: document the public preparation/reset contracts and test commands.

### Task 1: Add the standalone core test target

**Files:**
- Create: `CMakeLists.txt`
- Create: `Tests/TestHarness.h`
- Create: `Tests/TestMain.cpp`
- Create: `Tests/AdaaOversamplingTests.cpp`
- Create: `Tests/StageTriodeTests.cpp`
- Create: `Tests/SolverWdfZdfTests.cpp`

- [ ] **Step 1: Add the header-only library and test executable**

Create `CMakeLists.txt` with this content:

```cmake
cmake_minimum_required(VERSION 3.22)
project(eon_dsp LANGUAGES CXX)

add_library(eon_dsp INTERFACE)
add_library(eon::dsp ALIAS eon_dsp)
target_include_directories(eon_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(eon_dsp INTERFACE cxx_std_20)

include(CTest)
if(BUILD_TESTING)
    add_executable(eon_dsp_tests
        Tests/TestMain.cpp
        Tests/AdaaOversamplingTests.cpp
        Tests/StageTriodeTests.cpp
        Tests/SolverWdfZdfTests.cpp)
    target_link_libraries(eon_dsp_tests PRIVATE eon::dsp)
    if(MSVC)
        target_compile_options(eon_dsp_tests PRIVATE /W4)
    else()
        target_compile_options(eon_dsp_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME eon_dsp_core COMMAND eon_dsp_tests)
endif()
```

- [ ] **Step 2: Add a test harness that does not disappear under `NDEBUG`**

Create `Tests/TestHarness.h`:

```cpp
#pragma once
#include <cmath>
#include <cstdio>

namespace eon::test {
inline int failures = 0;

inline void check (bool condition, const char* message)
{
    std::printf ("%s  %s\n", condition ? "ok  " : "FAIL", message);
    if (! condition) ++failures;
}

inline bool near (double actual, double expected, double tolerance)
{
    return std::isfinite (actual) && std::abs (actual - expected) <= tolerance;
}
}
```

Create `Tests/TestMain.cpp`:

```cpp
#include "TestHarness.h"

void runAdaaOversamplingTests();
void runStageTriodeTests();
void runSolverWdfZdfTests();

int main()
{
    runAdaaOversamplingTests();
    runStageTriodeTests();
    runSolverWdfZdfTests();
    std::printf ("\n%d failure(s)\n", eon::test::failures);
    return eon::test::failures == 0 ? 0 : 1;
}
```

Create the three test source files with the relevant includes and empty entry points:

```cpp
// Tests/AdaaOversamplingTests.cpp
#include "TestHarness.h"
#include "Dsp/Adaa.h"
#include "Dsp/Oversampling.h"
void runAdaaOversamplingTests() {}
```

```cpp
// Tests/StageTriodeTests.cpp
#include "TestHarness.h"
#include "Dsp/Stages.h"
#include "Dsp/Triode.h"
void runStageTriodeTests() {}
```

```cpp
// Tests/SolverWdfZdfTests.cpp
#include "TestHarness.h"
#include "Dsp/Solvers.h"
#include "Dsp/Wdf.h"
#include "Dsp/Zdf.h"
void runSolverWdfZdfTests() {}
```

- [ ] **Step 3: Configure and run the empty suite**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected: configure/build succeed and `eon_dsp_core` passes with `0 failure(s)`.

- [ ] **Step 4: Commit the harness**

```bash
git add CMakeLists.txt Tests
git commit -m "test: add standalone eon dsp core suite"
```

### Task 2: Correct ADAA2 repeated-node handling

**Files:**
- Modify: `Tests/AdaaOversamplingTests.cpp`
- Modify: `Dsp/Adaa.h:47`

- [ ] **Step 1: Add the failing repeated-outer-sample test**

Put this helper and test inside `runAdaaOversamplingTests()`:

```cpp
{
    eon::SoftClipSat sat;
    sat.reset();
    sat.process (0.5f);
    sat.process (-0.5f);
    const double actual = sat.process (0.5f);
    const double a = 0.5, b = -0.5;
    const double expected = 2.0 * (eon::SoftClip::F1 (a) * (a - b)
                                 - eon::SoftClip::F2 (a)
                                 + eon::SoftClip::F2 (b)) / ((a - b) * (a - b));
    eon::test::check (eon::test::near (actual, expected, 1.0e-6),
                      "ADAA2 uses the repeated-node limit when x0 equals x2");
}
```

- [ ] **Step 2: Run the focused binary and verify the regression fails**

Run: `cmake --build build --target eon_dsp_tests && ./build/eon_dsp_tests`

Expected: FAIL for the repeated-node test; observed unfixed values are approximately `0.458333` versus `0.158333`.

- [ ] **Step 3: Implement the two distinct limits**

Replace the `dx02` fallback in `ADAA2::process` with:

```cpp
if (std::abs (dx02) < eps)
{
    if (std::abs (dx01) < eps)
    {
        y = f (x0);
    }
    else
    {
        y = 2.0 * (f1 (x0) * dx01 - F2x0 + F2x1) / (dx01 * dx01);
    }
}
else
{
    const double d01 = std::abs (dx01) < eps ? f1 (0.5 * ((double) x0 + x1))
                                             : (F2x0 - F2x1) / dx01;
    const double d12 = std::abs (dx12) < eps ? f1 (0.5 * (x1 + x2))
                                             : (F2x1 - F2x2) / dx12;
    y = 2.0 * (d01 - d12) / dx02;
}
```

- [ ] **Step 4: Add a near-equal stability check**

Add after the first ADAA2 test:

```cpp
{
    eon::SoftClipSat sat;
    for (float x : { 0.5f, -0.5f, std::nextafter (0.5f, 1.0f) })
        eon::test::check (std::isfinite (sat.process (x)),
                          "ADAA2 stays finite around the repeated-node branch");
}
```

- [ ] **Step 5: Run and commit**

Run: `cmake --build build --target eon_dsp_tests && ctest --test-dir build --output-on-failure`

Expected: all tests pass.

```bash
git add Dsp/Adaa.h Tests/AdaaOversamplingTests.cpp
git commit -m "fix: handle repeated nodes in ADAA2"
```

### Task 3: Make oversampling block-partition invariant

**Files:**
- Modify: `Tests/AdaaOversamplingTests.cpp`
- Modify: `Dsp/Oversampling.h:129`

- [ ] **Step 1: Add a block-partition regression**

Add this helper above `runAdaaOversamplingTests()`:

```cpp
static std::vector<float> renderRoundTrip (int blockSize, int stages)
{
    constexpr int total = 8192;
    eon::Oversampler os;
    os.setStages (stages);
    os.prepare (blockSize);
    std::vector<float> input ((size_t) blockSize);
    std::vector<float> up ((size_t) blockSize * (size_t) os.factor());
    std::vector<float> output ((size_t) total);
    for (int offset = 0; offset < total; offset += blockSize)
    {
        for (int i = 0; i < blockSize; ++i)
            input[(size_t) i] = 0.5f * std::sin (2.0 * M_PI * 1000.0
                                                * (offset + i) / 48000.0);
        os.up (input.data(), blockSize, up.data());
        os.down (up.data(), output.data() + offset, blockSize);
    }
    return output;
}
```

Add required includes and the assertion:

```cpp
#include <algorithm>
#include <vector>

{
    const auto small = renderRoundTrip (16, 3);
    const auto large = renderRoundTrip (256, 3);
    double peak = 0.0;
    for (size_t i = 256; i < small.size(); ++i)
        peak = std::max (peak, std::abs ((double) small[i] - large[i]));
    eon::test::check (peak < 1.0e-6,
                      "8x oversampling output is independent of host block partition");
}
```

- [ ] **Step 2: Verify the test fails**

Run: `cmake --build build --target eon_dsp_tests && ./build/eon_dsp_tests`

Expected: FAIL with peak difference near `0.735`.

- [ ] **Step 3: Split interpolation and decimation state**

In `Oversampler`, replace the three stage members with six:

```cpp
HalfBand2x<HbTaps71> up0, down0;
HalfBand2x<HbTaps47> up1, down1;
HalfBand2x<HbTaps35> up2, down2;
```

Update preparation and reset:

```cpp
void prepare (int maxBlock)
{
    up0.init(); up1.init(); up2.init();
    down0.init(); down1.init(); down2.init();
    tmp.assign  ((size_t) maxBlock * 8u + 8u, 0.0f);
    tmp2.assign ((size_t) maxBlock * 8u + 8u, 0.0f);
}

void reset()
{
    up0.reset(); up1.reset(); up2.reset();
    down0.reset(); down1.reset(); down2.reset();
}
```

Use only `up0/up1/up2` in `upStage`, and only `down0/down1/down2` in `down` and `downStage`:

```cpp
if (stage == 0)      up0.up (in[i], e, o);
else if (stage == 1) up1.up (in[i], e, o);
else                 up2.up (in[i], e, o);
```

```cpp
const double y = stage == 0 ? down0.down (in[2 * i], in[2 * i + 1])
               : stage == 1 ? down1.down (in[2 * i], in[2 * i + 1])
                            : down2.down (in[2 * i], in[2 * i + 1]);
```

- [ ] **Step 4: Cover 2x, 4x, and 8x**

Replace the single assertion block with a loop over stages `{ 1, 2, 3 }`, retaining the same `peak < 1.0e-6` threshold.

- [ ] **Step 5: Run and commit**

Run: `cmake --build build --target eon_dsp_tests && ctest --test-dir build --output-on-failure`

Expected: all three partition comparisons pass.

```bash
git add Dsp/Oversampling.h Tests/AdaaOversamplingTests.cpp
git commit -m "fix: separate oversampler up and down histories"
```

### Task 4: Make time constants and Class-A limiting continuous

**Files:**
- Modify: `Tests/StageTriodeTests.cpp`
- Modify: `Dsp/Stages.h:11`

- [ ] **Step 1: Add rate-invariance and continuity regressions**

Add to `runStageTriodeTests()`:

```cpp
{
    for (double rate : { 48000.0 * 8.0, 48000.0 * 16.0, 48000.0 * 32.0 })
    {
        eon::DCBlocker blocker;
        blocker.prepare (rate, 18.0);
        const double measured = -std::log (blocker.R) * rate / (2.0 * M_PI);
        eon::test::check (eon::test::near (measured, 18.0, 1.0e-9),
                          "DC blocker cutoff remains 18 Hz at every processing rate");
    }
}

{
    eon::ClassAStage below, above;
    const double y0 = below.process (3.27077f);
    const double y1 = above.process (3.27078f);
    eon::test::check (std::abs (y1 - y0) < 1.0e-3,
                      "Class-A limiter is continuous across its former threshold");
}
```

- [ ] **Step 2: Verify both checks fail**

Run: `cmake --build build --target eon_dsp_tests && ./build/eon_dsp_tests`

Expected: compilation fails because `prepare` does not exist; after temporarily isolating that check, the continuity check fails with a jump near `0.358`.

- [ ] **Step 3: Give the DC blocker a physical cutoff API**

Change `DCBlocker` to:

```cpp
struct DCBlocker
{
    double x1 = 0.0, y1 = 0.0;
    double R = 0.9997;

    void prepare (double sampleRate, double cutoffHz = 18.0)
    {
        R = std::exp (-2.0 * M_PI * cutoffHz / sampleRate);
    }

    void reset() { x1 = y1 = 0.0; }
    inline float process (float x)
    {
        const double y = (double) x - x1 + R * y1;
        x1 = x;
        y1 = y;
        return (float) y;
    }
};
```

The 18 Hz default preserves the measured Eco 8x voicing while removing quality-mode drift. Product code must call `prepare`; that is handled in the integration plan.

- [ ] **Step 4: Replace the discontinuous Class-A branch**

Replace the thresholded limiter with a continuous function:

```cpp
out = 1.5 * std::tanh (out / 1.5);
```

- [ ] **Step 5: Run and commit**

Run: `cmake --build build --target eon_dsp_tests && ctest --test-dir build --output-on-failure`

Expected: cutoff and continuity tests pass.

```bash
git add Dsp/Stages.h Tests/StageTriodeTests.cpp
git commit -m "fix: stabilize stage timing and saturation"
```

### Task 5: Converge the triode operating point at reset

**Files:**
- Modify: `Tests/StageTriodeTests.cpp`
- Modify: `Dsp/Triode.h:69`

- [ ] **Step 1: Add grounded- and self-biased zero-input tests**

Add:

```cpp
{
    eon::TriodeStage triode;
    triode.iterations = 2;
    triode.reset();
    eon::test::check (std::abs (triode.process (0.0f)) < 1.0e-5,
                      "grounded-cathode triode starts at zero output");
}

{
    eon::TriodeStage triode;
    triode.iterations = 2;
    triode.setCathode (1500.0f, 25.0e-6f, 48000.0 * 32.0);
    triode.reset();
    float y = 0.0f;
    for (int i = 0; i < 500000; ++i) y = triode.process (0.0f);
    eon::test::check (std::abs (y) < 1.0e-4,
                      "self-biased triode remains at its reset operating point");
}
```

- [ ] **Step 2: Verify both regressions fail**

Run: `cmake --build build --target eon_dsp_tests && ./build/eon_dsp_tests`

Expected: grounded output is about `0.00528`; self-biased settled output is about `0.208`.

- [ ] **Step 3: Make the plate solve return consistent current**

After the Newton loop in `solveVp`, recompute current at the returned voltage:

```cpp
lastIp = korenIp (Vg - Vk, Vp - Vk);
return Vp;
```

Remove `lastIp = Ip;` from inside the loop.

- [ ] **Step 4: Add a converged operating-point helper used only by reset**

Add this private-style member before `reset()`:

```cpp
float solveOperatingPoint (float Vk)
{
    const int savedIterations = iterations;
    iterations = 16;
    const float vp = solveVp (biasVg, Vk);
    iterations = savedIterations;
    VpPrev = vp;
    return vp;
}
```

Replace the reset loop with a bounded convergence loop:

```cpp
for (int i = 0; i < 32; ++i)
{
    Vq = solveOperatingPoint ((float) cathodeVk);
    const double nextVk = cathodeRk > 0.0f ? (double) lastIp * cathodeRk : 0.0;
    if (std::abs (nextVk - cathodeVk) < 1.0e-9)
    {
        cathodeVk = nextVk;
        break;
    }
    cathodeVk = nextVk;
}
Vq = solveOperatingPoint ((float) cathodeVk);
outScale = 1.0f / std::max (40.0f, Bplus - Vq);
VpPrev = Vq;
```

- [ ] **Step 5: Run and commit**

Run: `cmake --build build --target eon_dsp_tests && ctest --test-dir build --output-on-failure`

Expected: both zero-input checks pass with the normal processing budget still set to two iterations.

```bash
git add Dsp/Triode.h Tests/StageTriodeTests.cpp
git commit -m "fix: converge triode reset operating point"
```

### Task 6: Harden Lambert W and WDF primitives

**Files:**
- Modify: `Tests/SolverWdfZdfTests.cpp`
- Modify: `Dsp/Solvers.h:23`
- Modify: `Dsp/Wdf.h:49`

- [ ] **Step 1: Add finite-domain and WDF contract tests**

Add to `runSolverWdfZdfTests()`:

```cpp
{
    const double w = eon::lambertW0log (-1000.0);
    eon::test::check (std::isfinite (w) && w >= 0.0,
                      "Lambert W log-domain solver handles underflow-scale input");
}

{
    eon::WdfISourceRes source (1000.0);
    source.Is = 0.001;
    source.incident (0.3);
    eon::test::check (eon::test::near (source.emitted(), -1.7, 1.0e-12),
                      "WDF current source emits its Norton wave");
}

{
    eon::WdfCapacitor cap (1.0e-6);
    cap.setSampleRate (48000.0);
    cap.incident (1.0);
    eon::test::check (eon::test::near (cap.voltage(), 0.5, 1.0e-12),
                      "WDF capacitor reports half the incident plus reflected wave");
}

{
    eon::WdfDiodePair diode;
    diode.R = 4700.0;
    for (double input : { 1.0, 2.0, 20.0, 0.0 })
    {
        diode.incident (input);
        eon::test::check (std::isfinite (diode.emitted()),
                          "WDF diode pair remains finite under large input and recovery");
    }
    diode.reset();
    diode.incident (0.0);
    eon::test::check (eon::test::near (diode.emitted(), 0.0, 1.0e-12),
                      "WDF diode reset clears the warm start");
}
```

- [ ] **Step 2: Verify the failures**

Run: `cmake --build build --target eon_dsp_tests && ./build/eon_dsp_tests`

Expected: Lambert W and the three WDF contract groups fail.

- [ ] **Step 3: Handle underflow directly in `lambertW0log`**

Add at the top of the function:

```cpp
if (logX < std::log (std::numeric_limits<double>::min()))
    return 0.0;
```

Add `#include <limits>` to `Dsp/Solvers.h`. Clamp every Newton update to the positive domain:

```cpp
const double next = w - dw;
w = next > 0.0 && std::isfinite (next) ? next : 0.5 * w;
```

- [ ] **Step 4: Correct WDF state and source waves**

Change `WdfCapacitor` to retain the voltage computed during the latest scattering step:

```cpp
double C, state = 0.0, voltageState = 0.0;
void reset() override { state = voltageState = 0.0; }
double emitted() const override { return state; }
void incident (double a) override
{
    voltageState = 0.5 * (a + state);
    state = a;
}
double voltage() const { return voltageState; }
```

Use the Norton wave for `WdfISourceRes`:

```cpp
double aIn = 0.0;
double emitted() const override { return aIn - 2.0 * R * Is; }
void incident (double a) override { aIn = a; }
void reset() override { aIn = 0.0; }
```

- [ ] **Step 5: Replace diode-pair Newton with a bounded voltage solve**

Use the physical bound `[-abs(aIn), abs(aIn)]`, safe exponent clamping, and the existing bracketed solver:

```cpp
void reset() override { aIn = vPrev = 0.0; }

double emitted() const override
{
    const double bound = std::max (std::abs (aIn), 1.0e-12);
    const auto current = [this] (double v)
    {
        const double q = std::clamp (v / Vt, -40.0, 40.0);
        return 2.0 * Is * std::sinh (q);
    };
    const auto residual = [this, &current] (double v)
    {
        return current (v) - (aIn - v) / R;
    };
    const auto derivative = [this] (double v)
    {
        const double q = std::clamp (v / Vt, -40.0, 40.0);
        return (2.0 * Is / Vt) * std::cosh (q) + 1.0 / R;
    };
    const double initial = std::isfinite (vPrev) ? vPrev : 0.0;
    const double v = newtonScalar (residual, derivative, initial,
                                   -bound, bound, 32, 1.0e-12);
    vPrev = v;
    return 2.0 * v - aIn;
}
```

- [ ] **Step 6: Run and commit**

Run: `cmake --build build --target eon_dsp_tests && ctest --test-dir build --output-on-failure`

Expected: all solver/WDF tests pass and no output is NaN or infinite.

```bash
git add Dsp/Solvers.h Dsp/Wdf.h Tests/SolverWdfZdfTests.cpp
git commit -m "fix: harden solver and WDF edge cases"
```

### Task 7: Solve the nonlinear ladder feedback loop

**Files:**
- Modify: `Tests/SolverWdfZdfTests.cpp`
- Modify: `Dsp/Zdf.h:78`

- [ ] **Step 1: Add the implicit-equation residual test**

Add:

```cpp
{
    eon::Ladder4 ladder;
    ladder.setCutoff (12000.0, 48000.0);
    ladder.setResonance (4.0);
    ladder.reset();
    const double input = 2.0;
    const double output = ladder.process (input);
    const double G = ladder.g / (1.0 + ladder.g);
    const double G4 = G * G * G * G;
    const double u = output / G4;
    const double residual = u - std::tanh (input * ladder.drive - ladder.k * output);
    eon::test::check (std::abs (residual) < 1.0e-9,
                      "saturated ladder satisfies its implicit feedback equation");
}
```

- [ ] **Step 2: Verify the residual test fails**

Run: `cmake --build build --target eon_dsp_tests && ./build/eon_dsp_tests`

Expected: FAIL with residual near `-0.0219`.

- [ ] **Step 3: Solve the feedback input with bounded Newton iterations**

Replace the `u0` calculation with:

```cpp
const double stateTerm = (1.0 - G) * S;
double u0 = (x * drive - k * stateTerm) / (1.0 + k * G4);
if (saturate)
{
    for (int i = 0; i < 8; ++i)
    {
        const double arg = x * drive - k * (G4 * u0 + stateTerm);
        const double t = std::tanh (arg);
        const double f = u0 - t;
        const double df = 1.0 + k * G4 * (1.0 - t * t);
        const double next = u0 - f / df;
        if (! std::isfinite (next)) break;
        u0 = next;
        if (std::abs (f) < 1.0e-12) break;
    }
}
```

- [ ] **Step 4: Add a bounded sweep**

Add this sweep after the residual test:

```cpp
for (double cutoff : { 20.0, 1000.0, 12000.0, 23000.0 })
{
    for (double resonance : { 0.0, 2.0, 4.0 })
    {
        eon::Ladder4 ladder;
        ladder.setCutoff (cutoff, 48000.0);
        ladder.setResonance (resonance);
        ladder.reset();
        for (int i = -400; i <= 400; ++i)
        {
            const double output = ladder.process ((double) i / 100.0);
            eon::test::check (std::isfinite (output) && std::abs (output) < 20.0,
                              "saturated ladder remains finite and bounded");
        }
    }
}
```

- [ ] **Step 5: Run and commit**

Run: `cmake --build build --target eon_dsp_tests && ctest --test-dir build --output-on-failure`

Expected: implicit residual and sweep tests pass.

```bash
git add Dsp/Zdf.h Tests/SolverWdfZdfTests.cpp
git commit -m "fix: solve saturated ladder feedback implicitly"
```

### Task 8: Document and run the complete core gate

**Files:**
- Modify: `NOTES.md`

- [ ] **Step 1: Document lifecycle contracts**

Add these points to `NOTES.md`:

```markdown
## Runtime contracts

- Call `DCBlocker::prepare(processingRateHz, cutoffHz)` before audio processing.
- Call `Oversampler::prepare(maxBlockSize)` before `up()`/`down()`; output must be invariant to host block partition.
- Each ADAA object belongs to exactly one logical processing stage and channel.
- `reset()` establishes a finite, zero-input operating state for triodes, WDF roots, filters, and oversamplers.

## Test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```
```

- [ ] **Step 2: Run normal, sanitizer, and optimized builds**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure

cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-san --parallel
ctest --test-dir build-san --output-on-failure

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
```

Expected: all three CTest runs pass; sanitizer output contains no diagnostics.

- [ ] **Step 3: Commit documentation**

```bash
git add NOTES.md
git commit -m "docs: record eon dsp runtime contracts"
```

## Completion criteria

- Clean configure/build/test succeeds from the canonical `eon_dsp` repository.
- ADAA2 repeated-node error is below `1e-6`.
- 2x/4x/8x round-trip output changes by less than `1e-6` across 16- and 256-sample block partitions.
- DC blocker cutoff is 18 Hz at every tested processing rate.
- Triode zero-input output meets the grounded/self-biased tolerances above with the normal two-iteration runtime setting.
- Lambert W and WDF diode tests remain finite through extreme input and reset.
- Saturated ladder residual is below `1e-9`.
- Debug, ASan/UBSan, and Release CTest runs pass.
