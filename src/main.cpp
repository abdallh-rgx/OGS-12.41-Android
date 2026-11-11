// ============================================================================
// main.cpp — the Android entry point & main flow (1:1 port of dllmain.cpp).
//
// Entry: the patched APK loads this library via System.loadLibrary("OGS-12.41")
// in GameActivity.onCreate(); JNI_OnLoad fires and starts the worker thread.
//
// Flow (dllmain.cpp port, LISTEN-SERVER semantics):
//   1. wait for libUE4.so + the engine singleton
//   2. wait for login (Frontend world, match state InProgress)
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
// WaitForEngineModule — wait for libUE4.so / libUnreal.so to be mapped.
//
// THE SINUM INIT MODEL (merged per the device tombstones): the patched APK
// calls System.loadLibrary("OGS-12.41") as the FIRST statement of
// GameActivity.onCreate — at that moment libUE4.so does not exist yet. Any
// engine access from .init_array / JNI_OnLoad is therefore a guaranteed
// null-deref (tombstone #2: GetWorld() → [0+0x130]) or a linker-lock deadlock
// (tombstone #1: dlopen under call_constructors). Like libsinum.so — which
// never crashes — we touch NOTHING at load time; the worker polls
// /proc/self/maps (pure file reads, no dl_iterate_phdr, no linker lock) until
// the engine module is mapped, and only then resolves anything.
// ═══════════════════════════════════════════════════════════════════════════
static bool MapsContainsEngineModule(const char** OutName)
{
        FILE* Maps = fopen("/proc/self/maps", "r");
        if (!Maps)
                return false;

        char Line[512];
        bool Found = false;
        while (fgets(Line, sizeof(Line), Maps))
        {
                if (strstr(Line, "/libUE4.so") || strstr(Line, "/libUnreal.so"))
                {
                        if (OutName)
                                *OutName = strstr(Line, "/libUE4.so") ? "libUE4.so" : "libUnreal.so";
                        Found = true;
                        break;
                }
        }
        fclose(Maps);
        return Found;
}

static const char* WaitForEngineModule()
{
        Log("Waiting for the engine module (libUE4.so) to be mapped…");
        const auto Started = std::chrono::steady_clock::now();
        int Quiet = 0;
        for (;;)
        {
                const char* Name = nullptr;
                if (MapsContainsEngineModule(&Name))
                {
                        Log(std::string("Engine module mapped: ") + Name);
                        return Name;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(250));

                if (++Quiet % 40 == 0)
                {
                        const auto Secs = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::steady_clock::now() - Started).count();
                        Log("Still waiting for libUE4.so (" + std::to_string(Secs) + "s)…");
                }
        }
}

// ═══════════════════════════════════════════════════════════════════════════
// Main — dllmain.cpp Main() port, LISTEN-SERVER edition
// ═══════════════════════════════════════════════════════════════════════════
static void Main()
{
        InitConsole();

        // 0) SINUM MODEL: wait until libUE4.so actually exists in this process
        //    BEFORE any engine access (GetEngine() iterates GObjects, which is
        //    resolved through the module base — null base = null-deref).
        WaitForEngineModule();

        while (UEngine::GetEngine() == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        Log("Engine is up — resolving internals…");
        ImageBase = InSDKUtils::GetImageBase();
        Log(std::string("libUE4.so base: 0x") + std::to_string(ImageBase));

        NativeResolver::Init();

        // GIs flags (Erbium chains)
        GIs::Client = (bool*)FindGIsClient();
        GIs::Server = (bool*)FindGIsServer();
        Log(std::string("GIsClient @ 0x") + std::to_string((uintptr_t)GIs::Client) +
                ", GIsServer @ 0x" + std::to_string((uintptr_t)GIs::Server));

        // deferred PC static-init (Bosses.h cached engine pointers — the PC
        // original initialized them at DLL attach into a running game; on
        // Android the engine only exists now)
        Bosses::Init();

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

// NOTE (tombstone post-mortem): there is deliberately NO
// __attribute__((constructor)) here. Constructors run inside
// soinfo::call_constructors — under the dynamic linker's write lock, before
// the rest of our .init_array (the PC-port data tables) and, in this patched
// APK, before libUE4.so is even mapped. Spawning threads / dlopen-ing there
// caused the linker deadlock (tombstone #1) and spawning the worker before
// the remaining TU constructors finished raced the data tables. ART calls
// JNI_OnLoad only AFTER dlopen returns (linker lock released) — that is the
// one safe load-time entry, exactly like libsinum.
