#include "PluginEditor.h"
#include "PluginProcessor.h"

Avalon737Editor::Avalon737Editor (Avalon737Processor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    setSize (860, 400);

    auto addKnob = [&] (const juce::String& id, const juce::String& name, int x, int y) {
        auto s = std::make_unique<juce::Slider>();
        s->setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
        s->setBounds (x, y, 90, 90);
        addAndMakeVisible (*s);
        auto l = std::make_unique<juce::Label>();
        l->setText (name, juce::dontSendNotification);
        l->setJustificationType (juce::Justification::centred);
        l->setBounds (x, y + 88, 90, 18);
        addAndMakeVisible (*l);
        sliderAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, *s));
        sliders.push_back (std::move (s));
        labels.push_back (std::move (l));
    };
    auto addCombo = [&] (const juce::String& id, int x, int y, int w = 90) {
        auto c = std::make_unique<juce::ComboBox>();
        c->setBounds (x, y, w, 24);
        addAndMakeVisible (*c);
        comboAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, *c));
        combos.push_back (std::move (c));
    };
    auto addToggle = [&] (const juce::String& id, const juce::String& name, int x, int y, int w = 110) {
        auto t = std::make_unique<juce::ToggleButton> (name);
        t->setBounds (x, y, w, 24);
        addAndMakeVisible (*t);
        buttonAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, *t));
        toggles.push_back (std::move (t));
    };

    addCombo ("hp", 30, 60, 80);
    addCombo ("lowFreq", 30, 140);  addKnob ("lowGain",     "LOW",      30, 170);
    addKnob ("lowMidFreq",  "LM FREQ",  160, 170);
    addKnob ("lowMidGain",  "LM GAIN",  270, 170);
    addKnob ("highMidFreq", "HM FREQ",  380, 170);
    addKnob ("highMidGain", "HM GAIN",  490, 170);
    addCombo ("highFreq", 600, 140);  addKnob ("highGain",    "HIGH",     600, 170);
    addKnob ("drive",   "DRIVE",  30, 290);
    addKnob ("output",  "OUTPUT", 160, 290);

    addCombo ("quality", 300, 300, 130);
    addToggle ("analog",   "Analog",       450, 300);
    addToggle ("adaptive", "Adaptive CPU", 570, 300, 140);
}

Avalon737Editor::~Avalon737Editor() { setLookAndFeel (nullptr); }

void Avalon737Editor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xFF12161A));
    g.setColour (juce::Colour (0xFF7FB4FF));
    g.fillRect (0, 0, getWidth(), 4);
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (22.f, juce::Font::bold)));
    g.drawText ("EON AUDIO  |  737 GOD", 20, 8, 400, 24, juce::Justification::left);
    g.setFont (juce::Font (juce::FontOptions (12.f)));
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawText ("CLASS-A • DA MEMORY • SAG+THERMAL • 32k LIQUID AIR", 20, 32, 500, 12, juce::Justification::left);
}
