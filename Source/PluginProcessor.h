#pragma once

#include <JuceHeader.h>
#include "SynthVoice.h"

class WaveSlaveAudioProcessor : public juce::AudioProcessor
{
public:
    WaveSlaveAudioProcessor();
    ~WaveSlaveAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    
    static constexpr int ringBufferSize = 4096;
    std::array<float, ringBufferSize> ringBuffer;
    std::array<std::array<float, ringBufferSize>, 16> partialRingBuffer;
    std::atomic<int> ringBufferWritePos { 0 };
    std::atomic<float> lastPlayedFrequency { 440.0f };
    std::atomic<float> lastTriggeredFrequency { 440.0f };
    std::atomic<int> noteTriggerCounter { 0 };

    // QWERTY MIDI virtual keyboard state - editor writes note on/off here
    juce::MidiKeyboardState midiKeyboardState;

    static constexpr int numCustomSlots = 4;
    std::array<std::array<std::atomic<float>, 512>, numCustomSlots> customWavetables;
    void generateStandardWaveToCustomTable(int slot, int waveType);
    void generateStandardWaveToCustomTable(int waveType) { generateStandardWaveToCustomTable(0, waveType); }
    void allNotesOff();

    // Live per-partial modulation state for graph node visualization (ADSR + LFO + Keyframes)
    std::array<LivePartialVisState, 16> livePartialState;
    std::atomic<bool> anyVoiceActiveVis { false };
    std::atomic<int> latestActiveVoiceIndex { 0 };

    // Per-Partial Keyframe Animation tracks (thread-safe access)
    PartialAnimationData getPartialAnimation(int partialIndex) const;
    void setPartialAnimation(int partialIndex, const PartialAnimationData& data);
    int addOrUpdateKeyframe(int partialIndex, const PartialKeyframe& kf, float timeTolerance = 0.015f);
    void removeKeyframe(int partialIndex, int keyframeIndex);
    int moveKeyframeTime(int partialIndex, int keyframeIndex, float newTime);
    void clearKeyframes(int partialIndex);
    void clearAllKeyframes();
    void ensureInitialKeyframes(int partialIndex);
    PartialKeyframe getPartialBaseSettings(int partialIndex) const;
    void serializeAnimationsToXml(juce::XmlElement& parentXml) const;
    void deserializeAnimationsFromXml(const juce::XmlElement& parentXml);

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    mutable juce::SpinLock animLock;
    std::array<PartialAnimationData, 16> partialAnimations;

    juce::Synthesiser synth;
    juce::AudioBuffer<float> partialVisualizerBuffer; // Used temporarily by SynthVoice during render

    juce::dsp::StateVariableTPTFilter<float> lowPassFilter;
    juce::dsp::StateVariableTPTFilter<float> highPassFilter;

    std::vector<std::pair<int, float>> heldMidiNotes;
    std::set<int> sustainedNotes;
    bool sustainPedalDown = false;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveSlaveAudioProcessor)

    int lastBaseNoteParam = -1;
};
