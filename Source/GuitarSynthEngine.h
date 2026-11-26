#pragma once

#include "SampleBank.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>

namespace icguitar
{

enum class ArticulationMode : int
{
    AutoStrum = 0,
    SusDown,
    SusUp,
    PalmMute,
    FingerMute,
    VelocitySwitch,
    Cc80FingerMute,
    Cc80PalmMute
};

struct EngineParams
{
    float attackTrim = 0.90f;        // 0..1 (onset trimming amount)
    float releaseTimeSec = 0.42f;    // 0.05..1.50s (key-off exponential decay)
    float damperSoftness = 0.75f;    // 0..1 (LPF roll-off on key-off)
    float releaseNoiseLevel = 0.12f; // 0..1 (deduplicated release noise level)
    int articulationMode = 0;        // ArticulationMode index
    float toneEq = 0.60f;            // 0..1 (pickup/body EQ character)
    float ambience = 0.18f;          // 0..1 (studio body room reverb)
    float outputGainDb = 2.0f;       // -12..+12 dB
    bool cc80Invert = false;         // false: >=64 Sus / <64 Mute, true: <64 Sus / >=64 Mute
};

class GuitarSynthEngine
{
public:
    static constexpr int kMaxVoices = 24;

    explicit GuitarSynthEngine(SampleBank& bank);

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    void renderBlock(juce::AudioBuffer<float>& outputBuffer,
                     const juce::MidiBuffer& midiMessages,
                     const EngineParams& params);

    int getActiveVoiceCount() const noexcept { return activeVoiceCount.load(std::memory_order_relaxed); }
    bool wasLastStrumUp() const noexcept { return telemetryLastStrumUp.load(std::memory_order_relaxed); }
    int getLastPlayedNote() const noexcept { return telemetryLastMidiNote.load(std::memory_order_relaxed); }
    bool hasReceivedCc80() const noexcept { return telemetryCc80Received.load(std::memory_order_relaxed); }
    int getLastCc80Value() const noexcept { return telemetryLastCc80Value.load(std::memory_order_relaxed); }
    bool isCc80MuteActive() const noexcept { return telemetryCc80MuteActive.load(std::memory_order_relaxed); }
    Articulation getLastTriggeredArticulation() const noexcept
    {
        return static_cast<Articulation>(telemetryLastArticulation.load(std::memory_order_relaxed));
    }

private:
    struct Voice
    {
        bool active = false;
        bool isReleaseNoise = false;
        bool releasePending = false;
        bool inRelease = false;
        bool sustainedByPedal = false;
        bool stealFading = false;

        int midiNote = 60;
        int midiChannel = 1;
        Articulation articulation = Articulation::SusDown;
        float velocityGain = 1.0f;

        const SampleData* sample = nullptr;
        double srcPos = 0.0;
        double baseInc = 1.0;

        int fadeInCounter = 0;
        int fadeInLength = 48;

        juce::int64 ageSamples = 0;
        juce::int64 minGateSamples = 0;

        float envGain = 1.0f;
        float releaseCoeff = 0.9995f;
        float stealGain = 1.0f;
        float stealDecrement = 0.002f;

        // Per-voice 1-pole low-pass damper & velocity filter
        float lpL = 0.0f;
        float lpR = 0.0f;
        float velCutoffHz = 16000.0f;
        float currentDamperHz = 18000.0f;
        float targetDamperHz = 18000.0f;
        float damperAlpha = 0.001f;
    };

    void handleMidiMessage(const juce::MidiMessage& msg,
                           const BankData* bank,
                           const EngineParams& params,
                           juce::int64 eventSampleTime);

    void triggerNoteOn(int midiChannel,
                       int midiNote,
                       int velocity,
                       const BankData* bank,
                       const EngineParams& params,
                       juce::int64 eventSampleTime);

    void triggerNoteOff(int midiChannel,
                        int midiNote,
                        const BankData* bank,
                        const EngineParams& params,
                        juce::int64 eventSampleTime);

    void beginVoiceRelease(Voice& voice,
                           const BankData* bank,
                           const EngineParams& params,
                           juce::int64 eventSampleTime);

    void triggerReleaseNoise(int midiNote,
                             float velocityGain,
                             const BankData* bank,
                             const EngineParams& params);

    void releasePedalVoices(const BankData* bank,
                            const EngineParams& params,
                            juce::int64 eventSampleTime);

    void allVoicesFadeOut();
    Voice* allocateVoice(bool forReleaseNoise);

    void updateMasterToneEq(float toneEq);
    void updateReverbParams(float ambience);

    SampleBank& sampleBank;
    std::array<Voice, kMaxVoices> voices {};

    double hostSampleRate = 44100.0;
    juce::int64 totalSamplesRendered = 0;

    // Strum detector state
    juce::int64 lastNoteOnTimeSamples = -1000000;
    int lastNoteOnMidi = 60;
    bool lastStrumWasUp = false;
    int roundRobinCounter = 0;

    // Chord release-noise deduplication
    juce::int64 lastReleaseNoiseTimeSamples = -1000000;

    // MIDI controller state
    bool sustainPedalDown = false;
    bool cc80Received = false;
    int lastCc80Value = 127;
    float pitchBendSemitones = 0.0f;
    double pitchBendMultiplier = 1.0;

    // Master EQ (3 bands, stereo) & body ambience
    std::array<juce::SingleThreadedIIRFilter, 2> eqLow;
    std::array<juce::SingleThreadedIIRFilter, 2> eqMid;
    std::array<juce::SingleThreadedIIRFilter, 2> eqHigh;
    float lastToneEq = -1.0f;

    juce::Reverb reverb;
    float lastAmbience = -1.0f;

    std::atomic<int> activeVoiceCount { 0 };
    std::atomic<bool> telemetryLastStrumUp { false };
    std::atomic<int> telemetryLastMidiNote { -1 };
    std::atomic<bool> telemetryCc80Received { false };
    std::atomic<int> telemetryLastCc80Value { -1 };
    std::atomic<bool> telemetryCc80MuteActive { false };
    std::atomic<int> telemetryLastArticulation { static_cast<int>(Articulation::SusDown) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GuitarSynthEngine)
};

} // namespace icguitar
