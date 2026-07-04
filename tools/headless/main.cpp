/*
    liteDS-headless - Unit 0 headless benchmark runner for liteDS-v2.

    Runs a DS ROM headlessly with the software renderer, null audio and no
    input, for a fixed number of frames, and reports timing + framebuffer
    hashes. Uses FreeBIOS + generated firmware with direct boot, so no external
    BIOS/firmware files are required.

    See docs/liteDS-v2-plan.md B.2 "Unit 0" for the spec.
*/

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <chrono>

#include "types.h"
#include "Args.h"
#include "NDS.h"
#include "SPU.h"
#include "NDSCart.h"
#include "GPU.h"
#include "GPU_Soft.h"
#include "Platform.h"
#include "Savestate.h"
#include "SPI_Firmware.h"
#include "FreeBIOS.h"

#include "xxhash/xxhash.h"

#include "PlatformHeadless.h"
#include "LiteProfile.h"
#include "VerifyTrace.h"
#include "InputScript.h"

using namespace melonDS;

namespace {

enum class RunMode { Benchmark, RecordTrace, VerifyTrace, VerifyInterpConverge };

// Native DS screen dimensions; the software renderer writes 256x192 u32 per screen.
constexpr int kScreenW = 256;
constexpr int kScreenH = 192;
constexpr size_t kScreenBytes = (size_t)kScreenW * kScreenH * sizeof(u32);

struct Options
{
    std::string rom;
    std::string savestate;
    std::string dataDir = "./headless-data";
    std::string profileJson;
    int frames = 300;
    bool jit = true;
    bool fastmem = true;                // JIT fast-memory path (ignored if unsupported)
    int fbHashEvery = 0;                // 0 => only final hash
    int fbDumpFrame = -1;               // frame index to dump, -1 => none
    std::string fbDumpPath;

    // Unit 1 trace/verify modes.
    RunMode mode = RunMode::Benchmark;
    std::string tracePath;
    long long fixedRtc = liteds::kDefaultRtcEpoch;

    AudioInterpolation interp = AudioInterpolation::None;
    int frameskip = 0;                  // LITEV_AGGRESSIVE_SKIP target (0 = off)

    std::string inputScript;            // --input-script: scripted button input

    // --bench-window <start>:<end>: measure avg FPS ONLY over frames [start,end]
    // (inclusive), while the total run still executes all --frames frames. Lets a
    // benchmark isolate a steady-state gameplay window from the boot/menu ramp.
    int benchWindowStart = -1;          // -1 => no window (overall FPS only)
    int benchWindowEnd   = -1;

    // --dump-savestate <frame>:<path>: after running frame <frame>, write a full
    // core savestate to <path>. Lets a scripted menu run bake an in-race start
    // state once, so benchmark runs can load it and measure gameplay immediately
    // (script-once / savestate-many). Savestates embed copyrighted RAM contents,
    // so they are LOCAL-ONLY (gitignored) and regenerated from the input script.
    int dumpSavestateFrame = -1;
    std::string dumpSavestatePath;
};

[[noreturn]] void Usage(const char* argv0, int code)
{
    fprintf(stderr,
        "liteDS-headless - headless melonDS benchmark runner (liteDS-v2 Unit 0)\n"
        "Usage: %s --rom <path> [options]\n"
        "  --rom <path>              DS ROM to run (required)\n"
        "  --savestate <path>        load a savestate after boot (optional)\n"
        "  --dump-savestate <f>:<p>  write a core savestate to <p> after running frame f\n"
        "  --frames N                number of frames to run (default 300)\n"
        "  --mode jit|interp         execution mode (default jit)\n"
        "  --fastmem on|off          JIT fast-memory path (default on; no-op where unsupported)\n"
        "  --fb-hash-every N         print xxhash of both framebuffers every N frames\n"
        "  --fb-dump-ppm <f>:<path>  dump both framebuffers at frame f as PPM (side by side)\n"
        "  --audio-interp <mode>     SPU interpolation: none|linear|cosine|cubic|gaussian (default none)\n"
        "  --frameskip N             skip N of every N+1 frames' rasterization (LITEV_AGGRESSIVE_SKIP build)\n"
        "  --bench-window <s>:<e>    report avg FPS over frames [s,e] inclusive only (still runs all frames)\n"
        "  --profile-json <path>     write per-run totals as JSON\n"
        "  --data-dir <path>         local firmware/save directory (default ./headless-data)\n"
        "  --fixed-rtc <unix-ts>     fixed RTC epoch for determinism (default 946684800)\n"
        "  --input-script <path>     scripted button input: lines '<frame> <keys>'\n"
        "                            keys = comma list (A,B,SELECT,START,RIGHT,LEFT,UP,\n"
        "                            DOWN,R,L,X,Y), NONE, or a 0x hex pressed-mask; held\n"
        "                            level from that frame on (applies to all run modes)\n"
        "\n"
        "  Unit 1 oracle modes (mutually exclusive; run --frames frames):\n"
        "  --record-trace <path>     record a per-frame binary state trace to <path>\n"
        "  --verify-trace <path>     replay and compare against a golden trace <path>\n"
        "  --verify-interp-converge  run JIT + interp side by side, report convergence\n",
        argv0);
    exit(code);
}

bool ParseArgs(int argc, char** argv, Options& o)
{
    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) { fprintf(stderr, "error: %s requires an argument\n", name); Usage(argv[0], 2); }
            return argv[++i];
        };

        if (a == "--rom") o.rom = next("--rom");
        else if (a == "--savestate") o.savestate = next("--savestate");
        else if (a == "--dump-savestate")
        {
            std::string spec = next("--dump-savestate");
            auto colon = spec.find(':');
            if (colon == std::string::npos) { fprintf(stderr, "error: --dump-savestate needs <frame>:<path>\n"); return false; }
            o.dumpSavestateFrame = std::atoi(spec.substr(0, colon).c_str());
            o.dumpSavestatePath = spec.substr(colon + 1);
        }
        else if (a == "--frames") o.frames = std::atoi(next("--frames").c_str());
        else if (a == "--mode")
        {
            std::string m = next("--mode");
            if (m == "jit") o.jit = true;
            else if (m == "interp") o.jit = false;
            else { fprintf(stderr, "error: --mode must be jit or interp\n"); return false; }
        }
        else if (a == "--fastmem")
        {
            std::string m = next("--fastmem");
            if (m == "on" || m == "1" || m == "true") o.fastmem = true;
            else if (m == "off" || m == "0" || m == "false") o.fastmem = false;
            else { fprintf(stderr, "error: --fastmem must be on or off\n"); return false; }
        }
        else if (a == "--fb-hash-every") o.fbHashEvery = std::atoi(next("--fb-hash-every").c_str());
        else if (a == "--fb-dump-ppm")
        {
            std::string spec = next("--fb-dump-ppm");
            auto colon = spec.find(':');
            if (colon == std::string::npos) { fprintf(stderr, "error: --fb-dump-ppm needs <frame>:<path>\n"); return false; }
            o.fbDumpFrame = std::atoi(spec.substr(0, colon).c_str());
            o.fbDumpPath = spec.substr(colon + 1);
        }
        else if (a == "--audio-interp")
        {
            std::string m = next("--audio-interp");
            if (m == "none") o.interp = AudioInterpolation::None;
            else if (m == "linear") o.interp = AudioInterpolation::Linear;
            else if (m == "cosine") o.interp = AudioInterpolation::Cosine;
            else if (m == "cubic") o.interp = AudioInterpolation::Cubic;
            else if (m == "gaussian") o.interp = AudioInterpolation::SNESGaussian;
            else { fprintf(stderr, "error: --audio-interp must be none|linear|cosine|cubic|gaussian\n"); return false; }
        }
        else if (a == "--frameskip") o.frameskip = std::atoi(next("--frameskip").c_str());
        else if (a == "--profile-json") o.profileJson = next("--profile-json");
        else if (a == "--data-dir") o.dataDir = next("--data-dir");
        else if (a == "--fixed-rtc") o.fixedRtc = std::atoll(next("--fixed-rtc").c_str());
        else if (a == "--input-script") o.inputScript = next("--input-script");
        else if (a == "--bench-window")
        {
            std::string spec = next("--bench-window");
            auto colon = spec.find(':');
            if (colon == std::string::npos) { fprintf(stderr, "error: --bench-window needs <start>:<end>\n"); return false; }
            o.benchWindowStart = std::atoi(spec.substr(0, colon).c_str());
            o.benchWindowEnd   = std::atoi(spec.substr(colon + 1).c_str());
        }
        else if (a == "--record-trace") { o.mode = RunMode::RecordTrace; o.tracePath = next("--record-trace"); }
        else if (a == "--verify-trace") { o.mode = RunMode::VerifyTrace; o.tracePath = next("--verify-trace"); }
        else if (a == "--verify-interp-converge") o.mode = RunMode::VerifyInterpConverge;
        else if (a == "--help" || a == "-h") Usage(argv[0], 0);
        else { fprintf(stderr, "error: unknown argument '%s'\n", a.c_str()); return false; }
    }

    if (o.rom.empty()) { fprintf(stderr, "error: --rom is required\n"); return false; }
    if (o.frames <= 0) { fprintf(stderr, "error: --frames must be positive\n"); return false; }
    if (o.benchWindowStart >= 0 || o.benchWindowEnd >= 0)
    {
        if (o.benchWindowStart < 0 || o.benchWindowEnd < 0
            || o.benchWindowStart > o.benchWindowEnd
            || o.benchWindowEnd >= o.frames)
        {
            fprintf(stderr, "error: --bench-window needs 0 <= start <= end < frames (got %d:%d, frames=%d)\n",
                    o.benchWindowStart, o.benchWindowEnd, o.frames);
            return false;
        }
    }
    return true;
}

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

// Dump both framebuffers as a single side-by-side binary PPM (P6, 24-bit).
// The software renderer stores pixels as BGRA/ABGR u32; we extract RGB.
bool DumpPPM(const std::string& path, const u32* top, const u32* bottom)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const int W = kScreenW;
    const int H = kScreenH * 2; // stacked vertically
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    std::vector<u8> row(W * 3);
    auto writeScreen = [&](const u32* src) {
        for (int y = 0; y < kScreenH; y++)
        {
            for (int x = 0; x < W; x++)
            {
                u32 px = src[y * W + x];
                // melonDS software framebuffer is stored as 0xFFBBGGRR (little-endian RGBA8).
                row[x*3+0] = (u8)(px & 0xFF);         // R
                row[x*3+1] = (u8)((px >> 8) & 0xFF);  // G
                row[x*3+2] = (u8)((px >> 16) & 0xFF); // B
            }
            fwrite(row.data(), 1, row.size(), f);
        }
    };
    writeScreen(top);
    writeScreen(bottom);
    fclose(f);
    return true;
}

// Write a full core savestate to `path` using the same in-memory Savestate API
// as the Qt frontend (default 32 MB save buffer -> DoSavestate -> flush bytes).
bool SaveSavestate(NDS& nds, const std::string& path)
{
    Savestate state; // default ctor: save mode, DEFAULT_SIZE buffer
    if (state.Error) { fprintf(stderr, "error: savestate alloc failed\n"); return false; }
    if (!nds.DoSavestate(&state) || state.Error)
    {
        fprintf(stderr, "error: DoSavestate failed while dumping\n");
        return false;
    }
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { fprintf(stderr, "error: cannot open savestate '%s' for write\n", path.c_str()); return false; }
    size_t wr = fwrite(state.Buffer(), 1, state.Length(), f);
    fclose(f);
    if (wr != state.Length())
    {
        fprintf(stderr, "error: short write to savestate '%s'\n", path.c_str());
        return false;
    }
    fprintf(stderr, "dumped savestate (%u bytes) to %s\n", state.Length(), path.c_str());
    return true;
}

bool LoadSavestate(NDS& nds, const std::string& path)
{
    u32 len = 0;
    auto buf = ReadFile(path, len);
    if (!buf) { fprintf(stderr, "error: cannot read savestate '%s'\n", path.c_str()); return false; }
    Savestate state(buf.get(), len, false);
    if (state.Error) { fprintf(stderr, "error: savestate parse error\n"); return false; }
    if (!nds.DoSavestate(&state) || state.Error)
    {
        fprintf(stderr, "error: failed to load savestate into emulator\n");
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    Options opt;
    if (!ParseArgs(argc, argv, opt))
        Usage(argv[0], 2);

    HeadlessHost::SetDataDir(opt.dataDir);

    // --- Unit 1 oracle modes -------------------------------------------------
    if (opt.mode != RunMode::Benchmark)
    {
        liteds::TraceRunConfig cfg;
        cfg.rom = opt.rom;
        cfg.dataDir = opt.dataDir;
        cfg.jit = opt.jit;
        cfg.fixedRtcEpoch = opt.fixedRtc;
        cfg.inputScript = opt.inputScript;

        switch (opt.mode)
        {
        case RunMode::RecordTrace:
            return liteds::RecordTrace(cfg, opt.frames, opt.tracePath);
        case RunMode::VerifyTrace:
            return liteds::VerifyTrace(cfg, opt.tracePath);
        case RunMode::VerifyInterpConverge:
            return liteds::VerifyInterpConverge(cfg, opt.frames);
        default:
            break;
        }
    }

    // --- Load ROM ---
    u32 romlen = 0;
    auto romdata = ReadFile(opt.rom, romlen);
    if (!romdata)
    {
        fprintf(stderr, "error: cannot read ROM '%s'\n", opt.rom.c_str());
        return 1;
    }

    auto cart = NDSCart::ParseROM(std::move(romdata), romlen, nullptr, std::nullopt);
    if (!cart)
    {
        fprintf(stderr, "error: failed to parse DS ROM '%s'\n", opt.rom.c_str());
        return 1;
    }

    // --- Build NDS (FreeBIOS + generated firmware, software renderer) ---
    NDSArgs args; // defaults: FreeBIOS ARM9/ARM7, generated NDS firmware
    args.Interpolation = opt.interp;
#ifdef JIT_ENABLED
    if (opt.jit)
    {
        JITArgs ja{};               // default JIT settings
        ja.FastMemory = opt.fastmem;
        args.JIT = ja;
    }
    else
        args.JIT = std::nullopt;    // interpreter
#else
    if (opt.jit)
        fprintf(stderr, "warning: build has no JIT; running interpreter\n");
    args.JIT = std::nullopt;
#endif

    // NDS is a large object; heap-allocate it (a stack instance overflows the
    // main thread stack).
    auto nds = std::make_unique<NDS>(std::move(args), nullptr);
    nds->SetRenderer(std::make_unique<SoftRenderer>(*nds));
    nds->SetNDSCart(std::move(cart));
    nds->Reset();

    // Direct boot so no external firmware boot menu / BIOS files are needed.
    nds->SetupDirectBoot("headless.nds");
    nds->Start();

    if (!opt.savestate.empty())
    {
        if (!LoadSavestate(*nds, opt.savestate))
            return 1;
    }

    nds->SetKeyMask(0xFFFF); // no buttons pressed (active-low)

    // Scripted input (optional). Loaded once; the run loop applies the level
    // key mask for each frame before RunFrame so menu-driven test ROMs can be
    // navigated. Script content is part of the deterministic input, so a trace
    // recorded with a script only replays under the same script.
    liteds::InputScript inputScript;
    if (!opt.inputScript.empty())
    {
        std::string err;
        if (!inputScript.LoadFile(opt.inputScript, err))
        {
            fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        fprintf(stderr, "input-script: %s (%zu directives, hash=%016llx)\n",
                opt.inputScript.c_str(), inputScript.DirectiveCount(),
                (unsigned long long)inputScript.Hash());
    }

    if (opt.frameskip > 0)
    {
#ifdef LITEV_AGGRESSIVE_SKIP
        nds->GPU.SetFrameskipTarget(opt.frameskip);
        fprintf(stderr, "frameskip: rendering 1 of every %d frames\n", opt.frameskip + 1);
#else
        fprintf(stderr, "warning: --frameskip ignored (build lacks LITEV_AGGRESSIVE_SKIP)\n");
#endif
    }

    // Report the engine that ACTUALLY engaged (not just what was requested), so a
    // silent JIT->interp or fastmem->slowmem fallback is detectable on device.
#ifdef JIT_ENABLED
    {
        bool jitOn = nds->IsJITEnabled();
        bool fmOn  = jitOn && nds->JIT.FastMemoryEnabled();
        fprintf(stderr, "engine: %s  fastmem_requested=%s  fastmem_active=%s\n",
                jitOn ? "JIT" : "interpreter",
                opt.fastmem ? "on" : "off",
                fmOn ? "yes" : "no");
        if (opt.jit && !jitOn)
            fprintf(stderr, "warning: JIT requested but interpreter engaged (silent fallback)\n");
        if (opt.jit && opt.fastmem && !fmOn)
            fprintf(stderr, "note: fastmem requested but inactive (unsupported on this platform)\n");
    }
#endif

    fprintf(stderr, "liteDS-headless: rom=%s mode=%s frames=%d jit=%s\n",
            opt.rom.c_str(), opt.jit ? "jit" : "interp", opt.frames,
#ifdef JIT_ENABLED
            opt.jit ? "yes" : "no");
#else
            "unavailable");
#endif

    // --- Run loop ---
    u64 lastTopHash = 0, lastBotHash = 0;
    bool anyChange = false;
    u64 firstTopHash = 0;
    bool haveFirst = false;

    // Rolling xxhash over all SPU output samples. This lets acceptance checks
    // detect audio-only changes (e.g. LITEV_SPU_FAST_INTERP) that leave the
    // framebuffer identical. Deterministic given a fixed ROM + frame count.
    XXH3_state_t* audioHashState = XXH3_createState();
    XXH3_64bits_reset(audioHashState);
    u64 audioSampleCount = 0;                 // stereo frames drained
    std::vector<s16> audioDrain(2048 * 2);    // interleaved L/R scratch buffer

    auto wallStart = std::chrono::steady_clock::now();

    // --bench-window: measured span within the full run. windowStart is stamped
    // just before frame benchWindowStart's RunFrame; windowEnd just after frame
    // benchWindowEnd's RunFrame. The window covers [start,end] inclusive.
    const bool haveWindow = opt.benchWindowStart >= 0;
    std::chrono::steady_clock::time_point windowStart{}, windowEnd{};

#if LITEV_PROFILE
    // Run totals: g_Frame is reset every frame, so accumulate each frame's counters
    // into totals here to observe whole-run behaviour (esp. the Unit 4 link counters).
    struct { uint64_t linksPatched=0, linksUnlinked=0, pendingPeak=0,
                       cppReentries=0, dispatcherMisses=0,
                       linkSitesEmitted=0, dispatchOnlyExits=0,
                       schedIterations=0, schedEventsFired=0,
                       arm9ExecNs=0, arm7ExecNs=0, gpu3dNs=0, runSystemNs=0,
                       arm9IdleHits=0, arm7IdleHits=0, arm7IdleSkips=0,
                       memBlock9HelperCalls=0, memRead9U32HelperCalls=0; } profTotals;
#endif

    for (int frame = 0; frame < opt.frames; frame++)
    {
        if (inputScript.Loaded())
            nds->SetKeyMask(inputScript.KeyMaskForFrame(frame));

        if (haveWindow && frame == opt.benchWindowStart)
            windowStart = std::chrono::steady_clock::now();

        LITE_PROFILE_RESET_FRAME();
        nds->RunFrame();

        if (haveWindow && frame == opt.benchWindowEnd)
            windowEnd = std::chrono::steady_clock::now();

        if (opt.dumpSavestateFrame == frame && !opt.dumpSavestatePath.empty())
        {
            if (!SaveSavestate(*nds, opt.dumpSavestatePath))
                fprintf(stderr, "warning: savestate dump failed at frame %d\n", frame);
        }

#if LITEV_PROFILE
        {
            using namespace melonDS::LiteProfile;
            profTotals.linksPatched    += g_Frame.LinksPatched.load(std::memory_order_relaxed);
            profTotals.linksUnlinked   += g_Frame.LinksUnlinked.load(std::memory_order_relaxed);
            profTotals.cppReentries    += g_Frame.CppReentries.load(std::memory_order_relaxed);
            profTotals.dispatcherMisses+= g_Frame.DispatcherMisses.load(std::memory_order_relaxed);
            profTotals.linkSitesEmitted += g_Frame.LinkSitesEmitted.load(std::memory_order_relaxed);
            profTotals.dispatchOnlyExits+= g_Frame.DispatchOnlyExits.load(std::memory_order_relaxed);
            profTotals.schedIterations  += g_Frame.SchedulerIterations.load(std::memory_order_relaxed);
            profTotals.schedEventsFired += g_Frame.SchedulerEventsFired.load(std::memory_order_relaxed);
            profTotals.arm9ExecNs   += g_Frame.ARM9ExecNs.load(std::memory_order_relaxed);
            profTotals.arm7ExecNs   += g_Frame.ARM7ExecNs.load(std::memory_order_relaxed);
            profTotals.gpu3dNs      += g_Frame.GPU3DNs.load(std::memory_order_relaxed);
            profTotals.runSystemNs  += g_Frame.RunSystemNs.load(std::memory_order_relaxed);
            profTotals.arm9IdleHits += g_Frame.ARM9IdleHits.load(std::memory_order_relaxed);
            profTotals.arm7IdleHits += g_Frame.ARM7IdleHits.load(std::memory_order_relaxed);
            profTotals.arm7IdleSkips+= g_Frame.ARM7IdleSkips.load(std::memory_order_relaxed);
            profTotals.memBlock9HelperCalls   += g_Frame.MemBlock9HelperCalls.load(std::memory_order_relaxed);
            profTotals.memRead9U32HelperCalls += g_Frame.MemRead9U32HelperCalls.load(std::memory_order_relaxed);
            uint64_t pk = g_Frame.PendingPeak.load(std::memory_order_relaxed);
            if (pk > profTotals.pendingPeak) profTotals.pendingPeak = pk;
        }
#endif

        // Drain the SPU output buffer produced this frame into the rolling hash.
        for (;;)
        {
            int got = nds->SPU.ReadOutput(audioDrain.data(), 2048);
            if (got <= 0) break;
            XXH3_64bits_update(audioHashState, audioDrain.data(),
                               (size_t)got * 2 * sizeof(s16));
            audioSampleCount += (u64)got;
            if (got < 2048) break;
        }

        void* top = nullptr;
        void* bot = nullptr;
        bool haveFb = nds->GPU.GetFramebuffers(&top, &bot);

        if (haveFb && top && bot)
        {
            u64 topHash = XXH3_64bits(top, kScreenBytes);
            u64 botHash = XXH3_64bits(bot, kScreenBytes);

            if (!haveFirst) { firstTopHash = topHash; haveFirst = true; }
            else if (topHash != firstTopHash) anyChange = true;

            lastTopHash = topHash;
            lastBotHash = botHash;

            if (opt.fbHashEvery > 0 && ((frame + 1) % opt.fbHashEvery) == 0)
            {
                printf("frame %d  top=%016llx  bottom=%016llx\n",
                       frame + 1, (unsigned long long)topHash, (unsigned long long)botHash);
                fflush(stdout);
            }

            if (opt.fbDumpFrame == frame && !opt.fbDumpPath.empty())
            {
                if (DumpPPM(opt.fbDumpPath, (const u32*)top, (const u32*)bot))
                    fprintf(stderr, "dumped frame %d to %s\n", frame, opt.fbDumpPath.c_str());
                else
                    fprintf(stderr, "warning: failed to dump PPM to %s\n", opt.fbDumpPath.c_str());
            }
        }
    }

    auto wallEnd = std::chrono::steady_clock::now();
    double wallSec = std::chrono::duration<double>(wallEnd - wallStart).count();
    double avgFps = wallSec > 0 ? opt.frames / wallSec : 0.0;

    // --bench-window: FPS over frames [start,end] inclusive only.
    int    windowFrames = 0;
    double windowSec = 0.0, windowFps = 0.0;
    if (haveWindow)
    {
        windowFrames = opt.benchWindowEnd - opt.benchWindowStart + 1;
        windowSec = std::chrono::duration<double>(windowEnd - windowStart).count();
        windowFps = windowSec > 0 ? windowFrames / windowSec : 0.0;
    }

    u64 audioHash = XXH3_64bits_digest(audioHashState);
    XXH3_freeState(audioHashState);

    printf("=== liteDS-headless summary ===\n");
    printf("mode:        %s\n", opt.jit ? "jit" : "interp");
    printf("frames:      %d\n", opt.frames);
    printf("wall_time_s: %.4f\n", wallSec);
    printf("avg_fps:     %.2f\n", avgFps);
    if (haveWindow)
    {
        printf("window:      %d:%d\n", opt.benchWindowStart, opt.benchWindowEnd);
        printf("window_frames: %d\n", windowFrames);
        printf("window_wall_s: %.4f\n", windowSec);
        printf("window_fps:  %.2f\n", windowFps);
    }
    printf("final_top:   %016llx\n", (unsigned long long)lastTopHash);
    printf("final_bot:   %016llx\n", (unsigned long long)lastBotHash);
    printf("fb_changing: %s\n", anyChange ? "yes" : "no");
    printf("audio_hash:  %016llx\n", (unsigned long long)audioHash);
    printf("audio_samples: %llu\n", (unsigned long long)audioSampleCount);
#if LITEV_PROFILE
    printf("links_patched:   %llu\n", (unsigned long long)profTotals.linksPatched);
    printf("links_unlinked:  %llu\n", (unsigned long long)profTotals.linksUnlinked);
    printf("pending_peak:    %llu\n", (unsigned long long)profTotals.pendingPeak);
    printf("cpp_reentries:   %llu\n", (unsigned long long)profTotals.cppReentries);
    printf("dispatcher_miss: %llu\n", (unsigned long long)profTotals.dispatcherMisses);
    printf("mem_block9_helper_calls:    %llu\n", (unsigned long long)profTotals.memBlock9HelperCalls);
    printf("mem_read9_u32_helper_calls: %llu\n", (unsigned long long)profTotals.memRead9U32HelperCalls);
    printf("link_sites_emitted:  %llu\n", (unsigned long long)profTotals.linkSitesEmitted);
    printf("dispatch_only_exits: %llu\n", (unsigned long long)profTotals.dispatchOnlyExits);
    printf("sched_iterations:    %llu\n", (unsigned long long)profTotals.schedIterations);
    printf("sched_events_fired:  %llu\n", (unsigned long long)profTotals.schedEventsFired);
    printf("sched_iters_per_frame: %.2f\n",
           opt.frames ? (double)profTotals.schedIterations / opt.frames : 0.0);
    printf("cpp_reentries_per_frame: %.2f\n",
           opt.frames ? (double)profTotals.cppReentries / opt.frames : 0.0);
    {
        uint64_t frameSumNs = profTotals.arm9ExecNs + profTotals.arm7ExecNs
                            + profTotals.gpu3dNs + profTotals.runSystemNs;
        double denom = frameSumNs ? (double)frameSumNs : 1.0;
        printf("arm9_exec_ns:    %llu\n", (unsigned long long)profTotals.arm9ExecNs);
        printf("arm7_exec_ns:    %llu\n", (unsigned long long)profTotals.arm7ExecNs);
        printf("gpu3d_ns:        %llu\n", (unsigned long long)profTotals.gpu3dNs);
        printf("run_system_ns:   %llu\n", (unsigned long long)profTotals.runSystemNs);
        printf("frame_decomp_sum_ns: %llu\n", (unsigned long long)frameSumNs);
        printf("arm9_share_pct:  %.2f\n", 100.0 * profTotals.arm9ExecNs / denom);
        printf("arm7_share_pct:  %.2f\n", 100.0 * profTotals.arm7ExecNs / denom);
        printf("gpu3d_share_pct: %.2f\n", 100.0 * profTotals.gpu3dNs / denom);
        printf("system_share_pct:%.2f\n", 100.0 * profTotals.runSystemNs / denom);
        printf("arm9_idle_hits:  %llu\n", (unsigned long long)profTotals.arm9IdleHits);
        printf("arm7_idle_hits:  %llu\n", (unsigned long long)profTotals.arm7IdleHits);
        printf("arm7_idle_skips: %llu\n", (unsigned long long)profTotals.arm7IdleSkips);
        printf("arm9_idle_hits_per_frame: %.2f\n",
               opt.frames ? (double)profTotals.arm9IdleHits / opt.frames : 0.0);
        printf("arm7_idle_hits_per_frame: %.2f\n",
               opt.frames ? (double)profTotals.arm7IdleHits / opt.frames : 0.0);
    }
#endif
    fflush(stdout);

    if (!opt.profileJson.empty())
    {
        FILE* jf = fopen(opt.profileJson.c_str(), "wb");
        if (jf)
        {
            fprintf(jf,
                "{\n"
                "  \"rom\": \"%s\",\n"
                "  \"mode\": \"%s\",\n"
                "  \"frames\": %d,\n"
                "  \"wall_time_s\": %.6f,\n"
                "  \"avg_fps\": %.4f,\n"
                "  \"window_start\": %d,\n"
                "  \"window_end\": %d,\n"
                "  \"window_frames\": %d,\n"
                "  \"window_wall_s\": %.6f,\n"
                "  \"window_fps\": %.4f,\n"
                "  \"final_top_hash\": \"%016llx\",\n"
                "  \"final_bottom_hash\": \"%016llx\",\n"
                "  \"framebuffer_changing\": %s,\n"
                "  \"audio_hash\": \"%016llx\",\n"
                "  \"audio_samples\": %llu\n"
#if LITEV_PROFILE
                "  ,\"links_patched\": %llu\n"
                "  ,\"links_unlinked\": %llu\n"
                "  ,\"pending_peak\": %llu\n"
                "  ,\"cpp_reentries\": %llu\n"
                "  ,\"dispatcher_misses\": %llu\n"
                "  ,\"sched_iterations\": %llu\n"
                "  ,\"sched_events_fired\": %llu\n"
                "  ,\"arm9_exec_ns\": %llu\n"
                "  ,\"arm7_exec_ns\": %llu\n"
                "  ,\"gpu3d_ns\": %llu\n"
                "  ,\"run_system_ns\": %llu\n"
                "  ,\"arm9_idle_hits\": %llu\n"
                "  ,\"arm7_idle_hits\": %llu\n"
                "  ,\"arm7_idle_skips\": %llu\n"
#endif
                "}\n",
                opt.rom.c_str(),
                opt.jit ? "jit" : "interp",
                opt.frames,
                wallSec,
                avgFps,
                opt.benchWindowStart,
                opt.benchWindowEnd,
                windowFrames,
                windowSec,
                windowFps,
                (unsigned long long)lastTopHash,
                (unsigned long long)lastBotHash,
                anyChange ? "true" : "false",
                (unsigned long long)audioHash,
                (unsigned long long)audioSampleCount
#if LITEV_PROFILE
                , (unsigned long long)profTotals.linksPatched
                , (unsigned long long)profTotals.linksUnlinked
                , (unsigned long long)profTotals.pendingPeak
                , (unsigned long long)profTotals.cppReentries
                , (unsigned long long)profTotals.dispatcherMisses
                , (unsigned long long)profTotals.schedIterations
                , (unsigned long long)profTotals.schedEventsFired
                , (unsigned long long)profTotals.arm9ExecNs
                , (unsigned long long)profTotals.arm7ExecNs
                , (unsigned long long)profTotals.gpu3dNs
                , (unsigned long long)profTotals.runSystemNs
                , (unsigned long long)profTotals.arm9IdleHits
                , (unsigned long long)profTotals.arm7IdleHits
                , (unsigned long long)profTotals.arm7IdleSkips
#endif
                );
            fclose(jf);
            fprintf(stderr, "wrote profile json: %s\n", opt.profileJson.c_str());
        }
        else
        {
            fprintf(stderr, "warning: could not write profile json '%s'\n", opt.profileJson.c_str());
        }
    }

    return 0;
}
