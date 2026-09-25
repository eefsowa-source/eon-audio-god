// Plugin-level DSP quality harness for EON GOD.
//
// The smoke test covers the framework-free core. This harness drives the real
// AudioProcessor::processBlock() implementations, because the defects that
// matter most are integration defects:
//
//   1. Heap allocation inside the steady-state audio callback.
//   2. Adaptive CPU bypassing a signal that is only present on the right channel.
//   3. The drive smoother running at the oversampled rate while being prepared
//      for the host rate (a 50 ms ramp that finishes in ~1.6 ms).
//   4. Analog memory whose time constants scale with the oversampling factor,
//      so the DC blocker's bass response moves between quality modes.
//
// Exits non-zero on the first failing check, and prints every measurement.

#include <JuceHeader.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

#include "Neve1073/PluginProcessor.h"
#include "Avalon737/PluginProcessor.h"

// ---------------------------------------------------------------------------
// Global allocation counters.
//
// Replacing global operator new is the only portable way to observe heap use
// from inside processBlock without touching the product code.
// ---------------------------------------------------------------------------
namespace {
std::atomic<long long> g_allocations {0};
std::atomic<bool>      g_counting {false};
}

void* operator new (std::size_t size)
{
    if (g_counting.load (std::memory_order_relaxed))
        g_allocations.fetch_add (1, std::memory_order_relaxed);
    if (auto* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t size) { return ::operator new (size); }

void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace {

int failures = 0;

void check (bool condition, const char* message)
{
    if (condition)
        std::printf ("ok    %s\n", message);
    else
    {
        std::printf ("FAIL  %s\n", message);
        ++failures;
    }
}

void setParam (juce::AudioProcessorValueTreeState& apvts, const char* id, float value)
{
    if (auto* parameter = apvts.getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

// Neutral starting point: no EQ, no drive, no noise, adaptive off.
void neutralise (juce::AudioProcessorValueTreeState& apvts)
{
    setParam (apvts, "drive", 0.0f);
    setParam (apvts, "output", 0.0f);
    setParam (apvts, "analog", 0.0f);
    setParam (apvts, "adaptive", 0.0f);
}

struct Sine
{
    double phase = 0.0;
    double w = 0.0;
    float amp = 0.5f;

    void prepare (double freq, double sr) { w = 2.0 * juce::MathConstants<double>::pi * freq / sr; }
    float next() { const float v = amp * (float) std::sin (phase); phase += w; return v; }
};

struct RmsMeter
{
    double sumSq = 0.0;
    long long count = 0;
    void add (const float* d, int n) { for (int i = 0; i < n; ++i) { sumSq += (double) d[i] * d[i]; ++count; } }
    double rms() const { return count > 0 ? std::sqrt (sumSq / (double) count) : 0.0; }
};

struct Runner
{
    enum class Routing { Both, RightOnly };

    juce::AudioProcessor& proc;
    juce::AudioBuffer<float> buffer;
    Sine sine;

    Runner (juce::AudioProcessor& p, int blockSize) : proc (p), buffer (2, blockSize) {}

    // Feeds `blocks` blocks. RightOnly routes the signal to channel 1 and
    // leaves channel 0 silent.
    void run (int blocks, Routing routing, RmsMeter* meter = nullptr, int meterChannel = 0)
    {
        juce::MidiBuffer midi;
        for (int b = 0; b < blocks; ++b)
        {
            const int n = buffer.getNumSamples();
            for (int i = 0; i < n; ++i)
            {
                const float v = sine.next();
                const bool rightOnly = (routing == Routing::RightOnly);
                buffer.setSample (0, i, rightOnly ? 0.0f : v);
                buffer.setSample (1, i, rightOnly ? v : 0.0f);
            }
            proc.processBlock (buffer, midi);
            if (meter != nullptr)
                meter->add (buffer.getReadPointer (meterChannel), n);
        }
    }
};

// ---------------------------------------------------------------------------
// 1. Steady-state callback must not touch the heap.
// ---------------------------------------------------------------------------
template <typename Processor>
void testNoAllocation (const char* name)
{
    Processor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    neutralise (proc.apvts);
    setParam (proc.apvts, "midGain", 6.0f);

    Runner runner (proc, 512);
    runner.sine.prepare (1000.0, 48000.0);

    // Warm up: the first blocks build the coefficient storage and the
    // oversampler delay lines.
    runner.run (16, Runner::Routing::Both);

    g_allocations.store (0);
    g_counting.store (true);
    runner.run (24, Runner::Routing::Both);
    g_counting.store (false);

    const long long allocations = g_allocations.load();
    std::printf ("      %s steady-state allocations over 24 blocks: %lld\n", name, allocations);
    char message[256];
    std::snprintf (message, sizeof (message), "%s: no heap allocation in steady-state processBlock", name);
    check (allocations == 0, message);
}

// ---------------------------------------------------------------------------
// 2. Adaptive CPU must not bypass a right-channel-only signal.
//
// Runs the *same* instance twice, re-preparing between runs. Component
// tolerances are randomised once per instance in the constructor, so both runs
// see identical tolerances and identical DSP state; the only difference is the
// adaptive switch. For a signal that is actually present, both runs must
// produce the same output. With adaptive CPU inspecting only channel 0, the
// right-only run treated the input as silence and skipped the whole chain.
// ---------------------------------------------------------------------------
template <typename Processor>
void testRightChannelActivity (const char* name)
{
    Processor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);

    Runner runner (proc, 512);
    runner.sine.prepare (1600.0, 48000.0);        // 1073/737 mid detent

    const auto measure = [&] (bool adaptiveOn)
    {
        proc.prepareToPlay (48000.0, 512);
        neutralise (proc.apvts);
        setParam (proc.apvts, "midGain", 12.0f);
        setParam (proc.apvts, "adaptive", adaptiveOn ? 1.0f : 0.0f);

        runner.sine.phase = 0.0;
        runner.run (60, Runner::Routing::RightOnly);   // settle
        RmsMeter meter;
        runner.run (40, Runner::Routing::RightOnly, &meter, 1);
        return meter.rms();
    };

    const double adaptiveOff = measure (false);
    const double adaptiveOn  = measure (true);

    const double inputRms = (double) runner.sine.amp / std::sqrt (2.0);
    const double relative = std::abs (adaptiveOn - adaptiveOff) / std::max (adaptiveOff, 1e-12);
    std::printf ("      %s right-only: adaptive on/off relative difference %.4f "
                 "(chain level change %.2f dB)\n",
                 name, relative,
                 20.0 * std::log10 (std::max (adaptiveOff, 1e-12) / inputRms));

    char message[256];
    std::snprintf (message, sizeof (message),
                   "%s: adaptive CPU still processes a right-channel-only signal", name);
    check (relative < 0.01, message);
}

// ---------------------------------------------------------------------------
// 3. The drive ramp must be a real ~50 ms ramp.
// ---------------------------------------------------------------------------
template <typename Processor>
void testDriveRampDuration (const char* name)
{
    Processor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    neutralise (proc.apvts);
    setParam (proc.apvts, "drive", 0.0f);

    Runner runner (proc, 512);
    runner.sine.prepare (1000.0, 48000.0);

    runner.run (30, Runner::Routing::Both);          // settle at drive 0

    setParam (proc.apvts, "drive", 10.0f);

    runner.run (1, Runner::Routing::Both);           // ~10.7 ms
    RmsMeter early;                                  // ~10.7 - 21.3 ms
    runner.run (1, Runner::Routing::Both, &early, 0);

    runner.run (18, Runner::Routing::Both);          // well past the ramp
    RmsMeter settled;
    runner.run (10, Runner::Routing::Both, &settled, 0);

    const double ratio = early.rms() / std::max (settled.rms(), 1e-12);
    std::printf ("      %s drive-ramp level at ~10-20 ms / settled: %.4f\n", name, ratio);

    char message[256];
    std::snprintf (message, sizeof (message),
                   "%s: drive ramp is still moving 10-20 ms after a parameter step", name);
    check (ratio < 0.97 && ratio > 0.0, message);
}

// ---------------------------------------------------------------------------
// 4. Bass response must not move between quality modes.
//
// Liquid (16x) and GOD (32x) share the same processing topology — only the
// oversampling factor differs — so any level difference at 20 Hz is a
// rate-dependent time constant. Eco is reported for information but excluded
// from the assertion: it substitutes a soft-clip stage for the triode, which is
// a deliberate voicing difference rather than a rate defect.
// ---------------------------------------------------------------------------
template <typename Processor>
void testQualityModeBassConsistency (const char* name)
{
    const auto bassGain = [] (int qualityIndex)
    {
        Processor proc;
        proc.setPlayConfigDetails (2, 2, 48000.0, 512);
        setParam (proc.apvts, "quality", (float) qualityIndex);
        proc.prepareToPlay (48000.0, 512);
        neutralise (proc.apvts);
        setParam (proc.apvts, "quality", (float) qualityIndex);

        Runner runner (proc, 512);
        runner.sine.prepare (20.0, 48000.0);
        runner.run (200, Runner::Routing::Both);     // settle the DC blocker and the tails
        RmsMeter meter;
        runner.run (200, Runner::Routing::Both, &meter, 0);
        return meter.rms();
    };

    const double eco = bassGain (0);    // 8x
    const double liq = bassGain (1);    // 16x
    const double god = bassGain (2);    // 32x
    const double ratioDb = 20.0 * std::log10 (std::max (god, 1e-12) / std::max (liq, 1e-12));
    const double ecoDb   = 20.0 * std::log10 (std::max (god, 1e-12) / std::max (eco, 1e-12));
    std::printf ("      %s 20 Hz level: GOD/Liquid %.2f dB, GOD/Eco %.2f dB (Eco voicing differs)\n",
                 name, ratioDb, ecoDb);

    char message[256];
    std::snprintf (message, sizeof (message),
                   "%s: 20 Hz response stays within 1.5 dB between Liquid and GOD", name);
    check (std::abs (ratioDb) < 1.5, message);
}

// ---------------------------------------------------------------------------
// 5. Extreme settings stay finite, and the reported latency/tail are sane.
// ---------------------------------------------------------------------------
template <typename Processor>
void testRobustness (const char* name)
{
    Processor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    for (auto* id : { "lowGain", "midGain", "highGain" })
        setParam (proc.apvts, id, 24.0f);
    setParam (proc.apvts, "drive", 10.0f);
    setParam (proc.apvts, "output", 18.0f);
    setParam (proc.apvts, "adaptive", 0.0f);

    Runner runner (proc, 512);
    runner.sine.prepare (1000.0, 48000.0);
    runner.sine.amp = 1.0f;

    juce::MidiBuffer midi;
    bool finite = true;
    for (int b = 0; b < 40; ++b)
    {
        runner.run (1, Runner::Routing::Both);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < runner.buffer.getNumSamples(); ++i)
                finite = finite && std::isfinite (runner.buffer.getSample (ch, i));
    }

    char message[256];
    std::snprintf (message, sizeof (message), "%s: extreme settings stay finite", name);
    check (finite, message);

    std::snprintf (message, sizeof (message), "%s: reports a non-zero latency and tail", name);
    check (proc.getLatencySamples() > 0 && proc.getTailLengthSeconds() > 0.0, message);
}

// ---------------------------------------------------------------------------
// 6. Switching quality modes mid-stream must not emit NaN/Inf.
// ---------------------------------------------------------------------------
template <typename Processor>
void testModeSwitch (const char* name)
{
    Processor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    neutralise (proc.apvts);

    Runner runner (proc, 512);
    runner.sine.prepare (1000.0, 48000.0);
    runner.run (10, Runner::Routing::Both);

    bool finite = true;
    for (int q = 0; q < 4; ++q)
    {
        setParam (proc.apvts, "quality", (float) q);
        runner.run (6, Runner::Routing::Both);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < runner.buffer.getNumSamples(); ++i)
                finite = finite && std::isfinite (runner.buffer.getSample (ch, i));
    }

    char message[256];
    std::snprintf (message, sizeof (message), "%s: quality-mode switching stays finite", name);
    check (finite, message);
}

} // namespace

int main()
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf ("EON GOD DSP quality harness\n");

    std::printf ("-- Neve1073 --\n");
    testNoAllocation<Neve1073Processor> ("Neve1073");
    testRightChannelActivity<Neve1073Processor> ("Neve1073");
    testDriveRampDuration<Neve1073Processor> ("Neve1073");
    testQualityModeBassConsistency<Neve1073Processor> ("Neve1073");
    testRobustness<Neve1073Processor> ("Neve1073");
    testModeSwitch<Neve1073Processor> ("Neve1073");

    std::printf ("-- Avalon737 --\n");
    testNoAllocation<Avalon737Processor> ("Avalon737");
    testRightChannelActivity<Avalon737Processor> ("Avalon737");
    testDriveRampDuration<Avalon737Processor> ("Avalon737");
    testQualityModeBassConsistency<Avalon737Processor> ("Avalon737");
    testRobustness<Avalon737Processor> ("Avalon737");
    testModeSwitch<Avalon737Processor> ("Avalon737");

    if (failures == 0)
    {
        std::printf ("all quality checks passed\n");
        return 0;
    }

    std::printf ("%d quality check(s) failed\n", failures);
    return 1;
}
