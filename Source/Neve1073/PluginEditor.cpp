#include "PluginEditor.h"
#include "PluginProcessor.h"

Neve1073Editor::Neve1073Editor (Neve1073Processor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    setSize (820, 400);

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

    // band row: freq combo above each gain knob
    addCombo ("lowFreq", 30, 60);  addKnob ("lowGain",  "LOW",   30, 90);
    addCombo ("midFreq", 160, 60); addKnob ("midGain",  "MID",  160, 90);
                                   addKnob ("midQ",     "MID Q", 270, 90);
    addCombo ("highFreq", 380, 60); addKnob ("highGain", "HIGH", 380, 90);
    addCombo ("hpf", 510, 60, 100);
    addKnob ("drive",  "DRIVE",  510, 90);
    addKnob ("output", "OUTPUT", 640, 90);

    addCombo ("quality", 30, 220, 130);
    addToggle ("analog",  "Analog", 180, 220);
    addToggle ("adaptive", "Adaptive CPU", 300, 220, 140);
}

Neve1073Editor::~Neve1073Editor() { setLookAndFeel (nullptr); }

void Neve1073Editor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xFF121212));
    g.setColour (juce::Colour (0xFF00FFCC));
    g.fillRect (0, 0, getWidth(), 4);
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (22.f, juce::Font::bold)));
    g.drawText ("EON AUDIO  |  1073 GOD", 20, 8, 400, 24, juce::Justification::left);
    g.setFont (juce::Font (juce::FontOptions (12.f)));
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawText ("JILES-ATHERTON • KOREN NR TRIODE • ADAA2 • 8/16/32x", 20, 32, 500, 12, juce::Justification::left);
}
