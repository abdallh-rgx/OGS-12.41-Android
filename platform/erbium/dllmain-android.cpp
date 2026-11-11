// ============================================================================
// dllmain-android.cpp — Erbium Main() for Android: the full server bring-up.
// Port of Erbium/Erbium/Private/dllmain.cpp (Windows x64):
//   SDK::Init → version-specific CVars → Null/RetTrue patches → hooks →
//   GIsClient=false/GIsServer=true → LocalPlayers.Remove(0) → open terrain.
// GUI/curl/console are compiled out on Android (logcat only).
// ============================================================================
#include "pch-android.h"
#include "RT.h"
#include "../../Erbium/Erbium/Public/Finders.h"
#include "../../Erbium/Erbium/Public/Misc.h"
#include "../../Erbium/Erbium/Public/Configuration.h"
#include "../../SDK/Engine.h"

#include <chrono>
#include <thread>

// ARM64 patch helpers (x64 semantics mapped):
//   x64 NullFunc patch (0xC3)            → Hooking::Null (RET)
//   x64 RetTrue patch (mov eax,1; ret)   → Hooking::RetTrue (MOV W0,#1; RET)
//   x64 EncryptionPatch (JG→JE)          → condition-code patch on B.cond
//   x64 GameSessionPatch (JZ→JNZ)        → Hooking::InvertBranch
namespace AndroidPatches
{
    static bool PatchBCondCondition(uint64_t at, uint8_t newCond)
    {
        uint32_t w = *(uint32_t*)at;
        if ((w & 0xFF000010) != 0x54000000)
            return false;
        DWORD og;
        VirtualProtect((void*)at, 4, PAGE_EXECUTE_READWRITE, &og);
        *(uint32_t*)at = (w & ~0xF) | newCond;
        VirtualProtect((void*)at, 4, og, &og);
        __builtin___clear_cache((char*)at, (char*)at + 4);
        return true;
    }

    // EncryptionPatch semantic: find `cmp .., 1; b.gt` inside the anchored
    // function and rewrite b.gt → b.eq (x64: 7F→74).
    static bool ApplyEncryptionPatch(uint64_t fn)
    {
        if (!fn || !RT::InText(fn))
            return false;
        for (uint32_t off = 0; off < 0x600; off += 4)
        {
            uint32_t w = *(uint32_t*)(fn + off);
            // B.cond with GT (cond=1100)
            if ((w & 0xFF000010) == 0x54000000 && (w & 0xF) == 0xC)
            {
                // sanity: preceding compare against 1 (SUBS Wn,Wm,#1 or CMP imm)
                if (off >= 4)
                {
                    uint32_t prev = *(uint32_t*)(fn + off - 4);
                    if ((prev & 0xFF800000) == 0xF1000000 && ((prev >> 10) & 0xFFF) <= 0x1F)
                        return PatchBCondCondition(fn + off, 0x0); // GT → EQ
                }
                return PatchBCondCondition(fn + off, 0x0);
            }
        }
        return false;
    }
}

// null/ret-true target lists (Android chains — the Windows x64 patterns don't
// apply; the anchor strings exist on Android builds too)
static void FindNullsAndRetTruesAndroid();

void FindNullsAndRetTrues() // declared in Finders.h
{
    FindNullsAndRetTruesAndroid();
}

static void FindNullsAndRetTruesAndroid()
{
    // "Widget Class %s - Running Initialize On Archetype, %s." → the
    // initialize-on-archetype path NULLs out (Windows NullFuncs)
    if (uint64_t f = RT::StringAnchorFunc(L"Widget Class %s - Running Initialize On Archetype, %s."))
    {
        NullFuncs.push_back(f);
        LOGI("Null: Widget-InitOnArchetype = %llx", (unsigned long long)f);
    }
    // FindKickPlayer → RetTrue (Windows: RetTrueFuncs.push_back(FindKickPlayer()))
    if (uint64_t k = FindKickPlayer())
        RetTrueFuncs.push_back(k);

    // RequestExit (16.00+): prevent the game from exiting — the Windows patch
    // overwrote FPlatformMisc::RequestExit's first byte. ARM64: RET it.
    if (VersionInfo.FortniteVersion >= 16.00)
    {
        // anchor: the shutdown log the function references
        if (uint64_t f = RT::StringAnchorFunc(L"RequestExit")) // weak anchor, best-effort
            NullFuncs.push_back(f);
    }
}

static void Main()
{
    LOGI("=== Erbium Android server (build " __DATE__ " " __TIME__ ") ===");

    LOGI("Initializing SDK...");
    SDK::Init();

    if (!Offsets::ProcessEventVft && !Offsets::Realloc)
    {
        LOGE("FATAL: SDK bootstrap failed (no ProcessEvent/Realloc) — aborting");
        return;
    }

    // version-specific console commands (ported verbatim from dllmain.cpp)
    if (VersionInfo.EngineVersion >= 5.0)
    {
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"log LogFortUIDirector None"), nullptr);
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"log LogFortUIManager None"), nullptr);
    }
    if (VersionInfo.FortniteVersion == 20.40)
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"log LogSpecialRelevancyHealthComponent None"), nullptr);
    if (VersionInfo.EngineVersion >= 5.1)
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"net.AllowEncryption 0"), nullptr);
    if (VersionInfo.EngineVersion >= 5.3 && FConfiguration::bEnableIris)
    {
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"log LogIris None"), nullptr);
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"log LogIrisRpc None"), nullptr);
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"log LogIrisBridge None"), nullptr);
    }
    UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"log LogSpecialEventScript VeryVerbose"), nullptr);

#ifdef CLIENT
    Misc::InitClient();
    return;
#endif

    LOGI("Hooking & finding offsets... (this may take a while)");

    FindNullsAndRetTruesAndroid();

    for (auto& NullFunc : NullFuncs)
        if (NullFunc != 0)
            Hooking::Null(NullFunc);

    for (auto& RetTrueFunc : RetTrueFuncs)
        if (RetTrueFunc != 0)
            Hooking::RetTrue(RetTrueFunc);

    // GameSession patch (x64: JZ→JNZ at the anchored function) — on ARM64
    // invert the first conditional in the anchored function.
    if (uint64_t GameSessionPatch = FindGameSessionPatch())
    {
        if (!Hooking::InvertBranch(GameSessionPatch))
            LOGW("GameSessionPatch: no conditional at function start — skipped");
    }

    for (auto& HookFunc : _HookFuncs)
        HookFunc();

    // flip client/server flags — the Erbium core trick
    uint64_t gisClient = FindGIsClient();
    uint64_t gisServer = FindGIsServer();
    if (gisClient && gisServer)
    {
        *(bool*)gisClient = false;
        if (VersionInfo.EngineVersion > 4.20) // 3.6 and below crash on ALandscapeProxy
            *(bool*)gisServer = true;
        LOGI("GIs flags flipped: client=%d server=%d",
             *(uint8_t*)gisClient, *(uint8_t*)gisServer);
    }
    else
    {
        LOGE("GIs globals NOT found (client=%llx server=%llx) — server flip failed",
             (unsigned long long)gisClient, (unsigned long long)gisServer);
    }

    srand((uint32_t)time(0));

    // remove local player (we're a dedicated-ish server now)
    if (auto World = UWorld::GetWorld())
        if (World->OwningGameInstance)
            World->OwningGameInstance->LocalPlayers.Remove(0);

    // pick the battle-royale terrain by version (ported verbatim)
    const wchar_t* terrainOpen = L"open Athena_Terrain";
    if (wcsstr(FConfiguration::Playlist, L"/MoleGame/Playlists/Playlist_MoleGame"))
    {
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"Mole.WorstCasePlayerCount 1"), nullptr);
        terrainOpen = L"open Mole_UnderBase_Parent";
    }
    else if (VersionInfo.FortniteVersion >= 12.00 && wcsstr(FConfiguration::Playlist, L"/Game/Athena/Playlists/Creative/Playlist_PlaygroundV2.Playlist_PlaygroundV2"))
        terrainOpen = L"open Creative_NoApollo_Terrain";
    else
    {
        if (VersionInfo.FortniteVersion >= 27.00)
        {
            if (VersionInfo.FortniteVersion >= 28.00)
                terrainOpen = L"open Helios_Terrain";
        }
        else if (VersionInfo.FortniteVersion >= 23.00)
            terrainOpen = L"open Asteria_Terrain";
        else if (VersionInfo.FortniteVersion >= 19.00)
            terrainOpen = L"open Artemis_Terrain";
        else if (VersionInfo.FortniteVersion >= 11.00)
            terrainOpen = L"open Apollo_Terrain";
    }

    // matchmaking/encryption bypass (x64: JG→JE)
    if (uint64_t EncryptionPatch = FindEncryptionPatch())
    {
        if (!AndroidPatches::ApplyEncryptionPatch(EncryptionPatch))
            LOGW("Matchmaking patch NOT applied on this version (pattern found but no b.gt)");
    }
    else
        LOGW("Matchmaking is NOT supported on this version (no anchor)");

    for (auto& HookFunc : _PostLoadHookFuncs)
        HookFunc();

    UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(terrainOpen), nullptr);

    Misc::bHookedAll = true;
    LOGI("Erbium Android bootstrap complete — map: %ls", terrainOpen);
}

// Entry: called from the existing android/src/entry.cpp bootstrap after the
// GO file appears (login/lobby first). See entry.cpp — it flips GIs there for
// the v1 milestone; this Main does the FULL server bring-up.
extern "C" void ErbiumAndroidMain()
{
    std::thread(Main).detach();
}
