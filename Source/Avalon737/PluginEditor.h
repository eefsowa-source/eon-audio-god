#pragma once
#include <JuceHeader.h>
#include "EonLookAndFeel.h"

class Avalon737Processor;

class Avalon737Editor : public juce::AudioProcessorEditor
{
public:
    explicit Avalon737Editor (Avalon737Processor&);
    ~Avalon737Editor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}

private:
    Avalon737Processor& proc;
    EonLookAndFeel lnf;

    std::vector<std::unique_ptr<juce::Slider>> sliders;
    std::vector<std::unique_ptr<juce::Label>> labels;
    std::vector<std::unique_ptr<juce::ComboBox>> combos;
    std::vector<std::unique_ptr<juce::ToggleButton>> toggles;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Avalon737Editor)
};
