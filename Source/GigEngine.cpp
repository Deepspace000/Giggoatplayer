// GigEngine.cpp
#include "GigEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "gig.h"

namespace gigeng
{

using namespace std::chrono_literals;

static constexpr int64_t kInfinite = INT64_MAX / 4;

// ---------------------------------------------------------------------------
Library::~Library()
{
    delete gig;
    delete riff;
}

// ---------------------------------------------------------------------------
GigEngine::GigEngine()
{
    for (auto& v : voices) v = Voice();
    cc[7] = 127;
    mixL.assign (512, 0.0f);
    mixR.assign (512, 0.0f);
    loaderThread = std::thread ([this] { loaderThreadMain(); });
    diskThread   = std::thread ([this] { diskThreadMain(); });
}

GigEngine::~GigEngine()
{
    quitting = true;
    if (loaderThread.joinable()) loaderThread.join();
    if (diskThread.joinable())   diskThread.join();

    // Nothing is playing any more, so it is safe to free everything.
    std::lock_guard<std::mutex> g (diskMutex);
    owned.clear();
}

// ---------------------------------------------------------------------------
//  UI thread API
// ---------------------------------------------------------------------------
void GigEngine::loadFile (const std::string& path, int instrumentToSelect, int preloadFrames)
{
    std::lock_guard<std::mutex> g (jobMutex);
    jobs.clear();
    jobs.push_back ({ path, instrumentToSelect, preloadFrames, true });
    hasJob = true;
    loadingFlag = true;
    progress = 0.0f;
}

void GigEngine::selectInstrument (int index, int preloadFrames)
{
    std::lock_guard<std::mutex> g (jobMutex);
    jobs.clear();
    jobs.push_back ({ "", index, preloadFrames, false });
    hasJob = true;
    loadingFlag = true;
    progress = 0.0f;
}

void GigEngine::cleanupRetired()
{
    for (auto& r : retiredSlots)
    {
        InstrumentData* p = r.exchange (nullptr, std::memory_order_acquire);
        if (p == nullptr) continue;

        // Take the disk lock so that no read is running while we free things.
        std::lock_guard<std::mutex> g (diskMutex);
        std::lock_guard<std::mutex> g2 (ownedMutex);
        owned.erase (std::remove_if (owned.begin(), owned.end(),
                                     [p] (const std::unique_ptr<InstrumentData>& o) { return o.get() == p; }),
                     owned.end());
    }

    std::lock_guard<std::mutex> g (ownedMutex);
    if (! unusedPending.empty())
    {
        std::lock_guard<std::mutex> g2 (diskMutex);
        unusedPending.clear();
    }
}

GigEngine::Status GigEngine::getStatus()
{
    Status s;
    {
        std::lock_guard<std::mutex> g (statusMutex);
        s = status;
    }
    s.loading = loadingFlag.load();
    s.progress = progress.load();
    s.activeVoices = voiceCount.load();
    s.underruns = underrunCount.load();
    return s;
}

// ---------------------------------------------------------------------------
//  Loader thread
// ---------------------------------------------------------------------------
static void setMessage (std::mutex& m, GigEngine::Status& st, const std::string& text)
{
    std::lock_guard<std::mutex> g (m);
    st.message = text;
}

bool GigEngine::openLibrary (const std::string& path, std::string& error)
{
    auto lib = std::make_shared<Library>();
    try
    {
        std::lock_guard<std::mutex> g (diskMutex);
        lib->riff = new RIFF::File (path);
        lib->gig  = new gig::File (lib->riff);
        lib->path = path;

        const size_t count = lib->gig->CountInstruments();
        for (size_t i = 0; i < count; ++i)
        {
            gig::Instrument* ins = lib->gig->GetInstrument ((unsigned int) i);
            std::string name = (ins != nullptr && ins->pInfo != nullptr) ? ins->pInfo->Name : "";
            if (name.empty()) name = "Instrument " + std::to_string (i + 1);
            lib->instrumentNames.push_back (name);
            progress = 0.2f * float (i + 1) / float (count);
            if (quitting) return false;
        }
    }
    catch (RIFF::Exception& e)
    {
        error = e.Message;
        return false;
    }
    catch (std::exception& e)
    {
        error = e.what();
        return false;
    }
    catch (...)
    {
        error = "Unknown error while opening the file";
        return false;
    }

    if (lib->instrumentNames.empty())
    {
        error = "This file has no instruments in it";
        return false;
    }

    {
        std::lock_guard<std::mutex> g (libMutex);
        library = lib;
    }
    {
        std::lock_guard<std::mutex> g (statusMutex);
        status.path = path;
        status.instruments = lib->instrumentNames;
    }
    return true;
}

void GigEngine::loaderThreadMain()
{
    while (! quitting)
    {
        if (! hasJob.load())
        {
            std::this_thread::sleep_for (20ms);
            continue;
        }

        LoadJob job;
        {
            std::lock_guard<std::mutex> g (jobMutex);
            if (jobs.empty()) { hasJob = false; continue; }
            job = jobs.back();
            jobs.clear();
            hasJob = false;
        }

        std::string error;
        bool ok = true;

        if (job.newFile)
        {
            setMessage (statusMutex, status, "Opening file...");
            ok = openLibrary (job.path, error);
        }

        std::shared_ptr<Library> lib;
        {
            std::lock_guard<std::mutex> g (libMutex);
            lib = library;
        }
        if (ok && ! lib) { ok = false; error = "No file is open"; }

        if (ok)
        {
            const int index = std::max (0, std::min (job.instrument, (int) lib->instrumentNames.size() - 1));
            setMessage (statusMutex, status, "Loading " + lib->instrumentNames[(size_t) index] + "...");

            std::unique_ptr<InstrumentData> inst;
            try
            {
                inst = buildInstrument (*lib, index, job.preload, error);
            }
            catch (RIFF::Exception& e) { error = e.Message; }
            catch (std::exception& e)  { error = e.what(); }
            catch (...)                { error = "Unknown error while loading the instrument"; }

            if (inst)
            {
                inst->lib = lib;
                const int64_t ram = inst->ramBytes;
                const std::string nm = inst->name;
                publishInstrument (std::move (inst));
                {
                    std::lock_guard<std::mutex> g (statusMutex);
                    status.currentInstrument = index;
                    status.ramBytes = ram;
                    status.hasInstrument = true;
                    status.message = "Ready: " + nm;
                }
            }
            else
            {
                ok = false;
            }
        }

        if (! ok)
        {
            std::lock_guard<std::mutex> g (statusMutex);
            status.message = "Error: " + error;
        }

        progress = 1.0f;
        loadingFlag = hasJob.load();
    }
}

// Reads the first part of a sample into RAM. Caller holds no locks.
bool GigEngine::loadPreload (SampleData& sd, int64_t wanted, void* decompBuffer)
{
    const int64_t frames = std::min<int64_t> (wanted, sd.totalFrames);
    sd.preloadFrames = 0;
    sd.preload.assign ((size_t) frames * 2 + 8, 0.0f);

    const int bytesPerSample = sd.bitDepth == 24 ? 3 : 2;
    const int ch = sd.channels;
    std::vector<uint8_t> tmp ((size_t) kDiskChunk * ch * bytesPerSample + 16);

    int64_t done = 0;
    while (done < frames)
    {
        const int64_t n = std::min<int64_t> (kDiskChunk, frames - done);
        int64_t got = 0;
        {
            std::lock_guard<std::mutex> g (diskMutex);
            sd.sample->SetPos ((RIFF::file_offset_t) done);
            got = (int64_t) sd.sample->Read (tmp.data(), (RIFF::file_offset_t) n, (gig::buffer_t*) decompBuffer);
        }
        if (got <= 0) break;

        float* dst = sd.preload.data() + done * 2;
        if (sd.bitDepth == 16)
        {
            const int16_t* s = reinterpret_cast<const int16_t*> (tmp.data());
            for (int64_t i = 0; i < got; ++i)
            {
                const float l = s[i * ch] * (1.0f / 32768.0f);
                const float r = ch == 2 ? s[i * ch + 1] * (1.0f / 32768.0f) : l;
                dst[i * 2] = l;
                dst[i * 2 + 1] = r;
            }
        }
        else
        {
            const uint8_t* s = tmp.data();
            auto s24 = [] (const uint8_t* p) -> float
            {
                int32_t v = (int32_t) ((uint32_t) p[0] << 8 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 24);
                return float (v >> 8) * (1.0f / 8388608.0f);
            };
            for (int64_t i = 0; i < got; ++i)
            {
                const float l = s24 (s + (i * ch) * 3);
                const float r = ch == 2 ? s24 (s + (i * ch + 1) * 3) : l;
                dst[i * 2] = l;
                dst[i * 2 + 1] = r;
            }
        }
        done += got;
        if (quitting) return false;
    }

    sd.preloadFrames = done;
    return done > 0;
}

std::unique_ptr<InstrumentData> GigEngine::buildInstrument (Library& lib, int index, int preloadFrames, std::string& error)
{
    auto out = std::make_unique<InstrumentData>();
    out->name = lib.instrumentNames[(size_t) index];
    preloadFrames = std::max (2048, preloadFrames);

    gig::Instrument* ins = nullptr;
    {
        std::lock_guard<std::mutex> g (diskMutex);
        ins = lib.gig->GetInstrument ((unsigned int) index);
    }
    if (ins == nullptr) { error = "Could not read the instrument"; return nullptr; }
    out->instrument = ins;
    out->pitchBendRange = ins->PitchbendRange > 0 ? ins->PitchbendRange : 2;

    gig::buffer_t decomp = gig::Sample::CreateDecompressionBuffer (kDiskChunk + 64);

    // First pass: count regions so that we can show progress.
    std::vector<gig::Region*> regionList;
    {
        std::lock_guard<std::mutex> g (diskMutex);
        for (gig::Region* r = ins->GetFirstRegion(); r != nullptr; r = ins->GetNextRegion())
            regionList.push_back (r);
    }

    std::map<gig::Sample*, SampleData*> sampleMap;
    bool anyKeyboardDim = false;

    for (size_t ri = 0; ri < regionList.size(); ++ri)
    {
        if (quitting) { gig::Sample::DestroyDecompressionBuffer (decomp); return nullptr; }
        gig::Region* r = regionList[ri];

        out->regions.emplace_back();
        RegionData& rd = out->regions.back();
        rd.region = r;
        rd.keyLow = r->KeyRange.low;
        rd.keyHigh = r->KeyRange.high;
        rd.nDims = (int) std::min<unsigned> (r->Dimensions, 8u);

        int bitpos = 0;
        int dimBitpos[8] = {};
        for (int i = 0; i < rd.nDims; ++i)
        {
            rd.dims[i].type = (int) r->pDimensionDefinitions[i].dimension;
            rd.dims[i].zones = std::max<int> (1, r->pDimensionDefinitions[i].zones);
            rd.dims[i].isBit = r->pDimensionDefinitions[i].split_type == gig::split_type_bit;
            dimBitpos[i] = bitpos;
            bitpos += r->pDimensionDefinitions[i].bits;
            if (rd.dims[i].type == gig::dimension_keyboard) anyKeyboardDim = true;
        }

        for (int idx = 0; idx < 256; ++idx)
        {
            gig::DimensionRegion* dr = r->pDimensionRegions[idx];
            if (dr == nullptr || dr->pSample == nullptr) continue;

            // find or create the sample data
            SampleData* sd = nullptr;
            auto it = sampleMap.find (dr->pSample);
            if (it != sampleMap.end())
            {
                sd = it->second;
            }
            else
            {
                out->samples.emplace_back();
                sd = &out->samples.back();
                sampleMap[dr->pSample] = sd;
                gig::Sample* s = dr->pSample;
                sd->sample = s;
                sd->bitDepth = s->BitDepth;
                sd->channels = s->Channels;
                sd->rate = s->SamplesPerSecond;
                sd->totalFrames = (int64_t) s->SamplesTotal;
                if ((sd->bitDepth != 16 && sd->bitDepth != 24) || sd->channels < 1 || sd->channels > 2
                    || sd->totalFrames < 4)
                {
                    sd->usable = false;
                }
                else if (! loadPreload (*sd, preloadFrames, &decomp))
                {
                    sd->usable = false;
                }
                out->ramBytes += (int64_t) sd->preload.size() * 4;
            }
            if (! sd->usable) continue;

            out->zones.emplace_back();
            Zone& z = out->zones.back();
            z.dimRgn = dr;
            z.sd = sd;
            z.gain = (float) dr->SampleAttenuation;
            z.pan = std::max (-1.0f, std::min (1.0f, dr->Pan / 64.0f));
            z.unityNote = dr->UnityNote;
            z.fineTuneCents = dr->FineTune;
            z.pitchTrack = dr->PitchTrack;
            z.startOffset = std::min<int64_t> (dr->SampleStartOffset, sd->totalFrames - 4);

            // loop
            if (dr->SampleLoops > 0 && dr->pSampleLoops != nullptr && dr->pSampleLoops[0].LoopLength > 8)
            {
                const int64_t ls = dr->pSampleLoops[0].LoopStart;
                int64_t le = ls + (int64_t) dr->pSampleLoops[0].LoopLength;
                le = std::min<int64_t> (le, sd->totalFrames);
                if (le - ls > 8 && ls < sd->totalFrames)
                {
                    z.loopEnabled = true;
                    z.loopStart = ls;
                    z.loopEnd = le;
                    z.loopLen = le - ls;
                }
            }

            // envelope
            z.preAttack = std::max (0.0f, std::min (1.0f, dr->EG1PreAttack / 1000.0f));
            z.attack = (float) dr->EG1Attack;
            z.decay1 = (float) dr->EG1Decay1;
            z.decay2 = (float) dr->EG1Decay2;
            z.sustain = std::max (0.0f, std::min (1.0f, dr->EG1Sustain / 1000.0f));
            z.release = (float) dr->EG1Release;
            z.infiniteSustain = dr->EG1InfiniteSustain;
            z.hold = dr->EG1Hold;

            // crossfade
            const gig::crossfade_t& xf = dr->Crossfade;
            const bool xfDefined = xf.in_start || xf.in_end || xf.out_start || xf.out_end;
            if (xfDefined)
            {
                const auto t = dr->AttenuationController.type;
                if (t == gig::attenuation_ctrl_t::type_controlchange)
                {
                    z.hasCrossfade = true; z.xfadeSource = 1;
                    z.xfadeCC = (int) dr->AttenuationController.controller_number;
                }
                else if (t == gig::attenuation_ctrl_t::type_velocity)
                {
                    z.hasCrossfade = true; z.xfadeSource = 0;
                }
                else if (t == gig::attenuation_ctrl_t::type_channelaftertouch)
                {
                    z.hasCrossfade = true; z.xfadeSource = 2;
                }
                z.xfadeInvert = dr->InvertAttenuationController;
                z.xin0 = xf.in_start; z.xin1 = xf.in_end; z.xout0 = xf.out_start; z.xout1 = xf.out_end;
            }

            // what role does this dimension region have?
            for (int d = 0; d < rd.nDims; ++d)
            {
                const int bits = r->pDimensionDefinitions[d].bits;
                const int zoneNo = (idx >> dimBitpos[d]) & ((1 << bits) - 1);
                if (rd.dims[d].type == gig::dimension_releasetrigger && zoneNo == 1)
                {
                    z.isRelease = true;
                    rd.hasRelease = true;
                }
                if (rd.dims[d].type == gig::dimension_samplechannel && sd->channels == 1)
                    z.channelRole = zoneNo == 0 ? 1 : 2;
            }
            if (z.isRelease)
            {
                z.loopEnabled = false;
                z.releaseDecay = dr->ReleaseTriggerDecay;
            }

            // RAM or disk?
            if (sd->preloadFrames >= sd->totalFrames || (z.loopEnabled && z.loopEnd <= sd->preloadFrames))
            {
                z.ramLimit = kInfinite;
                z.needsStream = false;
            }
            else
            {
                z.ramLimit = sd->preloadFrames;
                z.needsStream = true;
            }

            rd.zones[idx] = &z;
        }

        for (int k = rd.keyLow; k <= rd.keyHigh && k < 128; ++k)
            if (k >= 0) out->keyTable[k] = &rd;

        progress = 0.2f + 0.8f * float (ri + 1) / float (regionList.size());
    }

    gig::Sample::DestroyDecompressionBuffer (decomp);

    if (anyKeyboardDim)
    {
        out->keyswitchLow = ins->DimensionKeyRange.low;
        out->keyswitchHigh = ins->DimensionKeyRange.high;
    }

    bool anyKey = false;
    for (auto* p : out->keyTable) if (p) { anyKey = true; break; }
    if (! anyKey) { error = "This instrument has no playable notes"; return nullptr; }

    return out;
}

void GigEngine::publishInstrument (std::unique_ptr<InstrumentData> inst)
{
    // Make sure the ring buffers exist before the audio thread can use them.
    if (! ringsReady.load())
    {
        for (auto& s : slots) s.ring.assign ((size_t) kRingFrames * 2, 0.0f);
        ringsReady = true;
    }

    InstrumentData* raw = inst.get();
    {
        std::lock_guard<std::mutex> g (ownedMutex);
        owned.push_back (std::move (inst));
    }
    InstrumentData* old = pending.exchange (raw, std::memory_order_acq_rel);
    if (old != nullptr)
    {
        // The audio thread never saw this one, so it can go straight away.
        std::lock_guard<std::mutex> g (ownedMutex);
        auto it = std::find_if (owned.begin(), owned.end(),
                                [old] (const std::unique_ptr<InstrumentData>& o) { return o.get() == old; });
        if (it != owned.end())
        {
            unusedPending.push_back (std::move (*it));
            owned.erase (it);
        }
    }
}

// ---------------------------------------------------------------------------
//  Disk thread
// ---------------------------------------------------------------------------
void GigEngine::diskThreadMain()
{
    std::vector<uint8_t> tmp ((size_t) kDiskChunk * 2 * 3 + 64);
    gig::buffer_t decomp = gig::Sample::CreateDecompressionBuffer (kDiskChunk + 64);

    while (! quitting)
    {
        bool didWork = false;

        if (ringsReady.load())
        {
            for (auto& s : slots)
                if (s.state.load (std::memory_order_acquire) == 2)
                    s.state.store (0, std::memory_order_release);

            // pick the active stream that has the least data buffered
            int best = -1;
            int64_t bestFill = INT64_MAX;
            for (int i = 0; i < kMaxStreams; ++i)
            {
                StreamSlot& s = slots[i];
                if (s.state.load (std::memory_order_acquire) != 1) continue;
                if (s.eof.load (std::memory_order_acquire)) continue;
                const int64_t w = s.writeU.load (std::memory_order_acquire);
                const int64_t c = s.consumeU.load (std::memory_order_acquire);
                const int64_t space = c + kRingFrames - w;
                if (space < 1024) continue;
                const int64_t fill = w - c;
                if (fill < bestFill) { bestFill = fill; best = i; }
            }

            if (best >= 0)
            {
                StreamSlot& s = slots[best];
                std::lock_guard<std::mutex> g (diskMutex);
                // check again: the instrument may have been swapped while we waited
                if (s.state.load (std::memory_order_acquire) == 1)
                {
                    const Zone& z = *s.zone;
                    const SampleData& sd = *z.sd;
                    const int64_t w0 = s.writeU.load (std::memory_order_acquire);
                    const int64_t c = s.consumeU.load (std::memory_order_acquire);
                    int64_t want = std::min<int64_t> ({ (int64_t) kDiskChunk, c + kRingFrames - w0 });
                    int64_t done = 0;
                    bool hitEnd = false;

                    const int ch = sd.channels;
                    const int bps = sd.bitDepth == 24 ? 3 : 2;

                    while (done < want)
                    {
                        const int64_t u = w0 + done;
                        int64_t src = u;
                        int64_t segEnd = sd.totalFrames;
                        if (z.loopEnabled)
                        {
                            if (u >= z.loopEnd) src = z.loopStart + (u - z.loopStart) % z.loopLen;
                            segEnd = z.loopEnd;
                        }
                        const int64_t seg = std::min<int64_t> (want - done, segEnd - src);
                        if (seg <= 0) { hitEnd = true; break; }

                        sd.sample->SetPos ((RIFF::file_offset_t) src);
                        const int64_t got = (int64_t) sd.sample->Read (tmp.data(), (RIFF::file_offset_t) seg, &decomp);
                        if (got <= 0) { hitEnd = true; break; }

                        float* ring = s.ring.data();
                        if (sd.bitDepth == 16)
                        {
                            const int16_t* p = reinterpret_cast<const int16_t*> (tmp.data());
                            for (int64_t k = 0; k < got; ++k)
                            {
                                const float l = p[k * ch] * (1.0f / 32768.0f);
                                const float r = ch == 2 ? p[k * ch + 1] * (1.0f / 32768.0f) : l;
                                const size_t at = (size_t) ((u + k) & (kRingFrames - 1)) * 2;
                                ring[at] = l; ring[at + 1] = r;
                            }
                        }
                        else
                        {
                            auto s24 = [] (const uint8_t* q) -> float
                            {
                                int32_t v = (int32_t) ((uint32_t) q[0] << 8 | (uint32_t) q[1] << 16 | (uint32_t) q[2] << 24);
                                return float (v >> 8) * (1.0f / 8388608.0f);
                            };
                            for (int64_t k = 0; k < got; ++k)
                            {
                                const uint8_t* q = tmp.data() + (k * ch) * bps;
                                const float l = s24 (q);
                                const float r = ch == 2 ? s24 (q + bps) : l;
                                const size_t at = (size_t) ((u + k) & (kRingFrames - 1)) * 2;
                                ring[at] = l; ring[at + 1] = r;
                            }
                        }
                        done += got;
                        if (got < seg) { hitEnd = true; break; }
                    }

                    s.writeU.store (w0 + done, std::memory_order_release);
                    if (hitEnd) s.eof.store (true, std::memory_order_release);
                    didWork = true;
                }
            }
        }

        if (! didWork) std::this_thread::sleep_for (2ms);
    }

    gig::Sample::DestroyDecompressionBuffer (decomp);
}

// ---------------------------------------------------------------------------
//  Audio thread
// ---------------------------------------------------------------------------
void GigEngine::prepare (double sr, int maxBlock)
{
    // (never called while audio is running) stop anything that was playing at the old rate
    for (auto& v : voices)
        if (v.active) killVoice (v);
    sampleRate = sr > 0 ? sr : 44100.0;
    mixL.assign ((size_t) std::max (64, maxBlock), 0.0f);
    mixR.assign ((size_t) std::max (64, maxBlock), 0.0f);
}

int GigEngine::ccForDimension (int type) const
{
    switch (type)
    {
        case 0x01: return 1;   case 0x02: return 2;   case 0x04: return 4;   case 0x05: return 5;
        case 0x0c: return 12;  case 0x0d: return 13;
        case 0x10: return 16;  case 0x11: return 17;  case 0x12: return 18;  case 0x13: return 19;
        case 0x30: return 80;  case 0x31: return 81;  case 0x32: return 82;  case 0x33: return 83;
        case 0x40: return 64;  case 0x41: return 65;  case 0x42: return 66;  case 0x43: return 67;
        case 0x5b: return 91;  case 0x5c: return 92;  case 0x5d: return 93;  case 0x5e: return 94;
        case 0x5f: return 95;
        default: return -1;
    }
}

void GigEngine::handleSwap()
{
    InstrumentData* p = pending.exchange (nullptr, std::memory_order_acq_rel);
    if (p == nullptr) return;

    for (auto& v : voices)
        if (v.active) killVoice (v);

    InstrumentData* old = current.exchange (p, std::memory_order_acq_rel);
    keyswitchZone = 0;
    bendSemitones = 0.0f;
    if (old != nullptr)
    {
        for (auto& r : retiredSlots)
        {
            InstrumentData* expected = nullptr;
            if (r.compare_exchange_strong (expected, old, std::memory_order_acq_rel)) { old = nullptr; break; }
        }
        // (if every slot is full the old data is simply kept until exit)
    }
}

void GigEngine::killVoice (Voice& v)
{
    if (v.slot >= 0)
    {
        slots[v.slot].state.store (2, std::memory_order_release);
        v.slot = -1;
    }
    v.active = false;
}

Voice* GigEngine::allocVoice()
{
    for (auto& v : voices)
        if (! v.active) return &v;

    // steal: quietest releasing voice first, otherwise the oldest
    Voice* best = nullptr;
    for (auto& v : voices)
        if (v.releasing && (best == nullptr || v.env < best->env)) best = &v;
    if (best == nullptr)
        for (auto& v : voices)
            if (best == nullptr || v.age < best->age) best = &v;

    killVoice (*best);
    return best;
}

float GigEngine::crossfadeGain (const Zone& z, int value) const
{
    if (z.xfadeInvert) value = 127 - value;
    const float x = (float) value;
    float g = 1.0f;
    const float pi2 = 1.57079632679f;
    if (x < z.xin0) g = 0.0f;
    else if (x < z.xin1) g = std::sin (pi2 * (x - z.xin0) / std::max (1.0f, (float) (z.xin1 - z.xin0)));
    else if (x <= z.xout0) g = 1.0f;
    else if (x < z.xout1) g = std::cos (pi2 * (x - z.xout0) / std::max (1.0f, (float) (z.xout1 - z.xout0)));
    else g = (z.xout1 >= 127 && x <= 127 && z.xout0 >= 127) ? 1.0f : 0.0f;
    return g;
}

void GigEngine::updateEnvelopeCoefs (Voice& v)
{
    const Zone& z = *v.zone;
    const double sr = sampleRate;

    float relTime = std::max (0.004f, z.release * releaseScale.load());
    v.releaseCoef = (float) std::pow (0.001, 1.0 / (relTime * sr));

    v.sustainLevel = z.sustain;
    const float d1 = std::max (0.001f, z.decay1);
    v.decay1Coef = (float) std::pow (std::max (0.001f, z.sustain), 1.0 / (d1 * sr));

    const float d2 = std::max (0.01f, z.decay2);
    v.decay2Coef = (float) std::pow (0.001, 1.0 / (d2 * sr));
}

void GigEngine::startZone (RegionData& rd, Zone& z, int key, int vel, float heldSeconds)
{
    InstrumentData* inst = current.load (std::memory_order_acquire);
    if (inst == nullptr) return;
    (void) rd;

    // streams: find a free slot first, so we do not start a voice we cannot feed
    int slotIndex = -1;
    if (z.needsStream)
    {
        if (! ringsReady.load()) return;
        for (int i = 0; i < kMaxStreams; ++i)
            if (slots[i].state.load (std::memory_order_acquire) == 0) { slotIndex = i; break; }
        // no free slot: play from RAM only (the note will be short)
    }

    Voice* v = allocVoice();
    if (v == nullptr) return;
    *v = Voice();
    v->active = true;
    v->keyHeld = true;
    v->isReleaseSample = z.isRelease;
    v->key = key;
    v->velocity = vel;
    v->age = ++ageCounter;
    v->zone = &z;

    // pitch
    double semis = z.pitchTrack ? (double) (key - z.unityNote) : 0.0;
    semis += z.fineTuneCents / 100.0;
    v->baseStep = (z.sd->rate / sampleRate) * std::pow (2.0, semis / 12.0);

    // volume and pan
    double velAtt = 1.0;
    {
        const int velIdx = std::max (0, std::min (127, vel));
        velAtt = z.dimRgn->GetVelocityAttenuation ((uint8_t) velIdx);
    }
    float g = z.gain * (float) velAtt;
    if (z.isRelease && z.releaseDecay > 0)
    {
        const float db = std::min (40.0f, 1.5f * (float) z.releaseDecay * heldSeconds);
        g *= std::pow (10.0f, -db / 20.0f);
    }
    if (z.channelRole == 1)      { v->gainL = g; v->gainR = 0.0f; }
    else if (z.channelRole == 2) { v->gainL = 0.0f; v->gainR = g; }
    else if (z.sd->channels == 1)
    {
        const float a = (z.pan + 1.0f) * 0.78539816339f;       // 0 .. pi/2
        v->gainL = g * std::cos (a) * 1.41421356f;
        v->gainR = g * std::sin (a) * 1.41421356f;
    }
    else
    {
        v->gainL = g * (z.pan > 0 ? 1.0f - z.pan : 1.0f);
        v->gainR = g * (z.pan < 0 ? 1.0f + z.pan : 1.0f);
    }

    // crossfade starting value
    v->xfade = 1.0f;
    if (z.hasCrossfade)
    {
        int val = 0;
        if (z.xfadeSource == 0) val = vel;
        else if (z.xfadeSource == 1) val = cc[z.xfadeCC & 127];
        else val = channelPressureValue;
        v->xfade = crossfadeGain (z, val);
    }

    // envelope
    updateEnvelopeCoefs (*v);
    const float attackTime = std::max (0.0015f, z.attack + attackExtra.load());
    v->attackInc = (1.0f - z.preAttack) / (attackTime * (float) sampleRate);
    v->env = z.isRelease ? 1.0f : z.preAttack;
    if (z.isRelease) { v->stage = z.infiniteSustain || z.sustain >= 0.999f ? 3 : 1; v->env = 1.0f; }
    else v->stage = 0;
    v->pos = (double) z.startOffset;

    // streaming
    if (slotIndex >= 0)
    {
        StreamSlot& s = slots[slotIndex];
        s.zone = &z;
        s.startU = std::max<int64_t> (0, z.ramLimit - 4);
        s.writeU.store (s.startU, std::memory_order_relaxed);
        s.consumeU.store (s.startU, std::memory_order_relaxed);
        s.eof.store (false, std::memory_order_relaxed);
        s.state.store (1, std::memory_order_release);
        v->slot = slotIndex;
    }
}

void GigEngine::startNoteZones (RegionData& rd, int key, int vel, bool release, float heldSeconds)
{
    InstrumentData* inst = current.load (std::memory_order_acquire);
    if (inst == nullptr) return;

    unsigned dimValues[8] = {};
    int iterDim[8];
    int nIter = 0;

    for (int i = 0; i < rd.nDims; ++i)
    {
        const int type = rd.dims[i].type;
        const int zones = rd.dims[i].zones;
        switch (type)
        {
            case gig::dimension_none: break;
            case gig::dimension_velocity: dimValues[i] = (unsigned) vel; break;
            case gig::dimension_layer:
            case gig::dimension_samplechannel: iterDim[nIter++] = i; break;
            case gig::dimension_releasetrigger: dimValues[i] = release ? 1u : 0u; break;
            case gig::dimension_keyboard: dimValues[i] = (unsigned) std::min (keyswitchZone, zones - 1); break;
            case gig::dimension_roundrobin: dimValues[i] = (unsigned) (rd.rrCounter % zones); break;
            case gig::dimension_roundrobinkeyboard: dimValues[i] = (unsigned) (inst->rrKeyboardCounter % zones); break;
            case gig::dimension_random:
                randState = randState * 1664525u + 1013904223u;
                dimValues[i] = (randState >> 16) % (unsigned) zones;
                break;
            case gig::dimension_smartmidi: dimValues[i] = 0; break;
            case gig::dimension_channelaftertouch: dimValues[i] = (unsigned) channelPressureValue; break;
            default:
            {
                const int c = ccForDimension (type);
                dimValues[i] = c >= 0 ? cc[c] : 0u;
                break;
            }
        }
    }

    // walk through every combination of the "play all of them" dimensions
    int total = 1;
    for (int k = 0; k < nIter; ++k) total *= rd.dims[iterDim[k]].zones;
    total = std::min (total, kMaxZonesPerNote);

    for (int combo = 0; combo < total; ++combo)
    {
        int rest = combo;
        for (int k = 0; k < nIter; ++k)
        {
            const int zc = rd.dims[iterDim[k]].zones;
            dimValues[iterDim[k]] = (unsigned) (rest % zc);
            rest /= zc;
        }
        const int idx = rd.region->GetDimensionRegionIndexByValue (dimValues);
        if (idx < 0 || idx > 255) continue;
        Zone* z = rd.zones[idx];
        if (z == nullptr) continue;
        if (z->isRelease != release) continue;
        startZone (rd, *z, key, vel, heldSeconds);
    }
}

void GigEngine::noteOn (int key, int velocity)
{
    if (key < 0 || key > 127) return;
    if (velocity <= 0) { noteOff (key); return; }
    handleSwap();
    InstrumentData* inst = current.load (std::memory_order_acquire);
    if (inst == nullptr) return;

    if (inst->keyswitchHigh >= inst->keyswitchLow && inst->keyswitchHigh >= 0
        && key >= inst->keyswitchLow && key <= inst->keyswitchHigh)
    {
        keyswitchZone = key - inst->keyswitchLow;
        return;
    }

    keyDown[key] = true;
    keyVelocity[key] = velocity;
    keyDownTime[key] = sampleClock;

    RegionData* rd = inst->keyTable[key];
    if (rd == nullptr) return;
    startNoteZones (*rd, key, velocity, false, 0.0f);
    ++rd->rrCounter;
    ++inst->rrKeyboardCounter;
}

void GigEngine::releaseVoice (Voice& v)
{
    if (v.releasing) return;
    v.releasing = true;
    v.stage = 4;
    v.keyHeld = false;
    v.sustainHeld = false;
    updateEnvelopeCoefs (v);
}

void GigEngine::noteOff (int key)
{
    if (key < 0 || key > 127) return;
    handleSwap();
    InstrumentData* inst = current.load (std::memory_order_acquire);
    if (inst == nullptr) return;

    const bool wasDown = keyDown[key];
    keyDown[key] = false;

    for (auto& v : voices)
    {
        if (! v.active || v.key != key || ! v.keyHeld || v.isReleaseSample) continue;
        if (sustainPedal) { v.keyHeld = false; v.sustainHeld = true; }
        else releaseVoice (v);
    }

    if (wasDown)
    {
        RegionData* rd = inst->keyTable[key];
        if (rd != nullptr && rd->hasRelease)
        {
            const float held = (float) ((double) (sampleClock - keyDownTime[key]) / sampleRate);
            startNoteZones (*rd, key, keyVelocity[key], true, held);
        }
    }
}

void GigEngine::controller (int number, int value)
{
    if (number < 0 || number > 127) return;
    cc[number] = (uint8_t) std::max (0, std::min (127, value));

    if (number == 64)
    {
        const bool down = value >= 64;
        if (sustainPedal && ! down)
            for (auto& v : voices)
                if (v.active && v.sustainHeld) releaseVoice (v);
        sustainPedal = down;
    }
    else if (number == 120)
    {
        for (auto& v : voices) if (v.active) killVoice (v);
    }
    else if (number == 123)
    {
        allNotesOff();
    }
}

void GigEngine::pitchBend (int value14)
{
    InstrumentData* inst = current.load (std::memory_order_acquire);
    const int range = inst != nullptr ? inst->pitchBendRange : 2;
    bendSemitones = float (value14 - 8192) / 8192.0f * (float) range;
}

void GigEngine::channelPressure (int value)
{
    channelPressureValue = std::max (0, std::min (127, value));
}

void GigEngine::allNotesOff()
{
    for (auto& v : voices)
        if (v.active && ! v.isReleaseSample) releaseVoice (v);
    for (auto& k : keyDown) k = false;
}

// ---------------------------------------------------------------------------
//  Rendering
// ---------------------------------------------------------------------------
static inline float hermite (float x, float y0, float y1, float y2, float y3)
{
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * x + c2) * x + c1) * x + y1;
}

void GigEngine::renderVoice (Voice& v, float* L, float* R, int n)
{
    const Zone& z = *v.zone;
    const SampleData& sd = *z.sd;
    StreamSlot* slot = v.slot >= 0 ? &slots[v.slot] : nullptr;
    const float* ring = slot != nullptr ? slot->ring.data() : nullptr;

    const double step = v.baseStep * bendFactor;
    const int64_t total = sd.totalFrames;
    const float* pre = sd.preload.data();
    const int64_t preFrames = sd.preloadFrames;

    // crossfade ramp over this block
    float xf0 = v.xfade, xf1 = v.xfade;
    if (z.hasCrossfade && z.xfadeSource != 0)
        xf1 = crossfadeGain (z, z.xfadeSource == 1 ? cc[z.xfadeCC & 127] : channelPressureValue);
    const float xfStep = (xf1 - xf0) / (float) n;
    float xf = xf0;

    int64_t ringEnd = slot != nullptr ? slot->writeU.load (std::memory_order_acquire) : 0;

    auto ramFrame = [&] (int64_t t, float& l, float& r)
    {
        int64_t s = t;
        if (z.loopEnabled && t >= z.loopEnd) s = z.loopStart + (t - z.loopStart) % z.loopLen;
        if (s < 0) s = 0;
        if (s >= preFrames || s >= total) { l = r = 0.0f; return; }
        l = pre[s * 2];
        r = pre[s * 2 + 1];
    };

    for (int i = 0; i < n; ++i)
    {
        int64_t fl = (int64_t) v.pos;

        if (! z.loopEnabled && fl >= total) { killVoice (v); return; }

        float l0, r0, l1, r1, l2, r2, l3, r3;
        const bool useRing = slot != nullptr && (fl + 2 >= z.ramLimit);

        if (! useRing)
        {
            ramFrame (fl - 1, l0, r0);
            ramFrame (fl,     l1, r1);
            ramFrame (fl + 1, l2, r2);
            ramFrame (fl + 2, l3, r3);
        }
        else
        {
            if (fl + 2 >= ringEnd)
            {
                const bool eof = slot->eof.load (std::memory_order_acquire);
                ringEnd = slot->writeU.load (std::memory_order_acquire);
                if (fl + 2 >= ringEnd && ! eof)
                {
                    underrunCount.fetch_add (1, std::memory_order_relaxed);
                    break;                                   // wait for the disk thread
                }
            }
            auto ringFrame = [&] (int64_t t, float& l, float& r)
            {
                if (t < slot->startU) t = slot->startU;
                if (t >= ringEnd) { l = r = 0.0f; return; }
                const size_t at = (size_t) (t & (kRingFrames - 1)) * 2;
                l = ring[at]; r = ring[at + 1];
            };
            ringFrame (fl - 1, l0, r0);
            ringFrame (fl,     l1, r1);
            ringFrame (fl + 1, l2, r2);
            ringFrame (fl + 2, l3, r3);
        }

        const float fr = (float) (v.pos - (double) fl);
        const float sl = hermite (fr, l0, l1, l2, l3);
        const float sr = hermite (fr, r0, r1, r2, r3);

        // envelope
        switch (v.stage)
        {
            case 0:
                v.env += v.attackInc;
                if (v.env >= 1.0f)
                {
                    v.env = 1.0f;
                    if (z.hold && z.loopEnabled) { v.holdWait = true; v.stage = 1; }
                    else v.stage = (z.sustain >= 0.999f) ? 3 : 1;
                }
                break;
            case 1:
                if (v.holdWait)
                {
                    if (fl >= z.loopStart) v.holdWait = false;
                    break;
                }
                v.env *= v.decay1Coef;
                if (v.env <= v.sustainLevel)
                {
                    v.env = v.sustainLevel;
                    // a very short Decay2 time almost always means "no second decay"
                    v.stage = (z.infiniteSustain || z.decay2 < 0.05f) ? 3 : 2;
                }
                break;
            case 2:
                v.env *= v.decay2Coef;
                if (v.env < 0.001f) { killVoice (v); return; }
                break;
            case 3:
                break;
            case 4:
                v.env *= v.releaseCoef;
                if (v.env < 0.0005f) { killVoice (v); return; }
                break;
        }

        const float amp = v.env * xf;
        L[i] += sl * v.gainL * amp;
        R[i] += sr * v.gainR * amp;

        xf += xfStep;
        v.pos += step;
    }

    v.xfade = xf1;
    if (slot != nullptr && v.slot >= 0)
        slot->consumeU.store (std::max<int64_t> (slot->startU, (int64_t) v.pos - 2), std::memory_order_release);
}

void GigEngine::render (float* left, float* right, int numSamples)
{
    if (mixL.empty()) return;
    handleSwap();

    int offset = 0;
    while (offset < numSamples)
    {
        const int n = std::min<int> (numSamples - offset, (int) mixL.size());
        float* mL = mixL.data();
        float* mR = mixR.data();
        std::fill (mL, mL + n, 0.0f);
        std::fill (mR, mR + n, 0.0f);

        bendFactor = std::pow (2.0, (double) (bendSemitones + tuneCents.load() / 100.0f) / 12.0);

        int active = 0;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            renderVoice (v, mL, mR, n);
            if (v.active) ++active;
        }
        voiceCount.store (active, std::memory_order_relaxed);

        // master volume (with a short ramp to avoid clicks)
        const float c7 = cc[7] / 127.0f;
        const float target = masterGain.load() * c7 * c7;
        const float g0 = lastMaster;
        const float dg = (target - g0) / (float) n;
        float g = g0;
        for (int i = 0; i < n; ++i)
        {
            left[offset + i]  += mL[i] * g;
            right[offset + i] += mR[i] * g;
            g += dg;
        }
        lastMaster = target;

        sampleClock += (uint64_t) n;
        offset += n;
    }
}

} // namespace gigeng
