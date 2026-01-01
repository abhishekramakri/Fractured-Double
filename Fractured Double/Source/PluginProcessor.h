#pragma once

#include <JuceHeader.h>
#include <vector>

class FracturedDoubleAudioProcessor : public juce::AudioProcessor
{
public:
    FracturedDoubleAudioProcessor();
    ~FracturedDoubleAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;

private:
    // grain settings - might make these adjustable later
    float grainSizeMs = 40.0f;     
    float hopRatio = 0.5f;      
    float fixedSemiA = +1.2f;      // up pitch
    float fixedSemiB = -1.2f;      // down pitch  
    float dryDelayMs = 20.0f;      // delay dry signal to match grains

    double sr = 44100.0;
    int bufferSizeSamples = 0;

    juce::AudioBuffer<float> circularBuffer; // secondary buffer
    int writePos = 0;                       

    // each grain is a little chunk of audio
    struct Grain 
    {
        juce::AudioBuffer<float> data;
        int length = 0;               
        float rate = 1.0f;             // playback speed
        float gain = 1.0f;            
        float pos = 0.0f;              // where we are in the grain
        bool finished = false;        
    };

    std::vector<Grain> activeGrains;  
    juce::Random rng;                 

    // when to spawn the next grain
    int triggerCounter = 0;           
    int nextTriggerInSamples = 0;     
    int baseHopSamples = 0;           
    int grainSamples = 0;             

    static float semiToRate(float semi);
    static float hannAt(int n, int N);
    void spawnGrain(float pitchDepthSemi, float jitterMs);
    void scheduleNextTrigger();       
};
