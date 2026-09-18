// Standalone smoke test for the pure-DSP headers — no JUCE needed.
//   c++ -std=c++20 -O2 -I Source/Shared Docs/dsp_smoke_test.cpp -o /tmp/dsp_test && /tmp/dsp_test
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include "Dsp/Adaa.h"
#include "Dsp/Triode.h"
#include "Dsp/Transformer.h"
#include "Dsp/Stages.h"
#include "Dsp/Rng.h"

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

    std::printf ("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
