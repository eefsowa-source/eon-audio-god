#pragma once
#include <JuceHeader.h>
#include "EonLookAndFeel.h"
#include "EonEditorUtils.h"

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
    eonui::ControlSet cs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Avalon737Editor)
};
