#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>

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
    apvts (*this, &undoManager, "Parameters", createParameterLayout())
{
    formatManager.registerBasicFormats();

    synth.addSound(new WaveSlaveSound());
    for (int i = 0; i < 8; i++) // 8 voices of polyphony
    {
        auto* voice = new WaveSlaveVoice();
        voice->setVoiceIndex(i);
        voice->setCustomWavetables(&customWavetables);
        voice->setLiveVisPointers(&livePartialState, &latestActiveVoiceIndex);
        voice->setSoloStatePointer(&partialSoloState);
        synth.addVoice(voice);
    }

    for (int i = 0; i < 16; ++i)
    {
        livePartialState[i].effectiveRatio.store((float)(i + 1), std::memory_order_relaxed);
        livePartialState[i].effectiveGain.store(i < 4 ? (1.0f / (float)(i + 1)) : 0.0f, std::memory_order_relaxed);
        livePartialState[i].effectiveSemitone.store(0.0f, std::memory_order_relaxed);
        livePartialState[i].effectiveDetune.store(0.0f, std::memory_order_relaxed);
        livePartialState[i].effectivePan.store(0.0f, std::memory_order_relaxed);
        livePartialState[i].animPhase.store(0.0f, std::memory_order_relaxed);
        livePartialState[i].envLevel.store(0.0f, std::memory_order_relaxed);
        livePartialState[i].lfoValue.store(0.0f, std::memory_order_relaxed);
    }

    if (auto* p = apvts.getRawParameterValue("BASE_NOTE"))
    {
        lastBaseNoteParam = (int)p->load();
        lastPlayedFrequency.store((float)juce::MidiMessage::getMidiNoteInHertz(lastBaseNoteParam), std::memory_order_relaxed);
    }

    // Initialize custom wavetable slots with useful initial shapes
    generateStandardWaveToCustomTable(0, 0); // Slot 1: Sine
    generateStandardWaveToCustomTable(1, 1); // Slot 2: Triangle
    generateStandardWaveToCustomTable(2, 2); // Slot 3: Saw
    generateStandardWaveToCustomTable(3, 3); // Slot 4: Square

    customSplinePoints[0] = { {0.0f, 0.0f}, {0.25f, 1.0f}, {0.5f, 0.0f}, {0.75f, -1.0f}, {1.0f, 0.0f} }; // Sine
    customSplinePoints[1] = { {0.0f, 0.0f}, {0.25f, 1.0f}, {0.75f, -1.0f}, {1.0f, 0.0f} };               // Tri
    customSplinePoints[2] = { {0.0f, 0.0f}, {0.49f, 1.0f}, {0.51f, -1.0f}, {1.0f, 0.0f} };               // Saw
    customSplinePoints[3] = { {0.0f, 1.0f}, {0.49f, 1.0f}, {0.51f, -1.0f}, {1.0f, -1.0f} };              // Square

    // Ensure filters and FX start disabled by default
    if (auto* p = apvts.getParameter("LPF_ENABLED")) p->setValueNotifyingHost(0.0f);
    if (auto* p = apvts.getParameter("HPF_ENABLED")) p->setValueNotifyingHost(0.0f);
    if (auto* p = apvts.getParameter("CHORUS_ENABLED")) p->setValueNotifyingHost(0.0f);
    if (auto* p = apvts.getParameter("DELAY_ENABLED")) p->setValueNotifyingHost(0.0f);
    if (auto* p = apvts.getParameter("REVERB_ENABLED")) p->setValueNotifyingHost(0.0f);
}

void WaveSlaveAudioProcessor::generateStandardWaveToCustomTable(int slot, int waveType)
{
    slot = juce::jlimit(0, numCustomSlots - 1, slot);
    // 0: Sine, 1: Triangle, 2: Saw, 3: Square, 4: Pulse, 5: Sharkfin, 6: Fibonacci, 7: Noise
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
            case 6: // Fibonacci
                val = (std::exp(1.924847f * t) - 3.927f) / 2.927f;
                break;
            case 7: // Noise
                val = ((float)std::rand() / RAND_MAX) * 2.0f - 1.0f;
                break;
            default: break;
        }
        customWavetables[slot][i].store(val, std::memory_order_relaxed);
    }
}

WaveSlaveAudioProcessor::~WaveSlaveAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout WaveSlaveAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // 1. Master Group
    auto masterGroup = std::make_unique<juce::AudioProcessorParameterGroup>("group_master", "Master", "|");
    masterGroup->addChild(std::make_unique<juce::AudioParameterFloat>("MASTER_GAIN", "Master Gain", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
    masterGroup->addChild(std::make_unique<juce::AudioParameterFloat>("MASTER_PAN", "Master Pan", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));
    masterGroup->addChild(std::make_unique<juce::AudioParameterChoice>("VOICE_MODE", "Voice Mode", juce::StringArray{"Poly", "Mono", "Legato"}, 0));
    masterGroup->addChild(std::make_unique<juce::AudioParameterFloat>("GLIDE_TIME", "Glide Time", juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f, 0.5f), 0.0f));
    masterGroup->addChild(std::make_unique<juce::AudioParameterInt>("NUM_PARTIALS", "Active Partials", 1, 16, 4));
    masterGroup->addChild(std::make_unique<juce::AudioParameterInt>("BASE_NOTE", "Base Note", 24, 96, 60)); // C1 to C7, default C4 (60)
    masterGroup->addChild(std::make_unique<juce::AudioParameterBool>("VELOCITY_ENABLED", "Velocity Sensitivity", true));

    juce::StringArray waveforms = { "Sine", "Triangle", "Saw", "Square", "Pulse", "Sharkfin", "Fibonacci", "Noise", "Custom 1", "Custom 2", "Custom 3", "Custom 4" };
    masterGroup->addChild(std::make_unique<juce::AudioParameterChoice>("BASE_WAVEFORM", "Base Waveform", waveforms, 0));

    // Phase Mode (Key-Sync, Free-Running, Random Initial Phase)
    masterGroup->addChild(std::make_unique<juce::AudioParameterChoice>("PHASE_MODE", "Phase Mode", juce::StringArray{"Key-Sync (0 deg)", "Free-Running", "Random"}, 0));

    // Configurable Pitch Bend Range
    masterGroup->addChild(std::make_unique<juce::AudioParameterChoice>("PITCH_BEND_RANGE", "Pitch Bend Range", juce::StringArray{"+/- 2 st", "+/- 7 st", "+/- 12 st", "+/- 24 st"}, 0));

    // Master Soft Clipper
    masterGroup->addChild(std::make_unique<juce::AudioParameterBool>("MASTER_CLIPPER", "Soft Clipper", true));

    // Built-in Effects
    // Chorus
    masterGroup->addChild(std::make_unique<juce::AudioParameterBool>("CHORUS_ENABLED", "Chorus Enable", false));
    masterGroup->addChild(std::make_unique<juce::AudioParameterFloat>("CHORUS_RATE", "Chorus Rate", juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f, 0.5f), 1.2f));
    masterGroup->addChild(std::make_unique<juce::AudioParameterFloat>("CHORUS_DEPTH", "Chorus Depth", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.35f));
    masterGroup->addChild(std::make_unique<juce::AudioParameterFloat>("CHORUS_MIX", "Chorus Mix", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.4f));

    // Delay
    masterGroup->addChild(std::make_unique<juce::AudioParameterBool>("DELAY_ENABLED", "Delay Enable", false));
    masterGroup->addChild(std::make_unique<juce::AudioParameterFloat>("DELAY_TIME", "Delay Time", juce::NormalisableRange<float>(0.02f, 1.5f, 0.01f, 0.5f), 0.35f));
    masterGroup->addChild(std::make_unique<juce::AudioParameterFloat>("DELAY_FEEDBACK", "Delay Feedback", juce::NormalisableRange<float>(0.0f, 0.95f, 0.01f), 0.45f));
    masterGroup->addChild(std::make_unique<juce::AudioParameterFloat>("DELAY_MIX", "Delay Mix", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.3f));

    // Reverb
    masterGroup->addChild(std::make_unique<juce::AudioParameterBool>("REVERB_ENABLED", "Reverb Enable", false));
    masterGroup->addChild(std::make_unique<juce::AudioParameterFloat>("REVERB_SIZE", "Reverb Room Size", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.6f));
    masterGroup->addChild(std::make_unique<juce::AudioParameterFloat>("REVERB_DAMP", "Reverb Damping", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.4f));
    masterGroup->addChild(std::make_unique<juce::AudioParameterFloat>("REVERB_MIX", "Reverb Mix", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.25f));

    layout.add(std::move(masterGroup));

    // 2. Master Envelope Group
    auto envGroup = std::make_unique<juce::AudioProcessorParameterGroup>("group_envelope", "Master Envelope", "|");
    envGroup->addChild(std::make_unique<juce::AudioParameterFloat>("ATTACK", "Attack", juce::NormalisableRange<float>(0.001f, 5.0f, 0.001f, 0.5f), 0.01f));
    envGroup->addChild(std::make_unique<juce::AudioParameterFloat>("DECAY", "Decay", juce::NormalisableRange<float>(0.001f, 5.0f, 0.001f, 0.5f), 0.3f));
    envGroup->addChild(std::make_unique<juce::AudioParameterFloat>("SUSTAIN", "Sustain", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.8f));
    envGroup->addChild(std::make_unique<juce::AudioParameterFloat>("RELEASE", "Release", juce::NormalisableRange<float>(0.001f, 5.0f, 0.001f, 0.5f), 0.5f));
    layout.add(std::move(envGroup));

    // 3. Filters Group (with Dynamic Envelope Modulation)
    auto filterGroup = std::make_unique<juce::AudioProcessorParameterGroup>("group_filters", "Filters", "|");
    filterGroup->addChild(std::make_unique<juce::AudioParameterBool>("LPF_ENABLED", "Low Pass Filter", false));
    filterGroup->addChild(std::make_unique<juce::AudioParameterFloat>("LPF_CUTOFF", "LPF Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 0.1f, 0.25f), 20000.0f));
    filterGroup->addChild(std::make_unique<juce::AudioParameterFloat>("LPF_RESO", "LPF Resonance",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f, 0.5f), 0.707f));
    filterGroup->addChild(std::make_unique<juce::AudioParameterFloat>("LPF_ENV_AMT", "LPF Env Amount",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));

    filterGroup->addChild(std::make_unique<juce::AudioParameterBool>("HPF_ENABLED", "High Pass Filter", false));
    filterGroup->addChild(std::make_unique<juce::AudioParameterFloat>("HPF_CUTOFF", "HPF Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 0.1f, 0.25f), 20.0f));
    filterGroup->addChild(std::make_unique<juce::AudioParameterFloat>("HPF_RESO", "HPF Resonance",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f, 0.5f), 0.707f));
    filterGroup->addChild(std::make_unique<juce::AudioParameterFloat>("HPF_ENV_AMT", "HPF Env Amount",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));
    layout.add(std::move(filterGroup));

    // 4. Partials 1..16 Groups
    juce::StringArray modTargets = { "None", "Pitch", "Level", "Pan" };
    juce::StringArray modWaveforms = { "Sine", "Triangle", "Saw", "Square", "Custom 1", "Custom 2", "Custom 3", "Custom 4" };

    for (int i = 1; i <= 16; ++i)
    {
        juce::String idSuffix = juce::String(i);
        auto pGroup = std::make_unique<juce::AudioProcessorParameterGroup>("partial_" + idSuffix, "Partial " + idSuffix, "|");

        pGroup->addChild(std::make_unique<juce::AudioParameterBool>("P_ENABLED" + idSuffix, "Enable " + idSuffix, i <= 4));
        pGroup->addChild(std::make_unique<juce::AudioParameterChoice>("WAVEFORM" + idSuffix, "Waveform " + idSuffix, waveforms, 0));
        pGroup->addChild(std::make_unique<juce::AudioParameterFloat>("RATIO" + idSuffix, "Harmonic " + idSuffix, juce::NormalisableRange<float>(0.01f, 32.0f, 0.01f, 0.4f), (float)i));
        pGroup->addChild(std::make_unique<juce::AudioParameterFloat>("SEMITONE" + idSuffix, "Semitones " + idSuffix, juce::NormalisableRange<float>(-24.0f, 24.0f, 1.0f), 0.0f));
        pGroup->addChild(std::make_unique<juce::AudioParameterFloat>("DETUNE" + idSuffix, "Detune " + idSuffix, juce::NormalisableRange<float>(-50.0f, 50.0f, 1.0f), 0.0f));
        pGroup->addChild(std::make_unique<juce::AudioParameterFloat>("GAIN" + idSuffix, "Gain " + idSuffix, juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), i <= 4 ? 1.0f / i : 0.0f));
        pGroup->addChild(std::make_unique<juce::AudioParameterFloat>("PAN" + idSuffix, "Pan " + idSuffix, juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));

        pGroup->addChild(std::make_unique<juce::AudioParameterChoice>("MOD_TARGET" + idSuffix, "Mod Target " + idSuffix, modTargets, 0));
        pGroup->addChild(std::make_unique<juce::AudioParameterChoice>("MOD_WAVEFORM" + idSuffix, "Mod Wave " + idSuffix, modWaveforms, 0));
        pGroup->addChild(std::make_unique<juce::AudioParameterFloat>("MOD_RATE" + idSuffix, "Mod Rate " + idSuffix, juce::NormalisableRange<float>(0.1f, 20.0f, 0.01f, 0.3f), 2.0f));
        pGroup->addChild(std::make_unique<juce::AudioParameterFloat>("MOD_DEPTH" + idSuffix, "Mod Depth " + idSuffix, juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));
        pGroup->addChild(std::make_unique<juce::AudioParameterFloat>("MOD_PHASE" + idSuffix, "Mod Phase " + idSuffix, juce::NormalisableRange<float>(0.0f, 360.0f, 1.0f), 0.0f));

        // Per-Partial ADSR Envelope
        pGroup->addChild(std::make_unique<juce::AudioParameterBool>("P_ADSR_ENABLED" + idSuffix, "Part " + idSuffix + " ADSR Enable", false));
        pGroup->addChild(std::make_unique<juce::AudioParameterFloat>("P_ATTACK" + idSuffix, "Part " + idSuffix + " Attack", juce::NormalisableRange<float>(0.001f, 5.0f, 0.001f, 0.5f), 0.01f));
        pGroup->addChild(std::make_unique<juce::AudioParameterFloat>("P_DECAY" + idSuffix, "Part " + idSuffix + " Decay", juce::NormalisableRange<float>(0.001f, 5.0f, 0.001f, 0.5f), 0.3f));
        pGroup->addChild(std::make_unique<juce::AudioParameterFloat>("P_SUSTAIN" + idSuffix, "Part " + idSuffix + " Sustain", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));
        pGroup->addChild(std::make_unique<juce::AudioParameterFloat>("P_RELEASE" + idSuffix, "Part " + idSuffix + " Release", juce::NormalisableRange<float>(0.001f, 5.0f, 0.001f, 0.5f), 0.5f));
        pGroup->addChild(std::make_unique<juce::AudioParameterFloat>("P_GLIDE" + idSuffix, "Part " + idSuffix + " Glide", juce::NormalisableRange<float>(0.0f, 2.0f, 0.005f, 0.5f), 0.0f));

        // Per-Partial Keyframe Animation
        pGroup->addChild(std::make_unique<juce::AudioParameterChoice>("ANIM_MODE" + idSuffix, "Anim Mode " + idSuffix, juce::StringArray{"Off", "Note Length", "Time: One-Shot", "Time: Loop", "Time: Ping-Pong"}, 0));
        pGroup->addChild(std::make_unique<juce::AudioParameterChoice>("ANIM_CURVE" + idSuffix, "Anim Curve " + idSuffix, juce::StringArray{"Smooth", "Linear", "Step"}, 0));
        pGroup->addChild(std::make_unique<juce::AudioParameterFloat>("ANIM_DURATION" + idSuffix, "Anim Duration " + idSuffix, juce::NormalisableRange<float>(0.05f, 16.0f, 0.01f, 0.4f), 2.0f));
        pGroup->addChild(std::make_unique<juce::AudioParameterFloat>("ANIM_DEPTH" + idSuffix, "Anim Depth " + idSuffix, juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

        layout.add(std::move(pGroup));
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
            voice->cacheParameters(apvts);
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

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels = (juce::uint32) juce::jmax(1, getTotalNumOutputChannels());

    lowPassFilter.prepare(spec);
    lowPassFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    lowPassFilter.reset();

    highPassFilter.prepare(spec);
    highPassFilter.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    highPassFilter.reset();

    // Smoothed master gain de-zippering
    smoothedMasterGain.reset(sampleRate, 0.05);
    if (auto* mg = apvts.getRawParameterValue("MASTER_GAIN"))
        smoothedMasterGain.setCurrentAndTargetValue(mg->load());

    // Prepare Built-in Effects
    chorusEffect.prepare(spec);
    chorusEffect.reset();

    reverbEffect.prepare(spec);
    reverbEffect.reset();

    int maxDelaySamples = (int)(sampleRate * 2.5);
    delayBuffer.setSize(2, juce::jmax(256, maxDelaySamples));
    delayBuffer.clear();
    delayWritePos = 0;

    heldMidiNotes.clear();
}

void WaveSlaveAudioProcessor::releaseResources()
{
    lowPassFilter.reset();
    highPassFilter.reset();
    chorusEffect.reset();
    reverbEffect.reset();
    delayBuffer.clear();
    delayWritePos = 0;
    heldMidiNotes.clear();
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

    // Query host BPM from AudioPlayHead if available
    if (auto* playHead = getPlayHead())
    {
        if (auto pos = playHead->getPosition())
        {
            if (pos->getBpm().hasValue())
                currentBpm.store((float)*pos->getBpm(), std::memory_order_relaxed);
        }
    }

    // Merge virtual QWERTY keyboard notes into the MIDI stream before processing
    midiKeyboardState.processNextMidiBuffer(midiMessages, 0, buffer.getNumSamples(), true);

    // Determine voice mode directly from the global VOICE_MODE parameter
    int globalVoiceMode = 0; // 0 = Poly, 1 = Mono, 2 = Legato
    if (auto* vm = apvts.getRawParameterValue("VOICE_MODE"))
        globalVoiceMode = (int)vm->load(std::memory_order_relaxed);

    bool isPoly = (globalVoiceMode == 0);
    bool isLegato = (globalVoiceMode == 2);

    std::array<PartialAnimationData, 16> animSnapshot;
    {
        const juce::SpinLock::ScopedLockType sl(animLock);
        animSnapshot = partialAnimations;
    }

    if (isPoly)
    {
        if (!heldMidiNotes.empty())
            heldMidiNotes.clear();
        sustainedNotes.clear();
        sustainPedalDown = false;

        for (const auto metadata : midiMessages)
        {
            auto msg = metadata.getMessage();
            if (msg.isNoteOn())
            {
                float noteFreq = (float)juce::MidiMessage::getMidiNoteInHertz(msg.getNoteNumber());
                lastPlayedFrequency.store(noteFreq, std::memory_order_relaxed);
                lastTriggeredFrequency.store(noteFreq, std::memory_order_relaxed);
                noteTriggerCounter.fetch_add(1, std::memory_order_relaxed);
            }
        }

        for (int i = 0; i < synth.getNumVoices(); i++)
        {
            if (auto voice = dynamic_cast<WaveSlaveVoice*>(synth.getVoice(i)))
                voice->updateParameters(apvts, &animSnapshot);
        }

        synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());
    }
    else // Monophonic (Mono or Legato) - Sample-Accurate Sliced MIDI Processing
    {
        // Stop voices 1..7 so only voice 0 handles monophonic rendering
        for (int v = 1; v < synth.getNumVoices(); ++v)
        {
            if (synth.getVoice(v)->isVoiceActive())
                synth.getVoice(v)->stopNote(0.0f, false);
        }

        auto* voice0 = dynamic_cast<WaveSlaveVoice*>(synth.getVoice(0));
        if (voice0 != nullptr)
        {
            voice0->updateParameters(apvts, &animSnapshot);

            int currentSample = 0;
            int totalNumSamples = buffer.getNumSamples();

            for (const auto metadata : midiMessages)
            {
                int eventSample = juce::jlimit(currentSample, totalNumSamples, metadata.samplePosition);
                int sliceLen = eventSample - currentSample;
                if (sliceLen > 0)
                {
                    voice0->renderNextBlock(buffer, currentSample, sliceLen);
                    currentSample = eventSample;
                }

                auto msg = metadata.getMessage();
                if (msg.isNoteOn())
                {
                    int noteNum = msg.getNoteNumber();
                    float vel = msg.getFloatVelocity();

                    bool hadHeldNotes = !heldMidiNotes.empty();

                    heldMidiNotes.erase(
                        std::remove_if(heldMidiNotes.begin(), heldMidiNotes.end(),
                                       [noteNum](const std::pair<int, float>& p) { return p.first == noteNum; }),
                        heldMidiNotes.end());

                    heldMidiNotes.push_back({ noteNum, vel });

                    float noteFreq = (float)juce::MidiMessage::getMidiNoteInHertz(noteNum);
                    lastPlayedFrequency.store(noteFreq, std::memory_order_relaxed);
                    lastTriggeredFrequency.store(noteFreq, std::memory_order_relaxed);
                    noteTriggerCounter.fetch_add(1, std::memory_order_relaxed);

                    if (!hadHeldNotes)
                    {
                        auto sound = synth.getNumSounds() > 0 ? synth.getSound(0).get() : nullptr;
                        voice0->startNote(noteNum, vel, sound, 0);
                    }
                    else
                    {
                        voice0->setTargetNote(noteNum, vel, isLegato);
                    }
                }
                else if (msg.isNoteOff())
                {
                    int noteNum = msg.getNoteNumber();
                    heldMidiNotes.erase(
                        std::remove_if(heldMidiNotes.begin(), heldMidiNotes.end(),
                                       [noteNum](const std::pair<int, float>& p) { return p.first == noteNum; }),
                        heldMidiNotes.end());

                    if (sustainPedalDown)
                    {
                        sustainedNotes.insert(noteNum);
                    }
                    else
                    {
                        if (heldMidiNotes.empty())
                        {
                            voice0->stopNote(msg.getFloatVelocity(), true);
                        }
                        else
                        {
                            auto [prevNote, prevVel] = heldMidiNotes.back();
                            float prevFreq = (float)juce::MidiMessage::getMidiNoteInHertz(prevNote);
                            lastPlayedFrequency.store(prevFreq, std::memory_order_relaxed);
                            lastTriggeredFrequency.store(prevFreq, std::memory_order_relaxed);
                            noteTriggerCounter.fetch_add(1, std::memory_order_relaxed);

                            voice0->setTargetNote(prevNote, prevVel, isLegato);
                        }
                    }
                }
                else if (msg.isPitchWheel())
                {
                    voice0->pitchWheelMoved(msg.getPitchWheelValue());
                }
                else if (msg.isController())
                {
                    int cc = msg.getControllerNumber();
                    int val = msg.getControllerValue();
                    if (cc == 64) // Sustain Pedal
                    {
                        bool pedalWasDown = sustainPedalDown;
                        sustainPedalDown = (val >= 64);
                        if (pedalWasDown && !sustainPedalDown)
                        {
                            sustainedNotes.clear();
                            if (heldMidiNotes.empty())
                            {
                                voice0->stopNote(0.0f, true);
                            }
                            else
                            {
                                auto [prevNote, prevVel] = heldMidiNotes.back();
                                float prevFreq = (float)juce::MidiMessage::getMidiNoteInHertz(prevNote);
                                lastPlayedFrequency.store(prevFreq, std::memory_order_relaxed);
                                lastTriggeredFrequency.store(prevFreq, std::memory_order_relaxed);
                                noteTriggerCounter.fetch_add(1, std::memory_order_relaxed);
                                voice0->setTargetNote(prevNote, prevVel, isLegato);
                            }
                        }
                    }
                    else
                    {
                        voice0->controllerMoved(cc, val);
                    }
                }
                else if (msg.isAllNotesOff() || msg.isAllSoundOff())
                {
                    heldMidiNotes.clear();
                    sustainedNotes.clear();
                    sustainPedalDown = false;
                    voice0->stopNote(0.0f, false);
                }
            }

            if (currentSample < totalNumSamples)
            {
                voice0->renderNextBlock(buffer, currentSample, totalNumSamples - currentSample);
            }
        }
    }

    // Track whether any voice is currently active and get peak envelope level
    float currentMasterEnv = 0.0f;
    {
        bool anyActive = false;
        int latestIdx = latestActiveVoiceIndex.load(std::memory_order_relaxed);
        bool latestIsActive = false;

        for (int v = 0; v < synth.getNumVoices(); ++v)
        {
            if (auto* wv = dynamic_cast<WaveSlaveVoice*>(synth.getVoice(v)))
            {
                currentMasterEnv = juce::jmax(currentMasterEnv, wv->getCurrentEnvLevel());
                if (wv->isActive())
                {
                    anyActive = true;
                    if (v == latestIdx)
                        latestIsActive = true;
                }
            }
        }

        if (anyActive && !latestIsActive)
        {
            for (int v = 0; v < synth.getNumVoices(); ++v)
            {
                if (auto* wv = dynamic_cast<WaveSlaveVoice*>(synth.getVoice(v)))
                {
                    if (wv->isActive())
                    {
                        latestActiveVoiceIndex.store(v, std::memory_order_relaxed);
                        break;
                    }
                }
            }
        }
        anyVoiceActiveVis.store(anyActive, std::memory_order_relaxed);
    }

    // Apply Low Pass and High Pass filters (with dynamic envelope modulation)
    bool lpfOn = apvts.getRawParameterValue("LPF_ENABLED")->load(std::memory_order_relaxed) > 0.5f;
    bool hpfOn = apvts.getRawParameterValue("HPF_ENABLED")->load(std::memory_order_relaxed) > 0.5f;

    if (lpfOn || hpfOn)
    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);

        float sampleRate = (float)getSampleRate();
        if (sampleRate <= 0.0f) sampleRate = 44100.0f;
        float maxCutoff = sampleRate * 0.49f;

        if (lpfOn)
        {
            float baseCutoff = apvts.getRawParameterValue("LPF_CUTOFF")->load(std::memory_order_relaxed);
            float envAmt = 0.0f;
            if (auto* ep = apvts.getRawParameterValue("LPF_ENV_AMT"))
                envAmt = ep->load(std::memory_order_relaxed);

            float modCutoff = baseCutoff * std::pow(2.0f, envAmt * currentMasterEnv * 5.0f);
            float cutoff = juce::jlimit(20.0f, maxCutoff, modCutoff);
            float reso   = juce::jlimit(0.1f, 10.0f, apvts.getRawParameterValue("LPF_RESO")->load(std::memory_order_relaxed));
            lowPassFilter.setCutoffFrequency(cutoff);
            lowPassFilter.setResonance(reso);
            lowPassFilter.process(context);
        }

        if (hpfOn)
        {
            float baseCutoff = apvts.getRawParameterValue("HPF_CUTOFF")->load(std::memory_order_relaxed);
            float envAmt = 0.0f;
            if (auto* ep = apvts.getRawParameterValue("HPF_ENV_AMT"))
                envAmt = ep->load(std::memory_order_relaxed);

            float modCutoff = baseCutoff * std::pow(2.0f, envAmt * currentMasterEnv * 5.0f);
            float cutoff = juce::jlimit(20.0f, maxCutoff, modCutoff);
            float reso   = juce::jlimit(0.1f, 10.0f, apvts.getRawParameterValue("HPF_RESO")->load(std::memory_order_relaxed));
            highPassFilter.setCutoffFrequency(cutoff);
            highPassFilter.setResonance(reso);
            highPassFilter.process(context);
        }
    }

    // Parameter smoothing for Master Gain (eliminates zipper noise)
    if (auto* mg = apvts.getRawParameterValue("MASTER_GAIN"))
    {
        smoothedMasterGain.setTargetValue(mg->load(std::memory_order_relaxed));
        smoothedMasterGain.applyGain(buffer, buffer.getNumSamples());
    }

    // Master Soft Clipper (polyphonic saturation to prevent digital wrapping/harsh clips)
    bool clipperOn = true;
    if (auto* cp = apvts.getRawParameterValue("MASTER_CLIPPER"))
        clipperOn = cp->load(std::memory_order_relaxed) > 0.5f;

    if (clipperOn)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* data = buffer.getWritePointer(ch);
            for (int s = 0; s < buffer.getNumSamples(); ++s)
            {
                data[s] = std::tanh(data[s]);
            }
        }
    }

    // Built-in Effect: Chorus
    if (auto* cp = apvts.getRawParameterValue("CHORUS_ENABLED"))
    {
        if (cp->load(std::memory_order_relaxed) > 0.5f)
        {
            float rate = apvts.getRawParameterValue("CHORUS_RATE")->load(std::memory_order_relaxed);
            float depth = apvts.getRawParameterValue("CHORUS_DEPTH")->load(std::memory_order_relaxed);
            float mix = apvts.getRawParameterValue("CHORUS_MIX")->load(std::memory_order_relaxed);
            chorusEffect.setRate(rate);
            chorusEffect.setDepth(depth);
            chorusEffect.setMix(mix);
            juce::dsp::AudioBlock<float> block(buffer);
            juce::dsp::ProcessContextReplacing<float> context(block);
            chorusEffect.process(context);
        }
    }

    // Built-in Effect: Delay (Stereo Cross-Feedback)
    if (auto* dp = apvts.getRawParameterValue("DELAY_ENABLED"))
    {
        if (dp->load(std::memory_order_relaxed) > 0.5f && delayBuffer.getNumSamples() > 0)
        {
            float timeSec = apvts.getRawParameterValue("DELAY_TIME")->load(std::memory_order_relaxed);
            float feedback = juce::jlimit(0.0f, 0.95f, apvts.getRawParameterValue("DELAY_FEEDBACK")->load(std::memory_order_relaxed));
            float mix = juce::jlimit(0.0f, 1.0f, apvts.getRawParameterValue("DELAY_MIX")->load(std::memory_order_relaxed));
            int delaySamples = juce::jlimit(1, delayBuffer.getNumSamples() - 1, (int)(timeSec * getSampleRate()));

            for (int s = 0; s < buffer.getNumSamples(); ++s)
            {
                int readPos = (delayWritePos - delaySamples + delayBuffer.getNumSamples()) % delayBuffer.getNumSamples();
                float dL = delayBuffer.getSample(0, readPos);
                float dR = delayBuffer.getSample(1, readPos);

                float inL = buffer.getSample(0, s);
                float inR = (buffer.getNumChannels() > 1) ? buffer.getSample(1, s) : inL;

                delayBuffer.setSample(0, delayWritePos, inL + dR * feedback);
                delayBuffer.setSample(1, delayWritePos, inR + dL * feedback);

                delayWritePos = (delayWritePos + 1) % delayBuffer.getNumSamples();

                buffer.setSample(0, s, inL * (1.0f - mix) + dL * mix);
                if (buffer.getNumChannels() > 1)
                    buffer.setSample(1, s, inR * (1.0f - mix) + dR * mix);
            }
        }
    }

    // Built-in Effect: Reverb
    if (auto* rp = apvts.getRawParameterValue("REVERB_ENABLED"))
    {
        if (rp->load(std::memory_order_relaxed) > 0.5f)
        {
            juce::dsp::Reverb::Parameters revParams;
            revParams.roomSize = apvts.getRawParameterValue("REVERB_SIZE")->load(std::memory_order_relaxed);
            revParams.damping  = apvts.getRawParameterValue("REVERB_DAMP")->load(std::memory_order_relaxed);
            revParams.wetLevel = apvts.getRawParameterValue("REVERB_MIX")->load(std::memory_order_relaxed);
            revParams.dryLevel = 1.0f - revParams.wetLevel * 0.5f;
            revParams.width    = 1.0f;
            reverbEffect.setParameters(revParams);

            juce::dsp::AudioBlock<float> block(buffer);
            juce::dsp::ProcessContextReplacing<float> context(block);
            reverbEffect.process(context);
        }
    }

    // Peak Metering: capture max peak for GUI thread reading
    {
        float maxL = buffer.getMagnitude(0, 0, buffer.getNumSamples());
        float maxR = (buffer.getNumChannels() > 1) ? buffer.getMagnitude(1, 0, buffer.getNumSamples()) : maxL;

        float curL = peakMeterL.load(std::memory_order_relaxed);
        while (maxL > curL && !peakMeterL.compare_exchange_weak(curL, maxL, std::memory_order_relaxed)) {}

        float curR = peakMeterR.load(std::memory_order_relaxed);
        while (maxR > curR && !peakMeterR.compare_exchange_weak(curR, maxR, std::memory_order_relaxed)) {}
    }
    
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

void WaveSlaveAudioProcessor::allNotesOff()
{
    midiKeyboardState.allNotesOff(1);
    heldMidiNotes.clear();
    sustainedNotes.clear();
    sustainPedalDown = false;
    for (int i = 0; i < synth.getNumVoices(); ++i)
    {
        synth.getVoice(i)->stopNote(0.0f, false);
    }
    anyVoiceActiveVis.store(false, std::memory_order_relaxed);
    syncLivePartialStateFromParams();
}

void WaveSlaveAudioProcessor::syncLivePartialStateFromParams()
{
    int baseNote = 60;
    if (auto* bn = apvts.getRawParameterValue("BASE_NOTE"))
        baseNote = (int)bn->load();
    lastBaseNoteParam = baseNote;
    float baseFreq = 440.0f * std::pow(2.0f, (baseNote - 69.0f) / 12.0f);
    lastPlayedFrequency.store(baseFreq, std::memory_order_relaxed);
    lastTriggeredFrequency.store(baseFreq, std::memory_order_relaxed);

    float masterPan = 0.0f;
    if (auto* mp = apvts.getRawParameterValue("MASTER_PAN"))
        masterPan = mp->load();

    for (int i = 0; i < 16; ++i)
    {
        juce::String s = juce::String(i + 1);
        float ratio = (float)(i + 1);
        float gain  = 1.0f / (float)(i + 1);
        float semi  = 0.0f;
        float det   = 0.0f;
        float pan   = 0.0f;

        if (auto* p = apvts.getRawParameterValue("RATIO" + s))    ratio = p->load();
        if (auto* p = apvts.getRawParameterValue("GAIN" + s))     gain  = p->load();
        if (auto* p = apvts.getRawParameterValue("SEMITONE" + s)) semi  = p->load();
        if (auto* p = apvts.getRawParameterValue("DETUNE" + s))   det   = p->load();
        if (auto* p = apvts.getRawParameterValue("PAN" + s))      pan   = p->load();

        float effSemi = semi + (det / 100.0f);
        float effRatio = juce::jlimit(0.01f, 32.0f, ratio * std::pow(2.0f, effSemi / 12.0f));
        float totalPan = juce::jlimit(-1.0f, 1.0f, masterPan + pan);

        livePartialState[i].effectiveRatio.store(effRatio, std::memory_order_relaxed);
        livePartialState[i].effectiveGain.store(gain, std::memory_order_relaxed);
        livePartialState[i].effectiveSemitone.store(juce::jlimit(-24.0f, 24.0f, effSemi), std::memory_order_relaxed);
        livePartialState[i].effectiveDetune.store(det, std::memory_order_relaxed);
        livePartialState[i].effectivePan.store(totalPan, std::memory_order_relaxed);
        livePartialState[i].animPhase.store(0.0f, std::memory_order_relaxed);
        livePartialState[i].envLevel.store(1.0f, std::memory_order_relaxed);
        livePartialState[i].lfoValue.store(0.0f, std::memory_order_relaxed);
    }
}

PartialKeyframe WaveSlaveAudioProcessor::getPartialBaseSettings(int partialIndex) const
{
    partialIndex = juce::jlimit(0, 15, partialIndex);
    juce::String s = juce::String(partialIndex + 1);
    PartialKeyframe kf;
    kf.time = 0.0f;
    if (auto* p = apvts.getRawParameterValue("RATIO" + s))    kf.ratio    = p->load();
    if (auto* p = apvts.getRawParameterValue("GAIN" + s))     kf.gain     = p->load();
    if (auto* p = apvts.getRawParameterValue("SEMITONE" + s)) kf.semitone = p->load();
    if (auto* p = apvts.getRawParameterValue("DETUNE" + s))   kf.detune   = p->load();
    if (auto* p = apvts.getRawParameterValue("PAN" + s))      kf.pan      = p->load();
    return kf;
}

PartialAnimationData WaveSlaveAudioProcessor::getPartialAnimation(int partialIndex) const
{
    partialIndex = juce::jlimit(0, 15, partialIndex);
    const juce::SpinLock::ScopedLockType sl(animLock);
    return partialAnimations[partialIndex];
}

void WaveSlaveAudioProcessor::setPartialAnimation(int partialIndex, const PartialAnimationData& data)
{
    partialIndex = juce::jlimit(0, 15, partialIndex);
    PartialAnimationData sorted = data;
    sorted.numKeyframes = juce::jlimit(0, PartialAnimationData::maxKeyframes, sorted.numKeyframes);
    std::sort(sorted.keyframes.begin(), sorted.keyframes.begin() + sorted.numKeyframes,
              [](const PartialKeyframe& a, const PartialKeyframe& b) { return a.time < b.time; });
    const juce::SpinLock::ScopedLockType sl(animLock);
    partialAnimations[partialIndex] = sorted;
}

int WaveSlaveAudioProcessor::addOrUpdateKeyframe(int partialIndex, const PartialKeyframe& kf, float timeTolerance)
{
    partialIndex = juce::jlimit(0, 15, partialIndex);
    PartialKeyframe clamped = kf;
    clamped.time     = juce::jlimit(0.0f, 1.0f, clamped.time);
    clamped.ratio    = juce::jlimit(0.01f, 32.0f, clamped.ratio);
    clamped.gain     = juce::jlimit(0.0f, 1.0f, clamped.gain);
    clamped.semitone = juce::jlimit(-24.0f, 24.0f, clamped.semitone);
    clamped.detune   = juce::jlimit(-50.0f, 50.0f, clamped.detune);
    clamped.pan      = juce::jlimit(-1.0f, 1.0f, clamped.pan);

    const juce::SpinLock::ScopedLockType sl(animLock);
    auto& track = partialAnimations[partialIndex];

    // Find the closest existing keyframe within timeTolerance
    int bestIdx = -1;
    float bestDist = timeTolerance;
    for (int i = 0; i < track.numKeyframes; ++i)
    {
        float dist = std::abs(track.keyframes[i].time - clamped.time);
        if (dist <= bestDist)
        {
            bestDist = dist;
            bestIdx = i;
        }
    }

    if (bestIdx >= 0)
    {
        clamped.time = track.keyframes[bestIdx].time; // Preserve exact time when updating in-place
        track.keyframes[bestIdx] = clamped;
        return bestIdx;
    }

    if (track.numKeyframes < PartialAnimationData::maxKeyframes)
    {
        track.keyframes[track.numKeyframes] = clamped;
        track.numKeyframes++;
        std::sort(track.keyframes.begin(), track.keyframes.begin() + track.numKeyframes,
                  [](const PartialKeyframe& a, const PartialKeyframe& b) { return a.time < b.time; });
        for (int i = 0; i < track.numKeyframes; ++i)
        {
            if (std::abs(track.keyframes[i].time - clamped.time) < 0.0001f)
                return i;
        }
    }
    else if (track.numKeyframes > 0)
    {
        // When all 16 keyframe slots are full, replace the closest keyframe in time so edits never freeze
        int closestIdx = 0;
        float minD = std::abs(track.keyframes[0].time - clamped.time);
        for (int i = 1; i < track.numKeyframes; ++i)
        {
            float d = std::abs(track.keyframes[i].time - clamped.time);
            if (d < minD)
            {
                minD = d;
                closestIdx = i;
            }
        }
        track.keyframes[closestIdx] = clamped;
        std::sort(track.keyframes.begin(), track.keyframes.begin() + track.numKeyframes,
                  [](const PartialKeyframe& a, const PartialKeyframe& b) { return a.time < b.time; });
        for (int i = 0; i < track.numKeyframes; ++i)
        {
            if (std::abs(track.keyframes[i].time - clamped.time) < 0.0001f)
                return i;
        }
        return closestIdx;
    }
    return track.numKeyframes - 1;
}

void WaveSlaveAudioProcessor::removeKeyframe(int partialIndex, int keyframeIndex)
{
    partialIndex = juce::jlimit(0, 15, partialIndex);
    const juce::SpinLock::ScopedLockType sl(animLock);
    auto& track = partialAnimations[partialIndex];
    if (keyframeIndex >= 0 && keyframeIndex < track.numKeyframes)
    {
        for (int i = keyframeIndex; i < track.numKeyframes - 1; ++i)
            track.keyframes[i] = track.keyframes[i + 1];
        track.numKeyframes--;
    }
}

int WaveSlaveAudioProcessor::moveKeyframeTime(int partialIndex, int keyframeIndex, float newTime)
{
    partialIndex = juce::jlimit(0, 15, partialIndex);
    newTime = juce::jlimit(0.0f, 1.0f, newTime);
    const juce::SpinLock::ScopedLockType sl(animLock);
    auto& track = partialAnimations[partialIndex];
    if (keyframeIndex < 0 || keyframeIndex >= track.numKeyframes)
        return -1;

    // Remove any other keyframe that already occupies newTime (within 0.002f) to prevent duplicate timestamps
    for (int i = track.numKeyframes - 1; i >= 0; --i)
    {
        if (i != keyframeIndex && std::abs(track.keyframes[i].time - newTime) < 0.002f)
        {
            for (int j = i; j < track.numKeyframes - 1; ++j)
                track.keyframes[j] = track.keyframes[j + 1];
            track.numKeyframes--;
            if (i < keyframeIndex)
                keyframeIndex--;
        }
    }

    PartialKeyframe moved = track.keyframes[keyframeIndex];
    moved.time = newTime;
    track.keyframes[keyframeIndex] = moved;

    std::sort(track.keyframes.begin(), track.keyframes.begin() + track.numKeyframes,
              [](const PartialKeyframe& a, const PartialKeyframe& b) { return a.time < b.time; });

    for (int i = 0; i < track.numKeyframes; ++i)
    {
        if (std::abs(track.keyframes[i].time - newTime) < 0.0001f)
            return i;
    }
    return keyframeIndex;
}

void WaveSlaveAudioProcessor::clearKeyframes(int partialIndex)
{
    partialIndex = juce::jlimit(0, 15, partialIndex);
    const juce::SpinLock::ScopedLockType sl(animLock);
    partialAnimations[partialIndex].numKeyframes = 0;
}

void WaveSlaveAudioProcessor::clearAllKeyframes()
{
    const juce::SpinLock::ScopedLockType sl(animLock);
    for (int i = 0; i < 16; ++i)
        partialAnimations[i].numKeyframes = 0;
}

void WaveSlaveAudioProcessor::ensureInitialKeyframes(int partialIndex)
{
    partialIndex = juce::jlimit(0, 15, partialIndex);
    PartialKeyframe baseKf = getPartialBaseSettings(partialIndex);
    const juce::SpinLock::ScopedLockType sl(animLock);
    auto& track = partialAnimations[partialIndex];
    if (track.numKeyframes == 0)
    {
        baseKf.time = 0.0f;
        track.keyframes[0] = baseKf;
        PartialKeyframe endKf = baseKf;
        endKf.time = 1.0f;
        track.keyframes[1] = endKf;
        track.numKeyframes = 2;
    }
}

void WaveSlaveAudioProcessor::serializeAnimationsToXml(juce::XmlElement& parentXml) const
{
    auto* animsXml = parentXml.createNewChildElement("PartialAnimations");
    for (int p = 0; p < 16; ++p)
    {
        PartialAnimationData track = getPartialAnimation(p);
        if (track.numKeyframes > 0)
        {
            auto* pXml = animsXml->createNewChildElement("Partial");
            pXml->setAttribute("index", p);
            for (int k = 0; k < track.numKeyframes; ++k)
            {
                const auto& kf = track.keyframes[k];
                auto* kfXml = pXml->createNewChildElement("Keyframe");
                kfXml->setAttribute("time",     (double)kf.time);
                kfXml->setAttribute("ratio",    (double)kf.ratio);
                kfXml->setAttribute("gain",     (double)kf.gain);
                kfXml->setAttribute("semitone", (double)kf.semitone);
                kfXml->setAttribute("detune",   (double)kf.detune);
                kfXml->setAttribute("pan",      (double)kf.pan);
            }
        }
    }
}

void WaveSlaveAudioProcessor::deserializeAnimationsFromXml(const juce::XmlElement& parentXml)
{
    clearAllKeyframes();
    if (auto* animsXml = parentXml.getChildByName("PartialAnimations"))
    {
        for (auto* pXml : animsXml->getChildIterator())
        {
            if (pXml->hasTagName("Partial"))
            {
                int p = pXml->getIntAttribute("index", -1);
                if (p >= 0 && p < 16)
                {
                    PartialAnimationData track;
                    for (auto* kfXml : pXml->getChildIterator())
                    {
                        if (kfXml->hasTagName("Keyframe") && track.numKeyframes < PartialAnimationData::maxKeyframes)
                        {
                            PartialKeyframe kf;
                            kf.time     = (float)kfXml->getDoubleAttribute("time", 0.0);
                            kf.ratio    = (float)kfXml->getDoubleAttribute("ratio", (double)(p + 1));
                            kf.gain     = (float)kfXml->getDoubleAttribute("gain", 0.5);
                            kf.semitone = (float)kfXml->getDoubleAttribute("semitone", 0.0);
                            kf.detune   = (float)kfXml->getDoubleAttribute("detune", 0.0);
                            kf.pan      = (float)kfXml->getDoubleAttribute("pan", 0.0);
                            track.keyframes[track.numKeyframes++] = kf;
                        }
                    }
                    setPartialAnimation(p, track);
                }
            }
        }
    }
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
    if (xml != nullptr)
    {
        xml->setAttribute("editorWidth", editorWidth);
        xml->setAttribute("editorHeight", editorHeight);

        auto* wtXml = xml->createNewChildElement("CustomWavetables");
        for (int s = 0; s < numCustomSlots; ++s)
        {
            auto* slotXml = wtXml->createNewChildElement("Slot");
            slotXml->setAttribute("index", s);
            juce::MemoryOutputStream mos;
            for (int i = 0; i < 512; ++i)
                mos.writeFloat(customWavetables[s][i].load(std::memory_order_relaxed));
            slotXml->setAttribute("samples", mos.getMemoryBlock().toBase64Encoding());

            const auto& points = customSplinePoints[s];
            if (!points.empty())
            {
                juce::String ptStr;
                for (int p = 0; p < (int)points.size(); ++p)
                {
                    if (p > 0) ptStr << ";";
                    ptStr << juce::String(points[p].x, 4) << "," << juce::String(points[p].y, 4);
                }
                slotXml->setAttribute("splinePoints", ptStr);
            }
        }
        serializeAnimationsToXml(*xml);
        copyXmlToBinary (*xml, destData);
    }
}

void WaveSlaveAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState.get() != nullptr)
    {
        if (xmlState->hasTagName (apvts.state.getType()))
        {
            editorWidth = xmlState->getIntAttribute("editorWidth", 940);
            editorHeight = xmlState->getIntAttribute("editorHeight", 660);
            if (auto* editor = getActiveEditor())
                editor->setSize(editorWidth, editorHeight);

            if (auto* wtXml = xmlState->getChildByName("CustomWavetables"))
            {
                for (auto* slotXml : wtXml->getChildIterator())
                {
                    int s = slotXml->getIntAttribute("index", -1);
                    if (s >= 0 && s < numCustomSlots)
                    {
                        juce::MemoryBlock mb;
                        mb.fromBase64Encoding(slotXml->getStringAttribute("samples"));
                        if (mb.getSize() >= 512 * sizeof(float))
                        {
                            juce::MemoryInputStream mis(mb, false);
                            for (int i = 0; i < 512; ++i)
                                customWavetables[s][i].store(mis.readFloat(), std::memory_order_relaxed);
                        }

                        juce::String ptStr = slotXml->getStringAttribute("splinePoints");
                        if (ptStr.isNotEmpty())
                        {
                            customSplinePoints[s].clear();
                            auto tokens = juce::StringArray::fromTokens(ptStr, ";", "");
                            for (auto& tok : tokens)
                            {
                                auto xy = juce::StringArray::fromTokens(tok, ",", "");
                                if (xy.size() == 2)
                                    customSplinePoints[s].push_back({ xy[0].getFloatValue(), xy[1].getFloatValue() });
                            }
                        }
                    }
                }
            }
            deserializeAnimationsFromXml(*xmlState);
            apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WaveSlaveAudioProcessor();
}
