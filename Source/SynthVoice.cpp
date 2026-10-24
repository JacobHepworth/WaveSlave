#include "SynthVoice.h"

WaveSlaveVoice::WaveSlaveVoice()
{
    for (int i = 0; i < maxPartials; ++i)
    {
        oscillators[i].setWaveform(0);
        partialPans[i] = 0.0f;
        partialAdsrEnabled[i] = true;
        partialGlideTimes[i] = 0.0f;
        partialCurrentPitch[i] = 0.0f;
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

void WaveSlaveVoice::setVoiceIndex(int idx)
{
    voiceIndex = idx;
    for (int i = 0; i < maxPartials; ++i)
    {
        uint32_t seed = (uint32_t)((voiceIndex + 1) * 7919 + (i + 1) * 104729);
        oscillators[i].setRandomSeed(seed);
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

void WaveSlaveVoice::cacheParameters(juce::AudioProcessorValueTreeState& apvts)
{
    cachedParams.attack = apvts.getRawParameterValue("ATTACK");
    cachedParams.decay = apvts.getRawParameterValue("DECAY");
    cachedParams.sustain = apvts.getRawParameterValue("SUSTAIN");
    cachedParams.release = apvts.getRawParameterValue("RELEASE");
    cachedParams.numPartials = apvts.getRawParameterValue("NUM_PARTIALS");
    cachedParams.masterPan = apvts.getRawParameterValue("MASTER_PAN");
    cachedParams.voiceMode = apvts.getRawParameterValue("VOICE_MODE");
    cachedParams.glideTime = apvts.getRawParameterValue("GLIDE_TIME");
    cachedParams.velocityEnabled = apvts.getRawParameterValue("VELOCITY_ENABLED");
    cachedParams.phaseMode = apvts.getRawParameterValue("PHASE_MODE");
    cachedParams.pitchBendRange = apvts.getRawParameterValue("PITCH_BEND_RANGE");

    for (int i = 0; i < maxPartials; ++i)
    {
        juce::String idSuffix = juce::String(i + 1);
        auto& cp = cachedParams.partials[i];
        cp.enabled = apvts.getRawParameterValue("P_ENABLED" + idSuffix);
        cp.waveform = apvts.getRawParameterValue("WAVEFORM" + idSuffix);
        cp.ratio = apvts.getRawParameterValue("RATIO" + idSuffix);
        cp.semitone = apvts.getRawParameterValue("SEMITONE" + idSuffix);
        cp.detune = apvts.getRawParameterValue("DETUNE" + idSuffix);
        cp.gain = apvts.getRawParameterValue("GAIN" + idSuffix);
        cp.pan = apvts.getRawParameterValue("PAN" + idSuffix);
        cp.modTarget = apvts.getRawParameterValue("MOD_TARGET" + idSuffix);
        cp.modWaveform = apvts.getRawParameterValue("MOD_WAVEFORM" + idSuffix);
        cp.modRate = apvts.getRawParameterValue("MOD_RATE" + idSuffix);
        cp.modDepth = apvts.getRawParameterValue("MOD_DEPTH" + idSuffix);
        cp.modPhase = apvts.getRawParameterValue("MOD_PHASE" + idSuffix);
        cp.adsrEnabled = apvts.getRawParameterValue("P_ADSR_ENABLED" + idSuffix);
        cp.attack = apvts.getRawParameterValue("P_ATTACK" + idSuffix);
        cp.decay = apvts.getRawParameterValue("P_DECAY" + idSuffix);
        cp.sustain = apvts.getRawParameterValue("P_SUSTAIN" + idSuffix);
        cp.release = apvts.getRawParameterValue("P_RELEASE" + idSuffix);
        cp.glide = apvts.getRawParameterValue("P_GLIDE" + idSuffix);
        cp.animMode = apvts.getRawParameterValue("ANIM_MODE" + idSuffix);
        cp.animCurve = apvts.getRawParameterValue("ANIM_CURVE" + idSuffix);
        cp.animDuration = apvts.getRawParameterValue("ANIM_DURATION" + idSuffix);
        cp.animDepth = apvts.getRawParameterValue("ANIM_DEPTH" + idSuffix);
    }
    paramsCached = true;
}

void WaveSlaveVoice::updateParameters(juce::AudioProcessorValueTreeState& apvts, const std::array<PartialAnimationData, 16>* animTracks)
{
    if (!paramsCached)
        cacheParameters(apvts);

    if (cachedParams.attack != nullptr)  adsrParams.attack = cachedParams.attack->load(std::memory_order_relaxed);
    if (cachedParams.decay != nullptr)   adsrParams.decay = cachedParams.decay->load(std::memory_order_relaxed);
    if (cachedParams.sustain != nullptr) adsrParams.sustain = cachedParams.sustain->load(std::memory_order_relaxed);
    if (cachedParams.release != nullptr) adsrParams.release = cachedParams.release->load(std::memory_order_relaxed);
    adsr.setParameters(adsrParams);
    
    if (cachedParams.numPartials != nullptr)
        activePartials = juce::jlimit(1, maxPartials, (int)cachedParams.numPartials->load(std::memory_order_relaxed));

    if (cachedParams.masterPan != nullptr)
        masterPan = cachedParams.masterPan->load(std::memory_order_relaxed);

    if (cachedParams.voiceMode != nullptr)
        voiceMode = (int)cachedParams.voiceMode->load(std::memory_order_relaxed);

    if (cachedParams.glideTime != nullptr)
        glideTime = cachedParams.glideTime->load(std::memory_order_relaxed);

    if (cachedParams.velocityEnabled != nullptr)
        velocityEnabled = cachedParams.velocityEnabled->load(std::memory_order_relaxed) > 0.5f;

    for (int i = 0; i < maxPartials; ++i)
    {
        const auto& cp = cachedParams.partials[i];
        if (cp.enabled != nullptr)
            partialEnabled[i] = cp.enabled->load(std::memory_order_relaxed) > 0.5f;
        else
            partialEnabled[i] = true;

        if (cp.ratio != nullptr)    partialRatios[i] = cp.ratio->load(std::memory_order_relaxed);
        if (cp.detune != nullptr)   partialDetunes[i] = cp.detune->load(std::memory_order_relaxed);
        if (cp.gain != nullptr)     partialGains[i] = cp.gain->load(std::memory_order_relaxed);
        if (cp.semitone != nullptr) partialSemitones[i] = cp.semitone->load(std::memory_order_relaxed);
        
        if (cp.pan != nullptr)
            partialPans[i] = cp.pan->load(std::memory_order_relaxed);

        if (cp.waveform != nullptr)
        {
            int newWaveform = (int)cp.waveform->load(std::memory_order_relaxed);
            if (newWaveform != partialWaveforms[i])
            {
                partialWaveforms[i] = newWaveform;
                oscillators[i].setWaveform(newWaveform);
            }
        }

        if (cp.modTarget != nullptr)    partialModTargets[i] = (int)cp.modTarget->load(std::memory_order_relaxed);
        if (cp.modWaveform != nullptr)  partialModWaveforms[i] = (int)cp.modWaveform->load(std::memory_order_relaxed);
        if (cp.modRate != nullptr)      partialModRates[i] = cp.modRate->load(std::memory_order_relaxed);
        if (cp.modDepth != nullptr)     partialModDepths[i] = cp.modDepth->load(std::memory_order_relaxed);
        if (cp.modPhase != nullptr)     partialModPhaseOffsets[i] = cp.modPhase->load(std::memory_order_relaxed);

        // Per-partial ADSR
        if (cp.adsrEnabled != nullptr)
            partialAdsrEnabled[i] = cp.adsrEnabled->load(std::memory_order_relaxed) > 0.5f;
        else
            partialAdsrEnabled[i] = true;

        if (cp.glide != nullptr)
            partialGlideTimes[i] = cp.glide->load(std::memory_order_relaxed);
        else
            partialGlideTimes[i] = 0.0f;

        if (cp.attack != nullptr)  partialAdsrParams[i].attack = cp.attack->load(std::memory_order_relaxed);
        if (cp.decay != nullptr)   partialAdsrParams[i].decay = cp.decay->load(std::memory_order_relaxed);
        if (cp.sustain != nullptr) partialAdsrParams[i].sustain = cp.sustain->load(std::memory_order_relaxed);
        if (cp.release != nullptr) partialAdsrParams[i].release = cp.release->load(std::memory_order_relaxed);
        partialAdsr[i].setParameters(partialAdsrParams[i]);

        // Per-partial Keyframe Animation (replaces Mod 2)
        if (cp.animMode != nullptr)     partialAnimModes[i] = (int)cp.animMode->load(std::memory_order_relaxed);
        if (cp.animCurve != nullptr)    partialAnimCurves[i] = (int)cp.animCurve->load(std::memory_order_relaxed);
        if (cp.animDuration != nullptr) partialAnimDurations[i] = cp.animDuration->load(std::memory_order_relaxed);
        if (cp.animDepth != nullptr)    partialAnimDepths[i] = cp.animDepth->load(std::memory_order_relaxed);

        if (animTracks != nullptr)
            partialAnimTracks[i] = (*animTracks)[i];
    }
}

void WaveSlaveVoice::startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int /*currentPitchWheelPosition*/)
{
    targetPitch = (float)midiNoteNumber;
    currentVelocity = juce::jlimit(0.01f, 1.0f, velocity);

    if (latestActiveVoiceIdx != nullptr)
        latestActiveVoiceIdx->store(voiceIndex, std::memory_order_relaxed);

    adsr.noteOn();

    int pMode = 0;
    if (cachedParams.phaseMode != nullptr)
        pMode = (int)cachedParams.phaseMode->load(std::memory_order_relaxed);

    float masterG = (voiceMode > 0) ? glideTime : 0.0f;

    for (int i = 0; i < maxPartials; ++i)
    {
        if (i >= activePartials)
        {
            partialCurrentPitch[i] = targetPitch;
            partialAnimElapsed[i] = 0.0f;
            partialAnimPhase[i] = 0.0f;
            partialNoteHeld[i] = false;
            partialReleaseElapsed[i] = 0.0f;
            partialReleaseStartPhase[i] = 0.0f;
            continue;
        }

        if (pMode == 0) // Key-Sync (0 deg)
            oscillators[i].reset();
        else if (pMode == 2) // Random Initial Phase
            oscillators[i].setPhase(oscillators[i].nextRandomFloat() * 0.5f + 0.5f);
        // Free-Running (pMode == 1) retains current phase without interruption

        if (partialAdsrEnabled[i])
            partialAdsr[i].noteOn();
        partialLfoPhases[i] = partialModPhaseOffsets[i] / 360.0f;
        partialAnimElapsed[i] = 0.0f;
        partialAnimPhase[i] = 0.0f;
        partialNoteHeld[i] = true;
        partialReleaseElapsed[i] = 0.0f;
        partialReleaseStartPhase[i] = 0.0f;

        float effGlide = (partialGlideTimes[i] > 0.001f ? partialGlideTimes[i] : masterG);
        if (effGlide <= 0.001f || partialCurrentPitch[i] <= 0.0f)
            partialCurrentPitch[i] = targetPitch;

        float baseFreq = 440.0f * std::pow(2.0f, (partialCurrentPitch[i] - 69.0f) / 12.0f);
        float effDetuneSemi = partialDetunes[i] / 100.0f;
        float semitoneShift = std::pow(2.0f, (partialSemitones[i] + effDetuneSemi + currentPitchBendSemitones) / 12.0f);
        oscillators[i].setFrequency(baseFreq * semitoneShift * partialRatios[i], currentSampleRate);
    }
}

void WaveSlaveVoice::setTargetNote (int midiNoteNumber, float velocity, bool isLegatoTransition)
{
    targetPitch = (float)midiNoteNumber;
    currentVelocity = juce::jlimit(0.01f, 1.0f, velocity);

    if (latestActiveVoiceIdx != nullptr)
        latestActiveVoiceIdx->store(voiceIndex, std::memory_order_relaxed);

    float envLvl = currentEnvLevel.load(std::memory_order_relaxed);
    bool isAudible = adsr.isActive() && (envLvl > 0.005f || adsrParams.sustain > 0.005f);

    int pMode = 0;
    if (cachedParams.phaseMode != nullptr)
        pMode = (int)cachedParams.phaseMode->load(std::memory_order_relaxed);

    float masterG = (voiceMode > 0) ? glideTime : 0.0f;

    if (!isAudible || !isLegatoTransition)
    {
        adsr.noteOn();

        for (int i = 0; i < maxPartials; ++i)
        {
            if (i >= activePartials)
            {
                partialCurrentPitch[i] = targetPitch;
                continue;
            }

            if (partialAdsrEnabled[i])
                partialAdsr[i].noteOn();

            partialNoteHeld[i] = true;
            partialReleaseElapsed[i] = 0.0f;
            partialReleaseStartPhase[i] = 0.0f;

            if (!isLegatoTransition)
            {
                if (pMode == 0)
                    oscillators[i].reset();
                else if (pMode == 2)
                    oscillators[i].setPhase(oscillators[i].nextRandomFloat() * 0.5f + 0.5f);

                partialLfoPhases[i] = partialModPhaseOffsets[i] / 360.0f;
                partialAnimElapsed[i] = 0.0f;
                partialAnimPhase[i] = 0.0f;
            }

            float effGlide = (partialGlideTimes[i] > 0.001f ? partialGlideTimes[i] : masterG);
            if (effGlide <= 0.001f || partialCurrentPitch[i] <= 0.0f)
                partialCurrentPitch[i] = targetPitch;

            float baseFreq = 440.0f * std::pow(2.0f, (partialCurrentPitch[i] - 69.0f) / 12.0f);
            float effDetuneSemi = partialDetunes[i] / 100.0f;
            float semitoneShift = std::pow(2.0f, (partialSemitones[i] + effDetuneSemi + currentPitchBendSemitones) / 12.0f);
            oscillators[i].setFrequency(baseFreq * semitoneShift * partialRatios[i], currentSampleRate);
        }
    }
    else
    {
        for (int i = 0; i < maxPartials; ++i)
        {
            if (i >= activePartials)
            {
                partialCurrentPitch[i] = targetPitch;
                continue;
            }

            partialNoteHeld[i] = true;
            float effGlide = (partialGlideTimes[i] > 0.001f ? partialGlideTimes[i] : masterG);
            if (effGlide <= 0.001f || partialCurrentPitch[i] <= 0.0f)
                partialCurrentPitch[i] = targetPitch;
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
        currentEnvLevel.store(0.0f, std::memory_order_relaxed);
        clearCurrentNote();
        adsr.reset();
        targetPitch = 0.0f;
        for (int i = 0; i < maxPartials; ++i)
        {
            partialAdsr[i].reset();
            partialNoteHeld[i] = false;
            partialCurrentPitch[i] = 0.0f;
            partialAnimElapsed[i] = 0.0f;
            partialAnimPhase[i] = 0.0f;
            partialReleaseElapsed[i] = 0.0f;
            partialReleaseStartPhase[i] = 0.0f;
        }
    }
}

void WaveSlaveVoice::pitchWheelMoved (int newPitchWheelValue)
{
    float norm = (newPitchWheelValue - 8192) / 8192.0f;
    float bendRange = 2.0f;
    if (cachedParams.pitchBendRange != nullptr)
    {
        int choice = (int)cachedParams.pitchBendRange->load(std::memory_order_relaxed);
        switch (choice)
        {
            case 0: bendRange = 2.0f; break;
            case 1: bendRange = 7.0f; break;
            case 2: bendRange = 12.0f; break;
            case 3: bendRange = 24.0f; break;
            default: bendRange = 2.0f; break;
        }
    }
    currentPitchBendSemitones = norm * bendRange;
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
        currentEnvLevel.store(0.0f, std::memory_order_relaxed);
        clearCurrentNote();
        return;
    }
    
    if (synthBuffer.getNumSamples() < numSamples)
        synthBuffer.setSize(2, numSamples, false, false, true);
    synthBuffer.clear(0, 0, numSamples);
    synthBuffer.clear(1, 0, numSamples);
    
    bool capturePartials = (partialsVisBuffer != nullptr && partialsVisBuffer->getNumChannels() >= activePartials);
    bool isLatestVoice = (latestActiveVoiceIdx == nullptr || latestActiveVoiceIdx->load(std::memory_order_relaxed) == voiceIndex);

    bool anySolo = false;
    bool soloMask[maxPartials] = { false };
    if (partialSoloStates != nullptr)
    {
        for (int i = 0; i < maxPartials; ++i)
        {
            soloMask[i] = (*partialSoloStates)[i].load(std::memory_order_relaxed);
            if (soloMask[i]) anySolo = true;
        }
    }

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Per-partial smooth logarithmic pitch glide (MIDI pitch space)
        float masterG = (voiceMode > 0) ? glideTime : 0.0f;
        for (int i = 0; i < activePartials; ++i)
        {
            if (partialCurrentPitch[i] != targetPitch)
            {
                float pGlide = (partialGlideTimes[i] > 0.001f ? partialGlideTimes[i] : masterG);
                if (pGlide <= 0.001f || partialCurrentPitch[i] <= 0.0f)
                {
                    partialCurrentPitch[i] = targetPitch;
                }
                else
                {
                    float glideCoeff = std::exp(-1.0f / (pGlide * currentSampleRate));
                    partialCurrentPitch[i] = targetPitch + (partialCurrentPitch[i] - targetPitch) * glideCoeff;
                    if (std::abs(partialCurrentPitch[i] - targetPitch) < 0.001f)
                        partialCurrentPitch[i] = targetPitch;
                }
            }
        }

        float env = adsr.getNextSample();
        currentEnvLevel.store(env, std::memory_order_relaxed);
        float currentLeftSample = 0.0f;
        float currentRightSample = 0.0f;

        for (int i = 0; i < activePartials; ++i)
        {
            bool isAudible = partialEnabled[i] && (!anySolo || soloMask[i]);
            if (!isAudible)
            {
                if (isLatestVoice && liveVisStates != nullptr && sample == numSamples - 1)
                {
                    float effDetuneSemi = partialDetunes[i] / 100.0f;
                    float effSemitone = partialSemitones[i] + effDetuneSemi + currentPitchBendSemitones;
                    float semitoneShift = std::pow(2.0f, effSemitone / 12.0f);
                    (*liveVisStates)[i].effectiveRatio.store(juce::jlimit(0.01f, 32.0f, partialRatios[i] * semitoneShift), std::memory_order_relaxed);
                    (*liveVisStates)[i].effectiveGain.store(partialGains[i], std::memory_order_relaxed);
                    (*liveVisStates)[i].effectiveSemitone.store(juce::jlimit(-24.0f, 24.0f, effSemitone), std::memory_order_relaxed);
                    (*liveVisStates)[i].effectiveDetune.store(partialDetunes[i], std::memory_order_relaxed);
                    (*liveVisStates)[i].effectivePan.store(juce::jlimit(-1.0f, 1.0f, masterPan + partialPans[i]), std::memory_order_relaxed);
                    (*liveVisStates)[i].animPhase.store(0.0f, std::memory_order_relaxed);
                    (*liveVisStates)[i].envLevel.store(0.0f, std::memory_order_relaxed);
                    (*liveVisStates)[i].lfoValue.store(0.0f, std::memory_order_relaxed);
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
                if (aMode == 1) // Note Length (mapped to ADSR stages)
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
                else // Time-based modes
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

            float effectiveLfoDepth = juce::jlimit(0.0f, 1.0f, partialModDepths[i] + modWheelValue * (1.0f - partialModDepths[i]));
            if (partialModTargets[i] == 1)      pitchModShiftSemitones += lfoVal * effectiveLfoDepth * 12.0f;
            else if (partialModTargets[i] == 2) levelModMultiplier *= (1.0f - effectiveLfoDepth * 0.5f * (1.0f - lfoVal));
            else if (partialModTargets[i] == 3) panModAmount += lfoVal * effectiveLfoDepth;

            // Panning, ADSR & gain calculations
            float totalPan = juce::jlimit(-1.0f, 1.0f, masterPan + animPan + panModAmount);
            float totalEnv = env * pEnv;
            float visGain = juce::jlimit(0.0f, 1.0f, animGain * levelModMultiplier);
            float velFactor = velocityEnabled ? currentVelocity : 1.0f;
            float currentGain = animGain * totalEnv * levelModMultiplier * velFactor;

            float baseFreq = 440.0f * std::pow(2.0f, (partialCurrentPitch[i] - 69.0f) / 12.0f);
            float effDetuneSemi = partialDetunes[i] / 100.0f;
            float effSemitone = animSemi + effDetuneSemi + pitchModShiftSemitones + currentPitchBendSemitones;
            float semitoneShift = std::pow(2.0f, effSemitone / 12.0f);
            float freq = baseFreq * semitoneShift * animRatio;

            if (isLatestVoice && liveVisStates != nullptr && sample == numSamples - 1)
            {
                float targetF = 440.0f * std::pow(2.0f, (targetPitch - 69.0f) / 12.0f);
                float glideFactor = (targetF > 0.0f) ? (baseFreq / targetF) : 1.0f;
                float effRatio = animRatio * semitoneShift * glideFactor;
                (*liveVisStates)[i].effectiveRatio.store(juce::jlimit(0.01f, 32.0f, effRatio), std::memory_order_relaxed);
                (*liveVisStates)[i].effectiveGain.store(visGain, std::memory_order_relaxed);
                (*liveVisStates)[i].effectiveSemitone.store(juce::jlimit(-24.0f, 24.0f, effSemitone), std::memory_order_relaxed);
                (*liveVisStates)[i].effectiveDetune.store(partialDetunes[i], std::memory_order_relaxed);
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
