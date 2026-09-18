#include "PluginEditor.h"
#include "PluginProcessor.h"

Avalon737Editor::Avalon737Editor (Avalon737Processor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    setSize (920, 300);

    // band row: combos above their knobs where they exist
    eonui::addCombo  (*this, proc.apvts, cs, "lowFreq",     "LOW FREQ",   30, 62);
    eonui::addKnob   (*this, proc.apvts, cs, "lowGain",     "LOW",        30, 92);
    eonui::addKnob   (*this, proc.apvts, cs, "lowMidFreq",  "LM FREQ",   140, 92);
    eonui::addKnob   (*this, proc.apvts, cs, "lowMidGain",  "LM GAIN",   250, 92);
    eonui::addKnob   (*this, proc.apvts, cs, "highMidFreq", "HM FREQ",   360, 92);
    eonui::addKnob   (*this, proc.apvts, cs, "highMidGain", "HM GAIN",   470, 92);
    eonui::addCombo  (*this, proc.apvts, cs, "highFreq",    "HIGH FREQ", 580, 62);
    eonui::addKnob   (*this, proc.apvts, cs, "highGain",    "HIGH",      580, 92);
    eonui::addKnob   (*this, proc.apvts, cs, "drive",       "DRIVE",     690, 92);
    eonui::addKnob   (*this, proc.apvts, cs, "output",      "OUTPUT",    800, 92);

    // bottom row
    eonui::addCombo  (*this, proc.apvts, cs, "hp",      "HPF",      30, 238, 90);
    eonui::addCombo  (*this, proc.apvts, cs, "quality", "QUALITY",  160, 238, 140);
    eonui::addToggle (*this, proc.apvts, cs, "analog",   "Analog",      330, 238);
    eonui::addToggle (*this, proc.apvts, cs, "adaptive", "Adaptive CPU", 450, 238, 140);
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
    g.setFont (juce::Font (juce::FontOptions (11.f)));
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawText ("CLASS-A  |  DA MEMORY  |  SAG + THERMAL  |  32k LIQUID AIR",
                20, 32, 500, 14, juce::Justification::left);
}
