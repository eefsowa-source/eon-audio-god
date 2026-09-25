#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace eon {

// ---------------------------------------------------------------------------
// Adaptive-CPU activity detector.
//
// Two defects in the previous per-plugin version:
//
//  1. It inspected channel 0 only, so a signal present only on the right
//     channel looked like silence and the entire nonlinear chain was skipped
//     on both channels — the right channel silently lost its processing.
//  2. It dropped the chain the instant the envelope crossed the threshold,
//     cutting the decay of the transformer / dielectric-absorption tails.
//
// This version looks at every channel and keeps processing for `holdSeconds`
// after the last block above the threshold, so tails finish before the chain
// is skipped.
// ---------------------------------------------------------------------------
struct ActivityDetector
{
    void prepare (double sampleRate) { rate = sampleRate; reset(); }
    void reset() { envelope = 0.0; holdBlocks = 0; }

    // Returns true when the nonlinear chain should run for this block.
    bool active (const juce::AudioBuffer<float>& buffer,
                 double holdSeconds = 0.25,
                 double threshold   = 1e-6)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

        envelope = envelope * 0.9 + (double) peak * 0.1;

        const int numSamples = juce::jmax (1, buffer.getNumSamples());
        const int holdFor = (int) std::ceil (holdSeconds * rate / (double) numSamples);

        if (envelope > threshold) holdBlocks = holdFor;
        else if (holdBlocks > 0)      --holdBlocks;

        return holdBlocks > 0;
    }

    double rate = 48000.0;
    double envelope = 0.0;
    int holdBlocks = 0;
};

} // namespace eon
