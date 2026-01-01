#include "PluginEditor.h"

// basic setup
FracturedDoubleAudioProcessorEditor::FracturedDoubleAudioProcessorEditor(FracturedDoubleAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    // pitch depth
    addAndMakeVisible(pitchDepthSlider);
    pitchDepthSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    pitchDepthSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    pitchDepthLabel.setText("Pitch Depth", juce::dontSendNotification);
    pitchDepthLabel.attachToComponent(&pitchDepthSlider, false);
    addAndMakeVisible(pitchDepthLabel);

    // jitter
    addAndMakeVisible(jitterSlider);
    jitterSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    jitterSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    jitterLabel.setText("Jitter (ms)", juce::dontSendNotification);
    jitterLabel.attachToComponent(&jitterSlider, false);
    addAndMakeVisible(jitterLabel);

    // wet mix
    addAndMakeVisible(wetMixSlider);
    wetMixSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    wetMixSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    wetMixLabel.setText("Wet Mix", juce::dontSendNotification);
    wetMixLabel.attachToComponent(&wetMixSlider, false);
    addAndMakeVisible(wetMixLabel);

    // connect to processor
    pitchDepthAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, "pitchDepth", pitchDepthSlider);
    jitterAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, "jitterMs", jitterSlider);
    wetMixAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, "wetMix", wetMixSlider);

    // window size
    setSize(400, 200);
}

FracturedDoubleAudioProcessorEditor::~FracturedDoubleAudioProcessorEditor() {}

// draw UI
void FracturedDoubleAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colours::white);
    g.setFont(22.0f);
    g.drawFittedText("Fractured Double", getLocalBounds().removeFromTop(40), juce::Justification::centred, 1);
}

// layout
void FracturedDoubleAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(20);
    area.removeFromTop(50);

    auto sliderArea = area.removeFromBottom(130);
    auto sliderWidth = sliderArea.getWidth() / 3;

    pitchDepthSlider.setBounds(sliderArea.removeFromLeft(sliderWidth).reduced(10));
    jitterSlider.setBounds(sliderArea.removeFromLeft(sliderWidth).reduced(10));
    wetMixSlider.setBounds(sliderArea.removeFromLeft(sliderWidth).reduced(10));
}
