#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>

//==============================================================================
// Keyframe data structures for per-partial parameter animation
struct PartialKeyframe
{
    float time     = 0.0f; // Normalized time [0.0 .. 1.0] within ANIM_DURATION
    float ratio    = 1.0f; // Harmonic ratio [0.01 .. 32.0]
    float gain     = 1.0f; // Level [0.0 .. 1.0]
    float semitone = 0.0f; // Semitone offset [-24.0 .. 24.0]
    float detune   = 0.0f; // Detune cents [-50.0 .. 50.0]
    float pan      = 0.0f; // Stereo pan [-1.0 .. 1.0]
};

struct PartialAnimationData
{
    static constexpr int maxKeyframes = 16;
    int numKeyframes = 0;
    std::array<PartialKeyframe, maxKeyframes> keyframes {};

    static float applyCurve(float u, int curveType)
    {
        u = juce::jlimit(0.0f, 1.0f, u);
        if (curveType == 0) // Smooth (Hermite Ease-In / Ease-Out)
            return u * u * (3.0f - 2.0f * u);
        if (curveType == 2) // Step / Hold
            return 0.0f;
        return u; // Linear (curveType == 1)
    }

    static PartialKeyframe lerpKeyframe(const PartialKeyframe& a, const PartialKeyframe& b, float t, float u)
    {
        PartialKeyframe out;
        out.time     = t;
        out.ratio    = a.ratio    + u * (b.ratio    - a.ratio);
        out.gain     = a.gain     + u * (b.gain     - a.gain);
        out.semitone = a.semitone + u * (b.semitone - a.semitone);
        out.detune   = a.detune   + u * (b.detune   - a.detune);
        out.pan      = a.pan      + u * (b.pan      - a.pan);
        return out;
    }

    // Evaluate interpolated partial state at normalized time t in [0.0 .. 1.0]
    // curveType: 0 = Smooth, 1 = Linear, 2 = Step (Hold)
    PartialKeyframe evaluate(float t, int curveType, const PartialKeyframe& baseFallback) const
    {
        if (numKeyframes <= 0)
            return baseFallback;

        t = juce::jlimit(0.0f, 1.0f, t);

        // Direct hit on an existing keyframe timestamp
        for (int i = numKeyframes - 1; i >= 0; --i)
        {
            if (std::abs(t - keyframes[i].time) <= 0.0005f)
                return keyframes[i];
        }

        // If first keyframe is after t=0, smoothly transition from baseFallback (at t=0) to keyframes[0]
        if (t < keyframes[0].time)
        {
            if (keyframes[0].time <= 0.0001f)
                return keyframes[0];
            if (curveType == 2)
                return baseFallback;
            float u = applyCurve(t / keyframes[0].time, curveType);
            return lerpKeyframe(baseFallback, keyframes[0], t, u);
        }

        if (t >= keyframes[numKeyframes - 1].time)
            return keyframes[numKeyframes - 1];

        int seg = 0;
        for (; seg < numKeyframes - 1; ++seg)
        {
            if (t >= keyframes[seg].time && t < keyframes[seg + 1].time)
                break;
        }

        const auto& k0 = keyframes[seg];
        const auto& k1 = keyframes[seg + 1];
        if (curveType == 2) // Step / Hold
        {
            PartialKeyframe out = k0;
            out.time = t;
            return out;
        }

        float dt = k1.time - k0.time;
        float rawU = (dt > 0.00001f) ? (t - k0.time) / dt : 0.0f;
        float u = applyCurve(rawU, curveType);
        return lerpKeyframe(k0, k1, t, u);
    }
};

// Real-time modulated partial state published by the audio thread for graph node visualization
struct LivePartialVisState
{
    std::atomic<float> effectiveRatio    { 1.0f };
    std::atomic<float> effectiveGain     { 0.0f };
    std::atomic<float> effectiveSemitone { 0.0f };
    std::atomic<float> effectiveDetune   { 0.0f };
    std::atomic<float> effectivePan      { 0.0f };
    std::atomic<float> animPhase         { 0.0f };
    std::atomic<float> envLevel          { 0.0f };
    std::atomic<float> lfoValue          { 0.0f };
};

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
            case 6: // Fibonacci
                out = (std::exp(1.924847f * t) - 3.927f) / 2.927f;
                out -= polyBlep(t, dt);
                break;
            case 7: // Noise
                out = ((float)std::rand() / RAND_MAX) * 2.0f - 1.0f;
                break;
            case 8:  // Custom 1
            case 9:  // Custom 2
            case 10: // Custom 3
            case 11: // Custom 4
                if (customWavetables != nullptr)
                {
                    int slot = waveform - 8;
                    if (slot >= 0 && slot < 4)
                    {
                        float exactPos = t * 512.0f;
                        int i1 = (int)exactPos % 512;
                        int i2 = (i1 + 1) % 512;
                        float frac = exactPos - (int)exactPos;
                        float v1 = (*customWavetables)[slot][i1].load(std::memory_order_relaxed);
                        float v2 = (*customWavetables)[slot][i2].load(std::memory_order_relaxed);
                        out = v1 + frac * (v2 - v1);
                    }
                }
                break;
        }

        phase += phaseInc;
        if (phase >= 1.0f) phase -= 1.0f;
        return out;
    }

    void setCustomWavetables(std::array<std::array<std::atomic<float>, 512>, 4>* tables)
    {
        customWavetables = tables;
    }

private:
    float phase = 0.0f;
    float phaseInc = 0.0f;
    int waveform = 0;
    std::array<std::array<std::atomic<float>, 512>, 4>* customWavetables = nullptr;

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
    void updateParameters(juce::AudioProcessorValueTreeState& apvts, const std::array<PartialAnimationData, 16>* animTracks = nullptr);
    void setPartialVisualizerBuffer(juce::AudioBuffer<float>* buffer) { partialsVisBuffer = buffer; }
    void setCustomWavetables(std::array<std::array<std::atomic<float>, 512>, 4>* tables);
    void setLiveVisPointers(std::array<LivePartialVisState, 16>* states, std::atomic<int>* latestVoiceIdx)
    {
        liveVisStates = states;
        latestActiveVoiceIdx = latestVoiceIdx;
    }
    void setTargetNote(int midiNoteNumber, float velocity, bool isLegatoTransition);
    void setVoiceIndex(int idx) { voiceIndex = idx; }
    bool isActive() const { return adsr.isActive(); }

private:
    int voiceIndex = 0;
    std::array<std::array<std::atomic<float>, 512>, 4>* customWavetables = nullptr;
    std::array<LivePartialVisState, 16>* liveVisStates = nullptr;
    std::atomic<int>* latestActiveVoiceIdx = nullptr;
    juce::ADSR adsr;
    juce::ADSR::Parameters adsrParams;
    
    static const int maxPartials = 16;
    int activePartials = 4;
    BandlimitedOscillator oscillators[maxPartials];
    
    float currentSampleRate = 44100.0f;
    
    bool partialEnabled[maxPartials];
    bool partialAdsrEnabled[maxPartials];
    float partialRatios[maxPartials];
    float partialDetunes[maxPartials];
    float partialGains[maxPartials];
    float partialSemitones[maxPartials]; // semitone offset per partial
    int partialWaveforms[maxPartials];
    float partialPans[maxPartials];
    float currentPitchBendSemitones = 0.0f;
    float modWheelValue = 0.0f;
    
    int partialModTargets[maxPartials]; // 0=None, 1=Pitch, 2=Level, 3=Pan, 4=Detune
    int partialModWaveforms[maxPartials]; // 0=Sine, 1=Tri, 2=Saw, 3=Square
    float partialModRates[maxPartials];
    float partialModDepths[maxPartials];
    float partialModPhaseOffsets[maxPartials];
    float partialLfoPhases[maxPartials];

    // Per-Partial ADSR Envelopes
    juce::ADSR partialAdsr[maxPartials];
    juce::ADSR::Parameters partialAdsrParams[maxPartials];
    float partialGlideTimes[maxPartials];
    float partialCurrentFreq[maxPartials];

    // Per-Partial Keyframe Animation (replaces Mod 2)
    int partialAnimModes[maxPartials];      // 0=Off, 1=Note Length (ADSR), 2=Time: One-Shot, 3=Time: Loop, 4=Time: Ping-Pong
    int partialAnimCurves[maxPartials];     // 0=Smooth, 1=Linear, 2=Step
    float partialAnimDurations[maxPartials];
    float partialAnimDepths[maxPartials];
    float partialAnimElapsed[maxPartials];
    float partialAnimPhase[maxPartials];
    bool partialNoteHeld[maxPartials];
    float partialReleaseElapsed[maxPartials];
    float partialReleaseStartPhase[maxPartials];
    PartialAnimationData partialAnimTracks[maxPartials];

    float currentVelocity = 1.0f;
    bool velocityEnabled = true;

    float masterPan = 0.0f;
    int voiceMode = 0; // 0=Poly, 1=Mono, 2=Legato
    float glideTime = 0.0f;
    float targetFrequency = 0.0f;
    
    float currentFrequency = 0.0f;
    juce::AudioBuffer<float> synthBuffer;
    juce::AudioBuffer<float>* partialsVisBuffer = nullptr;
};
