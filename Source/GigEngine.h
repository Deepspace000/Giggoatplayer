// GigEngine.h
// The sampler core. It has no JUCE code in it, so it can be tested on its own.
//
// How it works, in plain words:
//  * A .gig file is opened with libgig.
//  * When you pick an instrument, the first part of every sample is copied
//    into RAM ("preload"). The rest stays on disk.
//  * When a note plays, the voice starts from the RAM copy. A disk thread
//    reads the rest of the sample in small pieces and fills a ring buffer,
//    so long samples play without filling your RAM.
//  * The audio thread never touches the disk and never waits for a lock.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gig { class File; class Sample; class Instrument; class Region; class DimensionRegion; }
namespace RIFF { class File; }

namespace gigeng
{

constexpr int kMaxVoices   = 128;
constexpr int kMaxStreams  = kMaxVoices + 32;
constexpr int kRingFrames  = 32768;          // must be a power of two
constexpr int kDiskChunk   = 4096;           // frames read from disk at a time
constexpr int kMaxZonesPerNote = 64;

// ---------------------------------------------------------------------------
// Data that is built when an instrument is loaded. Read-only while playing.
// ---------------------------------------------------------------------------

struct SampleData
{
    gig::Sample* sample = nullptr;
    int      bitDepth   = 16;
    int      channels   = 1;
    double   rate       = 44100.0;
    int64_t  totalFrames = 0;
    int64_t  preloadFrames = 0;           // frames held in RAM
    std::vector<float> preload;           // always stereo, interleaved
    bool     usable = true;
};

struct Zone                               // one gig "dimension region" that has a sample
{
    gig::DimensionRegion* dimRgn = nullptr;
    SampleData* sd = nullptr;

    float   gain = 1.0f;                  // sample attenuation (linear)
    float   pan  = 0.0f;                  // -1 .. +1
    int     channelRole = 0;              // 0 = normal, 1 = left only, 2 = right only
    int     unityNote = 60;
    double  fineTuneCents = 0.0;
    bool    pitchTrack = true;
    int64_t startOffset = 0;

    bool    loopEnabled = false;
    int64_t loopStart = 0, loopEnd = 0, loopLen = 0;

    // amplitude envelope (EG1)
    float   preAttack = 0.0f;             // 0..1
    float   attack = 0.0f, decay1 = 0.0f, decay2 = 0.0f, release = 0.05f;   // seconds
    float   sustain = 1.0f;               // 0..1
    bool    infiniteSustain = true;
    bool    hold = false;

    // crossfade between layers, driven by a controller
    bool    hasCrossfade = false;
    int     xfadeSource = 0;              // 0 velocity, 1 control change, 2 channel pressure
    int     xfadeCC = 0;
    bool    xfadeInvert = false;
    int     xin0 = 0, xin1 = 0, xout0 = 127, xout1 = 127;

    bool    isRelease = false;            // plays when the key is released
    int     releaseDecay = 0;             // 0..8, see README
    int64_t ramLimit = 0;                 // see GigEngine.cpp
    bool    needsStream = false;
};

struct DimInfo
{
    int type = 0;                         // gig::dimension_t value
    int zones = 1;
    bool isBit = false;
};

struct RegionData
{
    gig::Region* region = nullptr;
    int keyLow = 0, keyHigh = 127;
    int nDims = 0;
    DimInfo dims[8];
    Zone* zones[256] = {};                // indexed like gig::Region::pDimensionRegions
    bool hasRelease = false;
    int  rrCounter = 0;
};

struct Library;

struct InstrumentData
{
    std::shared_ptr<Library> lib;           // keeps the file open while this is alive
    std::string name;
    gig::Instrument* instrument = nullptr;
    std::deque<SampleData> samples;
    std::deque<Zone>       zones;
    std::deque<RegionData> regions;
    RegionData* keyTable[128] = {};
    int  keyswitchLow = 0, keyswitchHigh = -1;     // -1 means "none"
    int  pitchBendRange = 2;
    int  rrKeyboardCounter = 0;
    int64_t ramBytes = 0;
};

struct Library
{
    RIFF::File* riff = nullptr;
    gig::File*  gig  = nullptr;
    std::string path;
    std::vector<std::string> instrumentNames;
    ~Library();
};

// ---------------------------------------------------------------------------
// Streaming slot: one per voice that needs to read from disk.
// State changes: Free -> Active (audio thread) -> Releasing (audio thread)
//                -> Free (disk thread).
// ---------------------------------------------------------------------------
struct StreamSlot
{
    std::atomic<int>     state { 0 };       // 0 Free, 1 Active, 2 Releasing
    const Zone*          zone = nullptr;
    int64_t              startU = 0;        // first unrolled frame held by the ring
    std::atomic<int64_t> writeU { 0 };      // frames [startU, writeU) are valid
    std::atomic<int64_t> consumeU { 0 };    // frames below this are no longer needed
    std::atomic<bool>    eof { false };
    std::vector<float>   ring;              // kRingFrames * 2
};

struct Voice
{
    bool    active = false;
    bool    keyHeld = false;                // true until the key is released
    bool    sustainHeld = false;            // released while the pedal was down
    bool    releasing = false;
    bool    isReleaseSample = false;
    int     key = 0;
    int     velocity = 0;
    uint64_t age = 0;

    const Zone* zone = nullptr;
    int     slot = -1;

    double  pos = 0.0;                      // unrolled position, in source frames
    double  baseStep = 1.0;                 // source frames per output frame (without pitch bend)
    float   gainL = 1.0f, gainR = 1.0f;     // static gain and pan
    float   xfade = 1.0f;                   // smoothed crossfade gain
    float   releaseTriggerGain = 1.0f;

    // envelope
    int     stage = 0;                      // 0 attack, 1 decay1, 2 decay2, 3 sustain, 4 release
    float   env = 0.0f;
    float   attackInc = 0.0f;
    float   decay1Coef = 1.0f, decay2Coef = 1.0f, releaseCoef = 0.99f;
    float   sustainLevel = 1.0f;
    bool    holdWait = false;
    bool    overrideEnv = false;            // the part's own attack/decay/sustain is in charge
};

// ---------------------------------------------------------------------------
// The engine
// ---------------------------------------------------------------------------
class GigEngine
{
public:
    GigEngine();
    ~GigEngine();

    // ---- called from the UI (message) thread ----
    // Opens a file in the background. Check status() for progress.
    void loadFile (const std::string& path, int instrumentToSelect = 0, int preloadFrames = 32768);
    void selectInstrument (int index, int preloadFrames);
    void cleanupRetired();                 // call now and then from the UI thread

    struct Status
    {
        bool  loading = false;
        float progress = 0.0f;
        std::string message;
        std::string path;
        std::vector<std::string> instruments;
        int   currentInstrument = -1;
        int64_t ramBytes = 0;
        int   activeVoices = 0;
        int   underruns = 0;
        bool  hasInstrument = false;
    };
    Status getStatus();

    // ---- called from the audio thread ----
    void prepare (double sampleRate, int maxBlock);
    void render (float* left, float* right, int numSamples);   // adds to the buffers
    void noteOn (int key, int velocity);
    void noteOff (int key);
    void controller (int cc, int value);
    void pitchBend (int value14);          // 0..16383, 8192 = centre
    void channelPressure (int value);
    void allNotesOff();

    // ---- parameters, safe to set from any thread ----
    std::atomic<float> masterGain { 1.0f };     // linear
    std::atomic<float> tuneCents  { 0.0f };
    std::atomic<float> attackExtra { 0.0f };    // seconds added to the gig attack
    std::atomic<float> releaseScale { 1.0f };   // multiplies the gig release time
    // a part's own ADSR. 0 (sustain 1) = leave the instrument's own envelope alone
    std::atomic<float> userAttack { 0.0f }, userDecay { 0.0f }, userSustain { 1.0f }, userRelease { 0.0f };

private:
    // loading
    struct LoadJob { std::string path; int instrument; int preload; bool newFile; };
    void loaderThreadMain();
    void diskThreadMain();
    bool openLibrary (const std::string& path, std::string& error);
    std::unique_ptr<InstrumentData> buildInstrument (Library& lib, int index, int preloadFrames, std::string& error);
    void publishInstrument (std::unique_ptr<InstrumentData> inst);
    bool loadPreload (SampleData& sd, int64_t preloadFrames, void* decompBuffer);

    // audio helpers
    void handleSwap();
    void startZone (RegionData& rd, Zone& z, int key, int vel, float releaseHeldSeconds);
    void startNoteZones (RegionData& rd, int key, int vel, bool release, float heldSeconds);
    Voice* allocVoice();
    void killVoice (Voice& v);
    void releaseVoice (Voice& v);
    void renderVoice (Voice& v, float* L, float* R, int n);
    void updateEnvelopeCoefs (Voice& v);
    float crossfadeGain (const Zone& z, int value) const;
    int  ccForDimension (int type) const;

    // threads and shared state
    std::thread loaderThread, diskThread;
    std::atomic<bool> quitting { false };
    std::mutex jobMutex;
    std::vector<LoadJob> jobs;
    std::atomic<bool> hasJob { false };
    std::mutex diskMutex;                   // guards every libgig read

    std::shared_ptr<Library> library;       // message/loader threads only
    std::mutex libMutex;

    std::atomic<InstrumentData*> pending { nullptr };
    std::atomic<InstrumentData*> current { nullptr };
    std::atomic<InstrumentData*> retiredSlots[4] = {};       // old data waiting to be freed
    std::mutex ownedMutex;
    std::vector<std::unique_ptr<InstrumentData>> owned;     // loader/message thread
    std::vector<std::unique_ptr<InstrumentData>> unusedPending;
    std::atomic<bool> ringsReady { false };

    // status
    std::mutex statusMutex;
    Status status;
    std::atomic<bool> loadingFlag { false };
    std::atomic<float> progress { 0.0f };
    std::atomic<int> underrunCount { 0 };
    std::atomic<int> voiceCount { 0 };

    // audio-thread state
    double sampleRate = 44100.0;
    std::vector<float> mixL, mixR;
    double bendFactor = 1.0;
    float lastMaster = 0.0f;
    Voice voices[kMaxVoices];
    StreamSlot slots[kMaxStreams];
    uint64_t ageCounter = 0;
    uint8_t cc[128] = {};
    int  channelPressureValue = 0;
    float bendSemitones = 0.0f;
    int  keyswitchZone = 0;
    bool keyDown[128] = {};
    int  keyVelocity[128] = {};
    uint64_t keyDownTime[128] = {};
    uint64_t sampleClock = 0;
    uint32_t randState = 12345u;
    bool sustainPedal = false;
};

} // namespace gigeng
