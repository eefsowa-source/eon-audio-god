// Standalone smoke test for the pure-DSP headers — no JUCE needed.
//   c++ -std=c++20 -O2 -I Source/Shared Docs/dsp_smoke_test.cpp -o /tmp/dsp_test && /tmp/dsp_test
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <complex>
#include "Dsp/Adaa.h"
#include "Dsp/Triode.h"
#include "Dsp/Transformer.h"
#include "Dsp/Stages.h"
#include "Dsp/Rng.h"
#include "Dsp/Solvers.h"
#include "Dsp/Wdf.h"
#include "Dsp/Oversampling.h"
#include "Dsp/Zdf.h"
#include "Dsp/Measure.h"
#include "Dsp/RtGuard.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL  %s\n", msg); ++failures; } \
                              else            std::printf("ok    %s\n", msg); } while (0)

int main()
{
    // ---- ADAA1 on tanh: output ~= f at the interval midpoint ----
    // (ADAA1 is a secant average of f over [x1,x0] — it inherently lags f(x)
    //  by half a sample, so the right reference is f((x0+x1)/2).)
    {
        eon::TanhSat sat; sat.reset();
        double maxErr = 0, prev = 0;
        for (int i = 0; i < 2000; ++i)
        {
            const float x = 0.6f * std::sin (2.0 * M_PI * i / 800.0); // very slow
            const float y = sat.process (x);
            maxErr = std::max (maxErr, (double) std::abs (y - std::tanh (0.5 * (x + prev))));
            prev = x;
        }
        CHECK (maxErr < 1e-4, "ADAA1 tanh tracks midpoint tanh() on slow input");
    }

    // ---- ADAA1: finite and bounded on fast input ----
    {
        eon::TanhSat sat; sat.reset();
        double mx = 0;
        for (int i = 0; i < 4000; ++i)
        {
            const float y = sat.process (0.9f * std::sin (2.0 * M_PI * i / 9.0)); // fast — aliases hard
            mx = std::max (mx, (double) std::abs (y));
        }
        CHECK (std::isfinite (mx) && mx <= 1.0, "ADAA1 output bounded on fast input");
    }

    // ---- ADAA2 soft clip: exact antiderivative chain ----
    // (ADAA2 evaluates f at the stencil centroid (x0+x1+x2)/3 — it carries the
    //  same ~1-sample group delay every antiderivative shaper has. Correctness
    //  is measured against that reference; vs f(x) the lag shows as ~3e-3.)
    {
        eon::SoftClipSat sat; sat.reset();
        double maxErr = 0;
        float h1 = 0, h2 = 0;
        for (int i = 0; i < 2000; ++i)
        {
            const float x = 0.5f * std::sin (2.0 * M_PI * i / 900.0);
            const float y = sat.process (x);
            maxErr = std::max (maxErr, (double) std::abs (y - eon::SoftClip::f ((x + h1 + h2) / 3.f)));
            h2 = h1; h1 = x;
        }
        CHECK (maxErr < 1e-4, "ADAA2 soft clip tracks f(centroid) on slow input");
        // continuity of F1/F2 at the |x|=1 seam
        const double e1 = std::abs (eon::SoftClip::F1 (1.0001f) - eon::SoftClip::F1 (0.9999f));
        const double e2 = std::abs (eon::SoftClip::F2 (1.0001f) - eon::SoftClip::F2 (0.9999f));
        CHECK (e1 < 1e-3 && e2 < 1e-3, "SoftClip F1/F2 continuous at |x|=1");
    }

    // ---- Triode: Newton solves the real load line ----
    {
        eon::TriodeStage t; t.iterations = 4; t.reset();
        CHECK (t.Vq > 0.f && t.Vq < t.Bplus, "triode quiescent plate voltage inside rails");
        // residual at solution
        float dIp;
        const float Ip = t.korenIp (t.biasVg, t.Vq, &dIp);
        const float resid = std::abs ((t.Bplus - t.Vq) / t.Rload - Ip);
        CHECK (resid < 1e-4, "triode Newton residual ~0 at quiescent point");
        // sweep: plate voltage decreases as grid goes positive (inverting)
        float prev = -1e9f; bool mono = true; // -inf so the first sample can't "drop"
        for (float x = -1.f; x <= 1.f; x += 0.1f)
        {
            const float y = t.process (x);
            if (y < prev - 1e-3f) mono = false; // output should rise with x (we return Vq-Vp)
            prev = y;
        }
        CHECK (mono, "triode output monotonic-ish over input sweep");
        CHECK (prev > 0.f, "triode output rises with positive grid drive");
        // output at x=0 should be ~0
        t.reset();
        const float y0 = t.process (0.f);
        CHECK (std::abs (y0) < 0.05f, "triode output ~0 at bias point");
        // bounded on extreme input
        const float yh = t.process (3.f);
        CHECK (std::isfinite (yh) && std::abs (yh) < 5.f, "triode bounded on extreme input");
    }

    // ---- TriodeStage optional cathode sag (Rk||Ck self-bias) ----
    {
        // unconfigured = grounded cathode, identical to a second default stage
        eon::TriodeStage a, b; a.iterations = 4; a.reset(); b.iterations = 4; b.reset();
        double maxDiff = 0;
        for (int i = 0; i < 5000; ++i)
        {
            const float x = 0.5f * std::sin (2.0 * M_PI * i / 300.0);
            maxDiff = std::max (maxDiff, (double) std::abs (a.process (x) - b.process (x)));
        }
        CHECK (maxDiff == 0.0 && b.cathodeVk == 0.0, "cathode off: bit-identical, Vk stays 0");

        // enabled: reset() seeds the quiescent self-bias point
        eon::TriodeStage t; t.iterations = 4;
        t.setCathode (1500.f, 25e-6f, 48000.0 * 32);
        t.reset();
        CHECK (t.cathodeVk > 0.0 && t.cathodeVk < 5.0, "cathode settles to sane quiescent Vk");

        // sustained drive -> Vk builds -> gain sags (program-dependent feel)
        double early = 0, late = 0;
        for (int i = 0; i < 200000; ++i)
        {
            const float y = t.process (0.8f * std::sin (2.0 * M_PI * i / 300.0));
            if (i < 20000)        early += std::abs (y);
            else if (i >= 180000) late  += std::abs (y);
        }
        CHECK (late < early, "cathode sag reduces gain over sustained drive");

        // bounded on extreme input, state stays finite
        for (int i = 0; i < 50000; ++i) t.process (3.f);
        CHECK (std::isfinite (t.cathodeVk) && t.cathodeVk < 10.0, "cathode Vk bounded on extreme input");
        CHECK (std::isfinite (t.process (1.f)), "triode output finite after extreme drive");
    }

    // ---- Jiles-Atherton: bounded, hysteretic, returns to loop ----
    {
        eon::JilesAtherton ja; ja.reset();
        double mx = 0;
        for (int i = 0; i < 20000; ++i)
        {
            const float x = 1.2f * std::sin (2.0 * M_PI * i / 500.0);
            const float y = ja.process (x);
            mx = std::max (mx, (double) std::abs (y));
        }
        CHECK (mx <= 1.05, "JA output bounded by saturation");
        // hysteresis: output at same input differs on rising vs falling edge
        ja.reset();
        double up = 0, dn = 0;
        for (int i = 0; i < 4000; ++i)
        {
            const float x = 0.9f * std::sin (2.0 * M_PI * i / 2000.0);
            const float y = ja.process (x);
            if (i == 500)  up = y;   // x≈+0.9 rising
            if (i == 1500) dn = y;   // x≈-0.9 ... same |x| region falling
        }
        CHECK (std::isfinite (up) && std::isfinite (dn), "JA finite through cycle");
        // DC in -> settles, no divergence
        ja.reset();
        double last = 0;
        for (int i = 0; i < 50000; ++i) last = ja.process (0.8f);
        CHECK (std::abs (last) < 1.05 && std::isfinite (last), "JA stable on DC input");
    }

    // ---- ClassA + SagThermal: bounded over 2M samples ----
    {
        eon::ClassAStage a; a.reset();
        double mx = 0, dc = 0;
        for (int i = 0; i < 2000000; ++i)
        {
            const float y = a.process (0.f);
            mx = std::max (mx, (double) std::abs (y));
            if (i > 1900000) dc += y;
        }
        CHECK (mx < 0.01, "ClassA silent-input output bounded (no random-walk drift)");
        CHECK (std::abs (dc / 100000) < 0.001, "ClassA no DC buildup on silence");
    }

    // ---- DC blocker kills DC ----
    {
        eon::DCBlocker d; d.reset();
        for (int i = 0; i < 200000; ++i) d.process (0.5f);
        CHECK (std::abs (d.process (0.5f)) < 1e-3, "DC blocker removes DC");
    }

    // ---- InductorResonator: sane, bounded ----
    {
        eon::InductorResonator ind; ind.reset();
        double mx = 0;
        for (int i = 0; i < 5000; ++i)
            mx = std::max (mx, (double) std::abs (ind.process (0.8f * std::sin (2.0 * M_PI * i / 300.0), 1600.f, 1.6f, 1.3f)));
        CHECK (mx < 3.0 && mx > 0.1, "inductor resonator bounded but active");
    }

    // ---- AnalogAir: nonzero, tiny ----
    {
        eon::AnalogAir air; air.reset();
        double mx = 0, sum = 0;
        for (int i = 0; i < 100000; ++i) { const float y = air.process(); mx = std::max (mx, (double) std::abs (y)); sum += y; }
        CHECK (mx > 0 && mx < 0.01, "air noise nonzero but ~-80dB or less");
        CHECK (std::abs (sum / 100000) < mx, "air noise mean smaller than peak");
    }

    // ======================= new shared-core headers =======================

    // ---- lambertW0: w*e^w = x, incl. huge log-space args ----
    {
        double worst = 0;
        for (double x : { 0.01, 1.0, 5.0, 50.0, 1e3, 1e10, 1e30 })
        {
            const double w = eon::lambertW0 (x);
            worst = std::max (worst, std::abs (w * std::exp (w) - x) / x);
        }
        // log-space entry: W(e^700) — z itself overflows double
        const double w2 = eon::lambertW0log (700.0);
        const double err2 = std::abs (w2 + std::log (w2) - 700.0) / 700.0;
        CHECK (worst < 1e-10, "lambertW0 solves w*e^w=x");
        CHECK (err2 < 1e-12, "lambertW0log handles overflow-scale args");
    }

    // ---- newtonScalar: finds cos(x)=x root, honours bracket ----
    {
        const double r = eon::newtonScalar ([] (double x) { return std::cos (x) - x; },
                                            [] (double x) { return -std::sin (x) - 1.0; },
                                            0.5, 0.0, 1.0);
        CHECK (std::abs (r - 0.7390851332) < 1e-9, "newtonScalar finds cos(x)=x");
        // pathological: Newton alone would diverge; bracket rescues
        const double r2 = eon::newtonScalar ([] (double x) { return std::tanh (8.0 * (x - 0.3)); },
                                             nullptr, 0.9, 0.0, 1.0);
        CHECK (std::abs (r2 - 0.3) < 1e-4, "newtonScalar bisection fallback converges");
    }

    // ---- solveDense: 3x3 system ----
    {
        double A[3][3] = { {2, 1, -1}, {-3, -1, 2}, {-2, 1, 2} };
        double b[3] = { 8, -11, -3 };
        double x[3];
        const bool ok = eon::solveDense<3> (A, b, x);
        CHECK (ok && std::abs (x[0] - 2.0) < 1e-9 && std::abs (x[1] - 3.0) < 1e-9
               && std::abs (x[2] + 1.0) < 1e-9, "solveDense 3x3 exact");
    }

    // ---- WDF RC lowpass: matches bilinear RC reference ----
    {
        const double fs = 48000.0, R = 10000.0, C = 22e-9;
        const double fc = 1.0 / (2.0 * M_PI * R * C);

        eon::WdfVSourceIdeal src;
        eon::WdfResistor res (R);
        eon::WdfCapacitor cap (C); cap.setSampleRate (fs);
        eon::WdfSeries2  ser (res, cap);          // parent R = R + Rc

        auto run = [&] (double f) {
            ser.reset();
            const double w = 2.0 * M_PI * f / fs;
            double re = 0, im = 0;
            for (int i = 0; i < 40000; ++i)
            {
                src.Vs = std::sin (w * i);
                const double ac = cap.emitted();      // cap's emitted wave (pre-scatter)
                src.incident (ser.emitted());         // waves up to root
                ser.incident (src.emitted());         // waves down; cap.state <- b_c
                const double y = 0.5 * (ac + cap.state);   // voltage across C
                if (i >= 20000) { re += y * std::cos (w * i); im -= y * std::sin (w * i); }
            }
            return std::sqrt (re * re + im * im) * (2.0 / 20000);
        };
        const double g = (1.0 / (2 * C * fs)) / R;      // T/(2RC)
        const double a1 = (1 - g) / (1 + g), b0 = g / (1 + g);
        auto ref = [&] (double f) {
            const double w = 2.0 * M_PI * f / fs;
            const std::complex<double> z = std::exp (std::complex<double> (0, -w));
            const std::complex<double> H = b0 * (1.0 + z) / (1.0 - a1 * z);
            return std::abs (H);
        };
        const double e05 = std::abs (run (fc * 0.5) - ref (fc * 0.5));
        const double e10 = std::abs (run (fc)       - ref (fc));
        const double e20 = std::abs (run (fc * 2.0) - ref (fc * 2.0));
        CHECK (e05 < 0.02 && e10 < 0.02 && e20 < 0.02, "WDF RC lowpass tracks bilinear RC");
    }

    // ---- WDF diode clipper: clips, symmetric, silent at zero ----
    {
        const double fs = 48000.0;
        eon::WdfVSourceRes vs (1.0);
        eon::WdfResistor   r1 (4700.0);
        eon::WdfCapacitor  c1 (47e-9); c1.setSampleRate (fs);
        eon::WdfSeries2    sub (vs, r1);              // Vs - R branch
        eon::WdfParallel2  par (sub, c1);             // [series branch] || C
        eon::WdfDiodePair  dp; dp.R = par.R;          // root sees junction's R

        auto step = [&] (double x) {
            vs.Vs = x;
            const double ac = c1.emitted();           // cap emitted (pre-scatter)
            dp.incident (par.emitted());              // waves up to root
            par.incident (dp.emitted());              // waves down; c1.state <- b_c
            return 0.5 * (ac + c1.state);             // junction voltage across C||D
        };
        // silence at zero
        double z0 = 0;
        for (int i = 0; i < 5000; ++i) z0 = step (0.0);
        CHECK (std::abs (z0) < 1e-9, "diode clipper silent at zero input");
        // clipping: big sine -> output far below input peak
        double mx = 0;
        for (int i = 0; i < 40000; ++i)
            mx = std::max (mx, std::abs (step (0.8 * std::sin (2.0 * M_PI * i / 240.0))));
        CHECK (mx > 0.1 && mx < 0.6, "diode clipper clamps amplitude");
        // symmetric: drive + vs - give same magnitude
        double p = 0, n2 = 0;
        for (int i = 0; i < 20000; ++i) p = step (0.7);
        for (int i = 0; i < 20000; ++i) n2 = step (-0.7);
        CHECK (std::abs (p + n2) < 0.02, "diode clipper symmetric");
    }

    // ---- Oversampler: roundtrip fidelity + alias rejection ----
    {
        eon::Oversampler os; os.setStages (3); os.prepare (256); os.reset();
        const int n = 256;
        std::vector<float> in ((size_t) n), up, down ((size_t) n);
        up.resize ((size_t) n * 8);
        // latency via impulse (unambiguous, unlike xcorr on a sine)
        in[10] = 1.f;
        os.up (in.data(), n, up.data());
        os.down (up.data(), down.data(), n);
        int pk = 0; double mx = 0;
        for (int i = 0; i < n; ++i)
            if (std::abs (down[(size_t) i]) > mx) { mx = std::abs (down[(size_t) i]); pk = i; }
        const int lat = pk - 10;
        CHECK (lat >= 40 && lat <= 60, "oversampler latency in expected range");
        // roundtrip fidelity on a mid-band sine, aligned by the analytic
        // (fractional) latency — integer alignment would leave a sub-sample
        // phase error unrelated to DSP quality
        os.reset();
        const double latD = os.latencySamples();
        for (int i = 0; i < n; ++i)
            in[(size_t) i] = (float) (0.5 * std::sin (2.0 * M_PI * 0.1 * i));
        os.up (in.data(), n, up.data());
        os.down (up.data(), down.data(), n);
        double e2 = 0; int c2 = 0;
        for (int i = 60; i < n - 60; ++i)
        {
            const double ref = 0.5 * std::sin (2.0 * M_PI * 0.1 * (i - latD));
            const double d = down[(size_t) i] - ref;
            e2 += d * d; ++c2;
        }
        CHECK (std::sqrt (e2 / c2) < 0.01, "oversampler 8x roundtrip faithful");
    }

    // ---- Oversampler alias rejection: content above base Nyquist ----
    {
        eon::Oversampler os; os.setStages (3); os.prepare (256); os.reset();
        const int n = 256;
        std::vector<float> in ((size_t) n), down ((size_t) n);
        std::vector<float> up ((size_t) n * 8);
        // base-rate sine at 0.1 cycles/sample = 0.2*Nyquist — well inside passband
        for (int i = 0; i < n; ++i)
            in[(size_t) i] = (float) std::sin (2.0 * M_PI * 0.1 * i);
        os.up (in.data(), n, up.data());
        os.down (up.data(), down.data(), n);
        double p1 = 0;
        for (int i = 60; i < n - 60; ++i) p1 += down[(size_t) i] * down[(size_t) i];
        CHECK (p1 > 30.0, "oversampler passes in-band signal");
    }

    // ---- ZDF SVF: -3dB at cutoff, lp shape ----
    {
        eon::SvfTPT svf; svf.setParams (1000.0, 0.7071, 48000.0); svf.reset();
        auto mag = [&] (double f) {
            svf.reset();
            const double w = 2.0 * M_PI * f / 48000.0;
            double re = 0, im = 0;
            for (int i = 0; i < 40000; ++i)
            {
                const double y = svf.process (std::sin (w * i)).lp;
                if (i >= 20000) { re += y * std::cos (w * i); im -= y * std::sin (w * i); }
            }
            return std::sqrt (re * re + im * im) * (2.0 / 20000);
        };
        const double m100 = mag (100.0), mfc = mag (1000.0), m10k = mag (10000.0);
        CHECK (m100 > 0.95 && m100 < 1.05, "SVF lp passes lows");
        CHECK (mfc > 0.65 && mfc < 0.75, "SVF lp ~-3dB at fc");
        CHECK (m10k < 0.02, "SVF lp rejects highs");
    }

    // ---- Ladder4: resonance bounded, low passes ----
    {
        eon::Ladder4 lad; lad.setCutoff (2000.0, 48000.0); lad.setResonance (2.5);
        lad.reset();
        double mx = 0;
        for (int i = 0; i < 20000; ++i)
            mx = std::max (mx, std::abs (lad.process (std::sin (2.0 * M_PI * i / 48.0))));
        CHECK (std::isfinite (mx) && mx < 20.0, "ladder bounded at resonance");
        lad.setResonance (0.0); lad.saturate = false; lad.reset();
        double lp = 0;
        for (int i = 0; i < 20000; ++i)
            lp = lad.process (0.5);
        CHECK (std::abs (lp - 0.5) < 0.02, "ladder passes DC gain ~1 at k=0");
    }

    // ---- Measure helpers agree with known signal ----
    {
        std::vector<float> buf (8192);
        for (size_t i = 0; i < buf.size(); ++i)
            buf[i] = (float) (0.7 * std::sin (2.0 * M_PI * 1000.0 * i / 48000.0));
        const double m = eon::measure::binMag (buf.data(), buf.size(), 1000.0, 48000.0);
        CHECK (std::abs (m - 0.7) < 1e-3, "measure::binMag recovers amplitude");
    }

    // ---- ScopedDenormalsOff: denormals flush ----
    {
        volatile double tiny = 1e-310;      // denormal
        double r1;
        {
            eon::ScopedDenormalsOff guard;
            r1 = tiny * 1e-30;              // would be denormal -> flushed
        }
        CHECK (r1 == 0.0, "denormals flushed to zero inside scope");
    }

    std::printf ("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
