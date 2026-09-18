#pragma once
#include <JuceHeader.h>

// Shared editor plumbing: knobs / captioned combos / toggles bound to APVTS.
// ComboBoxParameterAttachment does NOT populate items — we must add the
// AudioParameterChoice's choices ourselves before attaching.

namespace eonui {

struct ControlSet
{
    std::vector<std::unique_ptr<juce::Slider>>      sliders;
    std::vector<std::unique_ptr<juce::Label>>       labels;
    std::vector<std::unique_ptr<juce::ComboBox>>    combos;
    std::vector<std::unique_ptr<juce::ToggleButton>> toggles;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAtt;
};

inline juce::Label* caption (juce::Component& parent, ControlSet& cs,
                             const juce::String& text, juce::Rectangle<int> r)
{
    auto l = std::make_unique<juce::Label>();
    l->setText (text, juce::dontSendNotification);
    l->setJustificationType (juce::Justification::centredLeft);
    l->setBounds (r);
    l->setFont (juce::Font (juce::FontOptions (10.5f)));
    l->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
    parent.addAndMakeVisible (*l);
    auto* p = l.get();
    cs.labels.push_back (std::move (l));
    return p;
}

// NOTE: value text comes from the parameter's stringFromValue function
// (SliderParameterAttachment overrides textFromValueFunction) — format in
// createLayout via ParamFormat.h, not here.
inline void addKnob (juce::Component& parent, juce::AudioProcessorValueTreeState& apvts,
                     ControlSet& cs, const juce::String& id, const juce::String& name,
                     int x, int y)
{
    auto s = std::make_unique<juce::Slider>();
    s->setSliderStyle (juce::Slider::RotaryVerticalDrag);
    s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    s->setBounds (x, y, 90, 90);
    parent.addAndMakeVisible (*s);

    auto l = std::make_unique<juce::Label>();
    l->setText (name, juce::dontSendNotification);
    l->setJustificationType (juce::Justification::centred);
    l->setBounds (x, y + 92, 90, 16);
    parent.addAndMakeVisible (*l);

    cs.sliderAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, *s));
    cs.sliders.push_back (std::move (s));
    cs.labels.push_back (std::move (l));
}

// Combo with a small caption above it. Items are pulled from the
// AudioParameterChoice — attach AFTER populating so the initial index maps.
inline void addCombo (juce::Component& parent, juce::AudioProcessorValueTreeState& apvts,
                      ControlSet& cs, const juce::String& id, const juce::String& name,
                      int x, int y, int w = 90)
{
    caption (parent, cs, name, { x, y - 15, w, 14 });

    auto c = std::make_unique<juce::ComboBox>();
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
        c->addItemList (choice->choices, 1);
    c->setBounds (x, y, w, 24);
    parent.addAndMakeVisible (*c);

    cs.comboAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, id, *c));
    cs.combos.push_back (std::move (c));
}

inline void addToggle (juce::Component& parent, juce::AudioProcessorValueTreeState& apvts,
                       ControlSet& cs, const juce::String& id, const juce::String& name,
                       int x, int y, int w = 110)
{
    auto t = std::make_unique<juce::ToggleButton> (name);
    t->setBounds (x, y, w, 24);
    parent.addAndMakeVisible (*t);
    cs.buttonAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, id, *t));
    cs.toggles.push_back (std::move (t));
}

} // namespace eonui
