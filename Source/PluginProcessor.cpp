#include "PluginProcessor.h"
#include "PluginEditor.h"

WaveSlaveAudioProcessor::WaveSlaveAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
#else
    :
#endif
    apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    synth.addSound(new WaveSlaveSound());
    for (int i = 0; i < 8; i++) // 8 voices of polyphony
    {
        auto* voice = new WaveSlaveVoice();
        voice->setCustomWavetable(&customWavetable);
        synth.addVoice(voice);
    }

    if (auto* p = apvts.getRawParameterValue("BASE_NOTE"))
    {
        lastBaseNoteParam = (int)p->load();
        lastPlayedFrequency.store((float)juce::MidiMessage::getMidiNoteInHertz(lastBaseNoteParam), std::memory_order_relaxed);
    }
    for (int i = 0; i < 512; ++i)
        customWavetable[i].store(0.0f, std::memory_order_relaxed);
}

void WaveSlaveAudioProcessor::generateStandardWaveToCustomTable(int waveType)
{
    // 0: Sine, 1: Triangle, 2: Saw, 3: Square, 4: Pulse, 5: Sharkfin, 6: Golden Spiral, 7: Noise
    for (int i = 0; i < 512; ++i)
    {
        float t = (float)i / 512.0f; // 0 to 1
        float val = 0.0f;
        switch (waveType)
        {
            case 0: val = std::sin(juce::MathConstants<float>::twoPi * t); break;
            case 1: val = 2.0f * std::abs(2.0f * (t - std::floor(t + 0.5f))) - 1.0f; break;
            case 2: val = 2.0f * (t - std::floor(t + 0.5f)); break;
            case 3: val = (t < 0.5f) ? 1.0f : -1.0f; break;
            case 4: val = (t < 0.25f) ? 1.0f : -1.0f; break; // 25% pulse
            case 5: // Sharkfin
                val = std::sin(juce::MathConstants<float>::pi * t) * std::exp(-2.0f * t) * 2.0f - 0.5f;
                break;
            case 6: // Golden Spiral
                val = (std::exp(1.924847f * t) - 3.927f) / 2.927f;
                break;
            case 7: // Noise
                val = ((float)std::rand() / RAND_MAX) * 2.0f - 1.0f;
                break;
            default: break;
        }
        customWavetable[i].store(val, std::memory_order_relaxed);
    }
}

WaveSlaveAudioProcessor::~WaveSlaveAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout WaveSlaveAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>("ATTACK", "Attack", juce::NormalisableRange<float>(0.001f, 5.0f, 0.001f, 0.5f), 0.01f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("DECAY", "Decay", juce::NormalisableRange<float>(0.001f, 5.0f, 0.001f, 0.5f), 0.3f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("SUSTAIN", "Sustain", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.8f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("RELEASE", "Release", juce::NormalisableRange<float>(0.001f, 5.0f, 0.001f, 0.5f), 0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("MASTER_GAIN", "Master Gain", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
    layout.add(std::make_unique<juce::AudioParameterInt>("NUM_PARTIALS", "Active Partials", 0, 16, 4));
    layout.add(std::make_unique<juce::AudioParameterInt>("BASE_NOTE", "Base Note", 24, 96, 60)); // C1 to C7, default C4 (60)

    juce::StringArray waveforms = { "Sine", "Triangle", "Saw", "Square", "Pulse", "Sharkfin", "Golden Spiral", "Noise", "Custom" };
    layout.add(std::make_unique<juce::AudioParameterChoice>("BASE_WAVEFORM", "Base Waveform", waveforms, 0));

    for (int i = 1; i <= 16; ++i)
    {
        juce::String idSuffix = juce::String(i);
        layout.add(std::make_unique<juce::AudioParameterChoice>("WAVEFORM" + idSuffix, "Waveform " + idSuffix, waveforms, 0));
        layout.add(std::make_unique<juce::AudioParameterFloat>("RATIO" + idSuffix, "Harmonic " + idSuffix, juce::NormalisableRange<float>(0.01f, 32.0f, 0.01f, 0.4f), (float)i));
        layout.add(std::make_unique<juce::AudioParameterFloat>("SEMITONE" + idSuffix, "Semitones " + idSuffix, juce::NormalisableRange<float>(-24.0f, 24.0f, 1.0f), 0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("DETUNE" + idSuffix, "Detune " + idSuffix, juce::NormalisableRange<float>(-50.0f, 50.0f, 1.0f), 0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("GAIN" + idSuffix, "Gain " + idSuffix, juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), i <= 4 ? 1.0f / i : 0.0f));
        layout.add(std::make_unique<juce::AudioParameterChoice>("MOD_TARGET" + idSuffix, "Mod Target " + idSuffix, juce::StringArray{"None", "Pitch", "Level"}, 0));
        layout.add(std::make_unique<juce::AudioParameterChoice>("MOD_WAVEFORM" + idSuffix, "Mod Wave " + idSuffix, juce::StringArray{"Sine", "Triangle", "Saw", "Square"}, 0));
        layout.add(std::make_unique<juce::AudioParameterFloat>("MOD_RATE" + idSuffix, "Mod Rate " + idSuffix, juce::NormalisableRange<float>(0.1f, 20.0f, 0.01f, 0.3f), 2.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("MOD_DEPTH" + idSuffix, "Mod Depth " + idSuffix, juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("MOD_PHASE" + idSuffix, "Mod Phase " + idSuffix, juce::NormalisableRange<float>(0.0f, 360.0f, 1.0f), 0.0f));
    }

    return layout;
}

const juce::String WaveSlaveAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool WaveSlaveAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool WaveSlaveAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool WaveSlaveAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double WaveSlaveAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int WaveSlaveAudioProcessor::getNumPrograms()
{
    return 1;
}

int WaveSlaveAudioProcessor::getCurrentProgram()
{
    return 0;
}

void WaveSlaveAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String WaveSlaveAudioProcessor::getProgramName (int index)
{
    return {};
}

void WaveSlaveAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

void WaveSlaveAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);
    for (int i = 0; i < synth.getNumVoices(); i++)
    {
        if (auto voice = dynamic_cast<WaveSlaveVoice*>(synth.getVoice(i)))
        {
            voice->prepareToPlay(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
        }
    }
    
    partialVisualizerBuffer.setSize(16, samplesPerBlock);
    partialVisualizerBuffer.clear();
    
    for (int i = 0; i < synth.getNumVoices(); i++)
    {
        if (auto voice = dynamic_cast<WaveSlaveVoice*>(synth.getVoice(i)))
        {
            voice->setPartialVisualizerBuffer(&partialVisualizerBuffer);
        }
    }
}

void WaveSlaveAudioProcessor::releaseResources()
{
}

bool WaveSlaveAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}

void WaveSlaveAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    partialVisualizerBuffer.setSize(16, buffer.getNumSamples(), false, false, true);
    partialVisualizerBuffer.clear();

    for (const auto metadata : midiMessages)
    {
        auto msg = metadata.getMessage();
        if (msg.isNoteOn())
        {
            lastPlayedFrequency.store((float)juce::MidiMessage::getMidiNoteInHertz(msg.getNoteNumber()), std::memory_order_relaxed);
        }
    }

    for (int i = 0; i < synth.getNumVoices(); i++)
    {
        if (auto voice = dynamic_cast<WaveSlaveVoice*>(synth.getVoice(i)))
        {
            voice->updateParameters(apvts);
        }
    }

    synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());

    float masterGain = apvts.getRawParameterValue("MASTER_GAIN")->load();
    buffer.applyGain(masterGain);
    
    // Push to ring buffers for visualizers
    auto* readPtr = buffer.getReadPointer(0);
    int numSamples = buffer.getNumSamples();
    int writePos = ringBufferWritePos.load(std::memory_order_relaxed);
    
    for (int i = 0; i < numSamples; ++i)
    {
        ringBuffer[writePos] = readPtr[i];
        for (int ch = 0; ch < 16; ++ch)
        {
            if (ch < partialVisualizerBuffer.getNumChannels())
                partialRingBuffer[ch][writePos] = partialVisualizerBuffer.getSample(ch, i);
            else
                partialRingBuffer[ch][writePos] = 0.0f;
        }
        writePos = (writePos + 1) % ringBufferSize;
    }
    ringBufferWritePos.store(writePos, std::memory_order_release);
}

bool WaveSlaveAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* WaveSlaveAudioProcessor::createEditor()
{
    return new WaveSlaveAudioProcessorEditor (*this);
    // return new juce::GenericAudioProcessorEditor(*this);
}

void WaveSlaveAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void WaveSlaveAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WaveSlaveAudioProcessor();
}
