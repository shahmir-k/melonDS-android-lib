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

using namespace melonDS;

namespace {

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
    int fbHashEvery = 0;                // 0 => only final hash
    int fbDumpFrame = -1;               // frame index to dump, -1 => none
    std::string fbDumpPath;
};

[[noreturn]] void Usage(const char* argv0, int code)
{
    fprintf(stderr,
        "liteDS-headless - headless melonDS benchmark runner (liteDS-v2 Unit 0)\n"
        "Usage: %s --rom <path> [options]\n"
        "  --rom <path>              DS ROM to run (required)\n"
        "  --savestate <path>        load a savestate after boot (optional)\n"
        "  --frames N                number of frames to run (default 300)\n"
        "  --mode jit|interp         execution mode (default jit)\n"
        "  --fb-hash-every N         print xxhash of both framebuffers every N frames\n"
        "  --fb-dump-ppm <f>:<path>  dump both framebuffers at frame f as PPM (side by side)\n"
        "  --profile-json <path>     write per-run totals as JSON\n"
        "  --data-dir <path>         local firmware/save directory (default ./headless-data)\n",
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
        else if (a == "--frames") o.frames = std::atoi(next("--frames").c_str());
        else if (a == "--mode")
        {
            std::string m = next("--mode");
            if (m == "jit") o.jit = true;
            else if (m == "interp") o.jit = false;
            else { fprintf(stderr, "error: --mode must be jit or interp\n"); return false; }
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
        else if (a == "--profile-json") o.profileJson = next("--profile-json");
        else if (a == "--data-dir") o.dataDir = next("--data-dir");
        else if (a == "--help" || a == "-h") Usage(argv[0], 0);
        else { fprintf(stderr, "error: unknown argument '%s'\n", a.c_str()); return false; }
    }

    if (o.rom.empty()) { fprintf(stderr, "error: --rom is required\n"); return false; }
    if (o.frames <= 0) { fprintf(stderr, "error: --frames must be positive\n"); return false; }
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
#ifdef JIT_ENABLED
    if (opt.jit)
        args.JIT = JITArgs{};       // default JIT settings
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

    auto wallStart = std::chrono::steady_clock::now();

    for (int frame = 0; frame < opt.frames; frame++)
    {
        LITE_PROFILE_RESET_FRAME();
        nds->RunFrame();

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

    printf("=== liteDS-headless summary ===\n");
    printf("mode:        %s\n", opt.jit ? "jit" : "interp");
    printf("frames:      %d\n", opt.frames);
    printf("wall_time_s: %.4f\n", wallSec);
    printf("avg_fps:     %.2f\n", avgFps);
    printf("final_top:   %016llx\n", (unsigned long long)lastTopHash);
    printf("final_bot:   %016llx\n", (unsigned long long)lastBotHash);
    printf("fb_changing: %s\n", anyChange ? "yes" : "no");
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
                "  \"final_top_hash\": \"%016llx\",\n"
                "  \"final_bottom_hash\": \"%016llx\",\n"
                "  \"framebuffer_changing\": %s\n"
                "}\n",
                opt.rom.c_str(),
                opt.jit ? "jit" : "interp",
                opt.frames,
                wallSec,
                avgFps,
                (unsigned long long)lastTopHash,
                (unsigned long long)lastBotHash,
                anyChange ? "true" : "false");
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
