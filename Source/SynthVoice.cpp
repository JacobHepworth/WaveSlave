#include "SynthVoice.h"

WaveSlaveVoice::WaveSlaveVoice()
{
    baseOscillator.setWaveform(0);
    for (int i = 0; i < maxPartials; ++i)
    {
        oscillators[i].setWaveform(0);
    }
}

void WaveSlaveVoice::setCustomWavetable(std::array<std::atomic<float>, 512>* table)
{
    baseOscillator.setCustomWavetable(table);
    for (int i = 0; i < maxPartials; ++i)
    {
        oscillators[i].setCustomWavetable(table);
    }
}

bool WaveSlaveVoice::canPlaySound (juce::SynthesiserSound* sound)
{
    return dynamic_cast<WaveSlaveSound*> (sound) != nullptr;
}

void WaveSlaveVoice::prepareToPlay(double sampleRate, int samplesPerBlock, int outputChannels)
{
    currentSampleRate = (float)sampleRate;
    adsr.setSampleRate(sampleRate);
}

void WaveSlaveVoice::updateParameters(juce::AudioProcessorValueTreeState& apvts)
{
    adsrParams.attack = apvts.getRawParameterValue("ATTACK")->load();
    adsrParams.decay = apvts.getRawParameterValue("DECAY")->load();
    adsrParams.sustain = apvts.getRawParameterValue("SUSTAIN")->load();
    adsrParams.release = apvts.getRawParameterValue("RELEASE")->load();
    adsr.setParameters(adsrParams);
    
    activePartials = (int)apvts.getRawParameterValue("NUM_PARTIALS")->load();

    if (auto* bn = apvts.getRawParameterValue("BASE_NOTE"))
    {
        baseNoteOffset = (int)bn->load() - 60; // 60 is middle C (C4), no transpose
    }

    if (auto* bw = apvts.getRawParameterValue("BASE_WAVEFORM"))
    {
        int newBaseW = (int)bw->load();
        if (newBaseW != baseWaveform)
        {
            baseWaveform = newBaseW;
            baseOscillator.setWaveform(baseWaveform);
        }
    }

    for (int i = 0; i < maxPartials; ++i)
    {
        juce::String idSuffix = juce::String(i + 1);
        partialRatios[i] = apvts.getRawParameterValue("RATIO" + idSuffix)->load();
        partialDetunes[i] = apvts.getRawParameterValue("DETUNE" + idSuffix)->load();
        partialGains[i] = apvts.getRawParameterValue("GAIN" + idSuffix)->load();
        partialSemitones[i] = apvts.getRawParameterValue("SEMITONE" + idSuffix)->load();
        
        int newWaveform = (int)apvts.getRawParameterValue("WAVEFORM" + idSuffix)->load();
        if (newWaveform != partialWaveforms[i])
        {
            partialWaveforms[i] = newWaveform;
            oscillators[i].setWaveform(newWaveform);
        }

        partialModTargets[i] = (int)apvts.getRawParameterValue("MOD_TARGET" + idSuffix)->load();
        partialModWaveforms[i] = (int)apvts.getRawParameterValue("MOD_WAVEFORM" + idSuffix)->load();
        partialModRates[i] = apvts.getRawParameterValue("MOD_RATE" + idSuffix)->load();
        partialModDepths[i] = apvts.getRawParameterValue("MOD_DEPTH" + idSuffix)->load();
        partialModPhaseOffsets[i] = apvts.getRawParameterValue("MOD_PHASE" + idSuffix)->load();
    }
}

void WaveSlaveVoice::startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int /*currentPitchWheelPosition*/)
{
    int transposedNote = juce::jlimit(0, 127, midiNoteNumber + baseNoteOffset);
    currentFrequency = (float)juce::MidiMessage::getMidiNoteInHertz(transposedNote);
    adsr.noteOn();
    
    baseOscillator.reset();
    baseOscillator.setFrequency(currentFrequency, currentSampleRate);

    for (int i = 0; i < activePartials; ++i)
    {
        oscillators[i].reset();
        partialLfoPhases[i] = partialModPhaseOffsets[i] / 360.0f;
        float semitoneShift = std::pow(2.0f, partialSemitones[i] / 12.0f);
        float detuneShift = std::pow(2.0f, partialDetunes[i] / 1200.0f);
        oscillators[i].setFrequency(currentFrequency * semitoneShift * detuneShift * partialRatios[i], currentSampleRate);
    }
}

void WaveSlaveVoice::stopNote (float /*velocity*/, bool allowTailOff)
{
    if (allowTailOff)
    {
        adsr.noteOff();
    }
    else
    {
        clearCurrentNote();
        adsr.reset();
    }
}

void WaveSlaveVoice::pitchWheelMoved (int /*newPitchWheelValue*/) {}
void WaveSlaveVoice::controllerMoved (int /*controllerNumber*/, int /*newControllerValue*/) {}

void WaveSlaveVoice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    jassert(outputBuffer.getNumChannels() > 0);
    
    if (!adsr.isActive())
    {
        clearCurrentNote();
        return;
    }
    
    synthBuffer.setSize(1, numSamples, false, false, true);
    synthBuffer.clear();
    
    // Render base note fundamental if no partials are active
    bool playBaseOsc = (activePartials == 0);
    if (playBaseOsc)
    {
        baseOscillator.setFrequency(currentFrequency, currentSampleRate);
    }

    for (int i = 0; i < activePartials; ++i)
    {
        float semitoneShift = std::pow(2.0f, partialSemitones[i] / 12.0f);
        float detuneShift = std::pow(2.0f, partialDetunes[i] / 1200.0f);
        oscillators[i].setFrequency(currentFrequency * semitoneShift * detuneShift * partialRatios[i], currentSampleRate);
    }

    // Render partials with envelope
    bool capturePartials = (partialsVisBuffer != nullptr && partialsVisBuffer->getNumChannels() >= activePartials);
    for (int sample = 0; sample < numSamples; ++sample)
    {
        float env = adsr.getNextSample();
        float currentSample = 0.0f;

        if (playBaseOsc)
        {
            currentSample += baseOscillator.processSample() * env;
        }

        for (int i = 0; i < activePartials; ++i)
        {
            float lfoVal = 0.0f;
            if (partialModTargets[i] > 0)
            {
                float phase = partialLfoPhases[i];
                switch (partialModWaveforms[i]) {
                    case 0: // Sine
                        lfoVal = std::sin(phase * juce::MathConstants<float>::twoPi);
                        break;
                    case 1: // Triangle
                        lfoVal = 2.0f * std::abs(2.0f * phase - 1.0f) - 1.0f;
                        break;
                    case 2: // Saw
                        lfoVal = 2.0f * phase - 1.0f;
                        break;
                    case 3: // Square
                        lfoVal = phase < 0.5f ? 1.0f : -1.0f;
                        break;
                }
                
                partialLfoPhases[i] += partialModRates[i] / currentSampleRate;
                if (partialLfoPhases[i] >= 1.0f) partialLfoPhases[i] -= 1.0f;
            }

            float currentGain = partialGains[i] * env;
            
            if (partialModTargets[i] == 1) // Pitch
            {
                float semitoneShift = std::pow(2.0f, partialSemitones[i] / 12.0f);
                float detuneShift = std::pow(2.0f, partialDetunes[i] / 1200.0f);
                float baseFreq = currentFrequency * semitoneShift * detuneShift * partialRatios[i];
                // Modulate pitch by up to +/- 1 semitone
                float modPitchShift = std::pow(2.0f, (lfoVal * partialModDepths[i]) / 12.0f);
                oscillators[i].setFrequency(baseFreq * modPitchShift, currentSampleRate);
            }
            else if (partialModTargets[i] == 2) // Level
            {
                // Modulate gain down to 0 at 100% depth
                float tremolo = 1.0f - (partialModDepths[i] * 0.5f * (1.0f - lfoVal));
                currentGain *= tremolo;
            }

            float pSample = oscillators[i].processSample() * currentGain;
            currentSample += pSample;
            if (capturePartials && sample < partialsVisBuffer->getNumSamples())
            {
                partialsVisBuffer->addSample(i, sample, pSample);
            }
        }
        synthBuffer.setSample(0, sample, currentSample);
    }
    
    for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
    {
        outputBuffer.addFrom(channel, startSample, synthBuffer, 0, 0, numSamples);
    }
    
    if (!adsr.isActive())
    {
        clearCurrentNote();
    }
}
