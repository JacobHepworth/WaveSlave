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

    std::array<std::atomic<float>, 512> customWavetable;
    void generateStandardWaveToCustomTable(int waveType);

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::Synthesiser synth;
    juce::AudioBuffer<float> partialVisualizerBuffer; // Used temporarily by SynthVoice during render

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveSlaveAudioProcessor)

    int lastBaseNoteParam = -1;
};
