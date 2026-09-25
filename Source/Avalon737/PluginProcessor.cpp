#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamFormat.h"

namespace {

// 737 panel detents.
constexpr std::array<float, 4> hpF   { 5.f, 30.f, 60.f, 100.f };
constexpr std::array<float, 4> lowF  { 15.f, 30.f, 60.f, 150.f };
constexpr std::array<float, 4> highF { 10000.f, 15000.f, 20000.f, 32000.f };

// Physical corner of the DC blocker, in hertz, at whatever rate it runs.
constexpr double kDcBlockerHz = 18.0;

} // namespace

Avalon737Processor::Avalon737Processor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Params", createLayout())
{
    const float t1 = 0.988f + juce::Random::getSystemRandom().nextFloat() * 0.024f;
    const float t2 = 0.988f + juce::Random::getSystemRandom().nextFloat() * 0.024f;
    classA[0].setTolerance (t1); classA[1].setTolerance (t2);
    meltA[0].setTolerance (t1 * 1.01f); meltA[1].setTolerance (t2 * 0.99f);
}

juce::AudioProcessorValueTreeState::ParameterLayout Avalon737Processor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    const auto db = juce::AudioParameterFloatAttributes()
                        .withLabel ("dB").withStringFromValueFunction (eonparam::fmt (1, " dB"));
    const auto hz = juce::AudioParameterFloatAttributes()
                        .withLabel ("Hz").withStringFromValueFunction (eonparam::hz);
    const auto dr = juce::AudioParameterFloatAttributes()
                        .withStringFromValueFunction (eonparam::fmt (1));

    p.push_back (std::make_unique<juce::AudioParameterChoice> ("hp",          "HPF",
        juce::StringArray { "Off", "30", "60", "100" }, 0));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("lowFreq",     "Low Freq",
        juce::StringArray { "15", "30", "60", "150" }, 2));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("lowGain",     "Low Gain",
        juce::NormalisableRange<float> (-24.f, 24.f), 0.f, db));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("lowMidFreq",  "LowMid Freq",
        juce::NormalisableRange<float> (30.f, 450.f, 1.f, 0.35f), 200.f, hz));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("lowMidGain",  "LowMid Gain",
        juce::NormalisableRange<float> (-24.f, 24.f), 0.f, db));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("highMidFreq", "HighMid Freq",
        juce::NormalisableRange<float> (200.f, 2800.f, 1.f, 0.35f), 1000.f, hz));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("highMidGain", "HighMid Gain",
        juce::NormalisableRange<float> (-24.f, 24.f), 0.f, db));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("highFreq",    "High Freq",
        juce::StringArray { "10k", "15k", "20k", "32k" }, 1));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("highGain",    "High Gain",
        juce::NormalisableRange<float> (0.f, 24.f), 0.f, db));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("output",      "Output",
        juce::NormalisableRange<float> (-18.f, 18.f), 0.f, db));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("drive",       "Drive",
        juce::NormalisableRange<float> (0.f, 10.f), 2.f, dr));
    p.push_back (std::make_unique<juce::AudioParameterBool>   ("analog",      "Analog", true));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("quality",     "Quality",
        juce::StringArray { "Eco 8x", "Liquid 16x", "GOD 32x", "GOD+ NR 32x" }, 2));
    p.push_back (std::make_unique<juce::AudioParameterBool>   ("adaptive",    "Adaptive CPU", true));
    return { p.begin(), p.end() };
}

bool Avalon737Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void Avalon737Processor::prepareDspRate (double processingRate)
{
    // Every stage that carries a physical time constant must be told the rate
    // it actually runs at, otherwise its poles scale with the oversampling
    // factor and the emulation changes character between quality modes.
    for (auto& d : dc)   d.prepare (processingRate, kDcBlockerHz);
    for (auto& a : air)  a.prepare (processingRate);
    for (auto& a : classA) a.prepare (processingRate);
    for (auto& a : meltA)  a.prepare (processingRate);
}

void Avalon737Processor::refreshCoefficients (double processingRate)
{
    const int hpIdx = (int) apvts.getRawParameterValue ("hp")->load();
    const int lfIdx = (int) apvts.getRawParameterValue ("lowFreq")->load();
    const int hfIdx = (int) apvts.getRawParameterValue ("highFreq")->load();

    const float lg  = apvts.getRawParameterValue ("lowGain")->load();
    const float lmg = apvts.getRawParameterValue ("lowMidGain")->load();
    const float hmg = apvts.getRawParameterValue ("highMidGain")->load();
    const float hg  = apvts.getRawParameterValue ("highGain")->load();
    const float lmf = apvts.getRawParameterValue ("lowMidFreq")->load();
    const float hmf = apvts.getRawParameterValue ("highMidFreq")->load();

    const bool dirty = hpIdx != lastHp || lfIdx != lastLowF || hfIdx != lastHighF
                    || lg != lastLowG || lmg != lastLowMidG || hmg != lastHighMidG
                    || hg != lastHighG || lmf != lastLowMidF || hmf != lastHighMidF;

    if (! dirty)
        return;

    // Stack-allocated arrays assigned into preallocated coefficient objects:
    // IIR::Coefficients::assignImpl only grows its storage, so this is
    // allocation-free once prepareToPlay has created the objects.
    const auto pc  = ArrCoeff::makeHighPass   (processingRate, hpF[hpIdx]);
    const auto lc  = ArrCoeff::makeLowShelf   (processingRate, lowF[lfIdx], 0.68f,
                                               juce::Decibels::decibelsToGain (lg));
    const auto lmc = ArrCoeff::makePeakFilter (processingRate, lmf, 2.6f,
                                               juce::Decibels::decibelsToGain (lmg));
    const auto hmc = ArrCoeff::makePeakFilter (processingRate, hmf, 2.6f,
                                               juce::Decibels::decibelsToGain (hmg));
    const auto hc  = ArrCoeff::makeHighShelf  (processingRate, highF[hfIdx], 0.60f,
                                               juce::Decibels::decibelsToGain (hg));

    for (int ch = 0; ch < 2; ++ch)
    {
        *highPass[ch].coefficients  = pc;
        *lowShelf[ch].coefficients  = lc;
        *lowMid[ch].coefficients    = lmc;
        *highMid[ch].coefficients   = hmc;
        *highShelf[ch].coefficients = hc;
    }

    lastHp = hpIdx; lastLowF = lfIdx; lastHighF = hfIdx;
    lastLowG = lg; lastLowMidG = lmg; lastHighMidG = hmg; lastHighG = hg;
    lastLowMidF = lmf; lastHighMidF = hmf;
}

void Avalon737Processor::prepareToPlay (double sr, int sb)
{
    osm.prepare ((size_t) sb);
    osm.setMode ((eon::QualityMode) (int) apvts.getRawParameterValue ("quality")->load());
    lastQuality = -1;

    const double osRate = osm.rate (sr);
    juce::dsp::ProcessSpec spec { osRate, (juce::uint32) sb * 32, 2 };
    for (auto& f : highPass)  f.prepare (spec);
    for (auto& f : lowShelf)  f.prepare (spec);
    for (auto& f : lowMid)    f.prepare (spec);
    for (auto& f : highMid)   f.prepare (spec);
    for (auto& f : highShelf) f.prepare (spec);

    // Preallocate the coefficient objects so the audio-thread refresh only ever
    // writes into existing storage.
    for (int ch = 0; ch < 2; ++ch)
    {
        highPass[ch].coefficients  = Coeff::makeHighPass   (osRate, 30.f);
        lowShelf[ch].coefficients  = Coeff::makeLowShelf   (osRate, 60.f, 0.68f, 1.f);
        lowMid[ch].coefficients    = Coeff::makePeakFilter (osRate, 200.f, 2.6f, 1.f);
        highMid[ch].coefficients   = Coeff::makePeakFilter (osRate, 1000.f, 2.6f, 1.f);
        highShelf[ch].coefficients = Coeff::makeHighShelf  (osRate, 15000.f, 0.60f, 1.f);
    }
    lastHp = lastLowF = lastHighF = -1;   // force a refresh
    lastLowG = lastLowMidG = lastHighMidG = lastHighG = 0.f;
    lastLowMidF = lastHighMidF = 0.f;

    // Sized for the largest oversampled block the host can hand us.
    driveRamp.assign ((size_t) sb * 32u, 0.0f);

    prepareDspRate (osRate);

    for (auto& a : classA) a.reset();
    for (auto& a : meltA)  a.reset();
    for (auto& t : triodeA)    { t.iterations = osm.triodeIterations(); t.reset(); }
    for (auto& t : triodeB)    { t.iterations = osm.triodeIterations(); t.reset(); }
    for (auto& t : triodeMelt) { t.iterations = osm.triodeIterations(); t.reset(); }
    for (auto& a : tanhIn)   a.reset();
    for (auto& a : tanhMid)  a.reset();
    for (auto& a : tanhMelt) a.reset();
    for (auto& d : dc)       d.reset();
    for (auto& a : air)      a.reset();

    // The drive ramp is consumed once per *oversampled* sample, so it must be
    // prepared at the oversampled rate to stay a real 50 ms ramp. The output
    // gain is applied to the base-rate buffer and stays at the host rate.
    driveSm.reset (osRate, 0.05);
    outGainSm.reset (sr, 0.02);
    driveSm.setCurrentAndTargetValue (apvts.getRawParameterValue ("drive")->load());
    outGainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue ("output")->load()));

    activity.prepare (sr);
    setLatencySamples ((int) osm.latency());
}

void Avalon737Processor::handleAsyncUpdate()
{
    setLatencySamples ((int) osm.latency());
    updateHostDisplay();
}

void Avalon737Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int hpIdx = (int) apvts.getRawParameterValue ("hp")->load();
    const int hfIdx = (int) apvts.getRawParameterValue ("highFreq")->load();
    const int qIdx  = (int) apvts.getRawParameterValue ("quality")->load();
    const bool adaptiveOn = apvts.getRawParameterValue ("adaptive")->load() > 0.5f;
    const bool analogOn   = apvts.getRawParameterValue ("analog")->load() > 0.5f;

    const float hg = apvts.getRawParameterValue ("highGain")->load();

    driveSm.setTargetValue (apvts.getRawParameterValue ("drive")->load());
    outGainSm.setTargetValue (juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue ("output")->load()));

    const double sr = getSampleRate();
    double osRate = osm.rate (sr);

    if (qIdx != lastQuality)
    {
        osm.setMode ((eon::QualityMode) qIdx);
        osm.current().reset();  // drop stale audio from its delay line
        for (auto& t : triodeA)    t.iterations = osm.triodeIterations();
        for (auto& t : triodeB)    t.iterations = osm.triodeIterations();
        for (auto& t : triodeMelt) t.iterations = osm.triodeIterations();

        osRate = osm.rate (sr);
        prepareDspRate (osRate);

        // Keep the drive ramp continuous across the processing-rate change.
        const float target  = driveSm.getTargetValue();
        const float current = driveSm.getCurrentValue();
        driveSm.reset (osRate, 0.05);
        driveSm.setCurrentAndTargetValue (current);
        driveSm.setTargetValue (target);

        lastQuality = qIdx;
        triggerAsyncUpdate();
    }

    refreshCoefficients (osRate);

    // Adaptive CPU: look at every channel, and keep the chain running for a
    // hold-off window after the last active block so Class-A and DA tails
    // finish instead of being chopped.
    const bool runChain = ! adaptiveOn || activity.active (buffer);

    juce::dsp::AudioBlock<float> block (buffer);
    auto osBlock = osm.current().processSamplesUp (block);

    const int numCh = (int) std::min (osBlock.getNumChannels(), (size_t) 2);
    const int N = (int) osBlock.getNumSamples();
    const bool useTriode = osm.useTriode();
    const bool godPlus = qIdx == 3;
    const bool air32k = hfIdx == 3 && hg > 0.05f;

    if ((int) driveRamp.size() < N)
        driveRamp.resize ((size_t) N);   // safety net; prepareToPlay sizes this
    for (int i = 0; i < N; ++i) driveRamp[(size_t) i] = driveSm.getNextValue();

    if (runChain)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = osBlock.getChannelPointer (ch);
            for (int i = 0; i < N; ++i)
            {
                float x = d[i] * (1.f + driveRamp[(size_t) i] * 0.11f);

                x = tanhIn[ch].process (x * 0.46f) * 2.17f;
                x = classA[ch].process (x);                 // Class-A + DA memory + sag/thermal
                if (hpIdx != 0) x = highPass[ch].processSample (x);

                x = lowShelf[ch].processSample (x);
                if (useTriode)
                    x = triodeA[ch].process (x * 0.10f) * 0.18f + x * 0.82f;

                x = lowMid[ch].processSample (x);
                if (godPlus)
                    x = triodeB[ch].process (x * 0.14f) * 6.4f + x * 0.08f;
                else
                    x = tanhMid[ch].process (x * 0.09f) * 10.1f + x * 0.08f;

                x = highMid[ch].processSample (x);
                x = highShelf[ch].processSample (x);

                if (air32k)
                {
                    // 32k "liquid air": separate Class-A melt path
                    float melt = tanhMelt[ch].process (x * (0.78f + hg * 0.03f));
                    melt = meltA[ch].process (melt * 0.55f) * 1.8f;
                    if (godPlus)
                        melt = triodeMelt[ch].process (melt * 0.35f) * 1.2f + melt * 0.4f;
                    x = melt * 0.88f + x * 0.12f;
                }

                if (analogOn) x += air[ch].process();
                x = dc[ch].process (x);
                d[i] = x;
            }
        }
    }

    osm.current().processSamplesDown (block);
    outGainSm.applyGain (buffer, buffer.getNumSamples());
}

void Avalon737Processor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream mos (dest, true);
    apvts.state.writeToStream (mos);
}

void Avalon737Processor::setStateInformation (const void* data, int size)
{
    auto tree = juce::ValueTree::readFromData (data, size);
    if (tree.isValid()) apvts.state = tree;
}

juce::AudioProcessorEditor* Avalon737Processor::createEditor() { return new Avalon737Editor (*this); }

// Both processors export this entry point, so the quality harness (which
// compiles them into one executable) defines EON_QUALITY_HARNESS to skip it.
#ifndef EON_QUALITY_HARNESS
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Avalon737Processor(); }
#endif
