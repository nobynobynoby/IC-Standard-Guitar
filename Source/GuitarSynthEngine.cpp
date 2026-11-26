#include "GuitarSynthEngine.h"

#include <cmath>

namespace icguitar
{

namespace
{

inline float hermiteSample(const float* data, int numSamples, double pos) noexcept
{
    const int i1 = static_cast<int>(pos);
    if (i1 < 0 || i1 >= numSamples)
        return 0.0f;

    const float frac = static_cast<float>(pos - static_cast<double>(i1));
    const float y0 = data[i1 > 0 ? i1 - 1 : 0];
    const float y1 = data[i1];
    const float y2 = data[i1 + 1 < numSamples ? i1 + 1 : numSamples - 1];
    const float y3 = data[i1 + 2 < numSamples ? i1 + 2 : numSamples - 1];

    const float c0 = y1;
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

inline float softLimit(float x) noexcept
{
    const float ax = std::abs(x);
    if (ax <= 0.85f)
        return x;
    const float excess = ax - 0.85f;
    const float shaped = 0.85f + 0.14f * std::tanh(excess / 0.14f);
    return x >= 0.0f ? shaped : -shaped;
}

inline float computeOnePoleCoeff(float cutoffHz, double sampleRate) noexcept
{
    const float nyquistLimited = juce::jlimit(40.0f, static_cast<float>(sampleRate * 0.45), cutoffHz);
    const float omega = 2.0f * juce::MathConstants<float>::pi * nyquistLimited / static_cast<float>(sampleRate);
    return 1.0f - std::exp(-omega);
}

} // namespace

GuitarSynthEngine::GuitarSynthEngine(SampleBank& bank)
    : sampleBank(bank)
{
}

void GuitarSynthEngine::prepare(double sampleRate, int /*maxBlockSize*/)
{
    hostSampleRate = sampleRate > 1000.0 ? sampleRate : 44100.0;
    reverb.setSampleRate(hostSampleRate);
    lastToneEq = -1.0f;
    lastAmbience = -1.0f;
    reset();
}

void GuitarSynthEngine::reset()
{
    for (auto& v : voices)
        v = Voice {};

    for (auto& f : eqLow)
        f.reset();
    for (auto& f : eqMid)
        f.reset();
    for (auto& f : eqHigh)
        f.reset();

    reverb.reset();
    sustainPedalDown = false;
    cc80Received = false;
    lastCc80Value = 127;
    pitchBendSemitones = 0.0f;
    pitchBendMultiplier = 1.0;
    lastNoteOnTimeSamples = -1000000;
    lastReleaseNoiseTimeSamples = -1000000;
    activeVoiceCount.store(0, std::memory_order_relaxed);
    telemetryCc80Received.store(false, std::memory_order_relaxed);
    telemetryLastCc80Value.store(-1, std::memory_order_relaxed);
    telemetryCc80MuteActive.store(false, std::memory_order_relaxed);
    telemetryLastArticulation.store(static_cast<int>(Articulation::SusDown), std::memory_order_relaxed);
}

void GuitarSynthEngine::updateMasterToneEq(float toneEq)
{
    if (std::abs(toneEq - lastToneEq) < 0.001f)
        return;

    lastToneEq = toneEq;
    const float t = (juce::jlimit(0.0f, 1.0f, toneEq) - 0.5f) * 2.0f; // -1 .. +1

    const auto lowCoeff = juce::IIRCoefficients::makeLowShelf(
        hostSampleRate, 110.0, 0.7, juce::Decibels::decibelsToGain(1.0f - t * 2.5f));
    const auto midCoeff = juce::IIRCoefficients::makePeakFilter(
        hostSampleRate, 330.0, 1.1, juce::Decibels::decibelsToGain(-1.5f - std::abs(t) * 1.5f));
    const auto highCoeff = juce::IIRCoefficients::makePeakFilter(
        hostSampleRate, 1750.0, 0.85, juce::Decibels::decibelsToGain(1.5f + t * 4.5f));

    for (size_t ch = 0; ch < 2; ++ch)
    {
        eqLow[ch].setCoefficients(lowCoeff);
        eqMid[ch].setCoefficients(midCoeff);
        eqHigh[ch].setCoefficients(highCoeff);
    }
}

void GuitarSynthEngine::updateReverbParams(float ambience)
{
    if (std::abs(ambience - lastAmbience) < 0.001f)
        return;

    lastAmbience = ambience;
    juce::Reverb::Parameters rp;
    rp.roomSize = 0.26f;
    rp.damping = 0.65f;
    rp.width = 0.78f;
    rp.dryLevel = 1.0f;
    rp.wetLevel = juce::jlimit(0.0f, 1.0f, ambience) * 0.34f;
    rp.freezeMode = 0.0f;
    reverb.setParameters(rp);
}

GuitarSynthEngine::Voice* GuitarSynthEngine::allocateVoice(bool forReleaseNoise)
{
    for (auto& v : voices)
    {
        if (!v.active)
            return &v;
    }

    // Prefer stealing an already steal-fading voice with lowest remaining gain
    Voice* bestSteal = nullptr;
    float lowestGain = 2.0f;
    for (auto& v : voices)
    {
        if (v.stealFading && v.stealGain < lowestGain)
        {
            lowestGain = v.stealGain;
            bestSteal = &v;
        }
    }
    if (bestSteal != nullptr)
        return bestSteal;

    // Otherwise steal a release-noise or releasing voice with lowest envelope
    Voice* bestRelease = nullptr;
    float lowestEnv = 2.0f;
    for (auto& v : voices)
    {
        if ((v.isReleaseNoise || v.inRelease) && v.envGain < lowestEnv)
        {
            lowestEnv = v.envGain;
            bestRelease = &v;
        }
    }
    if (bestRelease != nullptr)
        return bestRelease;

    if (forReleaseNoise)
        return nullptr; // Do not steal an active sustaining chord note for a release click

    // Fallback: steal the oldest voice
    Voice* oldest = &voices[0];
    for (auto& v : voices)
    {
        if (v.ageSamples > oldest->ageSamples)
            oldest = &v;
    }
    return oldest;
}

void GuitarSynthEngine::triggerNoteOn(int midiChannel,
                                      int midiNote,
                                      int velocity,
                                      const BankData* bank,
                                      const EngineParams& params,
                                      juce::int64 eventSampleTime)
{
    if (bank == nullptr)
        return;

    // 1. Auto Strum Direction Detection
    const juce::int64 dt = eventSampleTime - lastNoteOnTimeSamples;
    bool upstroke = false;
    if (dt >= 0 && dt <= static_cast<juce::int64>(hostSampleRate * 0.085))
    {
        if (midiNote > lastNoteOnMidi)
            upstroke = false; // Low to High = Downstroke
        else if (midiNote < lastNoteOnMidi)
            upstroke = true;  // High to Low = Upstroke
        else
            upstroke = !lastStrumWasUp;
    }
    else if (dt >= 0 && dt <= static_cast<juce::int64>(hostSampleRate * 0.220) && midiNote == lastNoteOnMidi)
    {
        upstroke = !lastStrumWasUp;
    }
    else
    {
        upstroke = false;
    }

    lastNoteOnTimeSamples = eventSampleTime;
    lastNoteOnMidi = midiNote;
    lastStrumWasUp = upstroke;

    // 2. Determine Articulation (with InstaChord CC#80 root-button mute integration)
    const bool cc80RootHeld = !cc80Received
        ? true
        : (params.cc80Invert ? (lastCc80Value < 64) : (lastCc80Value >= 64));

    Articulation art = Articulation::SusDown;
    const auto mode = static_cast<ArticulationMode>(juce::jlimit(0, 7, params.articulationMode));
    switch (mode)
    {
        case ArticulationMode::AutoStrum:
        case ArticulationMode::Cc80FingerMute:
            art = cc80RootHeld
                ? (upstroke ? Articulation::SusUp : Articulation::SusDown)
                : Articulation::FingerMute;
            telemetryLastStrumUp.store(upstroke, std::memory_order_relaxed);
            break;
        case ArticulationMode::Cc80PalmMute:
            art = cc80RootHeld
                ? (upstroke ? Articulation::SusUp : Articulation::SusDown)
                : (upstroke ? Articulation::MuteUp : Articulation::MuteDown);
            telemetryLastStrumUp.store(upstroke, std::memory_order_relaxed);
            break;
        case ArticulationMode::SusDown:
            art = Articulation::SusDown;
            telemetryLastStrumUp.store(false, std::memory_order_relaxed);
            break;
        case ArticulationMode::SusUp:
            art = Articulation::SusUp;
            telemetryLastStrumUp.store(true, std::memory_order_relaxed);
            break;
        case ArticulationMode::PalmMute:
            art = upstroke ? Articulation::MuteUp : Articulation::MuteDown;
            telemetryLastStrumUp.store(upstroke, std::memory_order_relaxed);
            break;
        case ArticulationMode::FingerMute:
            art = Articulation::FingerMute;
            telemetryLastStrumUp.store(upstroke, std::memory_order_relaxed);
            break;
        case ArticulationMode::VelocitySwitch:
            if (velocity < 65)
                art = upstroke ? Articulation::MuteUp : Articulation::MuteDown;
            else
                art = upstroke ? Articulation::SusUp : Articulation::SusDown;
            telemetryLastStrumUp.store(upstroke, std::memory_order_relaxed);
            break;
    }

    telemetryLastMidiNote.store(midiNote, std::memory_order_relaxed);
    telemetryLastArticulation.store(static_cast<int>(art), std::memory_order_relaxed);

    // 3. Lookup Sample & Pitch Ratio
    double pitchRatio = 1.0;
    const int rr = roundRobinCounter++;
    const SampleData* sample = sampleBank.findSample(bank, art, midiNote, rr, pitchRatio);
    if (sample == nullptr || sample->buffer.getNumSamples() < 4)
        return;

    // 4. Smoothly crossfade out any existing voice on the same pitch
    const float stealDec = 1.0f / juce::jmax(1.0f, static_cast<float>(hostSampleRate * 0.008));
    for (auto& existing : voices)
    {
        if (existing.active && !existing.isReleaseNoise && existing.midiNote == midiNote && !existing.stealFading)
        {
            existing.stealFading = true;
            existing.stealDecrement = stealDec;
        }
    }

    Voice* v = allocateVoice(false);
    if (v == nullptr)
        return;

    const float velNorm = juce::jlimit(0.0f, 1.0f, static_cast<float>(velocity) / 127.0f);
    const int trimmedOnset = static_cast<int>(std::round(static_cast<float>(sample->onsetSample) *
                                                         juce::jlimit(0.0f, 1.0f, params.attackTrim)));
    const int safeStart = juce::jlimit(0, juce::jmax(0, sample->buffer.getNumSamples() - 4), trimmedOnset);

    const bool isShortMute = (art == Articulation::MuteDown ||
                              art == Articulation::MuteUp ||
                              art == Articulation::FingerMute);

    *v = Voice {};
    v->active = true;
    v->isReleaseNoise = false;
    v->midiNote = midiNote;
    v->midiChannel = midiChannel;
    v->articulation = art;
    v->velocityGain = 0.25f + 0.75f * std::pow(velNorm, 1.35f);
    v->sample = sample;
    v->srcPos = static_cast<double>(safeStart);
    v->baseInc = (sample->sampleRate / hostSampleRate) * pitchRatio;
    v->fadeInCounter = 0;
    v->fadeInLength = (safeStart > 8) ? 32 : 16;
    v->ageSamples = 0;
    v->minGateSamples = isShortMute ? 0 : static_cast<juce::int64>(hostSampleRate * 0.035);
    v->envGain = 1.0f;
    v->stealGain = 1.0f;
    v->velCutoffHz = juce::jmap(velNorm, 3200.0f, 18500.0f);
    v->currentDamperHz = 18500.0f;
    v->targetDamperHz = 18500.0f;
}

void GuitarSynthEngine::triggerReleaseNoise(int midiNote,
                                            float velocityGain,
                                            const BankData* bank,
                                            const EngineParams& params)
{
    double pitchRatio = 1.0;
    const int rr = roundRobinCounter++;
    const SampleData* relSample = sampleBank.findSample(bank, Articulation::ReleaseNoise, midiNote, rr, pitchRatio);
    if (relSample == nullptr || relSample->buffer.getNumSamples() < 4)
        return;

    Voice* nv = allocateVoice(true);
    if (nv == nullptr)
        return;

    *nv = Voice {};
    nv->active = true;
    nv->isReleaseNoise = true;
    nv->midiNote = midiNote;
    nv->articulation = Articulation::ReleaseNoise;
    nv->velocityGain = velocityGain * juce::jlimit(0.0f, 1.0f, params.releaseNoiseLevel) * 0.35f;
    nv->sample = relSample;
    nv->srcPos = static_cast<double>(juce::jmin(relSample->onsetSample, relSample->buffer.getNumSamples() - 4));
    nv->baseInc = (relSample->sampleRate / hostSampleRate) * pitchRatio;
    nv->fadeInCounter = 0;
    nv->fadeInLength = 24;
    nv->envGain = 1.0f;
    nv->stealGain = 1.0f;
    nv->velCutoffHz = 14000.0f;
    nv->currentDamperHz = 14000.0f;
    nv->targetDamperHz = 14000.0f;
}

void GuitarSynthEngine::beginVoiceRelease(Voice& v,
                                          const BankData* bank,
                                          const EngineParams& params,
                                          juce::int64 eventSampleTime)
{
    if (!v.active || v.inRelease || v.isReleaseNoise)
        return;

    v.inRelease = true;
    v.releasePending = false;
    v.sustainedByPedal = false;

    const float relSec = juce::jlimit(0.05f, 1.50f, params.releaseTimeSec);
    // Target -40dB at relSec so warm body resonance bridges chord changes naturally
    v.releaseCoeff = std::exp(-4.6051702f / (relSec * static_cast<float>(hostSampleRate)));
    v.targetDamperHz = juce::jmap(juce::jlimit(0.0f, 1.0f, params.damperSoftness), 16000.0f, 550.0f);
    v.damperAlpha = 1.0f - std::exp(-1.0f / (0.070f * static_cast<float>(hostSampleRate)));

    // Deduplicate release noise across simultaneously released chord strings (at most once per 80ms)
    if (bank != nullptr &&
        params.releaseNoiseLevel > 0.01f &&
        v.envGain > 0.05f &&
        v.articulation != Articulation::FingerMute)
    {
        const juce::int64 cooldown = static_cast<juce::int64>(hostSampleRate * 0.080);
        if (eventSampleTime - lastReleaseNoiseTimeSamples >= cooldown)
        {
            lastReleaseNoiseTimeSamples = eventSampleTime;
            triggerReleaseNoise(v.midiNote, v.velocityGain, bank, params);
        }
    }
}

void GuitarSynthEngine::triggerNoteOff(int midiChannel,
                                       int midiNote,
                                       const BankData* bank,
                                       const EngineParams& params,
                                       juce::int64 eventSampleTime)
{
    for (auto& v : voices)
    {
        if (!v.active || v.isReleaseNoise || v.inRelease || v.stealFading)
            continue;
        if (v.midiNote != midiNote)
            continue;
        if (midiChannel > 0 && v.midiChannel > 0 && v.midiChannel != midiChannel)
            continue;

        if (sustainPedalDown)
        {
            v.sustainedByPedal = true;
            continue;
        }

        if (v.ageSamples >= v.minGateSamples)
            beginVoiceRelease(v, bank, params, eventSampleTime);
        else
            v.releasePending = true;
    }
}

void GuitarSynthEngine::releasePedalVoices(const BankData* bank,
                                           const EngineParams& params,
                                           juce::int64 eventSampleTime)
{
    for (auto& v : voices)
    {
        if (v.active && !v.isReleaseNoise && v.sustainedByPedal && !v.inRelease)
        {
            v.sustainedByPedal = false;
            if (v.ageSamples >= v.minGateSamples)
                beginVoiceRelease(v, bank, params, eventSampleTime);
            else
                v.releasePending = true;
        }
    }
}

void GuitarSynthEngine::allVoicesFadeOut()
{
    const float fastFade = 1.0f / juce::jmax(1.0f, static_cast<float>(hostSampleRate * 0.015));
    for (auto& v : voices)
    {
        if (v.active)
        {
            v.stealFading = true;
            v.stealDecrement = fastFade;
        }
    }
}

void GuitarSynthEngine::handleMidiMessage(const juce::MidiMessage& msg,
                                          const BankData* bank,
                                          const EngineParams& params,
                                          juce::int64 eventSampleTime)
{
    if (msg.isNoteOn())
    {
        triggerNoteOn(msg.getChannel(), msg.getNoteNumber(), msg.getVelocity(), bank, params, eventSampleTime);
    }
    else if (msg.isNoteOff())
    {
        triggerNoteOff(msg.getChannel(), msg.getNoteNumber(), bank, params, eventSampleTime);
    }
    else if (msg.isController())
    {
        const int cc = msg.getControllerNumber();
        const int val = msg.getControllerValue();
        if (cc == 64) // Sustain Pedal
        {
            const bool nowDown = (val >= 64);
            if (sustainPedalDown && !nowDown)
                releasePedalVoices(bank, params, eventSampleTime);
            sustainPedalDown = nowDown;
        }
        else if (cc == 80) // InstaChord Root Button Hold / Mute Control
        {
            cc80Received = true;
            lastCc80Value = val;
            telemetryCc80Received.store(true, std::memory_order_relaxed);
            telemetryLastCc80Value.store(val, std::memory_order_relaxed);
            const bool rootHeld = params.cc80Invert ? (val < 64) : (val >= 64);
            telemetryCc80MuteActive.store(!rootHeld, std::memory_order_relaxed);
        }
        else if (cc == 120 || cc == 123) // All Sound Off / All Notes Off
        {
            sustainPedalDown = false;
            if (cc == 120)
            {
                allVoicesFadeOut();
            }
            else
            {
                for (auto& v : voices)
                {
                    if (v.active && !v.isReleaseNoise)
                        beginVoiceRelease(v, bank, params, eventSampleTime);
                }
            }
        }
    }
    else if (msg.isPitchWheel())
    {
        const float norm = (static_cast<float>(msg.getPitchWheelValue()) - 8192.0f) / 8192.0f;
        pitchBendSemitones = norm * 2.0f;
        pitchBendMultiplier = std::pow(2.0, static_cast<double>(pitchBendSemitones) / 12.0);
    }
    else if (msg.isAllNotesOff() || msg.isAllSoundOff())
    {
        sustainPedalDown = false;
        allVoicesFadeOut();
    }
}

void GuitarSynthEngine::renderBlock(juce::AudioBuffer<float>& outputBuffer,
                                    const juce::MidiBuffer& midiMessages,
                                    const EngineParams& params)
{
    const int numSamples = outputBuffer.getNumSamples();
    const int numOutChannels = outputBuffer.getNumChannels();
    outputBuffer.clear();

    if (numSamples <= 0 || numOutChannels <= 0)
        return;

    updateMasterToneEq(params.toneEq);
    updateReverbParams(params.ambience);

    if (cc80Received)
    {
        const bool rootHeld = params.cc80Invert ? (lastCc80Value < 64) : (lastCc80Value >= 64);
        telemetryCc80MuteActive.store(!rootHeld, std::memory_order_relaxed);
    }
    else
    {
        telemetryCc80MuteActive.store(false, std::memory_order_relaxed);
    }

    SampleBank::ReaderGuard guard(sampleBank);
    const BankData* bank = guard.get();

    auto renderChunk = [&](int startSample, int chunkLength)
    {
        if (chunkLength <= 0)
            return;

        float* outL = outputBuffer.getWritePointer(0) + startSample;
        float* outR = (numOutChannels > 1) ? (outputBuffer.getWritePointer(1) + startSample) : nullptr;

        for (auto& v : voices)
        {
            if (!v.active || v.sample == nullptr)
                continue;

            const auto& buf = v.sample->buffer;
            const int bufLen = buf.getNumSamples();
            const int bufCh = buf.getNumChannels();
            const float* srcL = buf.getReadPointer(0);
            const float* srcR = (bufCh > 1) ? buf.getReadPointer(1) : srcL;
            const double posInc = v.baseInc * (v.isReleaseNoise ? 1.0 : pitchBendMultiplier);

            float lpCoeff = computeOnePoleCoeff(juce::jmin(v.velCutoffHz, v.currentDamperHz), hostSampleRate);

            for (int i = 0; i < chunkLength; ++i)
            {
                if (v.releasePending && !v.inRelease && v.ageSamples >= v.minGateSamples)
                    beginVoiceRelease(v, bank, params, totalSamplesRendered + i);

                const int idx = static_cast<int>(v.srcPos);
                if (idx >= bufLen - 1)
                {
                    v.active = false;
                    break;
                }

                float sL = hermiteSample(srcL, bufLen, v.srcPos);
                float sR = (srcR == srcL) ? sL : hermiteSample(srcR, bufLen, v.srcPos);

                if (v.inRelease)
                {
                    v.currentDamperHz += v.damperAlpha * (v.targetDamperHz - v.currentDamperHz);
                    if ((i & 7) == 0)
                        lpCoeff = computeOnePoleCoeff(juce::jmin(v.velCutoffHz, v.currentDamperHz), hostSampleRate);
                    v.envGain *= v.releaseCoeff;
                }

                // Per-voice 1-pole low-pass damper & velocity brightness filter
                v.lpL += lpCoeff * (sL - v.lpL);
                v.lpR += lpCoeff * (sR - v.lpR);
                sL = v.lpL;
                sR = v.lpR;

                float fadeIn = 1.0f;
                if (v.fadeInCounter < v.fadeInLength)
                {
                    fadeIn = static_cast<float>(v.fadeInCounter + 1) / static_cast<float>(v.fadeInLength);
                    ++v.fadeInCounter;
                }

                if (v.stealFading)
                {
                    v.stealGain -= v.stealDecrement;
                    if (v.stealGain <= 0.0f)
                    {
                        v.active = false;
                        break;
                    }
                }

                const float gain = v.velocityGain * v.envGain * v.stealGain * fadeIn;
                if (outR != nullptr)
                {
                    outL[i] += sL * gain;
                    outR[i] += sR * gain;
                }
                else
                {
                    outL[i] += 0.5f * (sL + sR) * gain;
                }

                v.srcPos += posInc;
                ++v.ageSamples;

                if (v.inRelease && v.envGain < 0.0004f)
                {
                    v.active = false;
                    break;
                }
            }
        }

        totalSamplesRendered += chunkLength;
    };

    int cursor = 0;
    for (const auto metadata : midiMessages)
    {
        const int eventSample = juce::jlimit(0, numSamples - 1, metadata.samplePosition);
        if (eventSample > cursor)
        {
            renderChunk(cursor, eventSample - cursor);
            cursor = eventSample;
        }
        handleMidiMessage(metadata.getMessage(), bank, params, totalSamplesRendered);
    }

    if (cursor < numSamples)
        renderChunk(cursor, numSamples - cursor);

    // Master output stage: Tone EQ, Output Gain, Studio Ambience, Soft Limiter
    const float outGain = juce::Decibels::decibelsToGain(params.outputGainDb);
    const int processedChannels = juce::jmin(2, numOutChannels);

    for (int ch = 0; ch < processedChannels; ++ch)
    {
        float* data = outputBuffer.getWritePointer(ch);
        eqLow[static_cast<size_t>(ch)].processSamples(data, numSamples);
        eqMid[static_cast<size_t>(ch)].processSamples(data, numSamples);
        eqHigh[static_cast<size_t>(ch)].processSamples(data, numSamples);
        juce::FloatVectorOperations::multiply(data, outGain, numSamples);
    }

    if (params.ambience > 0.005f)
    {
        if (processedChannels == 2)
            reverb.processStereo(outputBuffer.getWritePointer(0), outputBuffer.getWritePointer(1), numSamples);
        else
            reverb.processMono(outputBuffer.getWritePointer(0), numSamples);
    }

    for (int ch = 0; ch < processedChannels; ++ch)
    {
        float* data = outputBuffer.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i)
            data[i] = softLimit(data[i]);
    }

    for (int ch = 2; ch < numOutChannels; ++ch)
        outputBuffer.clear(ch, 0, numSamples);

    int activeCount = 0;
    for (const auto& v : voices)
    {
        if (v.active && !v.isReleaseNoise)
            ++activeCount;
    }
    activeVoiceCount.store(activeCount, std::memory_order_relaxed);
}

} // namespace icguitar
