#pragma once

#include "GuitarSynthEngine.h"
#include "SampleBank.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

class ICStandardGuitarProcessor : public juce::AudioProcessor
{
public:
    ICStandardGuitarProcessor();
    ~ICStandardGuitarProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    icguitar::SampleBank& getSampleBank() noexcept { return sampleBank; }
    const icguitar::SampleBank& getSampleBank() const noexcept { return sampleBank; }
    const icguitar::GuitarSynthEngine& getEngine() const noexcept { return engine; }

    void setSamplesDirectory(const juce::File& dir);
    juce::String getSamplesDirectoryPath() const;

    // UI Audition controls
    void triggerTestStrum(bool upstroke, bool palmMute);
    void triggerTestRelease();

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    struct PendingMidiEvent
    {
        juce::MidiMessage message;
        juce::int64 targetSampleTime = 0;
    };

    icguitar::EngineParams readParams() const noexcept;
    void injectAuditionEvents(juce::MidiBuffer& midiMessages, int numSamples);

    icguitar::SampleBank sampleBank;
    icguitar::GuitarSynthEngine engine;
    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* attackTrimParam = nullptr;
    std::atomic<float>* releaseTimeParam = nullptr;
    std::atomic<float>* damperSoftnessParam = nullptr;
    std::atomic<float>* releaseNoiseParam = nullptr;
    std::atomic<float>* articulationModeParam = nullptr;
    std::atomic<float>* toneEqParam = nullptr;
    std::atomic<float>* ambienceParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* cc80InvertParam = nullptr;

    mutable juce::CriticalSection pathLock;
    juce::String configuredSamplesPath;

    juce::SpinLock auditionLock;
    struct QueuedAuditionRequest
    {
        bool isStrum = false;
        bool isRelease = false;
        bool upstroke = false;
        bool palmMute = false;
    };
    std::vector<QueuedAuditionRequest> pendingRequests;
    std::vector<PendingMidiEvent> scheduledAuditionEvents;
    juce::int64 audioSampleClock = 0;
    double currentSampleRate = 44100.0;
    int lastAuditionModeOverride = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ICStandardGuitarProcessor)
};
