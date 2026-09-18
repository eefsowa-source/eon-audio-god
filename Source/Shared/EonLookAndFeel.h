#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

struct EonLookAndFeel : public juce::LookAndFeel_V4
{
    EonLookAndFeel()
    {
        setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xFF00FFCC));
        setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xFF1A1A1A));
        setColour (juce::Slider::thumbColourId, juce::Colour (0xFFFFFFFF));
        setColour (juce::Label::textColourId, juce::Colour (0xFFEAEAEA));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float start, float end, juce::Slider&) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6);
        auto r  = bounds.getWidth() * 0.5f;
        auto cx = bounds.getCentreX();
        auto cy = bounds.getCentreY();

        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillEllipse (cx - r + 2, cy - r + 2, r * 2, r * 2);

        g.setColour (findColour (juce::Slider::rotarySliderOutlineColourId));
        g.fillEllipse (cx - r, cy - r, r * 2, r * 2);
        g.setColour (juce::Colour (0xFF2A2A2A));
        g.drawEllipse (cx - r, cy - r, r * 2, r * 2, 1.5f);

        g.setColour (findColour (juce::Slider::rotarySliderFillColourId).withAlpha (0.9f));
        juce::Path p;
        p.addArc (cx - r + 8, cy - r + 8, (r - 8) * 2, (r - 8) * 2,
                  start, start + (end - start) * pos, true);
        g.strokePath (p, juce::PathStrokeType (3.f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

        auto angle = start + pos * (end - start);
        auto tx = cx + (r - 18) * std::cos (angle - juce::MathConstants<float>::halfPi);
        auto ty = cy + (r - 18) * std::sin (angle - juce::MathConstants<float>::halfPi);
        g.setColour (juce::Colours::white);
        g.fillEllipse (tx - 4, ty - 4, 8, 8);
    }
};
