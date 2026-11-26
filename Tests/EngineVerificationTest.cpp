#include "GuitarSynthEngine.h"
#include "SampleBank.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <iostream>
#include <vector>

namespace
{

float measureRms(const juce::AudioBuffer<float>& buf, int start, int length)
{
    double sum = 0.0;
    const int end = juce::jmin(buf.getNumSamples(), start + length);
    const int count = end - start;
    if (count <= 0)
        return 0.0f;

    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        const float* ptr = buf.getReadPointer(ch);
        for (int i = start; i < end; ++i)
            sum += static_cast<double>(ptr[i]) * static_cast<double>(ptr[i]);
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(count * buf.getNumChannels())));
}

float measureHighFreqRatio(const juce::AudioBuffer<float>& buf, int start, int length)
{
    // First-difference energy / total energy as simple high-frequency brightness proxy
    double diffEnergy = 0.0;
    double totalEnergy = 1e-12;
    const int end = juce::jmin(buf.getNumSamples(), start + length);
    const float* ptr = buf.getReadPointer(0);
    for (int i = start + 1; i < end; ++i)
    {
        const double d = static_cast<double>(ptr[i] - ptr[i - 1]);
        diffEnergy += d * d;
        totalEnergy += static_cast<double>(ptr[i]) * static_cast<double>(ptr[i]);
    }
    return static_cast<float>(diffEnergy / totalEnergy);
}

int renderAndMeasureOnsetSamples(icguitar::GuitarSynthEngine& engine,
                                 int midiNote,
                                 float attackTrim,
                                 double sampleRate)
{
    engine.reset();
    engine.prepare(sampleRate, 512);

    icguitar::EngineParams params;
    params.attackTrim = attackTrim;
    params.ambience = 0.0f;
    params.outputGainDb = 0.0f;
    params.articulationMode = static_cast<int>(icguitar::ArticulationMode::SusDown);

    const int totalSamples = static_cast<int>(sampleRate * 0.12);
    juce::AudioBuffer<float> output(2, totalSamples);
    juce::AudioBuffer<float> block(2, 256);

    for (int pos = 0; pos < totalSamples; pos += 256)
    {
        const int len = juce::jmin(256, totalSamples - pos);
        block.setSize(2, len, false, false, true);
        juce::MidiBuffer midi;
        if (pos == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, midiNote, static_cast<juce::uint8>(110)), 0);

        engine.renderBlock(block, midi, params);
        for (int ch = 0; ch < 2; ++ch)
            output.copyFrom(ch, pos, block, ch, 0, len);
    }

    float peak = 0.0f;
    const int scanPeakLen = juce::jmin(totalSamples, static_cast<int>(sampleRate * 0.06));
    for (int i = 0; i < scanPeakLen; ++i)
        peak = juce::jmax(peak, std::abs(output.getSample(0, i)), std::abs(output.getSample(1, i)));

    const float thresh = peak * 0.25f;
    for (int i = 0; i < scanPeakLen; ++i)
    {
        const float amp = juce::jmax(std::abs(output.getSample(0, i)), std::abs(output.getSample(1, i)));
        if (amp >= thresh)
            return i;
    }
    return scanPeakLen;
}

} // namespace

int main()
{
    const juce::File samplesDir = icguitar::SampleBank::getDefaultSamplesDirectory();
    if (!samplesDir.isDirectory())
    {
        std::cerr << "FAIL: Samples directory missing: " << samplesDir.getFullPathName() << "\n";
        return 1;
    }

    icguitar::SampleBank bank;
    if (!bank.loadSynchronously(samplesDir))
    {
        std::cerr << "FAIL: Failed to load samples: " << bank.getLastError() << "\n";
        return 1;
    }

    const int loaded = bank.getLoadedCount();
    std::cout << "[1/5] Loaded sample count: " << loaded << "\n";
    if (loaded < 900)
    {
        std::cerr << "FAIL: Expected >= 900 samples, got " << loaded << "\n";
        return 1;
    }

    {
        icguitar::SampleBank::ReaderGuard guard(bank);
        const auto* data = guard.get();
        for (auto art : { icguitar::Articulation::SusDown,
                          icguitar::Articulation::SusUp,
                          icguitar::Articulation::MuteDown,
                          icguitar::Articulation::MuteUp,
                          icguitar::Articulation::FingerMute,
                          icguitar::Articulation::ReleaseNoise })
        {
            double ratio = 1.0;
            const auto* s = bank.findSample(data, art, 55, 0, ratio);
            if (s == nullptr || s->buffer.getNumSamples() <= 0)
            {
                std::cerr << "FAIL: Missing sample for articulation " << static_cast<int>(art) << "\n";
                return 1;
            }
        }
    }

    icguitar::GuitarSynthEngine engine(bank);
    const double sr = 44100.0;

    // 2. Onset Trimming Verification (G3 = MIDI 55 had ~19ms pre-attack delay)
    const int rawOnsetSamples = renderAndMeasureOnsetSamples(engine, 55, 0.0f, sr);
    const int trimmedOnsetSamples = renderAndMeasureOnsetSamples(engine, 55, 0.90f, sr);
    const double rawMs = rawOnsetSamples * 1000.0 / sr;
    const double trimmedMs = trimmedOnsetSamples * 1000.0 / sr;
    std::cout << "[2/5] G3 (MIDI 55) onset >25% peak: raw=" << rawMs << " ms (" << rawOnsetSamples
              << " smp), trimmed(90%)=" << trimmedMs << " ms (" << trimmedOnsetSamples << " smp)\n";

    if (trimmedMs >= 2.5)
    {
        std::cerr << "FAIL: Expected trimmed onset < 2.5ms, got " << trimmedMs << "ms\n";
        return 1;
    }
    if (trimmedOnsetSamples >= rawOnsetSamples)
    {
        std::cerr << "FAIL: Trimmed onset was not faster than raw onset\n";
        return 1;
    }

    // 3. Chord Strum Onset Verification
    {
        engine.reset();
        engine.prepare(sr, 256);
        icguitar::EngineParams params;
        params.attackTrim = 0.95f;
        params.ambience = 0.0f;
        params.outputGainDb = 0.0f;

        juce::AudioBuffer<float> block(2, 256);
        juce::MidiBuffer midi;
        for (int note : { 48, 52, 55, 60, 64 })
            midi.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(100)), 0);

        engine.renderBlock(block, midi, params);
        float peak = 0.0f;
        for (int i = 0; i < 256; ++i)
            peak = juce::jmax(peak, std::abs(block.getSample(0, i)));

        int chordOnset = 256;
        for (int i = 0; i < 256; ++i)
        {
            if (std::abs(block.getSample(0, i)) >= peak * 0.25f)
            {
                chordOnset = i;
                break;
            }
        }
        const double chordMs = chordOnset * 1000.0 / sr;
        std::cout << "[3/5] C chord simultaneous onset >25% peak: " << chordMs << " ms\n";
        if (chordMs >= 2.5)
        {
            std::cerr << "FAIL: Expected chord onset < 2.5ms, got " << chordMs << "ms\n";
            return 1;
        }
    }

    // 4. Key-Off Damper Smooth Decay & Release Noise Deduplication Test
    {
        engine.reset();
        engine.prepare(sr, 256);

        icguitar::EngineParams params;
        params.attackTrim = 0.90f;
        params.releaseTimeSec = 0.42f;
        params.damperSoftness = 0.75f;
        params.releaseNoiseLevel = 0.12f;
        params.ambience = 0.0f;
        params.outputGainDb = 0.0f;

        const int holdSamples = static_cast<int>(sr * 0.16);
        const int tailSamples = static_cast<int>(sr * 0.55);
        const int totalSamples = holdSamples + tailSamples;
        juce::AudioBuffer<float> full(2, totalSamples);
        juce::AudioBuffer<float> block(2, 256);

        const std::vector<int> eMajor { 40, 47, 52, 56, 59, 64 };
        bool sentOn = false;
        bool sentOff = false;

        for (int pos = 0; pos < totalSamples; pos += 256)
        {
            const int len = juce::jmin(256, totalSamples - pos);
            block.setSize(2, len, false, false, true);
            juce::MidiBuffer midi;

            if (!sentOn && pos == 0)
            {
                for (int n : eMajor)
                    midi.addEvent(juce::MidiMessage::noteOn(1, n, static_cast<juce::uint8>(100)), 0);
                sentOn = true;
            }
            if (!sentOff && pos <= holdSamples && pos + len > holdSamples)
            {
                const int offOffset = holdSamples - pos;
                for (int n : eMajor)
                    midi.addEvent(juce::MidiMessage::noteOff(1, n), offOffset);
                sentOff = true;
            }

            engine.renderBlock(block, midi, params);
            for (int ch = 0; ch < 2; ++ch)
                full.copyFrom(ch, pos, block, ch, 0, len);
        }

        const float preRms = measureRms(full, holdSamples - 2205, 2205);
        const float earlyRelRms = measureRms(full, holdSamples, 1323); // first 30ms
        const float midRelRms = measureRms(full, holdSamples + 4410, 4410); // 100..200ms
        const float lateRelRms = measureRms(full, holdSamples + static_cast<int>(sr * 0.43), 2205); // 430..480ms

        const float preBrightness = measureHighFreqRatio(full, holdSamples - 2205, 2205);
        const float relBrightness = measureHighFreqRatio(full, holdSamples + 3500, 3500);

        // Verify no huge discontinuity spike right at Note Off
        float maxStepAfterOff = 0.0f;
        for (int i = holdSamples + 1; i < holdSamples + 441; ++i)
            maxStepAfterOff = juce::jmax(maxStepAfterOff, std::abs(full.getSample(0, i) - full.getSample(0, i - 1)));

        std::cout << "[4/5] Key-off RMS progression: pre=" << preRms
                  << ", 0-30ms=" << earlyRelRms
                  << ", 100-200ms=" << midRelRms
                  << ", 430-480ms=" << lateRelRms << "\n";
        std::cout << "      Brightness (HF ratio): pre=" << preBrightness
                  << ", damped=" << relBrightness
                  << ", maxStepAfterOff=" << maxStepAfterOff << "\n";

        if (earlyRelRms < preRms * 0.35f)
        {
            std::cerr << "FAIL: Key-off cut off too abruptly in first 30ms\n";
            return 1;
        }
        if (lateRelRms > preRms * 0.06f)
        {
            std::cerr << "FAIL: Key-off did not decay sufficiently after 430ms\n";
            return 1;
        }
        if (relBrightness >= preBrightness)
        {
            std::cerr << "FAIL: Damper LPF did not soften high frequencies during key-off\n";
            return 1;
        }
        if (maxStepAfterOff > 0.35f)
        {
            std::cerr << "FAIL: Discontinuity/click detected after key-off: " << maxStepAfterOff << "\n";
            return 1;
        }
    }

    // 5. Auto Strum Direction Detection Verification
    {
        engine.reset();
        engine.prepare(sr, 256);
        icguitar::EngineParams params;
        params.articulationMode = static_cast<int>(icguitar::ArticulationMode::AutoStrum);

        juce::AudioBuffer<float> block(2, 1024);
        juce::MidiBuffer downMidi;
        downMidi.addEvent(juce::MidiMessage::noteOn(1, 48, static_cast<juce::uint8>(100)), 0);
        downMidi.addEvent(juce::MidiMessage::noteOn(1, 55, static_cast<juce::uint8>(100)), 300);
        downMidi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 600);
        engine.renderBlock(block, downMidi, params);
        const bool afterAsc = engine.wasLastStrumUp();

        // Advance > 100ms then strum descending
        juce::MidiBuffer emptyMidi;
        for (int k = 0; k < 6; ++k)
            engine.renderBlock(block, emptyMidi, params);

        juce::MidiBuffer upMidi;
        upMidi.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(100)), 0);
        upMidi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 300);
        upMidi.addEvent(juce::MidiMessage::noteOn(1, 55, static_cast<juce::uint8>(100)), 600);
        engine.renderBlock(block, upMidi, params);
        const bool afterDesc = engine.wasLastStrumUp();

        std::cout << "[5/6] Auto strum detector: ascending->up=" << afterAsc
                  << ", descending->up=" << afterDesc << "\n";

        if (afterAsc || !afterDesc)
        {
            std::cerr << "FAIL: Auto strum direction detector mismatch\n";
            return 1;
        }
    }

    // 6. InstaChord CC#80 Root-Button Mute & Polarity Verification
    {
        engine.reset();
        engine.prepare(sr, 256);
        icguitar::EngineParams params;
        params.articulationMode = static_cast<int>(icguitar::ArticulationMode::AutoStrum);
        params.cc80Invert = false;

        juce::AudioBuffer<float> block(2, 256);

        // (a) Default unreceived state -> Sustain
        if (engine.hasReceivedCc80() || engine.isCc80MuteActive())
        {
            std::cerr << "FAIL: Expected CC#80 unreceived and unmuted after reset\n";
            return 1;
        }
        {
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 55, static_cast<juce::uint8>(100)), 0);
            engine.renderBlock(block, midi, params);
            if (engine.getLastTriggeredArticulation() != icguitar::Articulation::SusDown)
            {
                std::cerr << "FAIL: Expected SusDown when CC#80 not yet received\n";
                return 1;
            }
        }

        // (b) CC#80 = 0 (Root button OFF) in AutoStrum / Cc80FingerMute -> FingerMute
        {
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 80, 0), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 55, static_cast<juce::uint8>(100)), 10);
            engine.renderBlock(block, midi, params);

            if (!engine.hasReceivedCc80() || engine.getLastCc80Value() != 0 || !engine.isCc80MuteActive())
            {
                std::cerr << "FAIL: CC#80=0 telemetry state mismatch\n";
                return 1;
            }
            if (engine.getLastTriggeredArticulation() != icguitar::Articulation::FingerMute)
            {
                std::cerr << "FAIL: Expected FingerMute on CC#80=0 in AutoStrum\n";
                return 1;
            }
        }

        // (c) CC#80 = 0 in Cc80PalmMute -> MuteDown
        params.articulationMode = static_cast<int>(icguitar::ArticulationMode::Cc80PalmMute);
        {
            // Advance >250ms so next note is treated as fresh downstroke (outside 220ms repeat window)
            juce::MidiBuffer empty;
            for (int k = 0; k < 50; ++k)
                engine.renderBlock(block, empty, params);

            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 55, static_cast<juce::uint8>(100)), 0);
            engine.renderBlock(block, midi, params);

            if (engine.getLastTriggeredArticulation() != icguitar::Articulation::MuteDown)
            {
                std::cerr << "FAIL: Expected MuteDown on CC#80=0 in Cc80PalmMute\n";
                return 1;
            }
        }

        // (d) CC#80 = 127 (Root button ON) -> SusDown
        {
            juce::MidiBuffer empty;
            for (int k = 0; k < 50; ++k)
                engine.renderBlock(block, empty, params);

            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 80, 127), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 55, static_cast<juce::uint8>(100)), 10);
            engine.renderBlock(block, midi, params);

            if (engine.getLastCc80Value() != 127 || engine.isCc80MuteActive())
            {
                std::cerr << "FAIL: CC#80=127 telemetry state mismatch\n";
                return 1;
            }
            if (engine.getLastTriggeredArticulation() != icguitar::Articulation::SusDown)
            {
                std::cerr << "FAIL: Expected SusDown on CC#80=127\n";
                return 1;
            }
        }

        // (e) Polarity inversion (cc80Invert = true): CC#80 = 127 -> Mute
        params.cc80Invert = true;
        params.articulationMode = static_cast<int>(icguitar::ArticulationMode::Cc80FingerMute);
        {
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 55, static_cast<juce::uint8>(100)), 0);
            engine.renderBlock(block, midi, params);

            if (!engine.isCc80MuteActive() ||
                engine.getLastTriggeredArticulation() != icguitar::Articulation::FingerMute)
            {
                std::cerr << "FAIL: Expected FingerMute when cc80Invert=true and CC#80=127\n";
                return 1;
            }
        }

        std::cout << "[6/6] CC#80 root-button mute & polarity inversion verified.\n";
    }

    std::cout << "ALL VERIFICATION CHECKS PASSED.\n";
    return 0;
}
