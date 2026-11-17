#include "PluginProcessor.h"
#include "PluginEditor.h"

FracturedDoubleAudioProcessor::FracturedDoubleAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    parameters(*this, nullptr, "PARAMETERS",
        {
            std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"pitchDepth", 1}, "Pitch Depth", 0.0f, maxPitchDepthSemi, 3.0f),
            std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"jitterMs",   1}, "Jitter (ms)",  0.0f, maxJitterMs,       30.0f),
            std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"wetMix",     1}, "Wet Mix",      0.0f, 1.0f,   0.4f)
        })
{
}

FracturedDoubleAudioProcessor::~FracturedDoubleAudioProcessor() {}

// basic plugin info
const juce::String FracturedDoubleAudioProcessor::getName() const { return "FracturedDouble"; }
bool FracturedDoubleAudioProcessor::acceptsMidi() const { return false; }
bool FracturedDoubleAudioProcessor::producesMidi() const { return false; }
bool FracturedDoubleAudioProcessor::isMidiEffect() const { return false; }
double FracturedDoubleAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int FracturedDoubleAudioProcessor::getNumPrograms() { return 1; }
int FracturedDoubleAudioProcessor::getCurrentProgram() { return 0; }
void FracturedDoubleAudioProcessor::setCurrentProgram(int) {}
const juce::String FracturedDoubleAudioProcessor::getProgramName(int) { return {}; }
void FracturedDoubleAudioProcessor::changeProgramName(int, const juce::String&) {}

void FracturedDoubleAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    bufferSizeSamples = (int)juce::jmax(1.0, std::floor(sr * 2.0)); // 2 second buffer

    circularBuffer.setSize(2, bufferSizeSamples);
    circularBuffer.clear();
    writePos = 0;

    // figure out grain and hop sizes
    grainSamples = (int)juce::jmax(5.0, std::floor(grainSizeMs * sr / 1000.0));
    baseHopSamples = juce::jmax(1, (int)std::floor(grainSamples * hopRatio));

    // window is computed once here instead of calling cos() on the audio thread
    hannWindow.resize((size_t)grainSamples);
    for (int n = 0; n < grainSamples; ++n)
        hannWindow[(size_t)n] = hannAt(n, grainSamples);

    // size the grain pool for the worst case so processBlock never allocates:
    // the lowest-pitched grain plays slowest and lives longest, and new grains
    // can spawn as often as every minHop samples (two at a time)
    const float minRate = semiToRate(fixedSemiB - maxPitchDepthSemi);
    const int maxGrainLifeSamples = (int)std::ceil((float)grainSamples / minRate) + 1;
    const int minHop = juce::jmax(1, (int)std::floor(baseHopSamples * (1.0f - hopJitterFrac)));
    const int maxOverlappingSpawns = maxGrainLifeSamples / minHop + 2;
    grainPool.assign((size_t)(2 * maxOverlappingSpawns), Grain{});

    // grains read straight from the circular buffer, so it has to hold the
    // longest lookback plus a full grain lifetime before the write head laps it
    const int maxJitterSamples = (int)std::ceil(maxJitterMs * sr / 1000.0);
    jassert(bufferSizeSamples > grainSamples + maxJitterSamples + maxGrainLifeSamples + samplesPerBlock);
    juce::ignoreUnused(samplesPerBlock, maxJitterSamples);

    triggerCounter = 0;
    scheduleNextTrigger();
}

void FracturedDoubleAudioProcessor::releaseResources() {}

bool FracturedDoubleAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet();
}

float FracturedDoubleAudioProcessor::semiToRate(float semi)
{
    return std::pow(2.0f, semi / 12.0f);
}

float FracturedDoubleAudioProcessor::hannAt(int n, int N)
{
    if (N <= 1) return 1.0f;
    return 0.5f * (1.0f - std::cos(juce::MathConstants<float>::twoPi * (float)n / (float)(N - 1)));
}

int FracturedDoubleAudioProcessor::wrapIndex(int idx) const
{
    idx %= bufferSizeSamples;
    if (idx < 0) idx += bufferSizeSamples;
    return idx;
}

FracturedDoubleAudioProcessor::Grain* FracturedDoubleAudioProcessor::findFreeGrain()
{
    for (auto& g : grainPool)
        if (!g.active)
            return &g;

    return nullptr;
}

void FracturedDoubleAudioProcessor::scheduleNextTrigger()
{
    // add some randomness to avoid periodic artifacts
    const int minHop = juce::jmax(1, (int)std::floor(baseHopSamples * (1.0f - hopJitterFrac)));
    const int maxHop = juce::jmax(minHop + 1, (int)std::ceil(baseHopSamples * (1.0f + hopJitterFrac)));
    nextTriggerInSamples = rng.nextInt(juce::Range<int>(minHop, maxHop));
    triggerCounter = nextTriggerInSamples;
}

void FracturedDoubleAudioProcessor::spawnGrain(int currentPos, float pitchDepthSemi, float jitterMs)
{
    const float maxJitSamples = juce::jlimit(0.0f, maxJitterMs, jitterMs) * (float)sr / 1000.0f;

    // create two grains: one pitched up, one down
    for (int which = 0; which < 2; ++which)
    {
        const float baseSemi = (which == 0 ? fixedSemiA : fixedSemiB);
        const float randSemi = (rng.nextFloat() * 2.0f - 1.0f) * pitchDepthSemi;
        const float totalSemi = baseSemi + randSemi;

        const float rate = semiToRate(totalSemi);

        // timing jitter only reaches further into the past, so a grain never
        // reads samples that haven't been written yet
        const int jitterSamps = (int)std::round(rng.nextFloat() * maxJitSamples);

        // pool is sized for the worst case in prepareToPlay, so this shouldn't happen
        Grain* g = findFreeGrain();
        if (g == nullptr)
            return;

        // grab audio ending at the current sample (or earlier, with jitter)
        g->readStart = wrapIndex(currentPos + 1 - grainSamples - jitterSamps);
        g->rate = rate;
        g->gain = 0.7f * std::sqrt(hopRatio); // boost to match input level
        g->pos = 0.0f;
        g->active = true;
    }
}

void FracturedDoubleAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin(2, buffer.getNumChannels());

    // get current parameter values
    const float pitchDepthSemi = *parameters.getRawParameterValue("pitchDepth");
    const float jitterMsParam = *parameters.getRawParameterValue("jitterMs");
    const float wetMix = *parameters.getRawParameterValue("wetMix");

    // store input in circular buffer
    for (int n = 0; n < numSamples; ++n)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float s = buffer.getReadPointer(ch)[n];
            circularBuffer.getWritePointer(ch)[writePos] = s;
        }
        writePos = (writePos + 1) % bufferSizeSamples;
    }

    const int dryDelaySamples = (int)std::round(dryDelayMs * sr / 1000.0);
    const float* circ[2] = { circularBuffer.getReadPointer(0), circularBuffer.getReadPointer(1) };

    // index of the first sample of this block in the circular buffer
    const int blockStart = writePos - numSamples;

    // process sample by sample for precise timing
    for (int n = 0; n < numSamples; ++n)
    {
        // where the current input sample lives in the circular buffer
        const int currentPos = wrapIndex(blockStart + n);

        // check if we need to spawn new grains
        if (triggerCounter <= 0)
        {
            spawnGrain(currentPos, pitchDepthSemi, jitterMsParam);
            scheduleNextTrigger();
        }
        --triggerCounter;

        // sum up all the grains, reading straight from the circular buffer
        float wetSum[2] = { 0.0f, 0.0f };
        for (auto& g : grainPool)
        {
            if (!g.active) continue;

            const float pos = g.pos * g.rate;
            const int   i = (int)pos;
            const float frac = pos - (float)i;

            if (i + 1 >= grainSamples)
            {
                g.active = false; // this grain is done, slot is free again
                continue;
            }

            const int idx0 = wrapIndex(g.readStart + i);
            const int idx1 = (idx0 + 1 == bufferSizeSamples) ? 0 : idx0 + 1;
            const float w0 = hannWindow[(size_t)i];      // Hann window to avoid clicks
            const float w1 = hannWindow[(size_t)i + 1];

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float s0 = circ[ch][idx0] * w0;
                const float s1 = circ[ch][idx1] * w1;
                const float interp = s0 + frac * (s1 - s0); // linear interpolation
                wetSum[ch] += g.gain * interp;
            }

            g.pos += 1.0f;
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            // get delayed dry signal
            const float dry = circ[ch][wrapIndex(currentPos - dryDelaySamples)];

            // mix dry and wet
            const float outSamp = (1.0f - wetMix) * dry + wetMix * wetSum[ch];

            // soft limiting to be safe
            buffer.getWritePointer(ch)[n] = juce::jlimit(-1.0f, 1.0f, std::tanh(outSamp));
        }
    }
}

juce::AudioProcessorEditor* FracturedDoubleAudioProcessor::createEditor()
{
    return new FracturedDoubleAudioProcessorEditor(*this);
}

bool FracturedDoubleAudioProcessor::hasEditor() const { return true; }

void FracturedDoubleAudioProcessor::getStateInformation(juce::MemoryBlock& /*destData*/) {}
void FracturedDoubleAudioProcessor::setStateInformation(const void* /*data*/, int /*sizeInBytes*/) {}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FracturedDoubleAudioProcessor();
}
