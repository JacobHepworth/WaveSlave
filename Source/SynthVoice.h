#pragma once
#include <JuceHeader.h>

class WaveSlaveSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class BandlimitedOscillator
{
public:
    void setFrequency(float freq, float sr)
    {
        phaseInc = freq / sr;
    }

    void setWaveform(int type)
    {
        waveform = type;
    }

    void reset() { phase = 0.0f; }

    float processSample()
    {
        float t = phase;
        float dt = phaseInc;
        float out = 0.0f;

        switch (waveform)
        {
            case 0: // Sine
                out = std::sin(t * juce::MathConstants<float>::twoPi);
                break;
            case 1: // Triangle
                out = 2.0f * std::abs(2.0f * t - 1.0f) - 1.0f;
                break;
            case 2: // Saw
                out = 2.0f * t - 1.0f;
                out -= polyBlep(t, dt);
                break;
            case 3: // Square
                out = t < 0.5f ? 1.0f : -1.0f;
                out += polyBlep(t, dt) - polyBlep(std::fmod(t + 0.5f, 1.0f), dt);
                break;
            case 4: // Pulse (25% duty)
                out = t < 0.25f ? 1.0f : -1.0f;
                out += polyBlep(t, dt) - polyBlep(std::fmod(t + 0.75f, 1.0f), dt);
                break;
            case 5: // Sharkfin (FM)
                out = std::sin(t * juce::MathConstants<float>::twoPi + 1.5f * std::sin(t * juce::MathConstants<float>::twoPi));
                break;
            case 6: // Golden Spiral
                out = (std::exp(1.924847f * t) - 3.927f) / 2.927f;
                out -= polyBlep(t, dt);
                break;
            case 7: // Noise
                out = ((float)std::rand() / RAND_MAX) * 2.0f - 1.0f;
                break;
            case 8: // Custom
                if (customWavetable != nullptr)
                {
                    float exactPos = t * 512.0f;
                    int i1 = (int)exactPos;
                    int i2 = (i1 + 1) % 512;
                    float frac = exactPos - i1;
                    float v1 = (*customWavetable)[i1].load(std::memory_order_relaxed);
                    float v2 = (*customWavetable)[i2].load(std::memory_order_relaxed);
                    out = v1 + frac * (v2 - v1);
                }
                break;
        }

        phase += phaseInc;
        if (phase >= 1.0f) phase -= 1.0f;
        return out;
    }

    void setCustomWavetable(std::array<std::atomic<float>, 512>* table)
    {
        customWavetable = table;
    }

private:
    float phase = 0.0f;
    float phaseInc = 0.0f;
    int waveform = 0;
    std::array<std::atomic<float>, 512>* customWavetable = nullptr;

    float polyBlep(float t, float dt)
    {
        if (dt <= 0.0f) return 0.0f;
        if (t < dt) {
            t /= dt;
            return t + t - t * t - 1.0f;
        } else if (t > 1.0f - dt) {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        } else return 0.0f;
    }
};

class WaveSlaveVoice : public juce::SynthesiserVoice
{
public:
    WaveSlaveVoice();
    
    bool canPlaySound (juce::SynthesiserSound* sound) override;
    void startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int currentPitchWheelPosition) override;
    void stopNote (float velocity, bool allowTailOff) override;
    void pitchWheelMoved (int newPitchWheelValue) override;
    void controllerMoved (int controllerNumber, int newControllerValue) override;
    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;
    void prepareToPlay(double sampleRate, int samplesPerBlock, int outputChannels);
    void updateParameters(juce::AudioProcessorValueTreeState& apvts);
    void setPartialVisualizerBuffer(juce::AudioBuffer<float>* buffer) { partialsVisBuffer = buffer; }
    void setCustomWavetable(std::array<std::atomic<float>, 512>* table);

private:
    juce::ADSR adsr;
    juce::ADSR::Parameters adsrParams;
    
    static const int maxPartials = 16;
    int activePartials = 4;
    BandlimitedOscillator baseOscillator;
    int baseWaveform = 0;
    BandlimitedOscillator oscillators[maxPartials];
    
    float currentSampleRate = 44100.0f;
    
    float partialRatios[maxPartials];
    float partialDetunes[maxPartials];
    float partialGains[maxPartials];
    float partialSemitones[maxPartials]; // semitone offset per partial
    int partialWaveforms[maxPartials]; // 0=Sine, 1=Triangle, 2=Saw, 3=Square
    
    int partialModTargets[maxPartials]; // 0=None, 1=Pitch, 2=Level
    int partialModWaveforms[maxPartials]; // 0=Sine, 1=Tri, 2=Saw, 3=Square
    float partialModRates[maxPartials];
    float partialModDepths[maxPartials];
    float partialModPhaseOffsets[maxPartials];
    float partialLfoPhases[maxPartials];
    
    int baseNoteOffset = 0;
    float currentFrequency = 0.0f;
    juce::AudioBuffer<float> synthBuffer;
    juce::AudioBuffer<float>* partialsVisBuffer = nullptr;
};
