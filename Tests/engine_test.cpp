// engine_test.cpp
// Makes a small .gig file with libgig, then plays it through GigEngine without
// any plugin host and checks the sound. Run it on Linux:  ./engine_test
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "gig.h"
#include "../Source/GigEngine.h"

static const double kRate = 44100.0;
static int failures = 0;

#define CHECK(cond, msg) do { if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++failures; } else { std::printf("  ok:   %s\n", msg); } } while (0)

static gig::Sample* addSample16 (gig::File& f, const char* name, int channels, const std::vector<int16_t>& data /*interleaved*/)
{
    gig::Sample* s = f.AddSample();
    s->pInfo->Name = name;
    s->Channels = channels;
    s->BitDepth = 16;
    s->FrameSize = 2 * channels;
    s->SamplesPerSecond = 44100;
    s->MIDIUnityNote = 69;
    s->Resize (data.size() / channels);
    return s;
}

static std::vector<int16_t> sine (int frames, double freq, double amp, int channels, double freqR = 0)
{
    std::vector<int16_t> d ((size_t) frames * channels);
    for (int i = 0; i < frames; ++i)
    {
        d[(size_t) i * channels] = (int16_t) std::lround (amp * 32767.0 * std::sin (2 * M_PI * freq * i / kRate));
        if (channels == 2)
            d[(size_t) i * 2 + 1] = (int16_t) std::lround (amp * 32767.0 * std::sin (2 * M_PI * (freqR > 0 ? freqR : freq) * i / kRate));
    }
    return d;
}

static void setLoop (gig::DimensionRegion* dr, uint32_t start, uint32_t len)
{
    DLS::sample_loop_t loop;
    loop.Size = sizeof (loop);
    loop.LoopType = gig::loop_type_normal;
    loop.LoopStart = start;
    loop.LoopLength = len;
    dr->AddSampleLoop (&loop);
}

static void makeGig (const std::string& path)
{
    gig::File f;
    f.pInfo->Name = "test";

    // --- instrument 0: short looped mono sine -------------------------------
    auto dLoop = sine (22050, 441.0, 0.5, 1);               // 441 Hz -> 100 samples per cycle
    gig::Sample* sLoop = addSample16 (f, "loop", 1, dLoop);

    // --- instrument 1: long stereo sine (streamed), and a long looped one ---
    auto dLong = sine (4 * 44100, 441.0, 0.5, 2, 882.0);
    gig::Sample* sLong = addSample16 (f, "long", 2, dLong);

    auto dLongLoop = sine (3 * 44100, 441.0, 0.5, 1);
    gig::Sample* sLongLoop = addSample16 (f, "longloop", 1, dLongLoop);

    // --- instrument 2: velocity layers + release trigger --------------------
    auto dSoft = sine (44100, 441.0, 0.1, 1);
    auto dLoud = sine (44100, 441.0, 0.8, 1);
    auto dRel  = sine (8000, 1764.0, 0.5, 1);               // release click, 4x pitch
    gig::Sample* sSoft = addSample16 (f, "soft", 1, dSoft);
    gig::Sample* sLoud = addSample16 (f, "loud", 1, dLoud);
    gig::Sample* sRel  = addSample16 (f, "rel", 1, dRel);

    // instruments
    gig::Instrument* i0 = f.AddInstrument();
    i0->pInfo->Name = "Loop";
    gig::Region* r0 = i0->AddRegion();
    r0->SetKeyRange (0, 127);
    r0->pDimensionRegions[0]->pSample = sLoop;
    r0->pDimensionRegions[0]->UnityNote = 69;
    setLoop (r0->pDimensionRegions[0], 2000, 10000);

    gig::Instrument* i1 = f.AddInstrument();
    i1->pInfo->Name = "Stream";
    gig::Region* r1 = i1->AddRegion();
    r1->SetKeyRange (0, 59);
    r1->pDimensionRegions[0]->pSample = sLong;
    r1->pDimensionRegions[0]->UnityNote = 69;
    gig::Region* r1b = i1->AddRegion();
    r1b->SetKeyRange (60, 127);
    r1b->pDimensionRegions[0]->pSample = sLongLoop;
    r1b->pDimensionRegions[0]->UnityNote = 69;
    setLoop (r1b->pDimensionRegions[0], 60000, 40000);       // loop sits behind the preload

    gig::Instrument* i2 = f.AddInstrument();
    i2->pInfo->Name = "Velocity+Release";
    gig::Region* r2 = i2->AddRegion();
    r2->SetKeyRange (0, 127);
    gig::dimension_def_t dv = {};
    dv.dimension = gig::dimension_velocity; dv.bits = 1; dv.zones = 2;
    r2->AddDimension (&dv);
    gig::dimension_def_t dr = {};
    dr.dimension = gig::dimension_releasetrigger; dr.bits = 1; dr.zones = 2;
    r2->AddDimension (&dr);
    // index = velocityZone | (release << 1)
    r2->pDimensionRegions[0]->pSample = sSoft;
    r2->pDimensionRegions[1]->pSample = sLoud;
    r2->pDimensionRegions[2]->pSample = sRel;
    r2->pDimensionRegions[3]->pSample = sRel;
    for (int k = 0; k < 4; ++k) r2->pDimensionRegions[k]->UnityNote = 69;
    setLoop (r2->pDimensionRegions[0], 4000, 20000);
    setLoop (r2->pDimensionRegions[1], 4000, 20000);

    f.Save (path);
    sLoop->Write (dLoop.data(), dLoop.size());
    sLong->Write (dLong.data(), dLong.size() / 2);
    sLongLoop->Write (dLongLoop.data(), dLongLoop.size());
    sSoft->Write (dSoft.data(), dSoft.size());
    sLoud->Write (dLoud.data(), dLoud.size());
    sRel->Write (dRel.data(), dRel.size());
}

// ---------------------------------------------------------------------------
struct Rendered { std::vector<float> L, R; };

static Rendered play (gigeng::GigEngine& e, int key, int vel, double holdSec, double tailSec)
{
    Rendered out;
    const int block = 256;
    const int holdBlocks = (int) (holdSec * kRate / block);
    const int tailBlocks = (int) (tailSec * kRate / block);
    e.noteOn (key, vel);
    std::vector<float> l (block), r (block);
    for (int b = 0; b < holdBlocks + tailBlocks; ++b)
    {
        if (b == holdBlocks) e.noteOff (key);
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        e.render (l.data(), r.data(), block);
        out.L.insert (out.L.end(), l.begin(), l.end());
        out.R.insert (out.R.end(), r.begin(), r.end());
        std::this_thread::sleep_for (std::chrono::microseconds (3500));   // roughly real time
    }
    return out;
}

static double estimateFreq (const std::vector<float>& x, size_t from, size_t to)
{
    int crossings = 0;
    size_t first = 0, last = 0;
    for (size_t i = from + 1; i < to; ++i)
        if (x[i - 1] <= 0.0f && x[i] > 0.0f) { if (!crossings) first = i; last = i; ++crossings; }
    if (crossings < 3) return 0;
    return (crossings - 1) * kRate / double (last - first);
}

static float peak (const std::vector<float>& x, size_t from, size_t to)
{
    float p = 0;
    for (size_t i = from; i < to && i < x.size(); ++i) p = std::max (p, std::fabs (x[i]));
    return p;
}

static void waitLoaded (gigeng::GigEngine& e)
{
    for (int i = 0; i < 4000; ++i)
    {
        auto s = e.getStatus();
        if (!s.loading && s.hasInstrument) return;
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
}

int main()
{
    const std::string path = "/tmp/engine_test.gig";
    makeGig (path);
    std::printf ("test file written\n");

    gigeng::GigEngine e;
    e.prepare (kRate, 256);
    e.loadFile (path, 0, 8192);
    waitLoaded (e);
    auto st = e.getStatus();
    std::printf ("status: %s | %zu instruments\n", st.message.c_str(), st.instruments.size());
    CHECK (st.instruments.size() == 3, "file lists 3 instruments");
    CHECK (st.hasInstrument, "instrument 0 loaded");

    // -------- 1. pitch and loop (all in RAM) --------
    std::printf ("\n[1] looped mono sample, pitch and loop\n");
    {
        auto o = play (e, 69, 100, 2.0, 0.5);        // sample is only 0.5s long; must loop
        const double f0 = estimateFreq (o.L, 20000, 80000);
        std::printf ("  freq at key 69: %.2f Hz (expect 441)\n", f0);
        CHECK (std::fabs (f0 - 441.0) < 2.0, "pitch is right at the root note");
        CHECK (peak (o.L, 60000, 80000) > 0.2f, "still sounding after 1.4 s (loop works)");
        CHECK (peak (o.L, o.L.size() - 2000, o.L.size()) < 0.01f, "silent after release");
        auto o2 = play (e, 81, 100, 1.0, 0.3);
        const double f1 = estimateFreq (o2.L, 8000, 40000);
        std::printf ("  freq at key 81: %.2f Hz (expect 882)\n", f1);
        CHECK (std::fabs (f1 - 882.0) < 4.0, "one octave up doubles the pitch");
    }

    // -------- 2. streaming from disk --------
    std::printf ("\n[2] streaming (preload 8192 frames of a 4 s sample)\n");
    e.selectInstrument (1, 8192);
    std::this_thread::sleep_for (std::chrono::milliseconds (100));
    waitLoaded (e);
    {
        const int before = e.getStatus().underruns;
        auto o = play (e, 59, 100, 3.8, 0.2);        // keys below 60 use the long stereo sample, root 69 -> play key 69? use 59 -> lower
        (void) o;
        (void) before;
    }
    {
        // key 69 is in the looped region, so test the plain stream region with the key that gives unity pitch:
        // region 0 covers keys 0-59 with unity note 69, so key 59 plays 10 semitones low. Check the pitch ratio instead.
        e.allNotesOff();
        auto o = play (e, 59, 100, 3.0, 0.3);
        const double ratio = std::pow (2.0, -10.0 / 12.0);
        const double fl = estimateFreq (o.L, 50000, 120000);
        const double fr = estimateFreq (o.R, 50000, 120000);
        std::printf ("  L %.2f Hz (expect %.2f), R %.2f Hz (expect %.2f)\n", fl, 441.0 * ratio, fr, 882.0 * ratio);
        CHECK (std::fabs (fl - 441.0 * ratio) < 3.0, "left channel pitch after streaming handoff");
        CHECK (std::fabs (fr - 882.0 * ratio) < 6.0, "right channel pitch after streaming handoff");
        CHECK (peak (o.L, 100000, 120000) > 0.2f, "still playing well past the preload");
        // check for glitches: largest jump between neighbouring samples
        float maxJump = 0;
        for (size_t i = 30000; i + 1 < 120000; ++i) maxJump = std::max (maxJump, std::fabs (o.L[i + 1] - o.L[i]));
        std::printf ("  largest sample-to-sample jump: %.4f\n", maxJump);
        CHECK (maxJump < 0.1f, "no clicks across the RAM-to-disk handoff");
        CHECK (e.getStatus().underruns == 0, "no disk underruns");
    }
    {
        // loop that lives behind the preload (keys 60+)
        auto o = play (e, 69, 100, 4.5, 0.3);        // sample is 3 s, loop 60000..100000
        const double f = estimateFreq (o.L, 200000, 260000);
        std::printf ("  looped-from-disk freq: %.2f Hz (expect 441)\n", f);
        CHECK (std::fabs (f - 441.0) < 2.0, "loop that sits past the preload keeps playing");
        CHECK (peak (o.L, 190000, 200000) > 0.2f, "still sounding after 4 s");
        float maxJump = 0;
        for (size_t i = 60000; i + 1 < 200000; ++i) maxJump = std::max (maxJump, std::fabs (o.L[i + 1] - o.L[i]));
        CHECK (maxJump < 0.1f, "no clicks at the loop point");
    }

    // -------- 3. velocity layers + release trigger --------
    std::printf ("\n[3] velocity layers and release samples\n");
    e.selectInstrument (2, 8192);
    std::this_thread::sleep_for (std::chrono::milliseconds (100));
    waitLoaded (e);
    {
        auto soft = play (e, 69, 30, 0.5, 0.2);
        auto loud = play (e, 69, 120, 0.5, 0.2);
        const float ps = peak (soft.L, 8000, 20000);
        const float pl = peak (loud.L, 8000, 20000);
        std::printf ("  soft peak %.3f, loud peak %.3f\n", ps, pl);
        CHECK (pl > ps * 2.0f, "loud velocity plays the louder layer");
        // release sample: 1764 Hz burst starting at key off
        auto o = play (e, 69, 120, 0.5, 0.15);
        const size_t offAt = (size_t) (0.5 * kRate / 256) * 256;
        const double fr = estimateFreq (o.L, offAt + 1500, offAt + 6000);
        std::printf ("  frequency right after key-off: %.1f Hz (release sample is 1764)\n", fr);
        CHECK (std::fabs (fr - 1764.0) < 20.0, "release sample plays on key-off");
    }

    std::printf ("\n%s (%d failure%s)\n", failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
