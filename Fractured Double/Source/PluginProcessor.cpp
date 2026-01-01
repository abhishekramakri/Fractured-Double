#include "PluginProcessor.h"
#include "PluginEditor.h"

FracturedDoubleAudioProcessor::FracturedDoubleAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    parameters(*this, nullptr, "PARAMETERS",
        {
            std::make_unique<juce::AudioParameterFloat>("pitchDepth", "Pitch Depth", 0.0f, 12.0f, 3.0f),
            std::make_unique<juce::AudioParameterFloat>("jitterMs",   "Jitter (ms)",  0.0f, 100.0f, 30.0f),
            std::make_unique<juce::AudioParameterFloat>("wetMix",     "Wet Mix",      0.0f, 1.0f,   0.4f)
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

void FracturedDoubleAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    sr = sampleRate;
    bufferSizeSamples = (int)juce::jmax(1.0, std::floor(sr * 2.0)); // 2 second buffer

    circularBuffer.setSize(2, bufferSizeSamples);
    circularBuffer.clear();
    writePos = 0;

    activeGrains.clear();

    // figure out grain and hop sizes
    grainSamples = (int)juce::jmax(5.0, std::floor(grainSizeMs * sr / 1000.0));
    baseHopSamples = juce::jmax(1, (int)std::floor(grainSamples * hopRatio));

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

void FracturedDoubleAudioProcessor::scheduleNextTrigger()
{
    // add some randomness to avoid periodic artifacts
    const float jitterFrac = 0.30f;
    const int minHop = juce::jmax(1, (int)std::floor(baseHopSamples * (1.0f - jitterFrac)));
    const int maxHop = juce::jmax(minHop + 1, (int)std::ceil(baseHopSamples * (1.0f + jitterFrac)));
    nextTriggerInSamples = rng.nextInt(juce::Range<int>(minHop, maxHop));
    triggerCounter = nextTriggerInSamples;
}

void FracturedDoubleAudioProcessor::spawnGrain(float pitchDepthSemi, float jitterMs)
{
    // create two grains: one pitched up, one down
    for (int which = 0; which < 2; ++which)
    {
        const float baseSemi = (which == 0 ? fixedSemiA : fixedSemiB);
        const float randSemi = (rng.nextFloat() * 2.0f - 1.0f) * pitchDepthSemi;
        const float totalSemi = baseSemi + randSemi;

        const float rate = semiToRate(totalSemi);

        // add some timing jitter
        const float maxJitSamples = juce::jlimit(0.0f, (float)(bufferSizeSamples - grainSamples - 1),
            (float)(jitterMs * (float)sr / 1000.0f));
        const float jitterSamps = juce::jlimit(-maxJitSamples, maxJitSamples,
            (rng.nextFloat() * 2.0f - 1.0f) * maxJitSamples);

        // grab audio from just before the write head
        int readStart = writePos - grainSamples - (int)std::round(jitterSamps);
        // handle wrap-around
        readStart %= bufferSizeSamples;
        if (readStart < 0) readStart += bufferSizeSamples;

        Grain g;
        g.length = grainSamples;
        g.rate = rate;
        g.gain = 0.7f * std::sqrt(hopRatio); // boost to match input level
        g.pos = 0.0f;
        g.finished = false;

        // copy audio and apply window
        g.data.setSize(2, g.length);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* circ = circularBuffer.getReadPointer(ch);
            float* dst = g.data.getWritePointer(ch);

            // copy from circular buffer
            for (int n = 0; n < g.length; ++n)
            {
                int idx = readStart + n;
                if (idx >= bufferSizeSamples) idx -= bufferSizeSamples;
                dst[n] = circ[idx];
            }

            // apply Hann window to avoid clicks
            for (int n = 0; n < g.length; ++n)
                dst[n] *= hannAt(n, g.length);
        }

        activeGrains.emplace_back(std::move(g));
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

    // process sample by sample for precise timing
    for (int n = 0; n < numSamples; ++n)
    {
        // check if we need to spawn new grains
        if (triggerCounter <= 0)
        {
            spawnGrain(pitchDepthSemi, jitterMsParam);
            scheduleNextTrigger();
        }
        --triggerCounter;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            // get delayed dry signal
            const int dryDelaySamples = (int)std::round(dryDelayMs * sr / 1000.0f);
            int dryIdx = writePos - dryDelaySamples - (numSamples - 1 - n);
            // wrap around
            dryIdx %= bufferSizeSamples;
            if (dryIdx < 0) dryIdx += bufferSizeSamples;

            const float dry = circularBuffer.getReadPointer(ch)[dryIdx];

            // sum up all the grains
            float wetSum = 0.0f;
            for (auto& g : activeGrains)
            {
                if (g.finished) continue;

                const float pos = g.pos * g.rate;               
                const int   i = (int)pos;
                const float frac = pos - (float)i;

                if (i + 1 < g.length)
                {
                    const float* gbuf = g.data.getReadPointer(ch);
                    const float s0 = gbuf[i];
                    const float s1 = gbuf[i + 1];
                    const float interp = s0 + frac * (s1 - s0); // linear interpolation
                    wetSum += g.gain * interp;
                }
                else
                {
                    g.finished = true; // this grain is done
                }
            }

            // mix dry and wet
            const float outSamp = (1.0f - wetMix) * dry + wetMix * wetSum;

            // soft limiting to be safe
            buffer.getWritePointer(ch)[n] = juce::jlimit(-1.0f, 1.0f, std::tanh(outSamp));
        }

        // advance all grains
        for (auto& g : activeGrains)
            g.pos += 1.0f;

        // clean up finished grains occasionally
        if ((n & 63) == 63) // every 64 samples
        {
            activeGrains.erase(
                std::remove_if(activeGrains.begin(), activeGrains.end(),
                    [](const Grain& g) { return g.finished; }),
                activeGrains.end());
        }
    }

    // final cleanup
    activeGrains.erase(
        std::remove_if(activeGrains.begin(), activeGrains.end(),
            [](const Grain& g) { return g.finished; }),
        activeGrains.end());
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
