#include <JuceHeader.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>

#if defined (_MSC_VER)
#include <malloc.h>
#endif

namespace
{
thread_local bool countAllocations = false;
thread_local std::size_t allocationCount = 0;

void countAllocation()
{
    if (countAllocations)
        ++allocationCount;
}

void* allocate(std::size_t size) noexcept
{
    auto* result = std::malloc (size == 0 ? 1 : size);
    if (result != nullptr)
        countAllocation();
    return result;
}

void* allocateAligned(std::size_t size, std::size_t alignment) noexcept
{
    if (alignment < sizeof (void*))
        alignment = sizeof (void*);

#if defined (_MSC_VER)
    auto* result = _aligned_malloc (size == 0 ? 1 : size, alignment);
    if (result != nullptr)
        countAllocation();
    return result;
#else
    void* result = nullptr;
    if (posix_memalign (&result, alignment, size == 0 ? 1 : size) != 0)
        return nullptr;

    countAllocation();
    return result;
#endif
}

void deallocateAligned (void* pointer) noexcept
{
#if defined (_MSC_VER)
    _aligned_free (pointer);
#else
    std::free (pointer);
#endif
}

void* allocateOrThrow(std::size_t size)
{
    if (auto* result = allocate (size))
        return result;
    throw std::bad_alloc();
}

void* allocateAlignedOrThrow(std::size_t size, std::size_t alignment)
{
    if (auto* result = allocateAligned (size, alignment))
        return result;
    throw std::bad_alloc();
}
} // namespace

void* operator new (std::size_t size) { return allocateOrThrow (size); }
void* operator new[] (std::size_t size) { return allocateOrThrow (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept { return allocate (size); }
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept { return allocate (size); }

void* operator new (std::size_t size, std::align_val_t alignment)
{
    return allocateAlignedOrThrow (size, static_cast<std::size_t> (alignment));
}

void* operator new[] (std::size_t size, std::align_val_t alignment)
{
    return allocateAlignedOrThrow (size, static_cast<std::size_t> (alignment));
}

void* operator new (std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return allocateAligned (size, static_cast<std::size_t> (alignment));
}

void* operator new[] (std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return allocateAligned (size, static_cast<std::size_t> (alignment));
}

void operator delete (void* pointer) noexcept { std::free (pointer); }
void operator delete[] (void* pointer) noexcept { std::free (pointer); }
void operator delete (void* pointer, std::size_t) noexcept { std::free (pointer); }
void operator delete[] (void* pointer, std::size_t) noexcept { std::free (pointer); }
void operator delete (void* pointer, const std::nothrow_t&) noexcept { std::free (pointer); }
void operator delete[] (void* pointer, const std::nothrow_t&) noexcept { std::free (pointer); }
void operator delete (void* pointer, std::align_val_t) noexcept { deallocateAligned (pointer); }
void operator delete[] (void* pointer, std::align_val_t) noexcept { deallocateAligned (pointer); }
void operator delete (void* pointer, std::size_t, std::align_val_t) noexcept { deallocateAligned (pointer); }
void operator delete[] (void* pointer, std::size_t, std::align_val_t) noexcept { deallocateAligned (pointer); }
void operator delete (void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { deallocateAligned (pointer); }
void operator delete[] (void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { deallocateAligned (pointer); }

#if EON_PROCESSOR_KIND == 1073
#include "Neve1073/PluginProcessor.h"
using Processor = Neve1073Processor;
constexpr const char* productName = "Neve1073";
#elif EON_PROCESSOR_KIND == 737
#include "Avalon737/PluginProcessor.h"
using Processor = Avalon737Processor;
constexpr const char* productName = "Avalon737";
#else
#error "EON_PROCESSOR_KIND must identify a supported processor"
#endif

struct ProcessorTestAccess
{
#if EON_PROCESSOR_KIND == 1073
    static bool allIIRFiltersAreSecondOrder (const Neve1073Processor& processor)
    {
        for (const auto& filter : processor.highPass)
            if (filter.coefficients->getFilterOrder() != 2)
                return false;
        for (const auto& filter : processor.lowShelf)
            if (filter.coefficients->getFilterOrder() != 2)
                return false;
        for (const auto& filter : processor.midPeak)
            if (filter.coefficients->getFilterOrder() != 2)
                return false;
        for (const auto& filter : processor.highShelf)
            if (filter.coefficients->getFilterOrder() != 2)
                return false;
        return true;
    }
#elif EON_PROCESSOR_KIND == 737
    static bool allIIRFiltersAreSecondOrder (const Avalon737Processor& processor)
    {
        for (const auto& filter : processor.highPass)
            if (filter.coefficients->getFilterOrder() != 2)
                return false;
        for (const auto& filter : processor.lowShelf)
            if (filter.coefficients->getFilterOrder() != 2)
                return false;
        for (const auto& filter : processor.lowMid)
            if (filter.coefficients->getFilterOrder() != 2)
                return false;
        for (const auto& filter : processor.highMid)
            if (filter.coefficients->getFilterOrder() != 2)
                return false;
        for (const auto& filter : processor.highShelf)
            if (filter.coefficients->getFilterOrder() != 2)
                return false;
        return true;
    }

    static double classACap1 (const Avalon737Processor& processor)
    {
        return processor.classA[0].cap1;
    }

    static double tanhState (const Avalon737Processor& processor, int stage, int channel)
    {
        switch (stage)
        {
            case 0: return processor.inputTanh[channel].adaa.x1;
            case 1: return processor.interstageTanh[channel].adaa.x1;
            default: return processor.airTanh[channel].adaa.x1;
        }
    }

    static void resetTanhStage (Avalon737Processor& processor, int stage, int channel)
    {
        switch (stage)
        {
            case 0: processor.inputTanh[channel].reset(); break;
            case 1: processor.interstageTanh[channel].reset(); break;
            default: processor.airTanh[channel].reset(); break;
        }
    }
#endif
};

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;
constexpr int renderSamples = 16384;
constexpr float twoPi = 6.2831853071795864769f;

struct Checks
{
    int failures = 0;

    void check (bool condition, const std::string& message)
    {
        if (! condition)
        {
            ++failures;
            std::cerr << productName << " FAIL: " << message << '\n';
        }
    }
};

void setParameter (Processor& processor, const char* parameterID, float value, Checks& checks)
{
    auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (
        processor.apvts.getParameter (parameterID));
    checks.check (parameter != nullptr, std::string ("missing parameter ") + parameterID);
    if (parameter != nullptr)
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

struct RenderedAudio
{
    std::vector<float> left;
    std::vector<float> right;
};

RenderedAudio renderRightOnly (Processor& processor, float lowGain, Checks& checks)
{
    setParameter (processor, "adaptive", 1.0f, checks);
    setParameter (processor, "analog", 0.0f, checks);
    setParameter (processor, "quality", 0.0f, checks);
    setParameter (processor, "lowGain", lowGain, checks);

    processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    RenderedAudio rendered;
    rendered.left.reserve (renderSamples);
    rendered.right.reserve (renderSamples);

    for (int offset = 0; offset < renderSamples; offset += blockSize)
    {
        buffer.clear();
        auto* right = buffer.getWritePointer (1);
        for (int i = 0; i < blockSize; ++i)
            right[i] = 0.1f * std::sin (twoPi * 60.0f * static_cast<float> (offset + i) / static_cast<float> (sampleRate));

        processor.processBlock (buffer, midi);
        for (int i = 0; i < blockSize; ++i)
        {
            rendered.left.push_back (buffer.getSample (0, i));
            rendered.right.push_back (buffer.getSample (1, i));
        }
    }

    processor.releaseResources();
    return rendered;
}

float rmsDifferenceAfterWarmup (const std::vector<float>& first,
                                const std::vector<float>& second,
                                std::size_t firstSample)
{
    const auto count = std::min (first.size(), second.size());
    if (count <= firstSample)
        return 0.0f;

    double sumSquares = 0.0;
    for (std::size_t i = firstSample; i < count; ++i)
    {
        const auto difference = static_cast<double> (first[i]) - static_cast<double> (second[i]);
        sumSquares += difference * difference;
    }
    return static_cast<float> (std::sqrt (sumSquares / static_cast<double> (count - firstSample)));
}

void testRightOnlyAdaptivePath (Checks& checks)
{
    Processor processor;
    const auto flat = renderRightOnly (processor, 0.0f, checks);
    const auto boosted = renderRightOnly (processor, 24.0f, checks);

    checks.check (flat.left.size() == renderSamples && flat.right.size() == renderSamples,
                  "flat render has unexpected length");
    checks.check (boosted.left.size() == renderSamples && boosted.right.size() == renderSamples,
                  "boosted render has unexpected length");

    float leftPeak = 0.0f;
    bool outputFinite = true;
    for (std::size_t i = 0; i < flat.left.size(); ++i)
    {
        leftPeak = std::max (leftPeak, std::abs (flat.left[i]));
        outputFinite = outputFinite
                    && std::isfinite (flat.left[i])
                    && std::isfinite (flat.right[i])
                    && std::isfinite (boosted.left[i])
                    && std::isfinite (boosted.right[i]);
    }

    for (const auto sample : boosted.left)
        leftPeak = std::max (leftPeak, std::abs (sample));

    checks.check (leftPeak <= 1.0e-7f, "right-only input produced non-silent left output");
    checks.check (outputFinite, "right-only output contains a non-finite sample");

    const auto difference = rmsDifferenceAfterWarmup (flat.right, boosted.right, 4096);
    std::cout << productName << " adaptive right-only RMS difference after 4096 samples: "
              << difference << '\n';
    checks.check (difference > 1.0e-4f,
                  "right-only adaptive processing did not respond to lowGain");
}

void fillStereoSine (juce::AudioBuffer<float>& buffer)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto value = 0.1f * std::sin (twoPi * 1000.0f * static_cast<float> (i) / static_cast<float> (sampleRate));
        buffer.setSample (0, i, value);
        buffer.setSample (1, i, value);
    }
}

void testPreparedFilterOrders (Checks& checks)
{
    Processor processor;
    setParameter (processor, "quality", 2.0f, checks);
    processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    checks.check (ProcessorTestAccess::allIIRFiltersAreSecondOrder (processor),
                  "prepareToPlay left callback-updated IIR filter at first-order storage");
}

#if EON_PROCESSOR_KIND == 737
void testSilentChannelStateProgress (Checks& checks)
{
    Processor processor;
    setParameter (processor, "adaptive", 0.0f, checks);
    setParameter (processor, "analog", 0.0f, checks);
    setParameter (processor, "quality", 0.0f, checks);

    processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    for (int block = 0; block < 8; ++block)
    {
        fillStereoSine (buffer);
        processor.processBlock (buffer, midi);
    }

    const auto stateBeforeSilentLeft = ProcessorTestAccess::classACap1 (processor);
    buffer.clear();
    auto* right = buffer.getWritePointer (1);
    for (int i = 0; i < blockSize; ++i)
        right[i] = 0.1f * std::sin (twoPi * 60.0f * static_cast<float> (i) / static_cast<float> (sampleRate));

    processor.processBlock (buffer, midi);

    const auto stateAfterSilentLeft = ProcessorTestAccess::classACap1 (processor);
    float leftPeak = 0.0f;
    for (int i = 0; i < blockSize; ++i)
        leftPeak = std::max (leftPeak, std::abs (buffer.getSample (0, i)));

    checks.check (std::abs (stateAfterSilentLeft - stateBeforeSilentLeft) > 1.0e-12,
                  "silent Avalon channel state did not progress while the other channel was active");
    checks.check (leftPeak <= 1.0e-7f,
                  "silent Avalon channel was not forced silent at the output");
}

void testTanhStagesHaveIndependentState (Checks& checks)
{
    Processor processor;
    setParameter (processor, "adaptive", 0.0f, checks);
    setParameter (processor, "analog", 0.0f, checks);
    setParameter (processor, "quality", 0.0f, checks);
    setParameter (processor, "highFreq", 3.0f, checks);
    setParameter (processor, "highGain", 12.0f, checks);

    processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    for (int stage = 0; stage < 3; ++stage)
        checks.check (std::abs (ProcessorTestAccess::tanhState (processor, stage, 0)) < 1.0e-15,
                      "prepareToPlay did not reset every Avalon TanhSat stage");

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    for (int block = 0; block < 8; ++block)
    {
        fillStereoSine (buffer);
        processor.processBlock (buffer, midi);
    }

    double before[3] {};
    for (int stage = 0; stage < 3; ++stage)
    {
        before[stage] = ProcessorTestAccess::tanhState (processor, stage, 0);
        checks.check (std::abs (before[stage]) > 1.0e-12,
                      "Avalon TanhSat stage did not advance during processing");
    }

    ProcessorTestAccess::resetTanhStage (processor, 0, 0);
    checks.check (std::abs (ProcessorTestAccess::tanhState (processor, 0, 0)) < 1.0e-15,
                  "input TanhSat reset did not clear its own state");
    checks.check (std::abs (ProcessorTestAccess::tanhState (processor, 1, 0) - before[1]) < 1.0e-15,
                  "input TanhSat reset changed interstage TanhSat state");
    checks.check (std::abs (ProcessorTestAccess::tanhState (processor, 2, 0) - before[2]) < 1.0e-15,
                  "input TanhSat reset changed air TanhSat state");

    processor.prepareToPlay (sampleRate, blockSize);
    for (int block = 0; block < 8; ++block)
    {
        fillStereoSine (buffer);
        processor.processBlock (buffer, midi);
    }

    for (int stage = 0; stage < 3; ++stage)
        before[stage] = ProcessorTestAccess::tanhState (processor, stage, 0);
    ProcessorTestAccess::resetTanhStage (processor, 1, 0);
    checks.check (std::abs (ProcessorTestAccess::tanhState (processor, 1, 0)) < 1.0e-15,
                  "interstage TanhSat reset did not clear its own state");
    checks.check (std::abs (ProcessorTestAccess::tanhState (processor, 0, 0) - before[0]) < 1.0e-15,
                  "interstage TanhSat reset changed input TanhSat state");
    checks.check (std::abs (ProcessorTestAccess::tanhState (processor, 2, 0) - before[2]) < 1.0e-15,
                  "interstage TanhSat reset changed air TanhSat state");

    processor.prepareToPlay (sampleRate, blockSize);
    for (int block = 0; block < 8; ++block)
    {
        fillStereoSine (buffer);
        processor.processBlock (buffer, midi);
    }

    for (int stage = 0; stage < 3; ++stage)
        before[stage] = ProcessorTestAccess::tanhState (processor, stage, 0);
    ProcessorTestAccess::resetTanhStage (processor, 2, 0);
    checks.check (std::abs (ProcessorTestAccess::tanhState (processor, 2, 0)) < 1.0e-15,
                  "air TanhSat reset did not clear its own state");
    checks.check (std::abs (ProcessorTestAccess::tanhState (processor, 0, 0) - before[0]) < 1.0e-15,
                  "air TanhSat reset changed input TanhSat state");
    checks.check (std::abs (ProcessorTestAccess::tanhState (processor, 1, 0) - before[1]) < 1.0e-15,
                  "air TanhSat reset changed interstage TanhSat state");
}
#endif

void testSteadyCallbackAllocations (Checks& checks)
{
    Processor processor;
    setParameter (processor, "adaptive", 0.0f, checks);
    setParameter (processor, "analog", 0.0f, checks);
    setParameter (processor, "quality", 2.0f, checks);

    processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    fillStereoSine (buffer);
    juce::MidiBuffer midi;

    // The first call applies the initial quality mode, so it must be covered
    // separately from the steady-state callback contract.
    allocationCount = 0;
    countAllocations = true;
    try
    {
        processor.processBlock (buffer, midi);
    }
    catch (...)
    {
        countAllocations = false;
        checks.check (false, "first processBlock threw an exception");
        return;
    }
    countAllocations = false;

    std::cout << productName << " first processBlock C++ heap allocations: "
              << allocationCount << '\n';
    checks.check (allocationCount == 0,
                  "first processBlock performed C++ heap allocation(s)");
    checks.check (ProcessorTestAccess::allIIRFiltersAreSecondOrder (processor),
                  "first processBlock changed callback-updated IIR filter to first-order storage");

    // Warm several unchanged callbacks before checking steady state.
    for (int i = 0; i < 4; ++i)
        processor.processBlock (buffer, midi);

    allocationCount = 0;
    countAllocations = true;
    try
    {
        processor.processBlock (buffer, midi);
    }
    catch (...)
    {
        countAllocations = false;
        checks.check (false, "steady processBlock threw an exception");
        return;
    }
    countAllocations = false;

    std::cout << productName << " steady processBlock C++ heap allocations: "
              << allocationCount << '\n';
    checks.check (allocationCount == 0,
                  "steady processBlock performed C++ heap allocation(s)");
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    Checks checks;

    std::cout << productName << " processor integration tests\n";
    testPreparedFilterOrders (checks);
    testRightOnlyAdaptivePath (checks);
#if EON_PROCESSOR_KIND == 737
    testSilentChannelStateProgress (checks);
    testTanhStagesHaveIndependentState (checks);
#endif
    testSteadyCallbackAllocations (checks);

    if (checks.failures != 0)
        std::cerr << productName << " failures: " << checks.failures << '\n';
    else
        std::cout << productName << " all checks passed\n";
    return checks.failures == 0 ? 0 : 1;
}
