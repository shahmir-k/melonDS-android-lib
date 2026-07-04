/*
    liteDS-v2 headless harness - trace recording / verification / convergence.
    See VerifyTrace.h for the oracle rationale.
*/

#include "VerifyTrace.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cinttypes>
#include <ctime>
#include <string>
#include <vector>
#include <memory>
#include <optional>

#include "Args.h"
#include "NDS.h"
#include "NDSCart.h"
#include "GPU.h"
#include "GPU_Soft.h"
#include "Platform.h"
#include "xxhash/xxhash.h"

#include "PlatformHeadless.h"
#include "LiteProfile.h"

using namespace melonDS;

namespace liteds
{

namespace
{

constexpr int    kScreenW = 256;
constexpr int    kScreenH = 192;
constexpr size_t kScreenBytes = (size_t)kScreenW * kScreenH * sizeof(u32);

constexpr char   kMagic[8]   = { 'L','I','T','E','T','R','A','C' };
constexpr u32    kTraceVersion = 1;

// ---------------------------------------------------------------------------
// Fixed-size on-disk trace layout. Hand-packed so natural alignment leaves no
// padding; static_asserts guard the sizes so a struct change can't silently
// invalidate committed golden traces.
// ---------------------------------------------------------------------------

#pragma pack(push, 1)
struct TraceHeader
{
    char magic[8];        // "LITETRAC"
    u32  version;         // kTraceVersion
    u32  recordSize;      // sizeof(TraceRecord)
    u32  frames;          // number of records that follow
    u32  jit;             // 1 if recorded under JIT, else 0
    u64  romSize;         // ROM length in bytes
    u64  romHash;         // XXH3_64bits of the ROM image
    s64  rtcEpoch;        // fixed RTC unix timestamp used
    char romName[64];     // ROM basename, NUL-padded/truncated
    u8   reserved[16];
};

struct TraceRecord
{
    u32 frame;
    u32 pad;
    u32 arm9R[16];
    u32 arm9CPSR;
    u32 arm7R[16];
    u32 arm7CPSR;
    u64 sysTimestamp;
    u64 arm9Timestamp;
    u64 arm7Timestamp;
    u64 mainRamHash;
    u64 fbTopHash;
    u64 fbBotHash;
};
#pragma pack(pop)

static_assert(sizeof(TraceHeader) == 128, "TraceHeader layout drift");
static_assert(sizeof(TraceRecord) == 192, "TraceRecord layout drift");

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::unique_ptr<u8[]> ReadFile(const std::string& path, u32& lenOut)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return nullptr; }
    auto buf = std::make_unique<u8[]>((size_t)len);
    size_t rd = fread(buf.get(), 1, (size_t)len, f);
    fclose(f);
    if (rd != (size_t)len) return nullptr;
    lenOut = (u32)len;
    return buf;
}

std::string Basename(const std::string& path)
{
    auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// A booted NDS instance plus the per-instance platform userdata it points at.
// The userdata must outlive the NDS, so they are bundled together.
struct BuiltNDS
{
    std::unique_ptr<NDS> nds;
    std::unique_ptr<HeadlessHost::InstanceUserData> udata;
    u64 romHash = 0;
    u32 romSize = 0;
};

// Build, reset, direct-boot and start an NDS from `cfg`. Returns false + err on
// failure. `jitOverride`, when set, wins over cfg.jit (used by the converge
// path to build one JIT and one interp instance from the same config).
bool BuildAndBoot(const TraceRunConfig& cfg, std::optional<bool> jitOverride,
                  BuiltNDS& out, std::string& err)
{
    u32 romlen = 0;
    auto romdata = ReadFile(cfg.rom, romlen);
    if (!romdata) { err = "cannot read ROM '" + cfg.rom + "'"; return false; }

    out.romHash = XXH3_64bits(romdata.get(), romlen);
    out.romSize = romlen;

    auto cart = NDSCart::ParseROM(std::move(romdata), romlen, nullptr, std::nullopt);
    if (!cart) { err = "failed to parse DS ROM '" + cfg.rom + "'"; return false; }

    bool jit = jitOverride.value_or(cfg.jit);

    NDSArgs args; // FreeBIOS ARM9/ARM7 + generated firmware, software renderer
#ifdef JIT_ENABLED
    if (jit) args.JIT = JITArgs{};
    else     args.JIT = std::nullopt;
#else
    (void)jit;
    args.JIT = std::nullopt;
#endif

    out.udata = std::make_unique<HeadlessHost::InstanceUserData>();
    out.udata->savePrefix = cfg.instanceTag;

    out.nds = std::make_unique<NDS>(std::move(args), out.udata.get());
    out.nds->SetRenderer(std::make_unique<SoftRenderer>(*out.nds));
    out.nds->SetNDSCart(std::move(cart));
    out.nds->Reset();

    // Pin the RTC to a fixed epoch for determinism (see kDefaultRtcEpoch).
    {
        time_t t = (time_t)cfg.fixedRtcEpoch;
        struct tm g;
#if defined(_WIN32)
        gmtime_s(&g, &t);
#else
        gmtime_r(&t, &g);
#endif
        out.nds->RTC.SetDateTime(g.tm_year + 1900, g.tm_mon + 1, g.tm_mday,
                                 g.tm_hour, g.tm_min, g.tm_sec);
    }

    out.nds->SetupDirectBoot("headless.nds");
    out.nds->Start();
    out.nds->SetKeyMask(0xFFFF); // no buttons pressed (active-low)
    return true;
}

// Fill a trace record from the current emulator state after a frame.
void CaptureRecord(NDS& nds, int frame, TraceRecord& rec)
{
    memset(&rec, 0, sizeof(rec));
    rec.frame = (u32)frame;

    for (int i = 0; i < 16; i++)
    {
        rec.arm9R[i] = nds.ARM9.R[i];
        rec.arm7R[i] = nds.ARM7.R[i];
    }
    rec.arm9CPSR = nds.ARM9.CPSR;
    rec.arm7CPSR = nds.ARM7.CPSR;

    rec.sysTimestamp  = nds.GetSysTimestamp();
    rec.arm9Timestamp = nds.ARM9Timestamp;
    rec.arm7Timestamp = nds.ARM7Timestamp;

    rec.mainRamHash = XXH3_64bits(nds.MainRAM, (size_t)nds.MainRAMMask + 1);

    void* top = nullptr; void* bot = nullptr;
    if (nds.GPU.GetFramebuffers(&top, &bot) && top && bot)
    {
        rec.fbTopHash = XXH3_64bits(top, kScreenBytes);
        rec.fbBotHash = XXH3_64bits(bot, kScreenBytes);
    }
}

u64 FramebufferPairHash(NDS& nds)
{
    void* top = nullptr; void* bot = nullptr;
    if (nds.GPU.GetFramebuffers(&top, &bot) && top && bot)
    {
        u64 h[2] = { XXH3_64bits(top, kScreenBytes), XXH3_64bits(bot, kScreenBytes) };
        return XXH3_64bits(h, sizeof(h));
    }
    return 0;
}

// Report the fields that differ between an expected and actual record.
// Returns the number of differing fields (0 == identical).
int DiffRecords(const TraceRecord& e, const TraceRecord& a)
{
    int n = 0;
    auto u32field = [&](const char* name, u32 ev, u32 av) {
        if (ev != av) { printf("  %-16s expected=0x%08x  actual=0x%08x\n", name, ev, av); n++; }
    };
    auto u64field = [&](const char* name, u64 ev, u64 av) {
        if (ev != av)
        {
            printf("  %-16s expected=0x%016" PRIx64 "  actual=0x%016" PRIx64 "\n",
                   name, ev, av);
            n++;
        }
    };

    char buf[24];
    for (int i = 0; i < 16; i++)
    {
        snprintf(buf, sizeof(buf), "arm9.R%d", i);  u32field(buf, e.arm9R[i], a.arm9R[i]);
    }
    u32field("arm9.CPSR", e.arm9CPSR, a.arm9CPSR);
    for (int i = 0; i < 16; i++)
    {
        snprintf(buf, sizeof(buf), "arm7.R%d", i);  u32field(buf, e.arm7R[i], a.arm7R[i]);
    }
    u32field("arm7.CPSR", e.arm7CPSR, a.arm7CPSR);

    u64field("sysTimestamp",  e.sysTimestamp,  a.sysTimestamp);
    u64field("arm9Timestamp", e.arm9Timestamp, a.arm9Timestamp);
    u64field("arm7Timestamp", e.arm7Timestamp, a.arm7Timestamp);
    u64field("mainRamHash",   e.mainRamHash,   a.mainRamHash);
    u64field("fbTopHash",     e.fbTopHash,     a.fbTopHash);
    u64field("fbBotHash",     e.fbBotHash,     a.fbBotHash);
    return n;
}

} // namespace

// ---------------------------------------------------------------------------
// --record-trace
// ---------------------------------------------------------------------------

int RecordTrace(const TraceRunConfig& cfg, int frames, const std::string& outPath)
{
    BuiltNDS b;
    std::string err;
    if (!BuildAndBoot(cfg, std::nullopt, b, err))
    {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }

    FILE* f = fopen(outPath.c_str(), "wb");
    if (!f) { fprintf(stderr, "error: cannot open trace '%s' for writing\n", outPath.c_str()); return 1; }

    TraceHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, kMagic, sizeof(hdr.magic));
    hdr.version    = kTraceVersion;
    hdr.recordSize = sizeof(TraceRecord);
    hdr.frames     = (u32)frames;
    hdr.jit        = cfg.jit ? 1u : 0u;
    hdr.romSize    = b.romSize;
    hdr.romHash    = b.romHash;
    hdr.rtcEpoch   = cfg.fixedRtcEpoch;
    {
        std::string base = Basename(cfg.rom);
        strncpy(hdr.romName, base.c_str(), sizeof(hdr.romName) - 1);
    }
    if (fwrite(&hdr, sizeof(hdr), 1, f) != 1)
    {
        fprintf(stderr, "error: failed to write trace header\n");
        fclose(f);
        return 1;
    }

    for (int frame = 0; frame < frames; frame++)
    {
        LITE_PROFILE_RESET_FRAME();
        b.nds->RunFrame();

        TraceRecord rec;
        CaptureRecord(*b.nds, frame, rec);
        if (fwrite(&rec, sizeof(rec), 1, f) != 1)
        {
            fprintf(stderr, "error: failed to write trace record at frame %d\n", frame);
            fclose(f);
            return 1;
        }
    }

    fflush(f);
    fclose(f);

    printf("=== liteDS-headless record-trace ===\n");
    printf("rom:      %s\n", cfg.rom.c_str());
    printf("mode:     %s\n", cfg.jit ? "jit" : "interp");
    printf("frames:   %d\n", frames);
    printf("rtc:      %lld\n", cfg.fixedRtcEpoch);
    printf("rom_hash: 0x%016" PRIx64 "\n", b.romHash);
    printf("trace:    %s (%zu bytes)\n", outPath.c_str(),
           sizeof(TraceHeader) + (size_t)frames * sizeof(TraceRecord));
    fflush(stdout);
    return 0;
}

// ---------------------------------------------------------------------------
// --verify-trace
// ---------------------------------------------------------------------------

int VerifyTrace(const TraceRunConfig& cfg, const std::string& tracePath)
{
    FILE* f = fopen(tracePath.c_str(), "rb");
    if (!f) { fprintf(stderr, "error: cannot open trace '%s'\n", tracePath.c_str()); return 1; }

    TraceHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1)
    {
        fprintf(stderr, "error: trace '%s' is too short for a header\n", tracePath.c_str());
        fclose(f);
        return 1;
    }
    if (memcmp(hdr.magic, kMagic, sizeof(kMagic)) != 0)
    {
        fprintf(stderr, "error: '%s' is not a liteDS trace (bad magic)\n", tracePath.c_str());
        fclose(f);
        return 1;
    }
    if (hdr.version != kTraceVersion || hdr.recordSize != sizeof(TraceRecord))
    {
        fprintf(stderr, "error: trace version/record-size mismatch (v%u rec=%u, expected v%u rec=%zu)\n",
                hdr.version, hdr.recordSize, kTraceVersion, sizeof(TraceRecord));
        fclose(f);
        return 1;
    }

    // Match the recording conditions: same JIT mode + RTC epoch.
    TraceRunConfig rc = cfg;
    rc.jit           = (hdr.jit != 0);
    rc.fixedRtcEpoch = hdr.rtcEpoch;

    BuiltNDS b;
    std::string err;
    if (!BuildAndBoot(rc, std::nullopt, b, err))
    {
        fprintf(stderr, "error: %s\n", err.c_str());
        fclose(f);
        return 1;
    }

    if (b.romHash != hdr.romHash)
        fprintf(stderr, "warning: ROM hash differs from trace (trace=0x%016" PRIx64
                        " current=0x%016" PRIx64 ") - verifying anyway\n",
                (u64)hdr.romHash, b.romHash);

    for (u32 frame = 0; frame < hdr.frames; frame++)
    {
        TraceRecord expected;
        if (fread(&expected, sizeof(expected), 1, f) != 1)
        {
            fprintf(stderr, "error: trace truncated: expected %u records, ran out at %u\n",
                    hdr.frames, frame);
            fclose(f);
            return 1;
        }

        LITE_PROFILE_RESET_FRAME();
        b.nds->RunFrame();

        TraceRecord actual;
        CaptureRecord(*b.nds, (int)frame, actual);

        if (memcmp(&expected, &actual, sizeof(TraceRecord)) != 0)
        {
            printf("MISMATCH at frame %u\n", frame);
            int nd = DiffRecords(expected, actual);
            printf("(%d differing field%s)\n", nd, nd == 1 ? "" : "s");
            fflush(stdout);
            fclose(f);
            return 2;
        }
    }

    fclose(f);
    printf("=== liteDS-headless verify-trace: OK ===\n");
    printf("trace:  %s\n", tracePath.c_str());
    printf("frames: %u (all identical)\n", hdr.frames);
    printf("mode:   %s\n", rc.jit ? "jit" : "interp");
    fflush(stdout);
    return 0;
}

// ---------------------------------------------------------------------------
// --verify-interp-converge
// ---------------------------------------------------------------------------

int VerifyInterpConverge(const TraceRunConfig& cfg, int frames)
{
    TraceRunConfig jc = cfg; jc.jit = true;  jc.instanceTag = "headless-jit";
    TraceRunConfig ic = cfg; ic.jit = false; ic.instanceTag = "headless-interp";

    BuiltNDS jb, ib;
    std::string err;
    if (!BuildAndBoot(jc, true, jb, err))  { fprintf(stderr, "error (jit): %s\n", err.c_str()); return 1; }
    if (!BuildAndBoot(ic, false, ib, err)) { fprintf(stderr, "error (interp): %s\n", err.c_str()); return 1; }

    int  diffCount        = 0;   // frames whose fb hashes differ
    int  longestRun       = 0;   // longest run of consecutive differing frames
    int  curRun           = 0;
    int  firstDiff        = -1;
    int  lastDiff         = -1;
    int  tailWindow       = 60;
    int  tailDiffs        = 0;   // diffs within the final `tailWindow` frames

    for (int frame = 0; frame < frames; frame++)
    {
        LITE_PROFILE_RESET_FRAME();
        jb.nds->RunFrame();
        ib.nds->RunFrame();

        u64 hj = FramebufferPairHash(*jb.nds);
        u64 hi = FramebufferPairHash(*ib.nds);

        bool differ = (hj != hi);
        if (differ)
        {
            diffCount++;
            if (firstDiff < 0) firstDiff = frame;
            lastDiff = frame;
            curRun++;
            if (curRun > longestRun) longestRun = curRun;
        }
        else
        {
            curRun = 0;
        }
        if (frame >= frames - tailWindow && differ)
            tailDiffs++;
    }

    bool tailClean = (tailDiffs == 0);

    printf("=== liteDS-headless verify-interp-converge ===\n");
    printf("rom:            %s\n", cfg.rom.c_str());
    printf("frames:         %d\n", frames);
    printf("differing:      %d\n", diffCount);
    printf("first_diff:     %d\n", firstDiff);
    printf("last_diff:      %d\n", lastDiff);
    printf("longest_run:    %d\n", longestRun);
    printf("tail_window:    %d\n", tailWindow);
    printf("tail_diffs:     %d\n", tailDiffs);
    printf("converged:      %s\n", tailClean ? "yes" : "no");
    printf("(interpretation: JIT and interp legitimately diverge mid-boot; the\n");
    printf(" oracle requires the final %d frames to be identical.)\n", tailWindow);
    fflush(stdout);

    return tailClean ? 0 : 3;
}

} // namespace liteds
