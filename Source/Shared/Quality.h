#pragma once
#include <juce_dsp/juce_dsp.h>

namespace eon {

// ---------------------------------------------------------------------------
// Quality modes — honest mapping (the original claimed 32x but ran 4x
// regardless of the parameter).
//
//   Eco 8x     -> factor 3, cheap saturation (ADAA soft clip, no NR)
//   Liquid 16x -> factor 4, Newton triode 2 iterations
//   GOD 32x    -> factor 5, Newton triode 2 iterations
//   GOD+ NR    -> factor 5, Newton triode 4 iterations
//
// All three oversamplers are prepared once; switching is a pointer selection.
// Latency differs between modes — call latency() and push it to the host
// whenever mode changes.
// ---------------------------------------------------------------------------

enum class QualityMode { Eco = 0, Liquid = 1, God = 2, GodPlus = 3 };

struct OversamplingManager
{
    using OS = juce::dsp::Oversampling<float>;
    static constexpr int numRates = 3;               // 8x, 16x, 32x
    std::unique_ptr<OS> os[numRates];
    QualityMode mode = QualityMode::God;

    void prepare (size_t maxBlockSize)
    {
        for (int f = 0; f < numRates; ++f)
        {
            os[f] = std::make_unique<OS> (2, f + 3,  // factor 3,4,5 -> 8x,16x,32x
                    OS::filterHalfBandPolyphaseIIR, true, true);
            os[f]->initProcessing (maxBlockSize);
            os[f]->reset();
        }
    }

    void reset()
    {
        for (auto& o : os) if (o) o->reset();
    }

    void setMode (QualityMode m) { mode = m; }

    OS& current() const { return *os[osIndex()]; }
    int osIndex() const { return mode == QualityMode::Eco ? 0
                              : mode == QualityMode::Liquid ? 1 : 2; }
    int oversamplingFactor() const { return 1 << (osIndex() + 3); }
    double rate (double sampleRate) const { return sampleRate * oversamplingFactor(); }
    float latency() const { return (float) current().getLatencyInSamples(); }

    int triodeIterations() const { return mode == QualityMode::GodPlus ? 4 : 2; }
    bool useTriode() const { return mode != QualityMode::Eco; }
};

} // namespace eon
