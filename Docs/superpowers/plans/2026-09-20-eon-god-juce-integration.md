# EON GOD JUCE Integration Repair Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Integrate the corrected `eon_dsp` core reproducibly into EON_GOD and remove verified stereo, state-sharing, smoothing, and real-time allocation faults from the Neve1073 and Avalon737 JUCE processors.

**Architecture:** Keep the complete DSP snapshot in `EON_GOD/eon_dsp` because the canonical local repository currently has no fetchable remote. Record its source revision and add a byte-for-byte sync check. Product tests link the existing shared-code targets and exercise actual `processBlock()` behavior; build/test, plugin validation, host loading, and listening remain separate gates.

**Tech Stack:** JUCE 8.0.9, C++20, CMake/CTest, VST3/AU/Standalone, pluginval, auval, REAPER/Ableton host smoke.

---

## Prerequisite

Complete `2026-09-20-eon-dsp-core-correctness.md` in the canonical `eon_dsp` repository first. Task 1 reads its final commit with `git -C ../eon_dsp rev-parse HEAD` and writes that exact value into `eon_dsp/REVISION`.

The current EON_GOD checkout already contains overlapping WIP in `CMakeLists.txt`, six untracked DSP headers, `Docs/alias_dump.cpp`, and `measurements/alias.csv`. Before executing Task 1, use the worktree workflow and preserve that WIP as a reviewed baseline commit on an execution branch; do not run the sync script over an unrecorded dirty checkout.

## File map

- Modify `CMakeLists.txt`: validate the complete vendored snapshot, export an `eon::dsp` target, and add processor regression targets.
- Track `eon_dsp/Dsp/*.h`: include all eleven canonical headers.
- Create `eon_dsp/REVISION`: records the exact canonical core commit.
- Create `scripts/sync_eon_dsp.sh`: deterministic local snapshot refresh and verification.
- Create `Tests/ProcessorTest.cpp`: shared processor behavior and real-time allocation tests, compiled once per product.
- Modify `Source/Neve1073/PluginProcessor.h/.cpp`: preallocate scratch, remove callback allocations, fix stereo activity and smoothing/DC preparation.
- Modify `Source/Avalon737/PluginProcessor.h/.cpp`: same real-time fixes plus one ADAA state per logical stage.
- Modify `README.md`: document core synchronization and the validation ladder.

### Task 1: Make the vendored core reproducible

**Files:**
- Modify: `CMakeLists.txt:10`
- Create: `eon_dsp/REVISION`
- Create: `scripts/sync_eon_dsp.sh`
- Track: `eon_dsp/Dsp/Measure.h`
- Track: `eon_dsp/Dsp/Oversampling.h`
- Track: `eon_dsp/Dsp/RtGuard.h`
- Track: `eon_dsp/Dsp/Solvers.h`
- Track: `eon_dsp/Dsp/Wdf.h`
- Track: `eon_dsp/Dsp/Zdf.h`
- Update: all other `eon_dsp/Dsp/*.h` from the corrected canonical core

- [ ] **Step 1: Add a deterministic snapshot sync script**

Create `scripts/sync_eon_dsp.sh`:

```bash
#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_dir=${1:-"$project_dir/../eon_dsp"}
dest_dir="$project_dir/eon_dsp"

test -d "$source_dir/.git"
test -f "$source_dir/Dsp/Adaa.h"
mkdir -p "$dest_dir/Dsp"

for header in Adaa Measure Oversampling Rng RtGuard Solvers Stages Transformer Triode Wdf Zdf
do
    cp "$source_dir/Dsp/$header.h" "$dest_dir/Dsp/$header.h"
done

git -C "$source_dir" rev-parse HEAD > "$dest_dir/REVISION"
printf 'Synced eon_dsp at %s\n' "$(cat "$dest_dir/REVISION")"
```

Run: `chmod +x scripts/sync_eon_dsp.sh`

- [ ] **Step 2: Sync from the corrected canonical checkout**

Run:

```bash
./scripts/sync_eon_dsp.sh '../eon_dsp'
test "$(cat eon_dsp/REVISION)" = "$(git -C ../eon_dsp rev-parse HEAD)"
```

Expected: the script prints the canonical repository's current commit and the equality check exits zero.

- [ ] **Step 3: Add a CMake completeness check and INTERFACE target**

Replace the raw include-path setup near the top of `CMakeLists.txt` with:

```cmake
set(EON_DSP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/eon_dsp"
    CACHE PATH "Path to the eon_dsp header library")

set(EON_DSP_REQUIRED_HEADERS
    Adaa.h Measure.h Oversampling.h Rng.h RtGuard.h Solvers.h
    Stages.h Transformer.h Triode.h Wdf.h Zdf.h)
foreach(header IN LISTS EON_DSP_REQUIRED_HEADERS)
    if(NOT EXISTS "${EON_DSP_DIR}/Dsp/${header}")
        message(FATAL_ERROR "Incomplete eon_dsp snapshot: missing Dsp/${header}")
    endif()
endforeach()

add_library(eon_dsp_headers INTERFACE)
add_library(eon::dsp ALIAS eon_dsp_headers)
target_include_directories(eon_dsp_headers INTERFACE "${EON_DSP_DIR}")
target_compile_features(eon_dsp_headers INTERFACE cxx_std_20)
```

Add a plugin build switch and place JUCE plus both product target declarations behind it:

```cmake
option(EON_BUILD_PLUGINS "Build the JUCE plug-in targets" ON)

if(EON_BUILD_PLUGINS)
    add_subdirectory(JUCE)
endif()
```

Define `add_eon_god` and call it for Neve/Avalon only inside `if(EON_BUILD_PLUGINS)`. In that function, remove `${EON_DSP_DIR}` from `target_include_directories` and add `eon::dsp` to `target_link_libraries`. Link `dsp_smoke_test` to `eon::dsp` instead of giving it a raw include path. Create `eq_response_dump`, `alias_dump`, and the processor tests only when `EON_BUILD_PLUGINS` is ON because they link JUCE.

- [ ] **Step 4: Prove the tracked snapshot can configure from a clean archive**

After all current changes are committed at the end of this task, run:

```bash
archive_dir=$(mktemp -d /tmp/eon-god-archive.XXXXXX)
git archive HEAD | tar -x -C "$archive_dir"
cmake -S "$archive_dir" -B "$archive_dir/build" \
  -DEON_BUILD_TESTS=ON -DEON_BUILD_PLUGINS=OFF
cmake --build "$archive_dir/build" --target dsp_smoke_test --parallel
ctest --test-dir "$archive_dir/build" --output-on-failure
```

Expected: configure, compile, and `dsp_smoke` pass without referencing the sibling checkout.

- [ ] **Step 5: Commit the complete snapshot**

```bash
git add CMakeLists.txt scripts/sync_eon_dsp.sh eon_dsp
git commit -m "build: make eon dsp snapshot reproducible"
```

### Task 2: Add actual processor regression targets

**Files:**
- Create: `Tests/ProcessorTest.cpp`
- Modify: `CMakeLists.txt:76`

- [ ] **Step 1: Add one test executable per processor**

Inside `if(EON_BUILD_TESTS)`, add:

```cmake
function(add_processor_regression_test test_name plugin_target processor_kind)
    add_executable(${test_name} Tests/ProcessorTest.cpp)
    target_include_directories(${test_name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Source"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/Shared"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/${plugin_target}"
        "${CMAKE_CURRENT_BINARY_DIR}/${plugin_target}_artefacts/JuceLibraryCode")
    target_link_libraries(${test_name} PRIVATE
        ${plugin_target}
        eon::dsp
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_audio_processors)
    target_compile_definitions(${test_name} PRIVATE
        EON_PROCESSOR_KIND=${processor_kind}
        EON_PROCESSOR_TESTING=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

add_processor_regression_test(neve_processor_tests Neve1073 1073)
add_processor_regression_test(avalon_processor_tests Avalon737 737)
```

- [ ] **Step 2: Create a shared processor test harness**

Create `Tests/ProcessorTest.cpp` with product selection, parameter setting, stereo render helpers, and an allocation counter:

```cpp
#include <JuceHeader.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

#if EON_PROCESSOR_KIND == 1073
#include "Neve1073/PluginProcessor.h"
using Processor = Neve1073Processor;
#else
#include "Avalon737/PluginProcessor.h"
using Processor = Avalon737Processor;
#endif

static thread_local bool countAllocations = false;
static thread_local size_t allocationCount = 0;

void* operator new (std::size_t size)
{
    if (countAllocations) ++allocationCount;
    if (void* memory = std::malloc (size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size) { return ::operator new (size); }
void operator delete (void* memory) noexcept { std::free (memory); }
void operator delete (void* memory, std::size_t) noexcept { std::free (memory); }
void operator delete[] (void* memory) noexcept { std::free (memory); }
void operator delete[] (void* memory, std::size_t) noexcept { std::free (memory); }

static int failures = 0;
static void check (bool condition, const char* message)
{
    std::printf ("%s  %s\n", condition ? "ok  " : "FAIL", message);
    if (! condition) ++failures;
}

static void setParameter (Processor& processor, const char* id, float plainValue)
{
    auto* parameter = processor.apvts.getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
}
```

- [ ] **Step 3: Add the right-only Adaptive CPU regression**

Add this render helper, which creates 16,384 samples in 256-sample blocks, keeps channel 0 silent, writes a 60 Hz sine to channel 1, and returns channel 1 output:

```cpp
static std::vector<float> renderRightOnly (float lowGain)
{
    Processor processor;
    setParameter (processor, "adaptive", 1.0f);
    setParameter (processor, "analog", 0.0f);
    setParameter (processor, "quality", 0.0f);
    setParameter (processor, "lowGain", lowGain);
    processor.setRateAndBufferSizeDetails (48000.0, 256);
    processor.prepareToPlay (48000.0, 256);

    juce::AudioBuffer<float> audio (2, 256);
    juce::MidiBuffer midi;
    std::vector<float> output (16384);
    for (int offset = 0; offset < 16384; offset += 256)
    {
        audio.clear();
        for (int i = 0; i < 256; ++i)
            audio.setSample (1, i, 0.1f * std::sin (2.0 * juce::MathConstants<double>::pi
                                                   * 60.0 * (offset + i) / 48000.0));
        processor.processBlock (audio, midi);
        std::copy_n (audio.getReadPointer (1), 256, output.data() + offset);
    }
    return output;
}
```

In `main()`, compare `lowGain=0` and `lowGain=24`:

```cpp
const auto flat = renderRightOnly (0.0f);
const auto boosted = renderRightOnly (24.0f);
double differencePower = 0.0;
for (size_t i = 4096; i < flat.size(); ++i)
{
    const double delta = flat[i] - boosted[i];
    differencePower += delta * delta;
}
const double differenceRms = std::sqrt (differencePower / (flat.size() - 4096));
check (differenceRms > 1.0e-4,
       "right-only audio remains processed when Adaptive CPU is enabled");
```

Place this check in `main()` after `juce::ScopedJuceInitialiser_GUI juce;` and return `failures == 0 ? 0 : 1` after all tests.

- [ ] **Step 4: Add the steady-state allocation regression**

Warm the processor for four blocks at 48 kHz, 256 samples, `quality=2`, unchanged parameters. Count only the fifth call:

```cpp
Processor processor;
setParameter (processor, "adaptive", 0.0f);
setParameter (processor, "analog", 0.0f);
setParameter (processor, "quality", 2.0f);
processor.setRateAndBufferSizeDetails (48000.0, 256);
processor.prepareToPlay (48000.0, 256);
juce::AudioBuffer<float> audio (2, 256);
juce::MidiBuffer midi;
for (int block = 0; block < 5; ++block)
{
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 256; ++i)
            audio.setSample (channel, i,
                             0.05f * std::sin (2.0 * juce::MathConstants<double>::pi
                                               * 1000.0 * (block * 256 + i) / 48000.0));
    if (block < 4) processor.processBlock (audio, midi);
}
allocationCount = 0;
countAllocations = true;
processor.processBlock (audio, midi);
countAllocations = false;
check (allocationCount == 0, "steady processBlock performs zero C++ heap allocations");
```

The test owns only C++ `new`; it does not claim to observe every platform allocator.

- [ ] **Step 5: Verify the regressions fail before product changes**

Run:

```bash
cmake -S . -B build-fix -DEON_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-fix --target neve_processor_tests avalon_processor_tests --parallel
ctest --test-dir build-fix -R processor_tests --output-on-failure
```

Expected: right-only tests show zero EQ difference; allocation checks report approximately 5 allocations for Neve and 6 for Avalon.

- [ ] **Step 6: Commit the failing tests**

```bash
git add CMakeLists.txt Tests/ProcessorTest.cpp
git commit -m "test: cover processor stereo and realtime contracts"
```

### Task 3: Remove callback allocations and fix stereo activity detection

**Files:**
- Modify: `Source/Neve1073/PluginProcessor.h:45`
- Modify: `Source/Neve1073/PluginProcessor.cpp:70`
- Modify: `Source/Avalon737/PluginProcessor.h:44`
- Modify: `Source/Avalon737/PluginProcessor.cpp:65`

- [ ] **Step 1: Preallocate the drive ramp scratch buffers**

Add to both processor headers:

```cpp
std::vector<float> driveScratch;
```

In both `prepareToPlay` methods, after selecting the quality mode, allocate the maximum 32x size:

```cpp
driveScratch.assign ((size_t) sb * 32u, 0.0f);
```

Replace each callback-local vector with:

```cpp
jassert ((size_t) N <= driveScratch.size());
for (int i = 0; i < N; ++i)
    driveScratch[(size_t) i] = driveSm.getNextValue();
```

Replace `drives[i]` reads with `driveScratch[(size_t) i]`.

- [ ] **Step 2: Update IIR coefficient objects without allocating**

Add to both `.cpp` files near the top:

```cpp
using ArrayCoeff = juce::dsp::IIR::ArrayCoefficients<float>;
```

Replace every `Coeff::make...` pointer factory with the corresponding `ArrayCoeff::make...` value. Assign values into the already-owned coefficient object:

```cpp
const auto lc = ArrayCoeff::makeLowShelf (osRate, lowF[lfIdx], 0.707f,
                                          juce::Decibels::decibelsToGain (lg));
for (auto& filter : lowShelf)
    *filter.coefficients = lc;
```

Apply the same pattern to peak, high-shelf, and high-pass filters. The first-order high-pass returns four coefficients and can be assigned through the same templated `operator=`.

- [ ] **Step 3: Detect input activity across every channel**

Replace the channel-0-only RMS read in both processors with:

```cpp
float blockRMS = 0.0f;
for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    blockRMS = std::max (blockRMS,
                         buffer.getRMSLevel (channel, 0, buffer.getNumSamples()));
silentEnv = silentEnv * 0.9 + blockRMS * 0.1;
const bool skipNonlinear = adaptiveOn && silentEnv < 1.0e-6;
```

This fixes the confirmed stereo loss. Do not expand the optimization in this task; tail-aware dormancy requires its own listening and render specification.

- [ ] **Step 4: Run the two processor tests**

Run:

```bash
cmake --build build-fix --target neve_processor_tests avalon_processor_tests --parallel
ctest --test-dir build-fix -R processor_tests --output-on-failure
```

Expected: right-only EQ difference exceeds `1e-4`; both steady callback allocation counts are zero.

- [ ] **Step 5: Commit**

```bash
git add Source/Neve1073/PluginProcessor.h Source/Neve1073/PluginProcessor.cpp \
        Source/Avalon737/PluginProcessor.h Source/Avalon737/PluginProcessor.cpp
git commit -m "fix: make processor callbacks stereo-safe and allocation-free"
```

### Task 4: Separate Avalon ADAA state by processing stage

**Files:**
- Modify: `Source/Avalon737/PluginProcessor.h:48`
- Modify: `Source/Avalon737/PluginProcessor.cpp:75`
- Modify: `Tests/ProcessorTest.cpp`

- [ ] **Step 1: Add an Avalon-only stage-isolation test hook**

Expose this helper in the public section of `Avalon737Processor` only under `EON_PROCESSOR_TESTING=1`; add that definition to the Avalon test target:

```cpp
#if EON_PROCESSOR_TESTING
std::array<float, 3> testTanhStageOutputs (float inputA, float inputB, float inputC)
{
    return { inputTanh[0].process (inputA),
             interstageTanh[0].process (inputB),
             airTanh[0].process (inputC) };
}
#endif
```

Add this Avalon-only test:

```cpp
#if EON_PROCESSOR_KIND == 737
Processor processor;
eon::TanhSat referenceA, referenceB, referenceC;
std::array<float, 3> actual {};
std::array<float, 3> expected {};
for (int i = 0; i < 32; ++i)
{
    actual = processor.testTanhStageOutputs (0.2f, 0.8f, -0.4f);
    expected = { referenceA.process (0.2f),
                 referenceB.process (0.8f),
                 referenceC.process (-0.4f) };
}
check (std::equal (actual.begin(), actual.end(), expected.begin()),
       "Avalon ADAA stages keep independent histories");
#endif
```

- [ ] **Step 2: Verify the test cannot pass with the shared array**

Run: `cmake --build build-fix --target avalon_processor_tests && ./build-fix/avalon_processor_tests`

Expected: compilation fails because the three state arrays do not exist, establishing the required interface.

- [ ] **Step 3: Replace the shared ADAA array**

In `PluginProcessor.h`, replace:

```cpp
eon::TanhSat tanh[2];
```

with:

```cpp
eon::TanhSat inputTanh[2];
eon::TanhSat interstageTanh[2];
eon::TanhSat airTanh[2];
```

Reset all three arrays in `prepareToPlay`. Change the three call sites as follows:

```cpp
x = inputTanh[ch].process (x * 0.46f) * 2.17f;
x = interstageTanh[ch].process (x * 0.09f) * 10.1f + x * 0.08f;
float melt = airTanh[ch].process (x * (0.78f + hg * 0.03f));
```

- [ ] **Step 4: Run and commit**

Run: `cmake --build build-fix --target avalon_processor_tests && ctest --test-dir build-fix -R avalon_processor_tests --output-on-failure`

Expected: independent-stage reference test passes.

```bash
git add Source/Avalon737/PluginProcessor.h Source/Avalon737/PluginProcessor.cpp \
        Tests/ProcessorTest.cpp CMakeLists.txt
git commit -m "fix: isolate Avalon ADAA stage state"
```

### Task 5: Keep smoothing and DC filtering invariant across quality modes

**Files:**
- Modify: `Source/Neve1073/PluginProcessor.cpp:70`
- Modify: `Source/Avalon737/PluginProcessor.cpp:65`
- Modify: `Tests/ProcessorTest.cpp`

- [ ] **Step 1: Add a 50 ms smoothing-duration regression**

Add these public test-only accessors to each processor under `EON_PROCESSOR_TESTING`:

```cpp
#if EON_PROCESSOR_TESTING
int testDriveRampSteps (int qualityIndex)
{
    osm.setMode ((eon::QualityMode) qualityIndex);
    driveSm.reset (osm.rate (48000.0), 0.05);
    driveSm.setCurrentAndTargetValue (0.0f);
    driveSm.setTargetValue (1.0f);
    int steps = 0;
    while (driveSm.isSmoothing())
    {
        driveSm.getNextValue();
        ++steps;
    }
    return steps;
}
int testOversamplingFactor() const { return osm.oversamplingFactor(); }
#endif
```

For quality indices 0, 1, and 2, assert:

```cpp
const double milliseconds = 1000.0 * steps
                          / (48000.0 * processor.testOversamplingFactor());
check (std::abs (milliseconds - 50.0) < 0.1,
       "drive smoothing lasts 50 ms at every quality mode");
```

- [ ] **Step 2: Verify the old setup fails**

Expected unfixed durations: 6.25 ms at 8x, 3.125 ms at 16x, and 1.5625 ms at 32x.

- [ ] **Step 3: Prepare oversampled state at the effective rate**

In both `prepareToPlay` methods use:

```cpp
const double processingRate = osm.rate (sr);
driveSm.reset (processingRate, 0.05);
for (auto& blocker : dc)
    blocker.prepare (processingRate, 18.0);
```

Keep `outGainSm.reset(sr, 0.02)` because output gain advances at the host rate.

- [ ] **Step 4: Preserve smoothing state when quality changes**

Inside the `qIdx != lastQuality` branch, after `osm.setMode`, add:

```cpp
const float currentDrive = driveSm.getCurrentValue();
const float targetDrive = apvts.getRawParameterValue ("drive")->load();
driveSm.reset (osm.rate (getSampleRate()), 0.05);
driveSm.setCurrentAndTargetValue (currentDrive);
driveSm.setTargetValue (targetDrive);
for (auto& blocker : dc)
    blocker.prepare (osm.rate (getSampleRate()), 18.0);
```

- [ ] **Step 5: Add a 20 Hz quality-invariance render test**

Add a render helper based on `renderRightOnly`, using both channels, `drive=0`, `analog=0`, all EQ gains at 0, a 20 Hz sine at amplitude 0.01, and 96,000 samples. Discard the first 48,000 samples, compute RMS for quality indices 0, 1, and 2, then assert:

```cpp
const auto dbDifference = [] (double a, double b)
{
    return std::abs (20.0 * std::log10 (a / b));
};
check (dbDifference (ecoRms, liquidRms) < 0.2
       && dbDifference (ecoRms, godRms) < 0.2
       && dbDifference (liquidRms, godRms) < 0.2,
       "20 Hz gain remains within 0.2 dB across quality modes");
```

- [ ] **Step 6: Run and commit**

Run:

```bash
cmake --build build-fix --target neve_processor_tests avalon_processor_tests --parallel
ctest --test-dir build-fix -R processor_tests --output-on-failure
```

Expected: smoothing duration and 20 Hz invariance checks pass for both processors.

```bash
git add Source/Neve1073/PluginProcessor.cpp Source/Avalon737/PluginProcessor.cpp \
        Tests/ProcessorTest.cpp CMakeLists.txt
git commit -m "fix: make processor timing quality-invariant"
```

### Task 6: Run product and plug-in validation gates

**Files:**
- Modify: `README.md`
- Update only if generated by the existing workflow: validation logs under the project evidence directory

- [ ] **Step 1: Run the complete unit and sanitizer gate**

Run:

```bash
cmake -S . -B build-fix -DEON_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-fix --target dsp_smoke_test neve_processor_tests avalon_processor_tests --parallel
ctest --test-dir build-fix --output-on-failure

cmake -S . -B build-san-fix -DEON_BUILD_TESTS=ON -DEON_ENABLE_SANITIZERS=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-san-fix --target dsp_smoke_test neve_processor_tests avalon_processor_tests --parallel
ctest --test-dir build-san-fix --output-on-failure
```

Expected: all CTest targets pass and sanitizer output is clean.

- [ ] **Step 2: Build final VST3/AU/Standalone artifacts without installing stale binaries**

Configure a dedicated release directory and build both product targets. Record SHA256 for every final bundle executable before validation:

```bash
cmake -S . -B build-release-fix -DEON_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-release-fix --target Neve1073_All Avalon737_All --parallel
find build-release-fix -type f -perm -111 -print0 | xargs -0 shasum -a 256
```

Expected: VST3, AU, and Standalone artifacts build for both products. Treat this only as build evidence.

- [ ] **Step 3: Run pluginval and auval separately**

Run the existing project validation script for VST3 and AU at pluginval strictness 10. Run the AU checks with the configured effect, subtype, and manufacturer codes:

```bash
auval -v aufx E73G EONA
auval -v aufx E37G EONA
```

Record binary SHA256, validator version, sample rate, and buffer size with each result.

Expected: both formats of both products pass pluginval; both AU components pass auval. A failure returns this plan to the responsible implementation task instead of being waived.

- [ ] **Step 4: Run host loading and automation/recall smoke**

Load the exact hashed binaries in REAPER and Ableton Live separately. At 48 kHz and 256 samples:

1. Verify mono and stereo insertion.
2. Feed right-only audio with Adaptive CPU enabled and confirm EQ changes affect the right output.
3. Automate Quality and Drive through all modes and confirm no dropout, crash, or latency desynchronization.
4. Save, close, reopen, and verify all parameters recall.

Record host/version/sample-rate/buffer/hash. Host loading does not count as listening approval.

- [ ] **Step 5: Perform focused level-matched listening**

Compare before/after renders for:

- 20–80 Hz bass material across Eco/Liquid/GOD;
- Avalon high-drive/Class-A material around the changed continuous limiter;
- automation through Drive and Quality;
- stereo, left-only, and right-only signals.

Level-match within 0.1 dB and record whether the changed Class-A curve and fixed 18 Hz DC cutoff are accepted. If not accepted, adjust only the associated voicing constant and rerun Tasks 5–6.

- [ ] **Step 6: Document the workflow and commit**

Add to `README.md`:

```markdown
## Shared DSP snapshot

`eon_dsp/` is a tracked snapshot of the canonical sibling repository. Refresh it with:

```bash
./scripts/sync_eon_dsp.sh ../eon_dsp
```

The snapshot revision is recorded in `eon_dsp/REVISION`. A clean archive must build and pass CTest before release.

## Validation order

1. CTest and sanitizer tests
2. pluginval strictness 10
3. auval
4. REAPER and Ableton host loading
5. automation and state recall
6. level-matched render/listening review

Each result records the exact binary SHA256, tool/host version, sample rate, and buffer size.
```

Commit:

```bash
git add README.md
git commit -m "docs: record dsp sync and validation workflow"
```

## Completion criteria

- A `git archive` of EON_GOD configures, builds `dsp_smoke_test`, and passes CTest without the sibling core.
- The vendored eleven headers match the commit printed by `git -C ../eon_dsp rev-parse HEAD`, and `eon_dsp/REVISION` contains that exact hash.
- Right-only audio remains processed with Adaptive CPU enabled in Neve and Avalon.
- Warm steady-state `processBlock()` performs zero observed C++ heap allocations.
- Avalon uses independent ADAA state for input, interstage, and 32 kHz air paths.
- Drive smoothing remains 50 ms at 8x, 16x, and 32x.
- The 20 Hz response differs by less than 0.2 dB across quality modes with nonlinear/analog paths disabled.
- Normal and sanitizer CTest gates pass.
- pluginval, auval, REAPER, Ableton, automation/recall, and listening evidence are recorded separately for the final hashed binaries.
