#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamFormat.h"

namespace {

// 1073 panel detents.
constexpr std::array<float, 4> lowF  { 35.f, 60.f, 110.f, 220.f };
constexpr std::array<float, 6> midF  { 360.f, 700.f, 1600.f, 3200.f, 4800.f, 7200.f };
constexpr std::array<float, 3> highF { 10000.f, 12000.f, 16000.f };
constexpr std::array<float, 5> hpfF  { 0.f, 50.f, 80.f, 160.f, 300.f };

// Physical corner of the DC blocker, in hertz, at whatever rate it runs.
constexpr double kDcBlockerHz = 18.0;

} // namespace

Neve1073Processor::Neve1073Processor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Params", createLayout())
{
    // unit-to-unit component tolerance, fixed per instance
    const float t1 = 0.982f + juce::Random::getSystemRandom().nextFloat() * 0.036f;
    const float t2 = 0.982f + juce::Random::getSystemRandom().nextFloat() * 0.036f;
    inTrans[0].setTolerance (t1);  inTrans[1].setTolerance (t2);
    outTrans[0].setTolerance (t1 * 0.985f);
    outTrans[1].setTolerance (t2 * 1.015f);
    inductor[0].setTolerance (t1); inductor[1].setTolerance (t2);
    // voicing: input transformer stays a light always-on color, output iron takes the heat
    for (auto& t : inTrans)  t.drive = 0.05f;
    for (auto& t : outTrans) t.drive = 1.4f;
}

juce::AudioProcessorValueTreeState::ParameterLayout Neve1073Processor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    const auto db = juce::AudioParameterFloatAttributes()
                        .withLabel ("dB").withStringFromValueFunction (eonparam::fmt (1, " dB"));
    const auto q  = juce::AudioParameterFloatAttributes()
                        .withStringFromValueFunction (eonparam::fmt (2));
    const auto dr = juce::AudioParameterFloatAttributes()
                        .withStringFromValueFunction (eonparam::fmt (1));

    p.push_back (std::make_unique<juce::AudioParameterChoice> ("lowFreq",  "Low Freq",
        juce::StringArray { "35", "60", "110", "220" }, 1));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("lowGain",  "Low Gain",
        juce::NormalisableRange<float> (-24.f, 24.f), 0.f, db));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("midFreq",  "Mid Freq",
        juce::StringArray { "360", "700", "1600", "3200", "4800", "7200" }, 2));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("midGain",  "Mid Gain",
        juce::NormalisableRange<float> (-24.f, 24.f), 0.f, db));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("midQ",     "Mid Q",
        juce::NormalisableRange<float> (0.3f, 5.f), 1.6f, q));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("highFreq", "High Freq",
        juce::StringArray { "10k", "12k", "16k" }, 1));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("highGain", "High Gain",
        juce::NormalisableRange<float> (-24.f, 24.f), 0.f, db));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("hpf",      "HPF",
        juce::StringArray { "Off", "50", "80", "160", "300" }, 0));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("output",   "Output",
        juce::NormalisableRange<float> (-24.f, 24.f), 0.f, db));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("drive",    "Drive",
        juce::NormalisableRange<float> (0.f, 10.f), 3.5f, dr));
    p.push_back (std::make_unique<juce::AudioParameterBool>   ("analog",   "Analog", true));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("quality",  "Quality",
        juce::StringArray { "Eco 8x", "Liquid 16x", "GOD 32x", "GOD+ NR 32x" }, 2));
    p.push_back (std::make_unique<juce::AudioParameterBool>   ("adaptive", "Adaptive CPU", true));
    return { p.begin(), p.end() };
}

bool Neve1073Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void Neve1073Processor::prepareDspRate (double processingRate)
{
    // Every stage that carries a physical time constant must be told the rate
    // it actually runs at, otherwise its poles scale with the oversampling
    // factor and the emulation changes character between quality modes.
    for (auto& d : dc)       d.prepare (processingRate, kDcBlockerHz);
    for (auto& a : air)      a.prepare (processingRate);
    for (auto& i : inductor) i.prepare (processingRate);
    for (auto& t : inTrans)  t.prepare (processingRate);
    for (auto& t : outTrans) t.prepare (processingRate);
}

void Neve1073Processor::refreshCoefficients (double processingRate)
{
    const int lfIdx = (int) apvts.getRawParameterValue ("lowFreq")->load();
    const int mfIdx = (int) apvts.getRawParameterValue ("midFreq")->load();
    const int hfIdx = (int) apvts.getRawParameterValue ("highFreq")->load();
    const int hpIdx = (int) apvts.getRawParameterValue ("hpf")->load();

    const float lg = apvts.getRawParameterValue ("lowGain")->load();
    const float mg = apvts.getRawParameterValue ("midGain")->load();
    const float hg = apvts.getRawParameterValue ("highGain")->load();
    const float mq = apvts.getRawParameterValue ("midQ")->load() * (1.f + std::abs (mg) * 0.07f);

    const bool dirty = lfIdx != lastLowF || mfIdx != lastMidF || hfIdx != lastHighF
                    || hpIdx != lastHp
                    || lg != lastLowG || mg != lastMidG || hg != lastHighG || mq != lastMidQ;

    if (! dirty)
        return;

    // Stack-allocated arrays assigned into preallocated coefficient objects:
    // IIR::Coefficients::assignImpl only grows its storage, so this is
    // allocation-free once prepareToPlay has created the objects.
    const auto lc = ArrCoeff::makeLowShelf  (processingRate, lowF[lfIdx], 0.707f,
                                             juce::Decibels::decibelsToGain (lg));
    const auto mc = ArrCoeff::makePeakFilter (processingRate, midF[mfIdx], mq,
                                             juce::Decibels::decibelsToGain (mg));
    const auto hc = ArrCoeff::makeHighShelf (processingRate, highF[hfIdx], 0.707f,
                                             juce::Decibels::decibelsToGain (hg));

    for (int ch = 0; ch < 2; ++ch)
    {
        *lowShelf[ch].coefficients  = lc;
        *midPeak[ch].coefficients   = mc;
        *highShelf[ch].coefficients = hc;
    }

    // The HPF switches between first and second order, so it is assigned
    // separately: the two makers return different array sizes. JUCE resizes the
    // filter state on the next block; that only happens when the user moves the
    // HPF switch, never in steady state.
    if (hpIdx > 0)
    {
        const auto pc = ArrCoeff::makeHighPass (processingRate, hpfF[hpIdx]);
        for (int ch = 0; ch < 2; ++ch)
            *highPass[ch].coefficients = pc;
    }
    else
    {
        const auto pc = ArrCoeff::makeFirstOrderHighPass (processingRate, 4.f);
        for (int ch = 0; ch < 2; ++ch)
            *highPass[ch].coefficients = pc;
    }

    lastLowF = lfIdx; lastMidF = mfIdx; lastHighF = hfIdx; lastHp = hpIdx;
    lastLowG = lg;    lastMidG = mg;    lastHighG = hg;    lastMidQ = mq;
}

void Neve1073Processor::prepareToPlay (double sr, int sb)
{
    osm.prepare ((size_t) sb);
    osm.setMode ((eon::QualityMode) (int) apvts.getRawParameterValue ("quality")->load());
    lastQuality = -1; // force coefficient rebuild on first block

    const double osRate = osm.rate (sr);
    juce::dsp::ProcessSpec spec { osRate, (juce::uint32) sb * 32, 2 };
    for (auto& f : highPass)  f.prepare (spec);
    for (auto& f : lowShelf)  f.prepare (spec);
    for (auto& f : midPeak)   f.prepare (spec);
    for (auto& f : highShelf) f.prepare (spec);

    // Preallocate the coefficient objects so the audio-thread refresh only ever
    // writes into existing storage. The initial values are placeholders; the
    // first processBlock overwrites them.
    for (int ch = 0; ch < 2; ++ch)
    {
        highPass[ch].coefficients  = Coeff::makeHighPass   (osRate, 300.f);
        lowShelf[ch].coefficients  = Coeff::makeLowShelf   (osRate, 110.f, 0.707f, 1.f);
        midPeak[ch].coefficients   = Coeff::makePeakFilter (osRate, 1600.f, 1.6f, 1.f);
        highShelf[ch].coefficients = Coeff::makeHighShelf  (osRate, 12000.f, 0.707f, 1.f);
    }
    lastLowF = lastMidF = lastHighF = lastHp = -1;   // force a refresh
    lastLowG = lastMidG = lastHighG = lastMidQ = 0.f;

    // Sized for the largest oversampled block the host can hand us.
    driveRamp.assign ((size_t) sb * 32u, 0.0f);

    prepareDspRate (osRate);

    for (auto& t : inTrans)  t.reset();
    for (auto& t : outTrans) t.reset();
    for (auto& i : inductor) i.reset();
    for (auto& t : triode)   { t.iterations = osm.triodeIterations(); t.reset(); }
    for (auto& c : clip)     c.reset();
    for (auto& a : tanh)     a.reset();
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

void Neve1073Processor::handleAsyncUpdate()
{
    setLatencySamples ((int) osm.latency());
    updateHostDisplay();
}

void Neve1073Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int qIdx = (int) apvts.getRawParameterValue ("quality")->load();
    const int mfIdx = (int) apvts.getRawParameterValue ("midFreq")->load();
    const int hpIdx = (int) apvts.getRawParameterValue ("hpf")->load();
    const bool adaptiveOn = apvts.getRawParameterValue ("adaptive")->load() > 0.5f;
    const bool analogOn   = apvts.getRawParameterValue ("analog")->load() > 0.5f;

    const float mg = apvts.getRawParameterValue ("midGain")->load();
    const float mq = apvts.getRawParameterValue ("midQ")->load() * (1.f + std::abs (mg) * 0.07f);

    driveSm.setTargetValue (apvts.getRawParameterValue ("drive")->load());
    outGainSm.setTargetValue (juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue ("output")->load()));

    const double sr = getSampleRate();
    double osRate = osm.rate (sr);

    // quality mode change -> pick another oversampler, re-derive every
    // rate-dependent constant, push latency async
    if (qIdx != lastQuality)
    {
        osm.setMode ((eon::QualityMode) qIdx);
        osm.current().reset();  // drop stale audio from its delay line
        for (auto& t : triode) t.iterations = osm.triodeIterations();

        osRate = osm.rate (sr);
        prepareDspRate (osRate);

        // Keep the drive ramp continuous across the processing-rate change.
        const float target  = driveSm.getTargetValue();
        const float current = driveSm.getCurrentValue();
        driveSm.reset (osRate, 0.05);
        driveSm.setCurrentAndTargetValue (current);
        driveSm.setTargetValue (target);

        lastQuality = qIdx;
        triggerAsyncUpdate();   // host latency notification off the audio path
    }

    refreshCoefficients (osRate);

    // Adaptive CPU: look at every channel, and keep the chain running for a
    // hold-off window after the last active block so transformer and DA tails
    // finish instead of being chopped.
    const bool runChain = ! adaptiveOn || activity.active (buffer);

    juce::dsp::AudioBlock<float> block (buffer);
    auto osBlock = osm.current().processSamplesUp (block);

    const int numCh = (int) std::min (osBlock.getNumChannels(), (size_t) 2);
    const int N = (int) osBlock.getNumSamples();
    const bool useTriode = osm.useTriode();
    const float indDrive = 0.5f + std::abs (mg) * 0.26f + driveSm.getCurrentValue() * 0.10f;

    // consume the drive ramp once per block — shared by every channel
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
                const float drive = driveRamp[(size_t) i];
                float x = d[i] * (1.f + drive * 0.50f);

                x = inTrans[ch].process (x);                    // JA input transformer
                if (hpIdx != 0) x = highPass[ch].processSample (x);
                x = lowShelf[ch].processSample (x);
                x = tanh[ch].process (x * 0.82f) * 1.05f;       // interstage
                x = inductor[ch].process (x, midF[mfIdx], mq, indDrive);
                x = midPeak[ch].processSample (x);

                if (useTriode)
                    x = triode[ch].process (x * 0.15f) * 0.9f + x * 0.35f;
                else
                    x = clip[ch].process (x * 0.35f) * 1.2f + x * 0.25f;

                x = highShelf[ch].processSample (x);
                x = outTrans[ch].process (x * 0.88f);           // JA output transformer
                if (analogOn) x += air[ch].process();
                x = dc[ch].process (x);
                d[i] = x;
            }
        }
    }

    osm.current().processSamplesDown (block);
    outGainSm.applyGain (buffer, buffer.getNumSamples());
}

void Neve1073Processor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream mos (dest, true);
    apvts.state.writeToStream (mos);
}

void Neve1073Processor::setStateInformation (const void* data, int size)
{
    auto tree = juce::ValueTree::readFromData (data, size);
    if (tree.isValid()) apvts.state = tree;
}

juce::AudioProcessorEditor* Neve1073Processor::createEditor() { return new Neve1073Editor (*this); }

// Both processors export this entry point, so the quality harness (which
// compiles them into one executable) defines EON_QUALITY_HARNESS to skip it.
#ifndef EON_QUALITY_HARNESS
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Neve1073Processor(); }
#endif
