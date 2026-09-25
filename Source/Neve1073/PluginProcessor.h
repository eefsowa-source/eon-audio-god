#pragma once
#include <JuceHeader.h>
#include <vector>
#include "Dsp/Adaa.h"
#include "Dsp/Triode.h"
#include "Dsp/Transformer.h"
#include "Dsp/Stages.h"
#include "ActivityDetector.h"
#include "Quality.h"

class Neve1073Processor : public juce::AudioProcessor,
                          private juce::AsyncUpdater
{
public:
    Neve1073Processor();
    ~Neve1073Processor() override = default;

    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "EON 1073 GOD"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    // Transformer hysteresis and the dielectric-absorption network keep
    // ringing after the input stops; report it so hosts render the decay.
    double getTailLengthSeconds() const override { return 0.1; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void handleAsyncUpdate() override;   // pushes new latency to host on quality change

    // Re-derives every rate-dependent constant for a new processing rate.
    void prepareDspRate (double processingRate);

    // Allocation-free coefficient refresh; only runs when a parameter moved.
    void refreshCoefficients (double processingRate);

    eon::OversamplingManager osm;
    int lastQuality = -1;

    using Filter = juce::dsp::IIR::Filter<float>;
    using Coeff  = juce::dsp::IIR::Coefficients<float>;
    using ArrCoeff = juce::dsp::IIR::ArrayCoefficients<float>;
    Filter highPass[2], lowShelf[2], midPeak[2], highShelf[2];

    // Last parameter values the coefficients were built from (dirty tracking).
    int   lastLowF = -1, lastMidF = -1, lastHighF = -1, lastHp = -1;
    float lastLowG = 0.f, lastMidG = 0.f, lastMidQ = 0.f, lastHighG = 0.f;

    eon::JilesAtherton    inTrans[2], outTrans[2];
    eon::InductorResonator inductor[2];
    eon::TriodeStage      triode[2];
    eon::SoftClipSat      clip[2];
    eon::TanhSat          tanh[2];
    eon::DCBlocker        dc[2];
    eon::AnalogAir        air[2];

    juce::SmoothedValue<float> driveSm, outGainSm;

    // Preallocated in prepareToPlay: the per-sample drive ramp used to run
    // through a fresh std::vector on every audio callback.
    std::vector<float> driveRamp;

    eon::ActivityDetector activity;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Neve1073Processor)
};
