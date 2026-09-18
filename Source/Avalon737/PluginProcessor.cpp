#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("hp",          "HPF",
        juce::StringArray { "Off", "30", "60", "100" }, 0));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("lowFreq",     "Low Freq",
        juce::StringArray { "15", "30", "60", "150" }, 2));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("lowGain",     "Low Gain",
        juce::NormalisableRange<float> (-24.f, 24.f), 0.f));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("lowMidFreq",  "LowMid Freq",
        juce::NormalisableRange<float> (30.f, 450.f, 1.f, 0.35f), 200.f));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("lowMidGain",  "LowMid Gain",
        juce::NormalisableRange<float> (-24.f, 24.f), 0.f));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("highMidFreq", "HighMid Freq",
        juce::NormalisableRange<float> (200.f, 2800.f, 1.f, 0.35f), 1000.f));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("highMidGain", "HighMid Gain",
        juce::NormalisableRange<float> (-24.f, 24.f), 0.f));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("highFreq",    "High Freq",
        juce::StringArray { "10k", "15k", "20k", "32k" }, 1));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("highGain",    "High Gain",
        juce::NormalisableRange<float> (0.f, 24.f), 0.f));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("output",      "Output",
        juce::NormalisableRange<float> (-18.f, 18.f), 0.f));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("drive",       "Drive",
        juce::NormalisableRange<float> (0.f, 10.f), 2.f));
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

void Avalon737Processor::prepareToPlay (double sr, int sb)
{
    osm.prepare ((size_t) sb);
    osm.setMode ((eon::QualityMode) (int) apvts.getRawParameterValue ("quality")->load());
    lastQuality = -1;

    juce::dsp::ProcessSpec spec { osm.rate (sr), (juce::uint32) sb * 32, 2 };
    for (auto& f : highPass)  f.prepare (spec);
    for (auto& f : lowShelf)  f.prepare (spec);
    for (auto& f : lowMid)    f.prepare (spec);
    for (auto& f : highMid)   f.prepare (spec);
    for (auto& f : highShelf) f.prepare (spec);

    for (auto& a : classA) a.reset();
    for (auto& a : meltA)  a.reset();
    for (auto& t : triode) { t.iterations = osm.triodeIterations(); t.reset(); }
    for (auto& c : clip)   c.reset();
    for (auto& a : tanh)   a.reset();
    for (auto& d : dc)     d.reset();
    for (auto& a : air)    a.reset();

    driveSm.reset (sr, 0.05);
    outGainSm.reset (sr, 0.02);
    driveSm.setCurrentAndTargetValue (apvts.getRawParameterValue ("drive")->load());
    outGainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue ("output")->load()));

    silentEnv = 0.0;
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

    static constexpr std::array<float, 4> hpF   { 5.f, 30.f, 60.f, 100.f };
    static constexpr std::array<float, 4> lowF  { 15.f, 30.f, 60.f, 150.f };
    static constexpr std::array<float, 4> highF { 10000.f, 15000.f, 20000.f, 32000.f };

    const int hpIdx = (int) apvts.getRawParameterValue ("hp")->load();
    const int lfIdx = (int) apvts.getRawParameterValue ("lowFreq")->load();
    const int hfIdx = (int) apvts.getRawParameterValue ("highFreq")->load();
    const int qIdx  = (int) apvts.getRawParameterValue ("quality")->load();
    const bool adaptiveOn = apvts.getRawParameterValue ("adaptive")->load() > 0.5f;
    const bool analogOn   = apvts.getRawParameterValue ("analog")->load() > 0.5f;

    const float lg  = apvts.getRawParameterValue ("lowGain")->load();
    const float lmg = apvts.getRawParameterValue ("lowMidGain")->load();
    const float hmg = apvts.getRawParameterValue ("highMidGain")->load();
    const float hg  = apvts.getRawParameterValue ("highGain")->load();
    const float lmf = apvts.getRawParameterValue ("lowMidFreq")->load();
    const float hmf = apvts.getRawParameterValue ("highMidFreq")->load();

    driveSm.setTargetValue (apvts.getRawParameterValue ("drive")->load());
    outGainSm.setTargetValue (juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue ("output")->load()));

    if (qIdx != lastQuality)
    {
        osm.setMode ((eon::QualityMode) qIdx);
        osm.current().reset();  // drop stale audio from its delay line
        for (auto& t : triode) t.iterations = osm.triodeIterations();
        lastQuality = qIdx;
        triggerAsyncUpdate();
    }

    const double osRate = osm.rate (getSampleRate());

    {
        const auto pc = Coeff::makeHighPass   (osRate, hpF[hpIdx]);
        const auto lc = Coeff::makeLowShelf   (osRate, lowF[lfIdx], 0.68f, juce::Decibels::decibelsToGain (lg));
        const auto lmc = Coeff::makePeakFilter (osRate, lmf, 2.6f, juce::Decibels::decibelsToGain (lmg));
        const auto hmc = Coeff::makePeakFilter (osRate, hmf, 2.6f, juce::Decibels::decibelsToGain (hmg));
        const auto hc = Coeff::makeHighShelf  (osRate, highF[hfIdx], 0.60f, juce::Decibels::decibelsToGain (hg));
        for (auto& f : highPass)  f.coefficients = pc;
        for (auto& f : lowShelf)  f.coefficients = lc;
        for (auto& f : lowMid)    f.coefficients = lmc;
        for (auto& f : highMid)   f.coefficients = hmc;
        for (auto& f : highShelf) f.coefficients = hc;
    }

    const float blockRMS = buffer.getRMSLevel (0, 0, buffer.getNumSamples());
    silentEnv = silentEnv * 0.9 + blockRMS * 0.1;
    const bool skipNonlinear = adaptiveOn && silentEnv < 1e-6;

    juce::dsp::AudioBlock<float> block (buffer);
    auto osBlock = osm.current().processSamplesUp (block);

    const int numCh = (int) std::min (osBlock.getNumChannels(), (size_t) 2);
    const int N = (int) osBlock.getNumSamples();
    const bool useTriode = osm.useTriode();
    const bool godPlus = qIdx == 3;
    const bool air32k = hfIdx == 3 && hg > 0.05f;

    auto drives = std::vector<float> ((size_t) N);
    for (int i = 0; i < N; ++i) drives[i] = driveSm.getNextValue();

    if (! skipNonlinear)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = osBlock.getChannelPointer (ch);
            for (int i = 0; i < N; ++i)
            {
                float x = d[i] * (1.f + drives[i] * 0.11f);

                x = tanh[ch].process (x * 0.46f) * 2.17f;
                x = classA[ch].process (x);                 // Class-A + DA memory + sag/thermal
                if (hpIdx != 0) x = highPass[ch].processSample (x);

                x = lowShelf[ch].processSample (x);
                if (useTriode)
                    x = triode[ch].process (x * 0.10f) * 0.18f + x * 0.82f;

                x = lowMid[ch].processSample (x);
                if (godPlus)
                    x = triode[ch].process (x * 0.14f) * 6.4f + x * 0.08f;
                else
                    x = tanh[ch].process (x * 0.09f) * 10.1f + x * 0.08f;

                x = highMid[ch].processSample (x);
                x = highShelf[ch].processSample (x);

                if (air32k)
                {
                    // 32k "liquid air": separate Class-A melt path
                    float melt = tanh[ch].process (x * (0.78f + hg * 0.03f));
                    melt = meltA[ch].process (melt * 0.55f) * 1.8f;
                    if (godPlus) melt = triode[ch].process (melt * 0.35f) * 1.2f + melt * 0.4f;
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

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Avalon737Processor(); }
