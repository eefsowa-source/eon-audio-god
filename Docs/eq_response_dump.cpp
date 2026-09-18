// Offline measurement dumper for EON 1073 GOD.
//
//   eq_response_dump [outdir]        (default: ./measurements)
//
// Dumps two CSVs:
//   eq_curves.csv — linear EQ chain magnitude response (HPF + low shelf +
//                   mid peak + high shelf) for every switch position and a
//                   gain sweep, computed exactly via Coefficients::
//                   getMagnitudeForFrequency at the GOD-mode rate (32x).
//   thd.csv       — simulated THD of the nonlinear interstage chain
//                   (JA in-transformer -> ADAA tanh -> inductor -> triode
//                   -> JA out-transformer -> DC blocker) vs Drive,
//                   at the GOD-mode 32x rate, 1 kHz sine input.
//
// The linear curve is coefficient-exact; the THD path mirrors
// Neve1073Processor::processBlock with all EQ gains at 0 and the
// (non-deterministic) air-noise stage omitted.
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

using Coeff = juce::dsp::IIR::Coefficients<float>;

static constexpr double baseRate = 48000.0;
static constexpr int osFactor = 32;                       // GOD mode
static const double osRate = baseRate * osFactor;

// Same tables as Neve1073Processor::processBlock
static constexpr float lowF[]  { 35.f, 60.f, 110.f, 220.f };
static constexpr float midF[]  { 360.f, 700.f, 1600.f, 3200.f, 4800.f, 7200.f };
static constexpr float highF[] { 10000.f, 12000.f, 16000.f };
static constexpr float hpfF[]  { 0.f, 50.f, 80.f, 160.f, 300.f };
static constexpr float gains[] { -24.f, -12.f, -6.f, 0.f, 6.f, 12.f, 24.f };
static constexpr float midQs[] { 0.5f, 1.6f, 3.0f, 5.0f };

static std::vector<double> freqGrid()
{
    std::vector<double> g;
    const double lo = std::log10 (10.0), hi = std::log10 (40000.0);
    for (int i = 0; i < 240; ++i)
        g.push_back (std::pow (10.0, lo + (hi - lo) * i / 239.0));
    return g;
}

static double chainDb (double f, const Coeff* hp, const Coeff* ls,
                       const Coeff* mp, const Coeff* hs)
{
    double m = hp->getMagnitudeForFrequency (f, osRate)
             * ls->getMagnitudeForFrequency (f, osRate)
             * mp->getMagnitudeForFrequency (f, osRate)
             * hs->getMagnitudeForFrequency (f, osRate);
    return juce::Decibels::gainToDecibels (m, -120.0);
}

static void dumpEq (const std::string& dir)
{
    const auto grid = freqGrid();
    const auto flat = Coeff::makePeakFilter (osRate, 1000.f, 0.707f, 1.0f); // 0 dB
    const auto noHp = Coeff::makeFirstOrderHighPass (osRate, 4.f);          // HPF "Off" path

    FILE* fp = std::fopen ((dir + "/eq_curves.csv").c_str(), "w");
    std::fprintf (fp, "band,setting_freq,setting_gain,setting_q,freq_hz,db\n");

    // HPF sweep (other bands flat)
    for (float hf : hpfF)
    {
        const auto hp = hf > 0.f ? Coeff::makeHighPass (osRate, hf)
                                 : Coeff::makeFirstOrderHighPass (osRate, 4.f);
        for (double f : grid)
            std::fprintf (fp, "hpf,%.0f,0,0,%.2f,%.4f\n", hf, f,
                          chainDb (f, hp.get(), flat.get(), flat.get(), flat.get()));
    }
    // Low shelf
    for (float lf : lowF) for (float g : gains)
    {
        const auto c = Coeff::makeLowShelf (osRate, lf, 0.707f,
                                            juce::Decibels::decibelsToGain (g));
        for (double f : grid)
            std::fprintf (fp, "low_shelf,%.0f,%.0f,0,%.2f,%.4f\n", lf, g, f,
                          chainDb (f, noHp.get(), c.get(), flat.get(), flat.get()));
    }
    // Mid peak — include the processBlock Q interaction: mq = q * (1 + |g|*0.07)
    for (float mf : midF) for (float q : midQs) for (float g : gains)
    {
        const float mq = q * (1.f + std::abs (g) * 0.07f);
        const auto c = Coeff::makePeakFilter (osRate, mf, mq,
                                              juce::Decibels::decibelsToGain (g));
        for (double f : grid)
            std::fprintf (fp, "mid_peak,%.0f,%.0f,%.1f,%.2f,%.4f\n", mf, g, q, f,
                          chainDb (f, noHp.get(), flat.get(), c.get(), flat.get()));
    }
    // High shelf
    for (float hf : highF) for (float g : gains)
    {
        const auto c = Coeff::makeHighShelf (osRate, hf, 0.707f,
                                             juce::Decibels::decibelsToGain (g));
        for (double f : grid)
            std::fprintf (fp, "high_shelf,%.0f,%.0f,0,%.2f,%.4f\n", hf, g, f,
                          chainDb (f, noHp.get(), flat.get(), flat.get(), c.get()));
    }
    std::fclose (fp);
}

// Goertzel magnitude of one harmonic in a finished buffer.
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

static void dumpThd (const std::string& dir)
{
    constexpr int settle = 16384, N = 65536;
    std::vector<float> buf ((size_t) N);
    FILE* fp = std::fopen ((dir + "/thd.csv").c_str(), "w");
    std::fprintf (fp, "drive,thd_pct,h2_pct,h3_pct,h4_pct,h5_pct\n");

    for (float drive = 0.f; drive <= 10.f; drive += 0.5f)
    {
        eon::JilesAtherton inTrans, outTrans;
        eon::InductorResonator ind;
        eon::TriodeStage triode;
        eon::TanhSat tanh;
        eon::DCBlocker dc;
        triode.iterations = 2;  // GOD mode
        inTrans.reset(); outTrans.reset(); ind.reset();
        triode.reset(); tanh.reset(); dc.reset();

        const float indDrive = 1.f + drive * 0.10f;
        const double w = 2.0 * M_PI * 1000.0 / osRate;

        for (int i = 0; i < settle + N; ++i)
        {
            float x = (float) (0.3 * std::sin (w * i));
            x *= 1.f + drive * 0.20f;
            x = inTrans.process (x);
            x = tanh.process (x * 0.82f) * 1.05f;
            x = ind.process (x, 1600.f, 1.6f, indDrive);
            x = triode.process (x * 0.45f) * 0.9f + x * 0.35f;
            x = outTrans.process (x * 0.88f);
            x = dc.process (x);
            if (i >= settle) buf[(size_t) i - settle] = x;
        }

        const double h1 = binMag (buf.data(), N, 1000.0, osRate);
        double harm[5], p2 = 0;
        for (int k = 0; k < 5; ++k)
        {
            harm[k] = binMag (buf.data(), N, 1000.0 * (k + 2), osRate);
            p2 += harm[k] * harm[k];
        }
        const double thd = h1 > 0 ? 100.0 * std::sqrt (p2) / h1 : 0.0;
        std::fprintf (fp, "%.1f,%.4f,%.4f,%.4f,%.4f,%.4f\n", drive, thd,
                      100 * harm[0] / h1, 100 * harm[1] / h1,
                      100 * harm[2] / h1, 100 * harm[3] / h1);
    }
    std::fclose (fp);
}

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "measurements";
    mkdir (dir.c_str(), 0755);
    dumpEq (dir);
    std::printf ("wrote %s/eq_curves.csv\n", dir.c_str());
    dumpThd (dir);
    std::printf ("wrote %s/thd.csv\n", dir.c_str());
    return 0;
}
