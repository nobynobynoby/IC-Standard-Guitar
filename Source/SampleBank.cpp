#include "SampleBank.h"

#include <algorithm>
#include <cmath>

namespace icguitar
{

namespace
{
struct FolderSpec
{
    Articulation articulation;
    const char* folderName;
    int basePriority;
};

constexpr std::array<FolderSpec, kNumArticulations> kFolderSpecs {{
    { Articulation::SusDown,      "Sus_Down",    0 },
    { Articulation::SusUp,        "Sus_Up",      1 },
    { Articulation::MuteDown,     "Mute_Down",   2 },
    { Articulation::MuteUp,       "Mute_Up",     3 },
    { Articulation::FingerMute,   "Finger Mute", 4 },
    { Articulation::ReleaseNoise, "Release1",    5 },
    { Articulation::Brush,        "Brush",       6 }
}};

int noteLetterToSemitone(juce::juce_wchar letter) noexcept
{
    switch (juce::CharacterFunctions::toLowerCase(letter))
    {
        case 'c': return 0;
        case 'd': return 2;
        case 'e': return 4;
        case 'f': return 5;
        case 'g': return 7;
        case 'a': return 9;
        case 'b': return 11;
        default:  return -1;
    }
}

const SampleData* pickReadyRoundRobin(const std::array<SampleData, kMaxRoundRobins>& rrSlots,
                                      int preferredRr) noexcept
{
    const int startIdx = ((preferredRr % kMaxRoundRobins) + kMaxRoundRobins) % kMaxRoundRobins;
    for (int offset = 0; offset < kMaxRoundRobins; ++offset)
    {
        const int idx = (startIdx + offset) % kMaxRoundRobins;
        if (rrSlots[static_cast<size_t>(idx)].ready.load(std::memory_order_acquire))
            return &rrSlots[static_cast<size_t>(idx)];
    }
    return nullptr;
}

} // namespace

SampleBank::SampleBank()
{
    ownedBank = std::make_unique<BankData>();
    activeBank.store(ownedBank.get(), std::memory_order_release);
}

SampleBank::~SampleBank()
{
    stopLoading();
    activeBank.store(nullptr, std::memory_order_release);
    while (activeReaders.load(std::memory_order_acquire) > 0)
        juce::Thread::sleep(1);
    ownedBank.reset();
}

juce::File SampleBank::resolveSamplesDirectory(const juce::File& candidate)
{
    if (!candidate.exists())
        return {};

    if (candidate.getChildFile("Sus_Down").isDirectory())
        return candidate;

    auto sub = candidate.getChildFile("Samples");
    if (sub.isDirectory() && sub.getChildFile("Sus_Down").isDirectory())
        return sub;

    return candidate;
}

namespace
{
juce::File getSettingsFile()
{
    // macOS: ~/Library/Application Support/ICStandardGuitar
    // Windows: %APPDATA%\ICStandardGuitar
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile("Application Support");
   #endif
    return base
        .getChildFile("ICStandardGuitar")
        .getChildFile("settings.xml");
}
} // namespace

juce::File SampleBank::getSavedSamplesDirectory()
{
    const auto settingsFile = getSettingsFile();
    if (!settingsFile.existsAsFile())
        return {};

    if (auto xml = juce::XmlDocument::parse(settingsFile))
    {
        const juce::String savedPath = xml->getStringAttribute("samplesPath").trim();
        if (savedPath.isNotEmpty())
        {
            const auto resolved = resolveSamplesDirectory(juce::File(savedPath));
            if (resolved.isDirectory() && resolved.getChildFile("Sus_Down").isDirectory())
                return resolved;
        }
    }
    return {};
}

void SampleBank::saveSamplesDirectoryPreference(const juce::File& dir)
{
    const auto resolved = resolveSamplesDirectory(dir);
    if (!resolved.isDirectory() || !resolved.getChildFile("Sus_Down").isDirectory())
        return;

    const auto settingsFile = getSettingsFile();
    const auto parent = settingsFile.getParentDirectory();
    if (!parent.isDirectory())
        parent.createDirectory();

    juce::XmlElement root("ICStandardGuitarSettings");
    root.setAttribute("samplesPath", resolved.getFullPathName());
    root.writeTo(settingsFile);
}

juce::File SampleBank::getDefaultSamplesDirectory()
{
    if (const auto saved = getSavedSamplesDirectory(); saved.isDirectory())
        return saved;

    const auto music = juce::File::getSpecialLocation(juce::File::userMusicDirectory);
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    const auto desktop = juce::File::getSpecialLocation(juce::File::userDesktopDirectory);

   #if JUCE_WINDOWS
    const auto documents = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    const std::array<juce::File, 5> candidates {{
        music.getChildFile("UI_Standard_Guitar/Samples"),
        documents.getChildFile("UI_Standard_Guitar/Samples"),
        home.getChildFile("Downloads/UI_Standard_Guitar/Samples"),
        desktop.getChildFile("UI_Standard_Guitar/Samples"),
        juce::File("C:\\UI_Standard_Guitar\\Samples")
    }};
   #else
    const std::array<juce::File, 5> candidates {{
        music.getChildFile("Logic/UI_Standard_Guitar/Samples"),
        music.getChildFile("UI_Standard_Guitar/Samples"),
        music.getChildFile("Audio Music Apps/UI_Standard_Guitar/Samples"),
        home.getChildFile("Downloads/UI_Standard_Guitar/Samples"),
        desktop.getChildFile("UI_Standard_Guitar/Samples")
    }};
   #endif

    for (const auto& candidate : candidates)
    {
        const auto resolved = resolveSamplesDirectory(candidate);
        if (resolved.isDirectory() && resolved.getChildFile("Sus_Down").isDirectory())
            return resolved;
    }

    return candidates[0];
}

bool SampleBank::parseNoteAndRoundRobin(const juce::String& stem, int& outMidiNote, int& outRrIndex)
{
    if (stem.startsWithIgnoreCase("Voice"))
        return false;

    const int underscoreIdx = stem.indexOfChar('_');
    if (underscoreIdx <= 1)
        return false;

    const juce::String noteToken = stem.substring(0, underscoreIdx).trim();
    if (noteToken.length() < 2)
        return false;

    int pos = 0;
    const int baseSemitone = noteLetterToSemitone(noteToken[pos++]);
    if (baseSemitone < 0)
        return false;

    int accidental = 0;
    if (pos < noteToken.length())
    {
        const auto ch = noteToken[pos];
        if (ch == '#')
        {
            accidental = 1;
            ++pos;
        }
        else if (ch == 'b' || ch == 'B')
        {
            accidental = -1;
            ++pos;
        }
    }

    const juce::String octaveStr = noteToken.substring(pos);
    if (octaveStr.isEmpty())
        return false;

    for (int i = 0; i < octaveStr.length(); ++i)
    {
        const auto ch = octaveStr[i];
        if (i == 0 && ch == '-' && octaveStr.length() > 1)
            continue;
        if (!juce::CharacterFunctions::isDigit(ch))
            return false;
    }

    const int octave = octaveStr.getIntValue();
    const int midiNote = (octave + 1) * 12 + baseSemitone + accidental;
    if (midiNote < 0 || midiNote >= kNumMidiNotes)
        return false;

    // Extract trailing integer round-robin index (1..kMaxRoundRobins)
    int digitStart = stem.length();
    while (digitStart > 0 && juce::CharacterFunctions::isDigit(stem[digitStart - 1]))
        --digitStart;

    if (digitStart >= stem.length())
        return false;

    const int rrNumber = stem.substring(digitStart).getIntValue();
    if (rrNumber < 1 || rrNumber > kMaxRoundRobins)
        return false;

    outMidiNote = midiNote;
    outRrIndex = rrNumber - 1;
    return true;
}

int SampleBank::computeOnsetSample(const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return 0;

    // Scan first 55ms to capture true pick transient while ignoring any pre-attack dead air
    const int scanLength = juce::jmin(numSamples, static_cast<int>(sampleRate * 0.055));
    float peak = 0.0f;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        for (int i = 0; i < scanLength; ++i)
            peak = juce::jmax(peak, std::abs(data[i]));
    }

    if (peak < 1.0e-4f)
        return 0;

    const float threshold = peak * 0.18f;
    int rawOnset = 0;
    for (int i = 0; i < scanLength; ++i)
    {
        float samplePeak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            samplePeak = juce::jmax(samplePeak, std::abs(buffer.getReadPointer(ch)[i]));

        if (samplePeak >= threshold)
        {
            rawOnset = i;
            break;
        }
    }

    // Back up ~0.36ms (16 samples at 44.1kHz) to retain the crisp pick edge without delay
    return juce::jmax(0, rawOnset - 16);
}

std::vector<SampleBank::LoadTask> SampleBank::buildLoadTasks(const juce::File& samplesDir) const
{
    std::vector<LoadTask> tasks;
    tasks.reserve(1024);

    for (const auto& spec : kFolderSpecs)
    {
        const auto folder = samplesDir.getChildFile(spec.folderName);
        if (!folder.isDirectory())
            continue;

        const auto files = folder.findChildFiles(juce::File::findFiles, false, "*.flac");
        for (const auto& f : files)
        {
            int midiNote = 0;
            int rrIndex = 0;
            if (!parseNoteAndRoundRobin(f.getFileNameWithoutExtension(), midiNote, rrIndex))
                continue;

            // Prioritize RR1 of SusDown/SusUp/MuteDown/MuteUp first so playback is responsive immediately
            const int priority = rrIndex * 10 + spec.basePriority;
            tasks.push_back({ f, spec.articulation, midiNote, rrIndex, priority });
        }
    }

    std::sort(tasks.begin(), tasks.end(), [](const LoadTask& a, const LoadTask& b)
    {
        if (a.priority != b.priority)
            return a.priority < b.priority;
        return a.midiNote < b.midiNote;
    });

    return tasks;
}

bool SampleBank::decodeTask(const LoadTask& task, BankData& targetBank, juce::AudioFormatManager& formatManager)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(task.file));
    if (reader == nullptr)
        return false;

    const int numSamples = static_cast<int>(juce::jmin<juce::int64>(reader->lengthInSamples, 44100 * 12));
    const int numChannels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
    if (numSamples <= 0)
        return false;

    auto& slot = targetBank.slots[static_cast<size_t>(task.articulation)]
                                 [static_cast<size_t>(task.midiNote)]
                                 [static_cast<size_t>(task.rrIndex)];

    slot.buffer.setSize(numChannels, numSamples, false, false, false);
    if (!reader->read(&slot.buffer, 0, numSamples, 0, true, numChannels > 1))
        return false;

    slot.sampleRate = reader->sampleRate > 1000.0 ? reader->sampleRate : 44100.0;
    slot.rootMidiNote = task.midiNote;
    slot.onsetSample = computeOnsetSample(slot.buffer, slot.sampleRate);
    slot.ready.store(true, std::memory_order_release);
    return true;
}

void SampleBank::swapBankData(std::unique_ptr<BankData> nextBank)
{
    auto* nextPtr = nextBank.get();
    activeBank.store(nextPtr, std::memory_order_release);
    while (activeReaders.load(std::memory_order_acquire) > 0)
        juce::Thread::sleep(1);
    ownedBank = std::move(nextBank);
}

void SampleBank::stopLoading()
{
    if (loaderThread != nullptr)
    {
        loaderThread->signalThreadShouldExit();
        loaderThread->stopThread(5000);
        loaderThread.reset();
    }
    loading.store(false, std::memory_order_release);
}

void SampleBank::startAsyncLoad(const juce::File& directory)
{
    stopLoading();

    const auto resolved = resolveSamplesDirectory(directory);
    {
        const juce::ScopedLock sl(stateLock);
        currentDirectory = resolved;
        lastErrorMessage.clear();
    }

    auto freshBank = std::make_unique<BankData>();
    swapBankData(std::move(freshBank));

    loadedCount.store(0, std::memory_order_release);
    totalTargetCount.store(0, std::memory_order_release);

    if (!resolved.isDirectory() || !resolved.getChildFile("Sus_Down").isDirectory())
    {
        const juce::ScopedLock sl(stateLock);
        lastErrorMessage = "Samples folder not found: " + directory.getFullPathName();
        return;
    }

    loading.store(true, std::memory_order_release);
    loaderThread = std::make_unique<LoaderThread>(*this);
    loaderThread->startThread(juce::Thread::Priority::normal);
}

bool SampleBank::loadSynchronously(const juce::File& directory)
{
    stopLoading();

    const auto resolved = resolveSamplesDirectory(directory);
    {
        const juce::ScopedLock sl(stateLock);
        currentDirectory = resolved;
        lastErrorMessage.clear();
    }

    auto freshBank = std::make_unique<BankData>();
    if (!resolved.isDirectory() || !resolved.getChildFile("Sus_Down").isDirectory())
    {
        const juce::ScopedLock sl(stateLock);
        lastErrorMessage = "Samples folder not found: " + directory.getFullPathName();
        swapBankData(std::move(freshBank));
        return false;
    }

    const auto tasks = buildLoadTasks(resolved);
    totalTargetCount.store(static_cast<int>(tasks.size()), std::memory_order_release);
    loadedCount.store(0, std::memory_order_release);

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    int decoded = 0;
    for (const auto& task : tasks)
    {
        if (decodeTask(task, *freshBank, formatManager))
            ++decoded;
        loadedCount.store(decoded, std::memory_order_release);
    }

    swapBankData(std::move(freshBank));
    return decoded > 0;
}

void SampleBank::runLoaderThread(juce::Thread& thread)
{
    juce::File dir;
    {
        const juce::ScopedLock sl(stateLock);
        dir = currentDirectory;
    }

    const auto tasks = buildLoadTasks(dir);
    totalTargetCount.store(static_cast<int>(tasks.size()), std::memory_order_release);

    if (tasks.empty())
    {
        const juce::ScopedLock sl(stateLock);
        lastErrorMessage = "No FLAC samples found in " + dir.getFullPathName();
        loading.store(false, std::memory_order_release);
        return;
    }

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    // activeBank points to ownedBank, which lives until stopLoading()/swapBankData()
    auto* targetBank = ownedBank.get();
    int decoded = 0;

    for (const auto& task : tasks)
    {
        if (thread.threadShouldExit())
            break;

        if (targetBank != nullptr && decodeTask(task, *targetBank, formatManager))
            ++decoded;

        loadedCount.store(decoded, std::memory_order_release);
    }

    if (decoded == 0 && !thread.threadShouldExit())
    {
        const juce::ScopedLock sl(stateLock);
        lastErrorMessage = "Failed to decode FLAC files in " + dir.getFullPathName();
    }

    loading.store(false, std::memory_order_release);
}

const SampleData* SampleBank::findSample(const BankData* bank,
                                         Articulation articulation,
                                         int targetMidiNote,
                                         int roundRobinIndex,
                                         double& outPitchRatio) const noexcept
{
    if (bank == nullptr)
        return nullptr;

    const int clampedNote = juce::jlimit(0, kNumMidiNotes - 1, targetMidiNote);
    const auto searchArticulation = [bank, clampedNote, roundRobinIndex, &outPitchRatio](Articulation art) -> const SampleData*
    {
        const auto& noteTable = bank->slots[static_cast<size_t>(art)];

        if (const auto* exact = pickReadyRoundRobin(noteTable[static_cast<size_t>(clampedNote)], roundRobinIndex))
        {
            outPitchRatio = 1.0;
            return exact;
        }

        for (int delta = 1; delta <= 48; ++delta)
        {
            if (clampedNote - delta >= 0)
            {
                if (const auto* lower = pickReadyRoundRobin(noteTable[static_cast<size_t>(clampedNote - delta)], roundRobinIndex))
                {
                    outPitchRatio = std::pow(2.0, static_cast<double>(delta) / 12.0);
                    return lower;
                }
            }
            if (clampedNote + delta < kNumMidiNotes)
            {
                if (const auto* upper = pickReadyRoundRobin(noteTable[static_cast<size_t>(clampedNote + delta)], roundRobinIndex))
                {
                    outPitchRatio = std::pow(2.0, static_cast<double>(-delta) / 12.0);
                    return upper;
                }
            }
        }
        return nullptr;
    };

    if (const auto* primary = searchArticulation(articulation))
        return primary;

    // Graceful fallback while secondary articulations are still loading
    if (articulation != Articulation::SusDown)
        return searchArticulation(Articulation::SusDown);

    return nullptr;
}

float SampleBank::getProgress() const noexcept
{
    const int total = totalTargetCount.load(std::memory_order_acquire);
    if (total <= 0)
        return isLoaded() ? 1.0f : 0.0f;
    return juce::jlimit(0.0f, 1.0f, static_cast<float>(loadedCount.load(std::memory_order_acquire)) / static_cast<float>(total));
}

juce::String SampleBank::getCurrentPath() const
{
    const juce::ScopedLock sl(stateLock);
    return currentDirectory.getFullPathName();
}

juce::String SampleBank::getLastError() const
{
    const juce::ScopedLock sl(stateLock);
    return lastErrorMessage;
}

} // namespace icguitar
