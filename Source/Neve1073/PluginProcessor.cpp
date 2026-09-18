#include "PluginProcessor.h"
#include "PluginEditor.h"

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
}

juce::AudioProcessorValueTreeState::ParameterLayout Neve1073Processor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("lowFreq",  "Low Freq",
        juce::StringArray { "35", "60", "110", "220" }, 1));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("lowGain",  "Low Gain",
        juce::NormalisableRange<float> (-24.f, 24.f), 0.f));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("midFreq",  "Mid Freq",
        juce::StringArray { "360", "700", "1600", "3200", "4800", "7200" }, 2));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("midGain",  "Mid Gain",
        juce::NormalisableRange<float> (-24.f, 24.f), 0.f));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("midQ",     "Mid Q",
        juce::NormalisableRange<float> (0.3f, 5.f), 1.6f));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("highFreq", "High Freq",
        juce::StringArray { "10k", "12k", "16k" }, 1));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("highGain", "High Gain",
        juce::NormalisableRange<float> (-24.f, 24.f), 0.f));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("hpf",      "HPF",
        juce::StringArray { "Off", "50", "80", "160", "300" }, 0));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("output",   "Output",
        juce::NormalisableRange<float> (-24.f, 24.f), 0.f));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  ("drive",    "Drive",
        juce::NormalisableRange<float> (0.f, 10.f), 3.5f));
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

void Neve1073Processor::prepareToPlay (double sr, int sb)
{
    osm.prepare ((size_t) sb);
    osm.setMode ((eon::QualityMode) (int) apvts.getRawParameterValue ("quality")->load());
    lastQuality = -1; // force coefficient rebuild on first block

    juce::dsp::ProcessSpec spec { osm.rate (sr), (juce::uint32) sb * 32, 2 };
    for (auto& f : highPass)  f.prepare (spec);
    for (auto& f : lowShelf)  f.prepare (spec);
    for (auto& f : midPeak)   f.prepare (spec);
    for (auto& f : highShelf) f.prepare (spec);

    for (auto& t : inTrans)  t.reset();
    for (auto& t : outTrans) t.reset();
    for (auto& i : inductor) i.reset();
    for (auto& t : triode)   { t.iterations = osm.triodeIterations(); t.reset(); }
    for (auto& c : clip)     c.reset();
    for (auto& a : tanh)     a.reset();
    for (auto& d : dc)       d.reset();
    for (auto& a : air)      a.reset();

    driveSm.reset (sr, 0.05);
    outGainSm.reset (sr, 0.02);
    driveSm.setCurrentAndTargetValue (apvts.getRawParameterValue ("drive")->load());
    outGainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue ("output")->load()));

    silentEnv = 0.0;
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

    static constexpr std::array<float, 4> lowF  { 35.f, 60.f, 110.f, 220.f };
    static constexpr std::array<float, 6> midF  { 360.f, 700.f, 1600.f, 3200.f, 4800.f, 7200.f };
    static constexpr std::array<float, 3> highF { 10000.f, 12000.f, 16000.f };
    static constexpr std::array<float, 5> hpfF  { 0.f, 50.f, 80.f, 160.f, 300.f };

    const int lfIdx = (int) apvts.getRawParameterValue ("lowFreq")->load();
    const int mfIdx = (int) apvts.getRawParameterValue ("midFreq")->load();
    const int hfIdx = (int) apvts.getRawParameterValue ("highFreq")->load();
    const int hpIdx = (int) apvts.getRawParameterValue ("hpf")->load();
    const int qIdx  = (int) apvts.getRawParameterValue ("quality")->load();
    const bool adaptiveOn = apvts.getRawParameterValue ("adaptive")->load() > 0.5f;
    const bool analogOn   = apvts.getRawParameterValue ("analog")->load() > 0.5f;

    const float lg = apvts.getRawParameterValue ("lowGain")->load();
    const float mg = apvts.getRawParameterValue ("midGain")->load();
    const float hg = apvts.getRawParameterValue ("highGain")->load();
    const float mq = apvts.getRawParameterValue ("midQ")->load() * (1.f + std::abs (mg) * 0.07f);

    driveSm.setTargetValue (apvts.getRawParameterValue ("drive")->load());
    outGainSm.setTargetValue (juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue ("output")->load()));

    // quality mode change -> pick another oversampler, push latency async
    if (qIdx != lastQuality)
    {
        osm.setMode ((eon::QualityMode) qIdx);
        osm.current().reset();  // drop stale audio from its delay line
        for (auto& t : triode) t.iterations = osm.triodeIterations();
        lastQuality = qIdx;
        triggerAsyncUpdate();   // host latency notification off the audio path
    }

    const double osRate = osm.rate (getSampleRate());

    {
        const auto lc = Coeff::makeLowShelf  (osRate, lowF[lfIdx], 0.707f, juce::Decibels::decibelsToGain (lg));
        const auto mc = Coeff::makePeakFilter (osRate, midF[mfIdx], mq,     juce::Decibels::decibelsToGain (mg));
        const auto hc = Coeff::makeHighShelf (osRate, highF[hfIdx], 0.707f, juce::Decibels::decibelsToGain (hg));
        const auto pc = hpIdx > 0 ? Coeff::makeHighPass (osRate, hpfF[hpIdx])
                                  : Coeff::makeFirstOrderHighPass (osRate, 4.f);
        for (auto& f : lowShelf)  f.coefficients = lc;
        for (auto& f : midPeak)   f.coefficients = mc;
        for (auto& f : highShelf) f.coefficients = hc;
        for (auto& f : highPass)  f.coefficients = pc;
    }

    // adaptive: track input level; sustained silence skips the nonlinear loop
    const float blockRMS = buffer.getRMSLevel (0, 0, buffer.getNumSamples());
    silentEnv = silentEnv * 0.9 + blockRMS * 0.1;
    const bool skipNonlinear = adaptiveOn && silentEnv < 1e-6;

    juce::dsp::AudioBlock<float> block (buffer);
    auto osBlock = osm.current().processSamplesUp (block);

    const int numCh = (int) std::min (osBlock.getNumChannels(), (size_t) 2);
    const int N = (int) osBlock.getNumSamples();
    const bool useTriode = osm.useTriode();
    const float indDrive = 1.f + std::abs (mg) * 0.26f + driveSm.getCurrentValue() * 0.10f;

    // consume the drive ramp once per block — shared by every channel
    auto drives = std::vector<float> ((size_t) N);
    for (int i = 0; i < N; ++i) drives[i] = driveSm.getNextValue();

    if (! skipNonlinear)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = osBlock.getChannelPointer (ch);
            for (int i = 0; i < N; ++i)
            {
                const float drive = drives[i];
                float x = d[i] * (1.f + drive * 0.20f);

                x = inTrans[ch].process (x);                    // JA input transformer
                if (hpIdx != 0) x = highPass[ch].processSample (x);
                x = lowShelf[ch].processSample (x);
                x = tanh[ch].process (x * 0.82f) * 1.05f;       // interstage
                x = inductor[ch].process (x, midF[mfIdx], mq, indDrive);
                x = midPeak[ch].processSample (x);

                if (useTriode)
                    x = triode[ch].process (x * 0.45f) * 0.9f + x * 0.35f;
                else
                    x = clip[ch].process (x * 0.7f) * 1.2f + x * 0.25f;

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

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Neve1073Processor(); }
