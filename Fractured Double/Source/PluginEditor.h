#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// main UI editor
class FracturedDoubleAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    FracturedDoubleAudioProcessorEditor(FracturedDoubleAudioProcessor&);
    ~FracturedDoubleAudioProcessorEditor() override;

    // draw + layout
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    FracturedDoubleAudioProcessor& audioProcessor;

    // sliders
    juce::Slider pitchDepthSlider;
    juce::Slider jitterSlider;
    juce::Slider wetMixSlider;

    // labels
    juce::Label pitchDepthLabel;
    juce::Label jitterLabel;
    juce::Label wetMixLabel;

    // attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitchDepthAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> jitterAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wetMixAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FracturedDoubleAudioProcessorEditor)
};
