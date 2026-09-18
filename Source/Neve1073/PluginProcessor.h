#pragma once
#include <JuceHeader.h>
#include "Dsp/Adaa.h"
#include "Dsp/Triode.h"
#include "Dsp/Transformer.h"
#include "Dsp/Stages.h"
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
    double getTailLengthSeconds() const override { return 0.0; }
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

    eon::OversamplingManager osm;
    int lastQuality = -1;

    using Filter = juce::dsp::IIR::Filter<float>;
    using Coeff  = juce::dsp::IIR::Coefficients<float>;
    Filter highPass[2], lowShelf[2], midPeak[2], highShelf[2];

    eon::JilesAtherton    inTrans[2], outTrans[2];
    eon::InductorResonator inductor[2];
    eon::TriodeStage      triode[2];
    eon::SoftClipSat      clip[2];
    eon::TanhSat          tanh[2];
    eon::DCBlocker        dc[2];
    eon::AnalogAir        air[2];

    juce::SmoothedValue<float> driveSm, outGainSm;
    double silentEnv = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Neve1073Processor)
};
