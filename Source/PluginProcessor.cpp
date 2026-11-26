#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr std::array<int, 5> kTestChordNotes { 48, 55, 60, 64, 67 }; // C3, G3, C4, E4, G4
} // namespace

ICStandardGuitarProcessor::ICStandardGuitarProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      engine(sampleBank),
      apvts(*this, nullptr, "ICStandardGuitarState", createParameterLayout())
{
    attackTrimParam = apvts.getRawParameterValue("attackTrim");
    releaseTimeParam = apvts.getRawParameterValue("releaseTime");
    damperSoftnessParam = apvts.getRawParameterValue("damperSoftness");
    releaseNoiseParam = apvts.getRawParameterValue("releaseNoise");
    articulationModeParam = apvts.getRawParameterValue("articulationMode");
    toneEqParam = apvts.getRawParameterValue("toneEq");
    ambienceParam = apvts.getRawParameterValue("ambience");
    outputGainParam = apvts.getRawParameterValue("outputGain");
    cc80InvertParam = apvts.getRawParameterValue("cc80Invert");

    pendingRequests.reserve(16);
    scheduledAuditionEvents.reserve(64);

    const auto defaultDir = icguitar::SampleBank::getDefaultSamplesDirectory();
    configuredSamplesPath = defaultDir.getFullPathName();
    if (icguitar::SampleBank::resolveSamplesDirectory(defaultDir).isDirectory())
        sampleBank.startAsyncLoad(defaultDir);
}

ICStandardGuitarProcessor::~ICStandardGuitarProcessor()
{
    sampleBank.stopLoading();
}

juce::AudioProcessorValueTreeState::ParameterLayout ICStandardGuitarProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("attackTrim", 1),
        "Attack Tightness",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.90f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("releaseTime", 1),
        "Key-Off Release",
        juce::NormalisableRange<float>(0.05f, 1.50f, 0.01f, 0.6f),
        0.42f,
        juce::AudioParameterFloatAttributes().withLabel("s")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("damperSoftness", 1),
        "Damper Softness",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.75f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("releaseNoise", 1),
        "Release Noise",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.12f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("articulationMode", 1),
        "Articulation",
        juce::StringArray {
            "Auto Strum (D/U)",
            "Sustain Down",
            "Sustain Up",
            "Palm Mute",
            "Finger Mute",
            "Velocity Switch",
            "IC CC#80 Auto (Sus / Finger Mute)",
            "IC CC#80 Auto (Sus / Palm Mute)"
        },
        0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("toneEq", 1),
        "Guitar Tone",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.60f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("ambience", 1),
        "Body Ambience",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.18f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("outputGain", 1),
        "Output Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        2.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("cc80Invert", 1),
        "CC#80 Polarity Invert",
        false));

    return { params.begin(), params.end() };
}

void ICStandardGuitarProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 1000.0 ? sampleRate : 44100.0;
    audioSampleClock = 0;
    {
        const juce::SpinLock::ScopedLockType lock(auditionLock);
        scheduledAuditionEvents.clear();
    }
    engine.prepare(currentSampleRate, samplesPerBlock);
}

void ICStandardGuitarProcessor::releaseResources()
{
    engine.reset();
}

bool ICStandardGuitarProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
        return false;

    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

icguitar::EngineParams ICStandardGuitarProcessor::readParams() const noexcept
{
    icguitar::EngineParams p;
    p.attackTrim = attackTrimParam != nullptr ? attackTrimParam->load() : 0.90f;
    p.releaseTimeSec = releaseTimeParam != nullptr ? releaseTimeParam->load() : 0.42f;
    p.damperSoftness = damperSoftnessParam != nullptr ? damperSoftnessParam->load() : 0.75f;
    p.releaseNoiseLevel = releaseNoiseParam != nullptr ? releaseNoiseParam->load() : 0.12f;
    p.articulationMode = articulationModeParam != nullptr
        ? static_cast<int>(std::round(articulationModeParam->load()))
        : 0;
    p.toneEq = toneEqParam != nullptr ? toneEqParam->load() : 0.60f;
    p.ambience = ambienceParam != nullptr ? ambienceParam->load() : 0.18f;
    p.outputGainDb = outputGainParam != nullptr ? outputGainParam->load() : 2.0f;
    p.cc80Invert = cc80InvertParam != nullptr ? (cc80InvertParam->load() >= 0.5f) : false;

    if (lastAuditionModeOverride >= 0)
        p.articulationMode = lastAuditionModeOverride;

    return p;
}

void ICStandardGuitarProcessor::injectAuditionEvents(juce::MidiBuffer& midiMessages, int numSamples)
{
    const juce::SpinLock::ScopedTryLockType tryLock(auditionLock);
    if (!tryLock.isLocked())
        return;

    if (!pendingRequests.empty())
    {
        for (const auto& req : pendingRequests)
        {
            if (req.isRelease)
            {
                scheduledAuditionEvents.clear();
                lastAuditionModeOverride = -1;
                for (int note : kTestChordNotes)
                {
                    scheduledAuditionEvents.push_back({
                        juce::MidiMessage::noteOff(1, note),
                        audioSampleClock
                    });
                }
            }
            else if (req.isStrum)
            {
                scheduledAuditionEvents.clear();
                lastAuditionModeOverride = req.palmMute
                    ? static_cast<int>(icguitar::ArticulationMode::PalmMute)
                    : -1;

                const juce::int64 spacing = static_cast<juce::int64>(currentSampleRate * 0.015);
                const int count = static_cast<int>(kTestChordNotes.size());
                for (int i = 0; i < count; ++i)
                {
                    const int note = req.upstroke
                        ? kTestChordNotes[static_cast<size_t>(count - 1 - i)]
                        : kTestChordNotes[static_cast<size_t>(i)];

                    scheduledAuditionEvents.push_back({
                        juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(104)),
                        audioSampleClock + static_cast<juce::int64>(i) * spacing
                    });
                }
            }
        }
        pendingRequests.clear();
    }

    if (scheduledAuditionEvents.empty())
        return;

    const juce::int64 blockEnd = audioSampleClock + numSamples;
    size_t writeIdx = 0;
    for (size_t i = 0; i < scheduledAuditionEvents.size(); ++i)
    {
        const auto& ev = scheduledAuditionEvents[i];
        if (ev.targetSampleTime < blockEnd)
        {
            const int sampleOffset = juce::jlimit(0, numSamples - 1,
                static_cast<int>(ev.targetSampleTime - audioSampleClock));
            midiMessages.addEvent(ev.message, sampleOffset);
        }
        else
        {
            scheduledAuditionEvents[writeIdx++] = ev;
        }
    }
    scheduledAuditionEvents.resize(writeIdx);
}

void ICStandardGuitarProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    if (!midiMessages.isEmpty())
    {
        // Real incoming MIDI cancels any temporary palm-mute audition override
        lastAuditionModeOverride = -1;
    }

    const int numSamples = buffer.getNumSamples();
    injectAuditionEvents(midiMessages, numSamples);

    const auto params = readParams();
    engine.renderBlock(buffer, midiMessages, params);

    audioSampleClock += numSamples;
}

void ICStandardGuitarProcessor::triggerTestStrum(bool upstroke, bool palmMute)
{
    const juce::SpinLock::ScopedLockType lock(auditionLock);
    if (pendingRequests.size() < 16)
        pendingRequests.push_back({ true, false, upstroke, palmMute });
}

void ICStandardGuitarProcessor::triggerTestRelease()
{
    const juce::SpinLock::ScopedLockType lock(auditionLock);
    if (pendingRequests.size() < 16)
        pendingRequests.push_back({ false, true, false, false });
}

void ICStandardGuitarProcessor::setSamplesDirectory(const juce::File& dir)
{
    const auto resolved = icguitar::SampleBank::resolveSamplesDirectory(dir);
    {
        const juce::ScopedLock sl(pathLock);
        configuredSamplesPath = resolved.getFullPathName();
    }
    icguitar::SampleBank::saveSamplesDirectoryPreference(resolved);
    sampleBank.startAsyncLoad(resolved);
}

juce::String ICStandardGuitarProcessor::getSamplesDirectoryPath() const
{
    const juce::ScopedLock sl(pathLock);
    return configuredSamplesPath;
}

void ICStandardGuitarProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    {
        const juce::ScopedLock sl(pathLock);
        state.setProperty("samplesPath", configuredSamplesPath, nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void ICStandardGuitarProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
    {
        auto tree = juce::ValueTree::fromXml(*xmlState);
        const juce::String savedPath = tree.getProperty("samplesPath", "").toString();
        apvts.replaceState(tree);

        if (savedPath.isNotEmpty() && savedPath != getSamplesDirectoryPath())
        {
            const juce::File dir(savedPath);
            if (icguitar::SampleBank::resolveSamplesDirectory(dir).isDirectory())
                setSamplesDirectory(dir);
        }
    }
}

juce::AudioProcessorEditor* ICStandardGuitarProcessor::createEditor()
{
    return new ICStandardGuitarEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ICStandardGuitarProcessor();
}
