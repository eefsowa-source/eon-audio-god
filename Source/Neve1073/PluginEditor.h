#pragma once
#include <JuceHeader.h>
#include "EonLookAndFeel.h"
#include "EonEditorUtils.h"

class Neve1073Processor;

class Neve1073Editor : public juce::AudioProcessorEditor
{
public:
    explicit Neve1073Editor (Neve1073Processor&);
    ~Neve1073Editor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}

private:
    Neve1073Processor& proc;
    EonLookAndFeel lnf;
    eonui::ControlSet cs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Neve1073Editor)
};
