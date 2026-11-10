// ============================================================================
// main.cpp — the Android entry point & main flow (1:1 port of dllmain.cpp).
//
// Entry: the patched APK loads this library via System.loadLibrary("OGS-12.41")
// in GameActivity.onCreate(); JNI_OnLoad fires and starts the worker thread.
//
// Flow (dllmain.cpp port, LISTEN-SERVER semantics):
//   1. wait for libUE4.so + the engine singleton
//   2. sinum merge: install the curl_easy_setopt redirect (Epic traffic →
//      the private server) — the earliest deadlock-free point; libsinum.so
//      must NOT be in the APK anymore (single library does everything now)
//   3. resolve internals (NativeResolver) + install hooks (all modules)
//   4. GIsClient = false / GIsServer = true   (the flip)
//   5. LISTEN SERVER: keep the LocalPlayer alive (UE NM_ListenServer — the
//      host plays at the same time), InitListen the NetDriver so LAN players
//      can join by IP, and open Apollo_Terrain
//   6. arm the 4-finger / 2-second touch gesture → in-game GUI (mode select
//      or join-by-IP)
// ============================================================================
#include "framework.h"
#include "Hooks.h"
#include "NativeResolver.h"

#include "GameMode.h"
#include "Abilities.h"
#include "Inventory.h"
#include "Building.h"
#include "Looting.h"
#include "Quests.h"
#include "Misc.h"
#include "Net.h"
#include "Tick.h"
#include "Bots.h"
#include "Creative.h"
#include "PE.h"
#include "PC.h"

#include "GuiTouchInput.h"
#include "GuiMenu.h"
#include "ListenServer.h"
#include "Sinum.h"

#include <chrono>
#include <fstream>
#include <jni.h>
#include <sys/stat.h>
#include <thread>
#include <mutex>

// module base of libUE4.so (SDK's InSDKUtils::GetImageBase resolves the same)
uint64_t ImageBase = 0;

// ═══════════════════════════════════════════════════════════════════════════
// Logging — logcat (tag OGS) + the on-device log file required by the user:
// /storage/emulated/0/Android/data/com.epicgames.fortnite1241/files/OGS-12.41.log
// ═══════════════════════════════════════════════════════════════════════════
namespace
{
constexpr const char* kLogPath =
        "/storage/emulated/0/Android/data/com.epicgames.fortnite1241/files/OGS-12.41.log";

std::string Timestamp()
{
        using namespace std::chrono;
        const auto Now = system_clock::now();
        const time_t Tt = system_clock::to_time_t(Now);
        tm Tm{};
        localtime_r(&Tt, &Tm);
        char Buf[32];
        strftime(Buf, sizeof(Buf), "%H:%M:%S", &Tm);
        return Buf;
}
}

void Log(const std::string& msg)
{
        static bool FirstCall = true;

        if (FirstCall)
        {
                mkdir("/storage/emulated/0/Android/data/com.epicgames.fortnite1241/files", 0770);
                std::ofstream LogFile(kLogPath, std::ios::trunc);
                if (LogFile.is_open())
                {
                        LogFile << "[OGS 12.41 Android]: Log file initialized!\n";
                        LogFile.close();
                }
                FirstCall = false;
        }

        std::ofstream LogFile(kLogPath, std::ios::app);
        if (LogFile.is_open())
        {
                LogFile << "[" << Timestamp() << "][OGS]: " << msg << std::endl;
                LogFile.close();
        }

        LOGI("%s", msg.c_str());
}

// ═══════════════════════════════════════════════════════════════════════════
// InitConsole — the PC version allocates a console; on Android we only log.
// ═══════════════════════════════════════════════════════════════════════════
static void InitConsole()
{
        Log("Welcome to OGS-12.41 Android, 1:1 port of MOGS by ObsessedTech (Android bring-up)");
        Log("UE 4.25 (12.41.0-12910313-Android) — libUE4.so base resolves on first use");
}

// ═══════════════════════════════════════════════════════════════════════════
// LoadWorld — dllmain.cpp port with LISTEN-SERVER semantics:
// the PC dedicated build removed the LocalPlayer (server-only); per the UE
// definition of NM_ListenServer the host keeps playing, so the LocalPlayer
// stays alive and the listen NetDriver accepts connections.
// ═══════════════════════════════════════════════════════════════════════════
static void LoadWorld()
{
        Log("Loading World! (Listen Server — host stays in the match)");

        if (!Globals::bCreativeEnabled && !Globals::bSTWEnabled)
        {
                UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), u"open Apollo_Terrain", nullptr);
        }
        else if (Globals::bCreativeEnabled)
        {
                UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), u"open Creative_NoApollo_Terrain", nullptr);
        }
        // LISTEN SERVER: the PC line 'LocalPlayers.Remove(0)' is intentionally NOT
        // ported — the host must keep its LocalPlayer to play at the same time
        // (UE NM_ListenServer). ListenServer::Setup() opens the NetDriver instead.
}

// install every module hook (dllmain.cpp Hook() port)
static void Hook()
{
        GameMode::Hook();
        PC::Hook();
        Abilities::Hook();
        Inventory::Hook();
        Building::Hook();
        Looting::Hook();
        Quests::Hook();

        Misc::Hook();
        Net::Hook();
        Tick::Hook();

        Bots::Hook();
        Creative::Hook();

        PE::Hook();
}

// wait for the login/lobby (dllmain.cpp WaitForLogin port)
static void WaitForLogin()
{
        Log("Waiting for login!");

        FName Frontend = UKismetStringLibrary::Conv_StringToName(u"Frontend");
        FName MatchState = UKismetStringLibrary::Conv_StringToName(u"InProgress");

        while (true)
        {
                UWorld* CurrentWorld = ((UWorld*)UWorld::GetWorld());
                if (CurrentWorld)
                {
                        if (CurrentWorld->Name == Frontend)
                        {
                                auto GameMode = (AGameMode*)CurrentWorld->AuthorityGameMode;
                                if (GameMode && GameMode->GetMatchState() == MatchState)
                                {
                                        break;
                                }
                        }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1000 * 1));
}

// the client/server globals (PC: ImageBase+0x804B659/0x804B65A) — resolved
// through the Erbium-Android GIs chains at bring-up (see main_GIs.h).
#include "main_GIs.h"
namespace GIs
{
        bool* Client = nullptr;
        bool* Server = nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════
// Main — dllmain.cpp Main() port, LISTEN-SERVER edition
// ═══════════════════════════════════════════════════════════════════════════
static void Main()
{
        InitConsole();

        while (UEngine::GetEngine() == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        Log("Engine is up — resolving internals…");
        ImageBase = InSDKUtils::GetImageBase();
        Log(std::string("libUE4.so base: 0x") + std::to_string(ImageBase));

        // sinum merge: redirect Epic traffic BEFORE anything else — the game
        // starts its first HTTP calls (lightswitch/MCP/manifest) right after
        // engine init. libUE4.so is fully loaded by now, so dlopen(RTLD_NOLOAD)
        // inside Sinum::Hook() cannot race the app's own loading (the libsinum
        // deadlock — see src/Sinum.cpp).
        Sinum::Hook();

        NativeResolver::Init();

        // GIs flags (Erbium chains)
        GIs::Client = (bool*)FindGIsClient();
        GIs::Server = (bool*)FindGIsServer();
        Log(std::string("GIsClient @ 0x") + std::to_string((uintptr_t)GIs::Client) +
                ", GIsServer @ 0x" + std::to_string((uintptr_t)GIs::Server));

        WaitForLogin();

        Hook();

        // the flip (PC: *(bool*)(ImageBase + 0x804B659/0x804B65A))
        if (GIs::Client && GIs::Server)
        {
                *GIs::Client = false; // GIsClient
                *GIs::Server = true;  // GIsServer
                Log("Flipped GIsClient=false / GIsServer=true");
        }
        else
        {
                Log("WARNING: GIs globals unresolved — server flip skipped");
        }

        while (UWorld::GetWorld() == nullptr)
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        while (UWorld::GetWorld()->OwningGameInstance->LocalPlayers.Num() == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // LISTEN SERVER bring-up (before the map opens so joiners see it in time)
        ListenServer::Setup();

        // 4-finger / 2-second touch gesture → GUI (mode select / join by IP)
        Gui::InitTouchDetection();
        Gui::InitRenderer();

        LoadWorld();

        ListenServer::OnWorldLoaded();

        Log("Main: bring-up complete — enjoy! (4 fingers, 2 seconds → menu)");
}

// ═══════════════════════════════════════════════════════════════════════════
// Entry — System.loadLibrary("OGS-12.41") → JNI_OnLoad → worker thread
// ═══════════════════════════════════════════════════════════════════════════
static void StartMainOnce(const char* Via)
{
        static std::once_flag Once;
        std::call_once(Once, [Via]()
        {
                LOGI("OGS-12.41 Android: entry via %s — starting worker thread", Via);
                std::thread(Main).detach();
        });
}

extern "C" __attribute__((visibility("default"))) jint JNI_OnLoad(JavaVM* Vm, void*)
{
        StartMainOnce("JNI_OnLoad");
        return JNI_VERSION_1_6;
}

// constructor fallback (loads even without JNI registration)
__attribute__((constructor)) static void OgsConstructor()
{
        StartMainOnce("constructor");
}
