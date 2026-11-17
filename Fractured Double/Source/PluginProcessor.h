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
    // parameter limits - also used to size the grain pool for the worst case
    static constexpr float maxPitchDepthSemi = 12.0f;
    static constexpr float maxJitterMs = 100.0f;

    // grain settings - might make these adjustable later
    float grainSizeMs = 40.0f;
    float hopRatio = 0.5f;
    float fixedSemiA = +1.2f;      // up pitch
    float fixedSemiB = -1.2f;      // down pitch
    float dryDelayMs = 20.0f;      // delay dry signal to match grains
    static constexpr float hopJitterFrac = 0.30f; // +/- 30% randomness on grain spacing

    double sr = 44100.0;
    int bufferSizeSamples = 0;

    juce::AudioBuffer<float> circularBuffer; // secondary buffer
    int writePos = 0;

    // each grain is a read head into the circular buffer - no audio is copied,
    // so spawning a grain never allocates on the audio thread
    struct Grain
    {
        int readStart = 0;             // where the grain starts in the circular buffer
        float rate = 1.0f;             // playback speed
        float gain = 1.0f;
        float pos = 0.0f;              // where we are in the grain
        bool active = false;           // free slot when false
    };

    std::vector<Grain> grainPool;      // fixed size, allocated in prepareToPlay
    std::vector<float> hannWindow;     // precomputed in prepareToPlay
    juce::Random rng;

    // when to spawn the next grain
    int triggerCounter = 0;
    int nextTriggerInSamples = 0;
    int baseHopSamples = 0;
    int grainSamples = 0;

    static float semiToRate(float semi);
    static float hannAt(int n, int N);
    int wrapIndex(int idx) const;
    Grain* findFreeGrain();
    void spawnGrain(int currentPos, float pitchDepthSemi, float jitterMs);
    void scheduleNextTrigger();
};
