#include "PluginEditor.h"
#include "PluginProcessor.h"

Neve1073Editor::Neve1073Editor (Neve1073Processor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    setSize (720, 300);

    // band row: freq combo (captioned) above each gain knob
    eonui::addCombo  (*this, proc.apvts, cs, "lowFreq",  "LOW FREQ",   40, 62);
    eonui::addKnob   (*this, proc.apvts, cs, "lowGain",  "LOW",    40, 92);
    eonui::addCombo  (*this, proc.apvts, cs, "midFreq",  "MID FREQ",  150, 62);
    eonui::addKnob   (*this, proc.apvts, cs, "midGain",  "MID",    150, 92);
    eonui::addKnob   (*this, proc.apvts, cs, "midQ",     "MID Q",  260, 92);
    eonui::addCombo  (*this, proc.apvts, cs, "highFreq", "HIGH FREQ", 370, 62);
    eonui::addKnob   (*this, proc.apvts, cs, "highGain", "HIGH",   370, 92);
    eonui::addKnob   (*this, proc.apvts, cs, "drive",    "DRIVE",  480, 92);
    eonui::addKnob   (*this, proc.apvts, cs, "output",   "OUTPUT", 590, 92);

    // bottom row: utility controls
    eonui::addCombo  (*this, proc.apvts, cs, "hpf",     "HPF",      40, 238, 100);
    eonui::addCombo  (*this, proc.apvts, cs, "quality", "QUALITY",  170, 238, 140);
    eonui::addToggle (*this, proc.apvts, cs, "analog",   "Analog",      340, 238);
    eonui::addToggle (*this, proc.apvts, cs, "adaptive", "Adaptive CPU", 460, 238, 140);
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
    g.setFont (juce::Font (juce::FontOptions (11.f)));
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawText ("JILES-ATHERTON  |  KOREN NR TRIODE  |  ADAA2  |  8/16/32x",
                20, 32, 500, 14, juce::Justification::left);
}
