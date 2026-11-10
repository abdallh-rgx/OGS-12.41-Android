#pragma once
#include "framework.h"

namespace Misc {
    void nullFunc() {}

    int True() {
        return 1;
    }

    int False() {
        return 0;
    }

    static inline void (*KickPlayerOG)(AGameSession*, AController*);
    static void KickPlayer(AGameSession*, AController*) {
        return;
    }

    void (*DispatchRequestOG)(__int64 a1, uint64_t* a2, int a3);
    void DispatchRequest(__int64 a1, uint64_t* a2, int a3)
    {
        return DispatchRequestOG(a1, a2, 3);
    }

    void (*K2_DestroyActorOG)(AActor* This, __int64 a2);
    void K2_DestroyActor(AActor* This, __int64 a2)
    {
        AFortGameModeAthena* GameMode = (AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode;
        AFortGameStateAthena* GameState = (AFortGameStateAthena*)UWorld::GetWorld()->GameState;

        std::string Name = This->GetName();
        //Log(Name);

        if (GameState->GamePhase <= EAthenaGamePhase::Warmup) {
            if (Name.contains("BGA") || Name.contains("StaticGenerator")) {
                This->bActorIsBeingDestroyed = true;
            }
        }
        else {
            if (Name.contains("BGA") || Name.contains("StaticGenerator")) {
                if (This->bActorIsBeingDestroyed) {
                    This->bActorIsBeingDestroyed = false;
                }
            }
        }

        return K2_DestroyActorOG(This, a2);
    }

    void LateGameAircraftThread(FVector BattleBusLocation)
    {
        auto GameState = (AFortGameStateAthena*)UWorld::GetWorld()->GameState;
        auto GameMode = (AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode;

        Sleep(2500);

        GameState->bAircraftIsLocked = false;

        GameMode->OnAircraftExitedDropZone(GameState->GetAircraft(0));

        GameState->GamePhase = EAthenaGamePhase::SafeZones;

        GameState->SafeZonesStartTime = 1;
    }

    uint8 NextIdx = 3;
    int CurrentPlayersOnTeam = 0;
    int MaxPlayersOnTeam;

    inline __int64 PickTeam(__int64 a1, uint8_t a2, __int64 a3)
    {
        uint8 Ret = NextIdx;
        CurrentPlayersOnTeam++;

        if (CurrentPlayersOnTeam == MaxPlayersOnTeam)
        {
            NextIdx++;
            CurrentPlayersOnTeam = 0;
        }
        return Ret;
    };

    void Hook() {
        OGS_HOOK_NOREF(0x2D95E00, False); // collectgarbage
        OGS_HOOK(0x4155600, KickPlayer, KickPlayerOG); // Kickplayer
        OGS_HOOK_NOREF(0x1E23840, False);// change gamesession id
        OGS_HOOK(0x108D740, DispatchRequest, DispatchRequestOG);

        OGS_HOOK_NOREF(0x2DBCBA0, True); // CanCreateContext

        OGS_HOOK_NOREF(0x3ca10c0, nullFunc);
        OGS_HOOK_NOREF(0x2d95e00, nullFunc);
        OGS_HOOK_NOREF(0x3262100, nullFunc);
        OGS_HOOK_NOREF(0x1e23840, nullFunc);
        OGS_HOOK_NOREF(0x2d95dc0, nullFunc);

        OGS_HOOK(0x7F0220, K2_DestroyActor, K2_DestroyActorOG);

        OGS_HOOK_NOREF(0x18E6B20, PickTeam);

        Log("Misc Hooked!");
    }
}