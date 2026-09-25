// Offline aliasing measurement for the EON 1073 nonlinear chain.
//
//   alias_dump [outdir]        (default: ./measurements)
//
// Feeds a 15 kHz sine at 48 kHz base rate through the same nonlinear
// interstage chain as eq_response_dump's THD path, through the real
// up->process->down loop at each quality-mode oversampling factor, for
// both the JUCE half-band oversampler and eon::Oversampler.
//
// At 15 kHz every harmonic exceeds base Nyquist (24 k), so ALL in-band
// distortion energy measured after down-sampling is aliased foldback —
// exactly what oversampling exists to suppress. Alias bins (k*15k mod 48k,
// folded): h2->18k, h3->3k, h4->12k, h5->21k, h6->6k, h7/h9->9k.
//
//   alias.csv — one row per (engine, factor, drive):
//               fundamental magnitude, alias floor in dB relative to
//               fundamental, per-bin magnitudes, and a 5 kHz sanity bin
//               (no k*15k product lands there; should sit near the floor).
#include <juce_dsp/juce_dsp.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <sys/stat.h>
#include "Dsp/Adaa.h"
#include "Dsp/Triode.h"
#include "Dsp/Transformer.h"
#include "Dsp/Stages.h"
#include "Dsp/Oversampling.h"

static constexpr double baseRate = 48000.0;
static constexpr double f0 = 15000.0;
static constexpr double inAmp = 0.8;
static constexpr int settle = 16384, N = 65536;
static constexpr int maxN = settle + N;

// Nonlinear chain, identical staging to dumpThd() at drive = param.
struct Chain
{
    eon::JilesAtherton inTrans, outTrans;
    eon::InductorResonator ind;
    eon::TriodeStage triode;
    eon::TanhSat tanh;
    eon::DCBlocker dc;
    float drive = 0.f;

    void prepare (float d)
    {
        drive = d;
        inTrans.drive = 0.05f; outTrans.drive = 1.4f;
        triode.iterations = 2;
        inTrans.reset(); outTrans.reset(); ind.reset();
        triode.reset(); tanh.reset(); dc.reset();
    }

    inline float process (float x)
    {
        x *= 1.f + drive * 0.50f;
        x = inTrans.process (x);
        x = tanh.process (x * 0.82f) * 1.05f;
        x = ind.process (x, 1600.f, 1.6f, 0.5f + drive * 0.10f);
        x = triode.process (x * 0.15f) * 0.9f + x * 0.35f;
        x = outTrans.process (x * 0.88f);
        return dc.process (x);
    }
};

// Goertzel magnitude of one frequency in a finished buffer.
static double binMag (const float* d, int n, double targetHz, double rate)
{
    const double w = 2.0 * M_PI * targetHz / rate;
    double s0 = 0, s1 = 0, s2 = 0;
    const double cw = 2.0 * std::cos (w);
    for (int i = 0; i < n; ++i)
    {
        s0 = d[i] + cw * s1 - s2;
        s2 = s1; s1 = s0;
    }
    return 2.0 / n * std::sqrt (s1 * s1 + s2 * s2 - cw * s1 * s2);
}

struct Row
{
    std::string engine;
    int factor;
    float drive;
    double fund, aliasDb;
    double bins[6];     // 3,6,9,12,18,21 kHz
    double sanity;      // 5 kHz
};

static Row measure (const char* engine, int factor, float drive,
                    const std::vector<float>& baseIn)
{
    Chain chain; chain.prepare (drive);
    std::vector<float> out ((size_t) maxN);

    if (factor == 1)
    {
        for (int i = 0; i < maxN; ++i)
            out[(size_t) i] = chain.process (baseIn[(size_t) i]);
    }
    else if (std::string (engine) == "juce")
    {
        using OS = juce::dsp::Oversampling<float>;
        const int stages = (int) std::log2 (factor);
        OS os (1, stages, OS::filterHalfBandPolyphaseIIR, true, true);
        os.initProcessing ((size_t) maxN);
        os.reset();

        std::vector<float> tmp = baseIn;
        float* ptr = tmp.data();
        juce::dsp::AudioBlock<float> inBlock (&ptr, 1, (size_t) maxN);
        auto upBlock = os.processSamplesUp (inBlock);
        float* up = upBlock.getChannelPointer (0);
        const int upN = (int) upBlock.getNumSamples();
        for (int i = 0; i < upN; ++i) up[i] = chain.process (up[i]);
        // NB: JUCE wants the *base-rate* block here — the oversampled data
        // stays in the stages' internal buffers and is written back into it.
        os.processSamplesDown (inBlock);
        std::copy (tmp.begin(), tmp.end(), out.begin());
    }
    else // eon
    {
        eon::Oversampler os;
        os.setStages ((int) std::log2 (factor));
        os.prepare (maxN); os.reset();
        std::vector<float> up ((size_t) maxN * (size_t) factor);
        os.up (baseIn.data(), maxN, up.data());
        for (int i = 0; i < maxN * factor; ++i)
            up[(size_t) i] = chain.process (up[(size_t) i]);
        os.down (up.data(), out.data(), maxN);
    }

    static const double aliasBins[6] { 3000, 6000, 9000, 12000, 18000, 21000 };
    Row r; r.engine = engine; r.factor = factor; r.drive = drive;
    r.fund = binMag (out.data() + settle, N, f0, baseRate);
    double p2 = 0;
    for (int k = 0; k < 6; ++k)
    {
        r.bins[k] = binMag (out.data() + settle, N, aliasBins[k], baseRate);
        p2 += r.bins[k] * r.bins[k];
    }
    r.sanity  = binMag (out.data() + settle, N, 5000.0, baseRate);
    r.aliasDb = r.fund > 0 ? 20.0 * std::log10 (std::sqrt (p2) / r.fund) : 0.0;
    return r;
}

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "measurements";
    mkdir (dir.c_str(), 0755);

    std::vector<float> baseIn ((size_t) maxN);
    for (int i = 0; i < maxN; ++i)
        baseIn[(size_t) i] = (float) (inAmp * std::sin (2.0 * M_PI * f0 * i / baseRate));

    FILE* fp = std::fopen ((dir + "/alias.csv").c_str(), "w");
    std::fprintf (fp, "engine,factor,drive,fund_mag,alias_db,"
                      "b3k,b6k,b9k,b12k,b18k,b21k,sanity5k\n");

    const float drives[] { 0.f, 10.f };
    for (float drive : drives)
    {
        const Row r = measure ("none", 1, drive, baseIn);
        std::fprintf (fp, "none,1,%.1f,%.6f,%.2f,%.3e,%.3e,%.3e,%.3e,%.3e,%.3e,%.3e\n",
                      drive, r.fund, r.aliasDb,
                      r.bins[0], r.bins[1], r.bins[2], r.bins[3], r.bins[4], r.bins[5], r.sanity);
        for (const char* engine : { "juce", "eon" })
        {
            // eon::Oversampler supports 2x/4x/8x only — setStages clamps to
            // 1..3, so asking it for 16x or 32x silently returns the 8x
            // measurement. Emit the factors each engine can actually reach
            // instead of three identical rows.
            const std::vector<int> factors = std::string (engine) == "juce"
                ? std::vector<int> { 8, 16, 32 }
                : std::vector<int> { 2, 4, 8 };

            for (int factor : factors)
            {
                const Row m = measure (engine, factor, drive, baseIn);
                std::fprintf (fp, "%s,%d,%.1f,%.6f,%.2f,%.3e,%.3e,%.3e,%.3e,%.3e,%.3e,%.3e\n",
                              engine, factor, drive, m.fund, m.aliasDb,
                              m.bins[0], m.bins[1], m.bins[2], m.bins[3],
                              m.bins[4], m.bins[5], m.sanity);
                std::fprintf (stderr, "%s %dx drive %.0f: alias %.1f dB\n",
                              engine, factor, drive, m.aliasDb);
            }
        }
    }
    std::fclose (fp);
    std::printf ("wrote %s/alias.csv\n", dir.c_str());
    return 0;
}
