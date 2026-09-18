#pragma once
#include <JuceHeader.h>

// Parameter-level text formatting. SliderParameterAttachment sets
// slider.textFromValueFunction = param.getText(...), so decimals/suffix
// must live on the AudioParameterFloat itself — hosts see the same text.

namespace eonparam {

inline std::function<juce::String (float, int)> fmt (int decimals, const char* suffix = "")
{
    const juce::String s (suffix);
    return [decimals, s] (float v, int) { return juce::String (v, decimals) + s; };
}

inline juce::String hz (float v, int)
{
    return v >= 1000.f ? juce::String (v / 1000.f, 1) + " kHz"
                       : juce::String (juce::roundToInt (v)) + " Hz";
}

} // namespace eonparam
