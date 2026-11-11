// ============================================================================
// SDKBootstrap.cpp — SDK::Init for Android (ARM64): version detection +
// the core offsets (GObjects/ProcessEvent/Realloc/StaticFindObject/...) via
// baked fast-path (android/Generated/sdk-<ver>.h) then universal runtime
// chains (string anchors, wrapper scans, ADRP extraction) for ANY version.
//
// Chain identities all verified against 11 real binaries (see
// android/erbium/bake.py + the mining data in the Erbium-Android repo).
// ============================================================================
#include "pch-android.h"
#include "RT.h"
#include <dlfcn.h>

// ─── baked selection ─────────────────────────────────────────────────────────
#ifndef ERBIUM_TARGET_VERSION
#define ERBIUM_TARGET_VERSION 21.30
#endif
#define ERBIUM_PP_STR2(x) #x
#define ERBIUM_PP_STR(x) ERBIUM_PP_STR2(x)
#include ERBIUM_PP_STR(../Generated/sdk-ERBIUM_TARGET_VERSION.h)

namespace SDK
{
    // ─── helpers over the RT engine ─────────────────────────────────────────
    static uint64_t BlTarget(uint64_t va)
    {
        uint32_t w = *(uint32_t*)va;
        if ((w & 0xFC000000) == 0x94000000)
        {
            int64_t imm = (int64_t)(w & 0x03FFFFFF);
            if (imm & (1LL << 25))
                imm -= (1LL << 26);
            return va + (imm << 2);
        }
        return 0;
    }

    static bool IsExternalPlt(uint64_t va)
    {
        // veneer: adrp x16,..; ldr x17,[x16,#off]; add x16,x16,#off; br x17
        uint32_t w1 = *(uint32_t*)va, w2 = *(uint32_t*)(va + 4);
        if ((w1 & 0x9F000000) != 0x90000000 || (w2 & 0xFFC00000) != 0xF9400000)
            return false;
        if ((w1 & 31) != 16 || ((w2 >> 5) & 31) != 16)
            return false;
        uint32_t w4 = *(uint32_t*)(va + 12);
        return w4 == 0xD61F0220; // br x17
    }

    // args x0/x1/w2 read by MOV sources in the prologue (wrapper discriminator)
    static int ArgsReadCount(uint64_t fn, int maxInsns = 14)
    {
        int mask = 0;
        for (int i = 0; i < maxInsns; i++)
        {
            uint32_t w = *(uint32_t*)(fn + i * 4);
            if ((w & 0xFFE0FFE0) == 0x2A0003E0 || (w & 0xFFE0FFE0) == 0xAA0003E0) // MOV Wd,Wm / Xd,Xm
            {
                uint32_t m = (w >> 16) & 31;
                if (m < 3)
                    mask |= (1 << m);
            }
        }
        int c = 0;
        for (int b = 0; b < 3; b++)
            if (mask & (1 << b))
                c++;
        return c;
    }

    // FMemory wrapper shape: ADRP+LDR global → chained derefs → vtable → slot → BR
    static uint64_t WrapperGlobal(uint64_t fn, uint32_t* slotOut = nullptr)
    {
        uint32_t sz = 0;
        for (uint32_t s = 8; s < 0x400; s <<= 1)
        {
            // approximate size: stop at first RET after start
            uint32_t off = 0;
            for (off = 0; off < 0x100; off += 4)
            {
                uint32_t w = *(uint32_t*)(fn + off);
                if (w == 0xD65F03C0)
                {
                    sz = off + 4;
                    break;
                }
            }
            if (sz)
                break;
        }
        if (!sz || sz > 0x100)
            return 0;

        for (uint32_t off = 0; off + 4 <= sz && off < 0x28; off += 4)
        {
            uint32_t w1 = *(uint32_t*)(fn + off);
            if ((w1 & 0x9F000000) != 0x90000000)
                continue;
            uint32_t rd = w1 & 31;
            uint64_t immlo = (w1 >> 29) & 3, immhi = (w1 >> 5) & 0x7FFFF;
            int64_t imm = (int64_t)((immhi << 2) | immlo);
            if (imm & (1LL << 20))
                imm -= (1LL << 21);
            uint64_t page = (fn + off) & ~0xFFFULL;
            page += (imm << 12);

            // find LDR Xt,[rd,#imm] within 4 insns
            uint32_t rt = 0xFF;
            for (uint32_t k = 1; k <= 4; k++)
            {
                uint32_t w2 = *(uint32_t*)(fn + off + k * 4);
                if ((w2 & 0xFFC00000) == 0xF9400000 && ((w2 >> 5) & 31) == rd)
                {
                    uint64_t gm = page + ((((w2 >> 10) & 0xFFF) << 3));
                    rt = w2 & 31;
                    if (gm & 7 || !RT::InData(gm))
                        break;
                    // walk the chain: up to two offset-0 derefs, then vtable+slot+BR
                    uint64_t cur = rt;
                    struct LD { uint32_t off, base, rt, imm; };
                    LD ldrs[16]; int nl = 0;
                    for (uint32_t j = off + 8; j + 4 <= sz && j < off + 0x38 && nl < 16; j += 4)
                    {
                        uint32_t w = *(uint32_t*)(fn + j);
                        if ((w & 0xFFC00000) == 0xF9400000)
                            ldrs[nl++] = {j, (w >> 5) & 31, w & 31, (w >> 10) & 0xFFF};
                    }
                    for (int a = 0; a < nl; a++)
                    {
                        if (ldrs[a].imm != 0 || ldrs[a].base != cur)
                            continue;
                        // candidate vtable reg = ldrs[a].rt
                        for (int b = a + 1; b < nl; b++)
                        {
                            if (ldrs[b].base == ldrs[a].rt && ldrs[b].imm != 0)
                            {
                                uint32_t slotOff = ldrs[b].imm << 3;
                                if (slotOff >= 0x10 && slotOff <= 0x60)
                                {
                                    for (uint32_t q = ldrs[b].off + 4; q + 4 <= sz; q += 4)
                                    {
                                        uint32_t wb = *(uint32_t*)(fn + q);
                                        if (wb == (0xD61F0000 | (ldrs[b].rt << 5))) // BR Xrt
                                        {
                                            if (slotOut)
                                                *slotOut = slotOff;
                                            return gm;
                                        }
                                    }
                                }
                            }
                        }
                        cur = ldrs[a].rt;
                    }
                    break;
                }
                if ((w2 & 31) == rd && (w2 & 0xFFC00000) != 0xF9400000)
                    break; // clobbered
            }
        }
        return 0;
    }

    // GObjects from StaticFindObject: (ADRP+ADD→G, LDR Xt,[G+off]) → G+off
    static uint64_t GObjectsFromFunc(uint64_t fn, int depth = 1)
    {
        if (!fn)
            return 0;
        uint64_t best = 0;
        int bestHits = 0;
        for (uint64_t f = fn; f && depth >= 0; depth--)
        {
            uint32_t off = 0;
            for (off = 0; off < 0x400; off += 4)
            {
                uint32_t w1 = *(uint32_t*)(f + off);
                if ((w1 & 0x9F000000) != 0x90000000)
                    continue;
                uint32_t rd = w1 & 31;
                uint64_t immlo = (w1 >> 29) & 3, immhi = (w1 >> 5) & 0x7FFFF;
                int64_t imm = (int64_t)((immhi << 2) | immlo);
                if (imm & (1LL << 20))
                    imm -= (1LL << 21);
                uint64_t page = (f + off) & ~0xFFFULL;
                page += (imm << 12);
                for (uint32_t k = 1; k <= 4; k++)
                {
                    uint32_t w2 = *(uint32_t*)(f + off + k * 4);
                    if ((w2 & 0xFF800000) == 0x91000000 && ((w2 >> 5) & 31) == rd && (w2 & 31) == rd)
                    {
                        uint64_t G = page + ((w2 >> 10) & 0xFFF);
                        for (uint32_t j = 1; j <= 8; j++)
                        {
                            uint32_t w3 = *(uint32_t*)(f + off + (k + j) * 4);
                            if ((w3 & 0xFFC00000) == 0xF9400000 && ((w3 >> 5) & 31) == rd)
                            {
                                uint64_t slot = G + ((((w3 >> 10) & 0xFFF) << 3));
                                if ((slot & 7) == 0 && RT::InData(slot) && slot != best)
                                {
                                    if (slot == best)
                                        bestHits++;
                                    else
                                    {
                                        best = slot;
                                        bestHits = 1;
                                    }
                                }
                                break;
                            }
                            if ((w3 & 31) == rd && (w3 & 0xFFC00000) != 0xF9400000)
                                break;
                        }
                        break;
                    }
                    if ((w2 & 31) == rd && (w2 & 0xFF800000) != 0x91000000)
                        break;
                }
            }
            if (best)
                return best;
            // try the first BL callee (StaticFindObjectFast outline)
            if (depth > 0)
            {
                uint64_t callee = 0;
                for (uint32_t o = 0; o < 0x100; o += 4)
                {
                    uint64_t t = BlTarget(f + o);
                    if (t && RT::InText(t) && !IsExternalPlt(t))
                    {
                        callee = t;
                        break;
                    }
                }
                f = callee;
            }
        }
        return 0;
    }

    // GEngine from the exported JNI console-command entry (3 shapes).
    static uint64_t GEngineFromJNI()
    {
        static const char* kSyms[] = {
            "Java_com_epicgames_unreal_GameActivity_nativeConsoleCommand",
            "Java_com_epicgames_ue4_GameActivity_nativeConsoleCommand",
        };
        for (auto sym : kSyms)
        {
            uint64_t fn = (uint64_t)dlsym(RTLD_DEFAULT, sym);
            if (!fn)
                continue;
            for (int i = 0; i < 40; i++)
            {
                uint64_t va = fn + i * 4;
                uint32_t w1 = *(uint32_t*)va;
                if ((w1 & 0x9F000000) != 0x90000000)
                    continue;
                uint32_t rd = w1 & 31;
                uint64_t immlo = (w1 >> 29) & 3, immhi = (w1 >> 5) & 0x7FFFF;
                int64_t imm = (int64_t)((immhi << 2) | immlo);
                if (imm & (1LL << 20))
                    imm -= (1LL << 21);
                uint64_t page = va & ~0xFFFULL;
                page += (imm << 12);
                uint32_t w2 = *(uint32_t*)(va + 4);
                if ((w2 & 0xFFC00000) == 0xF9400000 && ((w2 >> 5) & 31) == rd)
                {
                    uint64_t slot = page + ((((w2 >> 10) & 0xFFF) << 3));
                    uint32_t w3 = *(uint32_t*)(va + 8);
                    // GOT-indirect: ldr Xt2,[rt] follows → slot holds &GEngine
                    if ((w3 & 0xFFC00000) == 0xF9400000 && (w3 & 31) == (w2 & 31) && ((w3 >> 5) & 31) == (w2 & 31))
                    {
                        uint64_t inner = *(uint64_t*)slot; // resolved by the linker at load
                        if (inner && RT::InData(inner))
                            return inner;
                    }
                    // direct: [slot] = GEngine
                    if (RT::InData(slot))
                        return slot;
                }
                // ADRP+ADD then LDR[rd]/CBZ
                if ((w2 & 0xFF800000) == 0x91000000 && ((w2 >> 5) & 31) == rd && (w2 & 31) == rd)
                {
                    uint64_t G = page + ((w2 >> 10) & 0xFFF);
                    uint32_t w3 = *(uint32_t*)(va + 8);
                    if (((w3 & 0xFFC00000) == 0xF9400000 && ((w3 >> 5) & 31) == rd && ((w3 >> 10) & 0xFFF) == 0) ||
                        (w3 & 0x7F000000) == 0x34000000 || (w3 & 0x7F000000) == 0x35000000)
                        return G;
                }
            }
        }
        return 0;
    }

    // ─── version detection ───────────────────────────────────────────────────
    static bool DetectVersion()
    {
        // "++Fortnite+Release-XX.XX-CL-YYYYYYY" in module memory
        const char* found = (const char*)RT::FindAStr("++Fortnite+Release-");
        if (!found)
            return false;

        // parse EngineVersion-CL
        const char* p = found + 19;
        double engine = 0;
        int major = 0, minor = 0;
        if (sscanf(p, "%d.%d", &major, &minor) == 2)
            engine = major + minor / 10.0;
        const char* cl = strstr(p, "-CL-");
        if (!cl)
            return false;
        unsigned long long clNum = strtoull(cl + 4, nullptr, 10);

        // CL → Fortnite version (same mapping as Erbium's Windows Init)
        double fortnite = 0;
        if (clNum < 3901517)
        {
            switch (clNum)
            {
            case 3668626: fortnite = 1.64; break;
            case 3681159: fortnite = 1.7; break;
            case 3700114: fortnite = 1.72; break;
            case 3724489: fortnite = 1.8; break;
            case 3729133: fortnite = 1.81; break;
            case 3741772: fortnite = 1.82; break;
            case 3757339: fortnite = 1.9; break;
            case 3775276: fortnite = 1.91; break;
            case 3790078: fortnite = 1.10; break;
            case 3807424: fortnite = 1.11; break;
            case 3825894: fortnite = 2.1; break;
            case 3841827: fortnite = 2.2; break;
            case 3847564: fortnite = 2.3; break;
            case 3858292: fortnite = 2.4; break;
            case 3870737: fortnite = 2.42; break;
            case 3889387: fortnite = 2.5; break;
            default: fortnite = 0; break;
            }
        }
        else
        {
            const char* v = strrchr(found, '-');
            if (v)
                fortnite = atof(v + 1);
        }
        if (!fortnite)
            return false;

        if (engine == 4.261) // 4.26.1 maps to 4.27
            engine = 4.27;

        VersionInfo.EngineVersion = engine;
        VersionInfo.FortniteVersion = fortnite;
        return true;
    }

    static bool BakedMatches()
    {
        return fabs(VersionInfo.FortniteVersion - ERBIUM_BAKED_VERSION) < 0.001;
    }

    // ─── SDK::Init (Android) ────────────────────────────────────────────────
    void Init()
    {
        RT::Init();

        if (!DetectVersion())
        {
            LOGE("SDK: version detection FAILED — no '++Fortnite+Release-' string in module");
            return;
        }
        LOGI("SDK: FN %.2f UE %.1f (baked %.2f %s)", VersionInfo.FortniteVersion,
             VersionInfo.EngineVersion, (double)ERBIUM_BAKED_VERSION, BakedMatches() ? "MATCH" : "chain-mode");

        const bool baked = BakedMatches();
        uint64_t pe = 0, sfo = 0, slo = 0, spawn = 0, fnameCtor = 0, appendStr = 0, reallocW = 0;

        // ProcessEvent — pattern (baked) or "AccessNoneNoContext" string chain
        if (baked && ErbiumBaked::kProcessEvent)
            pe = ImageBase + ErbiumBaked::kProcessEvent;
        if (!pe)
            pe = RT::StringAnchorFunc(L"AccessNoneNoContext");

        // StaticLoadObject — "STAT_LoadObject" family
        if (baked && ErbiumBaked::kStaticLoadObject)
            slo = ImageBase + ErbiumBaked::kStaticLoadObject;
        if (!slo)
        {
            const wchar_t* anchors[] = {L"STAT_LoadObject",
                                        L"Calling StaticLoadObject during PostLoad may result in hitches during streaming."};
            for (auto a : anchors)
            {
                slo = RT::StringAnchorFunc(a);
                if (slo)
                    break;
            }
        }

        // StaticFindObject — "Illegal call..." chain (≤UE5.0) / SLO-BL (5.1+)
        if (baked && ErbiumBaked::kStaticFindObject)
            sfo = ImageBase + ErbiumBaked::kStaticFindObject;
        if (!sfo)
            sfo = RT::StringAnchorFunc(L"Illegal call to StaticFindObject() while serializing object data!");
        if (!sfo && slo)
        {
            // early BL in SLO to a mid-size function = StaticFindObject
            for (uint32_t o = 0; o < 0x200 && !sfo; o += 4)
            {
                uint64_t t = BlTarget(slo + o);
                if (!t || !RT::InText(t) || IsExternalPlt(t))
                    continue;
                // size proxy: distance to next RET
                uint32_t sz = 0;
                for (uint32_t q = 0; q < 0x3000; q += 4)
                {
                    if (*(uint32_t*)(t + q) == 0xD65F03C0)
                    {
                        sz = q + 4;
                        break;
                    }
                }
                if (sz > 0x150 && sz < 0x3000)
                    sfo = t;
            }
        }

        // SpawnActor — stat / failure string inside
        if (baked && ErbiumBaked::kSpawnActor)
            spawn = ImageBase + ErbiumBaked::kSpawnActor;
        if (!spawn)
        {
            const wchar_t* anchors[] = {L"STAT_SpawnActorTime", L"SpawnActor failed because no class was specified"};
            for (auto a : anchors)
            {
                spawn = RT::StringAnchorFunc(a);
                if (spawn)
                    break;
            }
        }

        // FNameConstructor — "ClientIgnoreLookInput" → first non-PLT BL
        if (baked && ErbiumBaked::kFNameConstructor)
            fnameCtor = ImageBase + ErbiumBaked::kFNameConstructor;
        if (!fnameCtor)
        {
            uint64_t ref = RT::FirstStringRef(L"ClientIgnoreLookInput");
            if (ref)
            {
                for (uint32_t o = 4; o < 0x200; o += 4)
                {
                    uint64_t t = BlTarget(ref + o);
                    if (t && RT::InText(t) && !IsExternalPlt(t))
                    {
                        fnameCtor = t;
                        break;
                    }
                }
            }
        }

        // AppendString — "ForwardShadingQuality_" → BL with NamePool shape
        if (baked && ErbiumBaked::kAppendString)
            appendStr = ImageBase + ErbiumBaked::kAppendString;
        if (!appendStr)
        {
            uint64_t ref = RT::FirstStringRef(L"ForwardShadingQuality_");
            if (ref)
            {
                for (uint32_t o = 4; o < 0x300; o += 4)
                {
                    uint64_t t = BlTarget(ref + o);
                    if (!t || !RT::InText(t) || IsExternalPlt(t))
                        continue;
                    // NamePool shape: LSR #13/16 + LDR [x,#0x30..0x60]
                    bool hasLsr = false, hasLdr = false;
                    for (uint32_t q = 0; q < 0x100; q += 4)
                    {
                        uint32_t w = *(uint32_t*)(t + q);
                        if (w == 0xD65F03C0)
                            break;
                        if ((w & 0xFF800000) == 0xD3000000 || (w & 0xFF800000) == 0x13000000)
                        {
                            uint32_t immr = (w >> 16) & 0x3F;
                            if (immr == 13 || immr == 16)
                                hasLsr = true;
                        }
                        if ((w & 0xFFC00000) == 0xF9400000)
                        {
                            uint32_t offv = ((w >> 10) & 0xFFF) << 3;
                            if (offv >= 0x30 && offv <= 0x60)
                                hasLdr = true;
                        }
                    }
                    if (hasLsr && hasLdr)
                    {
                        appendStr = t;
                        break;
                    }
                }
            }
        }

        // Realloc — wrapper scan seeded from AppendString's callee tree
        if (baked && ErbiumBaked::kRealloc)
            reallocW = ImageBase + ErbiumBaked::kRealloc;
        if (!reallocW)
        {
            uint64_t seed = appendStr;
            // BFS depth ≤3 over callees (skipping huge functions + PLT)
            uint64_t queue[64]; int depth[64]; int qh = 0, qt = 0;
            if (seed)
            {
                queue[qt] = seed;
                depth[qt] = 0;
                qt++;
            }
            uint64_t seen[128]; int nseen = 0;
            while (qh < qt && reallocW == 0)
            {
                uint64_t fn = queue[qh]; int d = depth[qh]; qh++;
                bool visited = false;
                for (int i = 0; i < nseen; i++)
                    if (seen[i] == fn)
                        visited = true;
                if (visited || d > 3)
                    continue;
                if (nseen < 128)
                    seen[nseen++] = fn;

                uint32_t slot = 0;
                uint64_t gm = WrapperGlobal(fn, &slot);
                if (gm)
                {
                    // candidate: FMemory wrapper group on gm.
                    // Realloc wrapper reads 3 args; collect and decide below.
                    // (store first 3-arg hit)
                    if (ArgsReadCount(fn) >= 3)
                        reallocW = fn;
                }
                // expand callees if function is small
                uint32_t sz = 0;
                for (uint32_t q = 0; q < 0x8000; q += 4)
                {
                    if (*(uint32_t*)(fn + q) == 0xD65F03C0)
                    {
                        sz = q + 4;
                        break;
                    }
                }
                if (sz && sz <= 0x4000)
                {
                    for (uint32_t o = 0; o < sz && qt < 64; o += 4)
                    {
                        uint64_t t = BlTarget(fn + o);
                        if (!t || !RT::InText(t) || IsExternalPlt(t))
                            continue;
                        bool dup = false;
                        for (int i = 0; i < qt; i++)
                            if (queue[i] == t)
                                dup = true;
                        if (!dup)
                        {
                            queue[qt] = t;
                            depth[qt] = d + 1;
                            qt++;
                        }
                    }
                }
            }
        }

        // commit offsets
        Offsets::ProcessEventVft = 0;
        if (pe)
            InitializeProcessEventVft(pe);
        else
            LOGE("SDK: ProcessEvent NOT FOUND — SDK calls will crash!");

        Offsets::Realloc = reallocW;
        Offsets::AppendString = appendStr;
        Offsets::ToString = 0; // AppendString path preferred on Android
        Offsets::StaticFindObject = sfo;
        Offsets::StaticLoadObject = slo;
        Offsets::SpawnActor = spawn;
        Offsets::FNameConstructor = fnameCtor;

        // GObjects from SFO (or baked)
        uint64_t gobjs = 0;
        if (baked && ErbiumBaked::kGObjects)
            gobjs = ImageBase + ErbiumBaked::kGObjects;
        if (!gobjs)
            gobjs = GObjectsFromFunc(sfo);
        if (VersionInfo.EngineVersion >= 4.21)
            Offsets::GObjectsChunked = gobjs;
        else
            Offsets::GObjectsUnchunked = gobjs;
        if (gobjs)
            LOGI("SDK: GObjects = %llx", (unsigned long long)gobjs);
        else
            LOGE("SDK: GObjects NOT FOUND!");

        // structural field offsets (identical across ABIs — v3-verified)
        Offsets::Offset_Internal = VersionInfo.FortniteVersion >= 12.10 && VersionInfo.FortniteVersion < 20 ? 0x4c : (VersionInfo.FortniteVersion >= 24.30 ? 0x3c : 0x44);
        Offsets::PropertyFlags = Offsets::Offset_Internal - 0xc;
        Offsets::ElementSize = Offsets::Offset_Internal - 0x10;
        Offsets::PropertiesSize = VersionInfo.FortniteVersion >= 12.10 ? 0x58 : (VersionInfo.EngineVersion >= 4.22 ? 0x50 : 0x40);
        Offsets::Super = VersionInfo.EngineVersion >= 4.22 ? 0x40 : 0x30;
        Offsets::FieldMask = VersionInfo.FortniteVersion >= 24.30 ? 0x6b : (VersionInfo.FortniteVersion >= 12.10 && VersionInfo.FortniteVersion < 20 ? 0x7b : 0x73);
        Offsets::Children = VersionInfo.EngineVersion >= 4.22 ? 0x48 : 0x38;
        Offsets::FField_Next = VersionInfo.FortniteVersion >= 24.30 ? 0x18 : 0x20;
        Offsets::FField_Name = VersionInfo.FortniteVersion >= 24.30 ? 0x20 : 0x28;
        Offsets::FFrame_PropertyChainForCompiledIn = VersionInfo.FortniteVersion >= 20.20 ? 0x88 : 0x80;
        Offsets::FFrame_CurrentNativeFunction = VersionInfo.FortniteVersion >= 20.20 ? 0x90 : 0x88;
        Offsets::FFrame_Next = VersionInfo.FortniteVersion >= 24.30 ? 0x18 : (VersionInfo.FortniteVersion >= 12.10 ? 0x20 : 0x28);

        if (VersionInfo.EngineVersion < 4.22)
            Offsets::ExecFunction = 0xB0;
        else if (VersionInfo.EngineVersion < 4.25)
            Offsets::ExecFunction = 0xC0;
        else if (VersionInfo.FortniteVersion >= 12.00 && VersionInfo.FortniteVersion < 12.10)
            Offsets::ExecFunction = 0xC8;
        else if (VersionInfo.FortniteVersion >= 12.10 && VersionInfo.FortniteVersion <= 12.61)
            Offsets::ExecFunction = 0xF0;
        else
            Offsets::ExecFunction = 0xD8;

        // report
        LOGI("SDK: PE=%llx SFO=%llx SLO=%llx Spawn=%llx FNameCtor=%llx AppendStr=%llx Realloc=%llx",
             (unsigned long long)pe, (unsigned long long)sfo, (unsigned long long)slo,
             (unsigned long long)spawn, (unsigned long long)fnameCtor,
             (unsigned long long)appendStr, (unsigned long long)reallocW);

        if (VersionInfo.FortniteVersion <= 6.01)
            UpdateNumElemsPerChunk();
    }
} // namespace SDK
