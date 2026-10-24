#include "SynthVoice.h"

WaveSlaveVoice::WaveSlaveVoice()
{
    for (int i = 0; i < maxPartials; ++i)
    {
        oscillators[i].setWaveform(0);
        partialPans[i] = 0.0f;
        partialAdsrEnabled[i] = true;
        partialGlideTimes[i] = 0.0f;
        partialCurrentFreq[i] = 0.0f;
        partialAnimModes[i] = 0;
        partialAnimCurves[i] = 0;
        partialAnimDurations[i] = 2.0f;
        partialAnimDepths[i] = 1.0f;
        partialAnimElapsed[i] = 0.0f;
        partialAnimPhase[i] = 0.0f;
        partialNoteHeld[i] = false;
        partialReleaseElapsed[i] = 0.0f;
        partialReleaseStartPhase[i] = 0.0f;
    }
}

void WaveSlaveVoice::setCustomWavetables(std::array<std::array<std::atomic<float>, 512>, 4>* tables)
{
    customWavetables = tables;
    for (int i = 0; i < maxPartials; ++i)
    {
        oscillators[i].setCustomWavetables(tables);
    }
}

bool WaveSlaveVoice::canPlaySound (juce::SynthesiserSound* sound)
{
    return dynamic_cast<WaveSlaveSound*> (sound) != nullptr;
}

void WaveSlaveVoice::prepareToPlay(double sampleRate, int samplesPerBlock, int /*outputChannels*/)
{
    currentSampleRate = (float)sampleRate;
    adsr.setSampleRate(sampleRate);
    for (int i = 0; i < maxPartials; ++i)
        partialAdsr[i].setSampleRate(sampleRate);
    synthBuffer.setSize(2, juce::jmax(256, samplesPerBlock * 2), false, true, false);
}

void WaveSlaveVoice::updateParameters(juce::AudioProcessorValueTreeState& apvts, const std::array<PartialAnimationData, 16>* animTracks)
{
    adsrParams.attack = apvts.getRawParameterValue("ATTACK")->load();
    adsrParams.decay = apvts.getRawParameterValue("DECAY")->load();
    adsrParams.sustain = apvts.getRawParameterValue("SUSTAIN")->load();
    adsrParams.release = apvts.getRawParameterValue("RELEASE")->load();
    adsr.setParameters(adsrParams);
    
    activePartials = juce::jlimit(1, maxPartials, (int)apvts.getRawParameterValue("NUM_PARTIALS")->load());

    if (auto* mp = apvts.getRawParameterValue("MASTER_PAN"))
        masterPan = mp->load();

    if (auto* vm = apvts.getRawParameterValue("VOICE_MODE"))
        voiceMode = (int)vm->load();

    if (auto* gt = apvts.getRawParameterValue("GLIDE_TIME"))
        glideTime = gt->load();

    if (auto* ve = apvts.getRawParameterValue("VELOCITY_ENABLED"))
        velocityEnabled = ve->load() > 0.5f;

    for (int i = 0; i < maxPartials; ++i)
    {
        juce::String idSuffix = juce::String(i + 1);
        if (auto* ep = apvts.getRawParameterValue("P_ENABLED" + idSuffix))
            partialEnabled[i] = ep->load() > 0.5f;
        else
            partialEnabled[i] = true;

        partialRatios[i] = apvts.getRawParameterValue("RATIO" + idSuffix)->load();
        partialDetunes[i] = apvts.getRawParameterValue("DETUNE" + idSuffix)->load();
        partialGains[i] = apvts.getRawParameterValue("GAIN" + idSuffix)->load();
        partialSemitones[i] = apvts.getRawParameterValue("SEMITONE" + idSuffix)->load();
        
        if (auto* pp = apvts.getRawParameterValue("PAN" + idSuffix))
            partialPans[i] = pp->load();

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

        // Per-partial ADSR
        if (auto* pae = apvts.getRawParameterValue("P_ADSR_ENABLED" + idSuffix))
            partialAdsrEnabled[i] = pae->load() > 0.5f;
        else
            partialAdsrEnabled[i] = true;

        if (auto* pg = apvts.getRawParameterValue("P_GLIDE" + idSuffix))
            partialGlideTimes[i] = pg->load();
        else
            partialGlideTimes[i] = 0.0f;

        partialAdsrParams[i].attack = apvts.getRawParameterValue("P_ATTACK" + idSuffix)->load();
        partialAdsrParams[i].decay = apvts.getRawParameterValue("P_DECAY" + idSuffix)->load();
        partialAdsrParams[i].sustain = apvts.getRawParameterValue("P_SUSTAIN" + idSuffix)->load();
        partialAdsrParams[i].release = apvts.getRawParameterValue("P_RELEASE" + idSuffix)->load();
        partialAdsr[i].setParameters(partialAdsrParams[i]);

        // Per-partial Keyframe Animation (replaces Mod 2)
        if (auto* p = apvts.getRawParameterValue("ANIM_MODE" + idSuffix))
            partialAnimModes[i] = (int)p->load();
        if (auto* p = apvts.getRawParameterValue("ANIM_CURVE" + idSuffix))
            partialAnimCurves[i] = (int)p->load();
        if (auto* p = apvts.getRawParameterValue("ANIM_DURATION" + idSuffix))
            partialAnimDurations[i] = p->load();
        if (auto* p = apvts.getRawParameterValue("ANIM_DEPTH" + idSuffix))
            partialAnimDepths[i] = p->load();

        if (animTracks != nullptr)
            partialAnimTracks[i] = (*animTracks)[i];
    }
}

void WaveSlaveVoice::startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int /*currentPitchWheelPosition*/)
{
    targetFrequency = (float)juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
    currentVelocity = juce::jlimit(0.01f, 1.0f, velocity);

    if (latestActiveVoiceIdx != nullptr)
        latestActiveVoiceIdx->store(voiceIndex, std::memory_order_relaxed);

    bool voiceIsActive = adsr.isActive();
    adsr.noteOn();

    bool isLegato = (voiceMode == 2 && voiceIsActive);

    for (int i = 0; i < activePartials; ++i)
    {
        if (!isLegato)
        {
            oscillators[i].reset();
            if (partialAdsrEnabled[i])
                partialAdsr[i].noteOn();
            partialLfoPhases[i] = partialModPhaseOffsets[i] / 360.0f;
            partialAnimElapsed[i] = 0.0f;
            partialAnimPhase[i] = 0.0f;
            partialNoteHeld[i] = true;
            partialReleaseElapsed[i] = 0.0f;
            partialReleaseStartPhase[i] = 0.0f;
        }
        else
        {
            partialNoteHeld[i] = true;
        }

        float effGlide = (partialGlideTimes[i] > 0.001f ? partialGlideTimes[i] : glideTime);
        if (effGlide <= 0.001f || partialCurrentFreq[i] <= 0.0f || !voiceIsActive)
            partialCurrentFreq[i] = targetFrequency;

        float semitoneShift = std::pow(2.0f, (partialSemitones[i] + currentPitchBendSemitones) / 12.0f);
        oscillators[i].setFrequency(partialCurrentFreq[i] * semitoneShift * partialRatios[i], currentSampleRate);
    }
}

void WaveSlaveVoice::setTargetNote (int midiNoteNumber, float velocity, bool isLegatoTransition)
{
    targetFrequency = (float)juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
    currentVelocity = juce::jlimit(0.01f, 1.0f, velocity);

    if (latestActiveVoiceIdx != nullptr)
        latestActiveVoiceIdx->store(voiceIndex, std::memory_order_relaxed);

    if (!adsr.isActive())
    {
        for (int i = 0; i < maxPartials; ++i)
            partialCurrentFreq[i] = targetFrequency;

        adsr.noteOn();

        for (int i = 0; i < activePartials; ++i)
        {
            oscillators[i].reset();
            if (partialAdsrEnabled[i])
                partialAdsr[i].noteOn();
            partialLfoPhases[i] = partialModPhaseOffsets[i] / 360.0f;
            partialAnimElapsed[i] = 0.0f;
            partialAnimPhase[i] = 0.0f;
            partialNoteHeld[i] = true;
            partialReleaseElapsed[i] = 0.0f;
            partialReleaseStartPhase[i] = 0.0f;

            float semitoneShift = std::pow(2.0f, (partialSemitones[i] + currentPitchBendSemitones) / 12.0f);
            oscillators[i].setFrequency(partialCurrentFreq[i] * semitoneShift * partialRatios[i], currentSampleRate);
        }
        return;
    }

    if (isLegatoTransition)
    {
        for (int i = 0; i < activePartials; ++i)
        {
            partialNoteHeld[i] = true;
            float effGlide = (partialGlideTimes[i] > 0.001f ? partialGlideTimes[i] : glideTime);
            if (effGlide <= 0.001f)
                partialCurrentFreq[i] = targetFrequency;
        }
    }
    else
    {
        adsr.noteOn();

        for (int i = 0; i < activePartials; ++i)
        {
            oscillators[i].reset();
            if (partialAdsrEnabled[i])
                partialAdsr[i].noteOn();
            partialLfoPhases[i] = partialModPhaseOffsets[i] / 360.0f;
            partialAnimElapsed[i] = 0.0f;
            partialAnimPhase[i] = 0.0f;
            partialNoteHeld[i] = true;
            partialReleaseElapsed[i] = 0.0f;
            partialReleaseStartPhase[i] = 0.0f;

            float effGlide = (partialGlideTimes[i] > 0.001f ? partialGlideTimes[i] : glideTime);
            if (effGlide <= 0.001f)
                partialCurrentFreq[i] = targetFrequency;

            float semitoneShift = std::pow(2.0f, (partialSemitones[i] + currentPitchBendSemitones) / 12.0f);
            oscillators[i].setFrequency(partialCurrentFreq[i] * semitoneShift * partialRatios[i], currentSampleRate);
        }
    }
}

void WaveSlaveVoice::stopNote (float /*velocity*/, bool allowTailOff)
{
    if (allowTailOff)
    {
        adsr.noteOff();
        for (int i = 0; i < activePartials; ++i)
        {
            if (partialAdsrEnabled[i])
                partialAdsr[i].noteOff();
            if (partialNoteHeld[i])
            {
                partialNoteHeld[i] = false;
                partialReleaseElapsed[i] = 0.0f;
                partialReleaseStartPhase[i] = partialAnimPhase[i];
            }
        }
    }
    else
    {
        clearCurrentNote();
        adsr.reset();
        for (int i = 0; i < maxPartials; ++i)
        {
            partialAdsr[i].reset();
            partialNoteHeld[i] = false;
        }
    }
}

void WaveSlaveVoice::pitchWheelMoved (int newPitchWheelValue)
{
    float norm = (newPitchWheelValue - 8192) / 8192.0f;
    currentPitchBendSemitones = norm * 2.0f;
}

void WaveSlaveVoice::controllerMoved (int controllerNumber, int newControllerValue)
{
    if (controllerNumber == 1) // Mod Wheel
    {
        modWheelValue = juce::jlimit(0.0f, 1.0f, (float)newControllerValue / 127.0f);
    }
}

void WaveSlaveVoice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    jassert(outputBuffer.getNumChannels() > 0);
    
    if (!adsr.isActive())
    {
        clearCurrentNote();
        return;
    }
    
    if (synthBuffer.getNumSamples() < numSamples)
        synthBuffer.setSize(2, numSamples, false, false, true);
    synthBuffer.clear(0, 0, numSamples);
    synthBuffer.clear(1, 0, numSamples);
    
    bool capturePartials = (partialsVisBuffer != nullptr && partialsVisBuffer->getNumChannels() >= activePartials);
    bool isLatestVoice = (latestActiveVoiceIdx == nullptr || latestActiveVoiceIdx->load(std::memory_order_relaxed) == voiceIndex);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Per-partial smooth frequency glide
        for (int i = 0; i < activePartials; ++i)
        {
            if (partialCurrentFreq[i] != targetFrequency)
            {
                float pGlide = (partialGlideTimes[i] > 0.001f ? partialGlideTimes[i] : glideTime);
                if (pGlide <= 0.001f || partialCurrentFreq[i] <= 0.0f)
                {
                    partialCurrentFreq[i] = targetFrequency;
                }
                else
                {
                    float glideCoeff = std::exp(-1.0f / (pGlide * currentSampleRate));
                    partialCurrentFreq[i] = targetFrequency + (partialCurrentFreq[i] - targetFrequency) * glideCoeff;
                    if (std::abs(partialCurrentFreq[i] - targetFrequency) < 0.05f)
                        partialCurrentFreq[i] = targetFrequency;
                }
            }
        }

        float env = adsr.getNextSample();
        float currentLeftSample = 0.0f;
        float currentRightSample = 0.0f;

        for (int i = 0; i < activePartials; ++i)
        {
            if (!partialEnabled[i])
            {
                if (isLatestVoice && liveVisStates != nullptr && sample == numSamples - 1)
                {
                    (*liveVisStates)[i].effectiveGain.store(0.0f, std::memory_order_relaxed);
                    (*liveVisStates)[i].envLevel.store(0.0f, std::memory_order_relaxed);
                }
                continue;
            }

            float pEnv = partialAdsrEnabled[i] ? partialAdsr[i].getNextSample() : 1.0f;

            // 1. Keyframe Animation Evaluation
            float animRatio  = partialRatios[i];
            float animGain   = partialGains[i];
            float animSemi   = partialSemitones[i];
            float animPan    = partialPans[i];

            int aMode = partialAnimModes[i];
            if (aMode > 0)
            {
                float phase = 0.0f;
                if (aMode == 1) // Note Length (mapped to ADSR stages: Attack -> Decay -> holds at Sustain -> finishes during Release)
                {
                    const auto& envP = partialAdsrEnabled[i] ? partialAdsrParams[i] : adsrParams;
                    float tA = juce::jmax(0.001f, envP.attack);
                    float tD = juce::jmax(0.001f, envP.decay);
                    float tR = juce::jmax(0.001f, envP.release);
                    float totalEnvTime = tA + tD + tR;
                    float sustainPhase = (tA + tD) / totalEnvTime;

                    if (partialNoteHeld[i])
                    {
                        float elapsed = partialAnimElapsed[i];
                        if (elapsed < tA + tD)
                            phase = juce::jlimit(0.0f, sustainPhase, elapsed / totalEnvTime);
                        else
                            phase = sustainPhase;

                        partialAnimElapsed[i] += 1.0f / currentSampleRate;
                        partialReleaseStartPhase[i] = phase;
                    }
                    else
                    {
                        float relProg = juce::jlimit(0.0f, 1.0f, partialReleaseElapsed[i] / tR);
                        phase = partialReleaseStartPhase[i] + (1.0f - partialReleaseStartPhase[i]) * relProg;
                        partialReleaseElapsed[i] += 1.0f / currentSampleRate;
                    }
                }
                else // Time-based modes (2 = Time: One-Shot, 3 = Time: Loop, 4 = Time: Ping-Pong)
                {
                    float dur = juce::jmax(0.05f, partialAnimDurations[i]);
                    float progress = partialAnimElapsed[i] / dur;
                    if (aMode == 2) // Time: One-Shot
                        phase = juce::jlimit(0.0f, 1.0f, progress);
                    else if (aMode == 3) // Time: Loop
                        phase = std::fmod(progress, 1.0f);
                    else if (aMode == 4) // Time: Ping-Pong
                    {
                        float cycle = std::fmod(progress, 2.0f);
                        phase = (cycle <= 1.0f) ? cycle : (2.0f - cycle);
                    }
                    partialAnimElapsed[i] += 1.0f / currentSampleRate;
                }

                partialAnimPhase[i] = phase;

                if (partialAnimTracks[i].numKeyframes > 0)
                {
                    const PartialKeyframe& startKf = partialAnimTracks[i].keyframes[0];
                    PartialKeyframe evalKf = partialAnimTracks[i].evaluate(phase, partialAnimCurves[i], startKf);
                    float depth = partialAnimDepths[i];
                    animRatio  = juce::jlimit(0.01f, 32.0f,  startKf.ratio    + depth * (evalKf.ratio    - startKf.ratio));
                    animGain   = juce::jlimit(0.0f,  1.0f,   startKf.gain     + depth * (evalKf.gain     - startKf.gain));
                    animSemi   = juce::jlimit(-24.0f, 24.0f, startKf.semitone + depth * (evalKf.semitone - startKf.semitone));
                    animPan    = juce::jlimit(-1.0f,  1.0f,   startKf.pan      + depth * (evalKf.pan      - startKf.pan));
                }
            }
            else
            {
                partialAnimPhase[i] = 0.0f;
            }

            // 2. LFO Calculation
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
                    case 4: // Custom 1
                    case 5: // Custom 2
                    case 6: // Custom 3
                    case 7: // Custom 4
                        if (customWavetables != nullptr)
                        {
                            int slot = partialModWaveforms[i] - 4;
                            if (slot >= 0 && slot < 4)
                            {
                                float exactPos = phase * 512.0f;
                                int i1 = (int)exactPos % 512;
                                int i2 = (i1 + 1) % 512;
                                float frac = exactPos - (int)exactPos;
                                float v1 = (*customWavetables)[slot][i1].load(std::memory_order_relaxed);
                                float v2 = (*customWavetables)[slot][i2].load(std::memory_order_relaxed);
                                lfoVal = v1 + frac * (v2 - v1);
                            }
                        }
                        break;
                }
                
                partialLfoPhases[i] += partialModRates[i] / currentSampleRate;
                if (partialLfoPhases[i] >= 1.0f) partialLfoPhases[i] -= 1.0f;
            }

            // 3. LFO Targets: 1=Pitch, 2=Level, 3=Pan
            float pitchModShiftSemitones = 0.0f;
            float levelModMultiplier = 1.0f;
            float panModAmount = 0.0f;

            // LFO (with Mod Wheel scaling if Mod Wheel is engaged)
            float effectiveLfoDepth = juce::jlimit(0.0f, 1.0f, partialModDepths[i] + modWheelValue * (1.0f - partialModDepths[i]));
            if (partialModTargets[i] == 1)      pitchModShiftSemitones += lfoVal * effectiveLfoDepth * 12.0f;
            else if (partialModTargets[i] == 2) levelModMultiplier *= (1.0f - effectiveLfoDepth * 0.5f * (1.0f - lfoVal));
            else if (partialModTargets[i] == 3) panModAmount += lfoVal * effectiveLfoDepth;

            // Panning, ADSR & gain calculations
            float totalPan = juce::jlimit(-1.0f, 1.0f, masterPan + animPan + panModAmount);
            float totalEnv = env * pEnv;
            float visGain = juce::jlimit(0.0f, 1.0f, animGain * totalEnv * levelModMultiplier);
            float velFactor = velocityEnabled ? currentVelocity : 1.0f;
            float currentGain = visGain * velFactor;

            float effSemitone = animSemi + pitchModShiftSemitones + currentPitchBendSemitones;
            float semitoneShift = std::pow(2.0f, effSemitone / 12.0f);
            float freq = partialCurrentFreq[i] * semitoneShift * animRatio;

            if (isLatestVoice && liveVisStates != nullptr && sample == numSamples - 1)
            {
                float glideFactor = (targetFrequency > 0.0f) ? (partialCurrentFreq[i] / targetFrequency) : 1.0f;
                float effRatio = animRatio * semitoneShift * glideFactor;
                (*liveVisStates)[i].effectiveRatio.store(juce::jlimit(0.01f, 32.0f, effRatio), std::memory_order_relaxed);
                (*liveVisStates)[i].effectiveGain.store(visGain, std::memory_order_relaxed);
                (*liveVisStates)[i].effectiveSemitone.store(juce::jlimit(-24.0f, 24.0f, effSemitone), std::memory_order_relaxed);
                (*liveVisStates)[i].effectiveDetune.store(0.0f, std::memory_order_relaxed);
                (*liveVisStates)[i].effectivePan.store(totalPan, std::memory_order_relaxed);
                (*liveVisStates)[i].animPhase.store(partialAnimPhase[i], std::memory_order_relaxed);
                (*liveVisStates)[i].envLevel.store(totalEnv, std::memory_order_relaxed);
                (*liveVisStates)[i].lfoValue.store(lfoVal, std::memory_order_relaxed);
            }

            if (currentGain <= 0.00001f)
                continue;

            oscillators[i].setFrequency(freq, currentSampleRate);
            float s = oscillators[i].processSample();

            float panAngle = (totalPan + 1.0f) * (juce::MathConstants<float>::pi * 0.25f);
            float lGain = std::cos(panAngle) * currentGain;
            float rGain = std::sin(panAngle) * currentGain;

            currentLeftSample += s * lGain;
            currentRightSample += s * rGain;

            if (capturePartials && sample < partialsVisBuffer->getNumSamples())
            {
                partialsVisBuffer->addSample(i, sample, s * currentGain);
            }
        }

        synthBuffer.setSample(0, sample, currentLeftSample);
        synthBuffer.setSample(1, sample, currentRightSample);
    }
    
    if (outputBuffer.getNumChannels() >= 2)
    {
        outputBuffer.addFrom(0, startSample, synthBuffer, 0, 0, numSamples);
        outputBuffer.addFrom(1, startSample, synthBuffer, 1, 0, numSamples);
    }
    else if (outputBuffer.getNumChannels() == 1)
    {
        outputBuffer.addFrom(0, startSample, synthBuffer, 0, 0, numSamples, 0.5f);
        outputBuffer.addFrom(0, startSample, synthBuffer, 1, 0, numSamples, 0.5f);
    }
    
    if (!adsr.isActive())
    {
        clearCurrentNote();
    }
}
