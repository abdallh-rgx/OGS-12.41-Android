#pragma once
#include "framework.h"
#include "BotNames.h"
#include "POI_Locs.h"

#include "Quests.h"
#include "Misc.h"

static std::vector<UAthenaCharacterItemDefinition*> CIDs{};
static std::vector<UAthenaPickaxeItemDefinition*> Pickaxes{};
static std::vector<UAthenaBackpackItemDefinition*> Backpacks{};
static std::vector<UAthenaGliderItemDefinition*> Gliders{};
static std::vector<UAthenaSkyDiveContrailItemDefinition*> Contrails{};
inline std::vector<UAthenaDanceItemDefinition*> Dances{};

enum class EBotState : uint8 {
    Warmup,
    PreBus,
    Bus,
    Skydiving,
    Gliding,
    Landed,
    Fleeing,
    Looting,
    LookingForPlayers,
    MovingToSafeZone,
    Stuck
};

enum class EBotWarmupChoice {
    Emote,
    MoveToPlayerEmote,
    MAX
};

enum class EBotStrafeType {
    StrafeLeft,
    StrafeRight
};

enum class ELootableType {
    None = -1,
    Chest = 0,
    Pickup = 1
};

std::vector<class PlayerBot*> PlayerBotArray{};

struct PlayerBot
{
public:
    EBotWarmupChoice BotWarmupChoice = EBotWarmupChoice::MAX;

public:
    uint64_t tick_counter = 0;
    AFortPlayerPawnAthena* Pawn = nullptr;
    ABP_PhoebePlayerController_C* PC = nullptr;
    AFortPlayerStateAthena* PlayerState = nullptr;
    EBotState BotState = EBotState::Warmup;
    EBotState CachedBotState = EBotState::Warmup;
    ABuildingActor* StuckBuildingActor = nullptr;
    AActor* TargetLootable = nullptr;
    ELootableType TargetLootableType = ELootableType::None;
    AActor* NearestPlayerActor = nullptr;
    FString DisplayName = u"Bot";
    bool bIsStressed = false;
    bool bIsDead = false;
    bool bHasThankedBusDriver = false;
    bool bHasJumpedFromBus = false;
    FVector TargetDropZone = FVector();
    float ClosestDistToDropZone = FLT_MAX;
    bool bIsCurrentlyStrafing = false;
    EBotStrafeType StrafeType = EBotStrafeType::StrafeLeft;
    float StrafeEndTime = 0.0f;
    float TimeToNextAction = 0.f;
    float LootTargetStartTime = 0.f;
    float LastLootTargetDistance = 0.f;
    
    // Новые поля
    float LastWeaponSwitchTime = 0.f;
    float LastBuildTime = 0.f;
    float LastTargetDist = 0.f;
    int NoApproachTicks = 0;
    int NoMoveTicks = 0;

public:
    void OnDied(AFortPlayerStateAthena* KillerState, AActor* DamageCauser, FName BoneName)
    {
        static auto Acc_DistanceShot = StaticLoadObject<UFortAccoladeItemDefinition>("/Game/Athena/Items/Accolades/AccoladeId_DistanceShot.AccoladeId_DistanceShot");
        static auto Acc_LongShot = StaticLoadObject<UFortAccoladeItemDefinition>("/Game/Athena/Items/Accolades/AccoladeId_051_LongShot.AccoladeId_051_LongShot");
        static auto Acc_LudicrousShot = StaticLoadObject<UFortAccoladeItemDefinition>("/Game/Athena/Items/Accolades/AccoladeId_052_LudicrousShot.AccoladeId_052_LudicrousShot");
        static auto Acc_ImpossibleShot = StaticLoadObject<UFortAccoladeItemDefinition>("/Game/Athena/Items/Accolades/AccoladeId_053_ImpossibleShot.AccoladeId_053_ImpossibleShot");
        static auto Acc_Headshot = StaticLoadObject<UFortAccoladeItemDefinition>("/Game/Athena/Items/Accolades/AccoladeId_048_HeadshotElim.AccoladeId_048_HeadshotElim");
        static auto Acc_Survival_Bronze = StaticLoadObject<UFortAccoladeItemDefinition>("/Game/Athena/Items/Accolades/AccoladeId_026_Survival_Default_Bronze.AccoladeId_026_Survival_Default_Bronze");
        static auto Acc_Survival_Silver = StaticLoadObject<UFortAccoladeItemDefinition>("/Game/Athena/Items/Accolades/AccoladeId_027_Survival_Default_Silver.AccoladeId_027_Survival_Default_Silver");
        static auto Acc_Survival_Gold = StaticLoadObject<UFortAccoladeItemDefinition>("/Game/Athena/Items/Accolades/AccoladeId_028_Survival_Default_Gold.AccoladeId_028_Survival_Default_Gold");

        Log("Bot Died");
        bIsDead = true;

        TargetLootable = nullptr;
        NearestPlayerActor = nullptr;
        StuckBuildingActor = nullptr;

        if (!KillerState || !DamageCauser || !PlayerState || !Pawn)
            return;

        FDeathInfo& DeathInfo = PlayerState->DeathInfo;
        DeathInfo.bDBNO = Pawn->bWasDBNOOnDeath;
        DeathInfo.DeathLocation = Pawn->K2_GetActorLocation();
        DeathInfo.DeathTags = Pawn->DeathTags;
        DeathInfo.Downer = KillerState ? KillerState : nullptr;
        AFortPawn* KillerPawn = KillerState ? KillerState->GetCurrentPawn() : nullptr;
        DeathInfo.Distance = (KillerPawn && Pawn) ? KillerPawn->GetDistanceTo(Pawn) : 0.f;
        DeathInfo.FinisherOrDowner = KillerState ? KillerState : nullptr;
        DeathInfo.DeathCause = PlayerState->ToDeathCause(DeathInfo.DeathTags, DeathInfo.bDBNO);
        DeathInfo.bInitialized = true;
        PlayerState->OnRep_DeathInfo();

        bool bIsKillerABot = KillerState->bIsABot;

        if (!PC->Inventory)
            return;

        auto KillerPC = (AFortPlayerControllerAthena*)KillerState->GetOwner();

        if (KillerPC && KillerPC->IsA(AFortPlayerControllerAthena::StaticClass()) && !bIsKillerABot)
        {
            KillerState->KillScore++;
            Quests::GiveAccolade(KillerPC, StaticLoadObject<UFortAccoladeItemDefinition>("/Game/Athena/Items/Accolades/AccoladeId_012_Elimination.AccoladeId_012_Elimination"));
            Quests::GiveAccolade(KillerPC, GetDefFromEvent(EAccoladeEvent::Kill, KillerState->KillScore));
            Quests::GiveAccolade((AFortPlayerControllerAthena*)KillerState->Owner, StaticLoadObject<UFortAccoladeItemDefinition>("/Game/Athena/Items/Accolades/AccoladeId_013_Assist.AccoladeId_013_Assist"));

            for (size_t i = 0; i < KillerState->PlayerTeam->TeamMembers.Num(); i++)
            {
                ((AFortPlayerStateAthena*)KillerState->PlayerTeam->TeamMembers[i]->PlayerState)->TeamKillScore++;
                ((AFortPlayerStateAthena*)KillerState->PlayerTeam->TeamMembers[i]->PlayerState)->OnRep_TeamKillScore();
            }

            KillerState->ClientReportKill(PlayerState);
            KillerState->ClientReportTeamKill(KillerState->KillScore);
            KillerState->OnRep_Kills();
        }

        if (bIsKillerABot) {
            return;
        }

        if (!bFirstElimTriggered) {
            Quests::GiveAccolade(KillerPC, StaticLoadObject<UFortAccoladeItemDefinition>("/Game/Athena/Items/Accolades/AccoladeId_017_First_Elimination.AccoladeId_017_First_Elimination"));
            bFirstElimTriggered = true;
        }

        float Distance = DeathInfo.Distance / 100.0f;

        if (Distance >= 100.0f && Distance < 150.0f)
            Quests::GiveAccolade(KillerPC, Acc_DistanceShot);
        else if (Distance >= 150.0f && Distance < 200.0f)
            Quests::GiveAccolade(KillerPC, Acc_LongShot);
        else if (Distance >= 200.0f && Distance < 250.0f)
            Quests::GiveAccolade(KillerPC, Acc_LudicrousShot);
        else if (Distance >= 250.0f)
            Quests::GiveAccolade(KillerPC, Acc_ImpossibleShot);

        if (BoneName.ToString() == "head")
        {
            KillerPC->HeadShots++;
            Quests::GiveAccolade(KillerPC, Acc_Headshot);
            if (KillerPC->HeadShots == 4) {
                Quests::GiveAccolade(KillerPC, StaticLoadObject<UFortAccoladeItemDefinition>("/Game/Athena/Items/Accolades/AccoladeId_049_Headhunter.AccoladeId_049_Headhunter"));
            }
        }

        if (KillerPC) {
            float CurrentTime = UGameplayStatics::GetDefaultObj()->GetTimeSeconds(UWorld::GetWorld());
            if (CurrentTime > KillerPC->LastKillTimeWindow) {
                KillerPC->KillsInKillTimeWindow = 0;
                KillerPC->LastRecordedKillsInKillTimeWindow = 0;
            }
            KillerPC->LastKillTimeWindow = CurrentTime + 10.f;
            KillerPC->KillsInKillTimeWindow++;
        }
    }

public:
    void SetStuck(AActor* OtherActor, FHitResult& Hit) {
        if (BotState < EBotState::Landed) return;
        if (BotState == EBotState::Stuck) return;

        if (OtherActor && OtherActor->IsA(ABuildingSMActor::StaticClass()))
        {
            ABuildingSMActor* Actor = (ABuildingSMActor*)OtherActor;
            float Health = Actor->GetHealth();
            FFindFloorResult Res;

            if (Actor->bCanBeDamaged == 1 && Health > 1 && Health < 2500)
            {
                Pawn->CharacterMovement->K2_FindFloor(Pawn->CapsuleComponent->K2_GetComponentLocation(), &Res);
                if (Res.HitResult.Actor.Get() == OtherActor) return;
                StuckBuildingActor = Actor;
            }
        }

        CachedBotState = BotState;
        BotState = EBotState::Stuck;
    }

public:
    void GiveItemBot(UFortItemDefinition* Def, int Count = 1, int LoadedAmmo = 0)
    {
        if (!Def) return;
        UFortWorldItem* Item = (UFortWorldItem*)Def->CreateTemporaryItemInstanceBP(Count, 0);
        Item->OwnerInventory = PC->Inventory;
        Item->ItemEntry.LoadedAmmo = LoadedAmmo;
        PC->Inventory->Inventory.ReplicatedEntries.Add(Item->ItemEntry);
        PC->Inventory->Inventory.ItemInstances.Add(Item);
        PC->Inventory->Inventory.MarkItemDirty(Item->ItemEntry);
        PC->Inventory->HandleInventoryLocalUpdate();
    }

    FFortItemEntry* GetEntry(UFortItemDefinition* Def)
    {
        for (size_t i = 0; i < PC->Inventory->Inventory.ReplicatedEntries.Num(); i++)
        {
            if (PC->Inventory->Inventory.ReplicatedEntries[i].ItemDefinition == Def)
                return &PC->Inventory->Inventory.ReplicatedEntries[i];
        }
        return nullptr;
    }

    void Emote()
    {
        if (Dances.empty()) return;
        auto EmoteDef = Dances[UKismetMathLibrary::GetDefaultObj()->RandomIntegerInRange(0, Dances.size() - 1)];
        if (!EmoteDef) return;

        static UClass* EmoteAbilityClass = StaticLoadObject<UClass>("/Game/Abilities/Emotes/GAB_Emote_Generic.GAB_Emote_Generic_C");

        FGameplayAbilitySpec Spec{};
        AbilitySpecConstructor(&Spec, reinterpret_cast<UGameplayAbility*>(EmoteAbilityClass->DefaultObject), 1, -1, EmoteDef);
        GiveAbilityAndActivateOnce(reinterpret_cast<AFortPlayerStateAthena*>(PC->PlayerState)->AbilitySystemComponent, &Spec.Handle, Spec);
    }

    void Run()
    {
        if (!PlayerState || !PlayerState->AbilitySystemComponent) return;
        for (size_t i = 0; i < PlayerState->AbilitySystemComponent->ActivatableAbilities.Items.Num(); i++)
        {
            if (PlayerState->AbilitySystemComponent->ActivatableAbilities.Items[i].Ability->IsA(UFortGameplayAbility_Sprint::StaticClass()))
            {
                if (PlayerState->AbilitySystemComponent->ActivatableAbilities.Items[i].ActivationInfo.PredictionKeyWhenActivated.bIsStale) continue;
                if (PlayerState->AbilitySystemComponent->CanActivateAbilityWithMatchingTag(PlayerState->AbilitySystemComponent->ActivatableAbilities.Items[i].Ability->AbilityTags)) {
                    PlayerState->AbilitySystemComponent->ServerTryActivateAbility(PlayerState->AbilitySystemComponent->ActivatableAbilities.Items[i].Handle, PlayerState->AbilitySystemComponent->ActivatableAbilities.Items[i].InputPressed, PlayerState->AbilitySystemComponent->ActivatableAbilities.Items[i].ActivationInfo.PredictionKeyWhenActivated);
                }
                break;
            }
        }
    }

    void ForceStrafe(bool override) {
        if (!bIsCurrentlyStrafing && override)
        {
            bIsCurrentlyStrafing = true;
            if (UKismetMathLibrary::RandomBool()) {
                StrafeType = EBotStrafeType::StrafeLeft;
            }
            else {
                StrafeType = EBotStrafeType::StrafeRight;
            }
            StrafeEndTime = UGameplayStatics::GetDefaultObj()->GetTimeSeconds(UWorld::GetWorld()) + UKismetMathLibrary::GetDefaultObj()->RandomFloatInRange(1.5f, 3.0f);
        }
        else
        {
            if (UGameplayStatics::GetDefaultObj()->GetTimeSeconds(UWorld::GetWorld()) < StrafeEndTime)
            {
                if (StrafeType == EBotStrafeType::StrafeLeft) {
                    Pawn->AddMovementInput((Pawn->GetActorRightVector() * -1.0f), 1.0f, true);
                }
                else {
                    Pawn->AddMovementInput(Pawn->GetActorRightVector(), 1.0f, true);
                }
            }
            else
            {
                bIsCurrentlyStrafing = false;
            }
        }
    }

    // Постройка стены при низком здоровье
    void AttemptBuildWall() {
        if (!Pawn || !PC) return;
        
        float CurrentTime = UGameplayStatics::GetDefaultObj()->GetTimeSeconds(UWorld::GetWorld());
        if (CurrentTime - LastBuildTime < 5.0f) return;

        // Проверяем есть ли дерево
        static UFortItemDefinition* WoodDef = StaticLoadObject<UFortItemDefinition>("/Game/Items/ResourcePickups/WoodItemData.WoodItemData");
        int WoodCount = 0;
        
        for (size_t i = 0; i < PC->Inventory->Inventory.ReplicatedEntries.Num(); i++) {
            if (PC->Inventory->Inventory.ReplicatedEntries[i].ItemDefinition == WoodDef) {
                WoodCount = PC->Inventory->Inventory.ReplicatedEntries[i].Count;
                break;
            }
        }

        if (WoodCount >= 10) {
            // Пытаемся использовать способность строительства
            for (size_t i = 0; i < PlayerState->AbilitySystemComponent->ActivatableAbilities.Items.Num(); i++)
            {
                auto Ability = PlayerState->AbilitySystemComponent->ActivatableAbilities.Items[i].Ability;
                if (Ability && Ability->GetName().contains("Build") && !Ability->GetName().contains("Edit"))
                {
                    PlayerState->AbilitySystemComponent->ServerTryActivateAbility(
                        PlayerState->AbilitySystemComponent->ActivatableAbilities.Items[i].Handle,
                        true,
                        PlayerState->AbilitySystemComponent->ActivatableAbilities.Items[i].ActivationInfo.PredictionKeyWhenActivated
                    );
                    LastBuildTime = CurrentTime;
                    break;
                }
            }
        }
    }

    void LookAt(AActor* Actor)
    {
        if (!Pawn || !PC) return;
        if (PC->GetFocusActor() == Actor) return;
        if (!Actor) { PC->K2_ClearFocus(); return; }
        PC->K2_SetFocus(Actor);
    }

    bool IsPickaxeEquiped() {
        if (!Pawn || !Pawn->CurrentWeapon) return false;
        return Pawn->CurrentWeapon->WeaponData->IsA(UFortWeaponMeleeItemDefinition::StaticClass());
    }

    bool HasGun()
    {
        if (!PC || !PC->Inventory) return false;
        for (size_t i = 0; i < PC->Inventory->Inventory.ReplicatedEntries.Num(); i++)
        {
            auto& Entry = PC->Inventory->Inventory.ReplicatedEntries[i];
            if (Entry.ItemDefinition) {
                std::string ItemName = Entry.ItemDefinition->Name.ToString();
                if (ItemName.contains("Shotgun") || ItemName.contains("SMG") || ItemName.contains("Assault")
                    || ItemName.contains("Sniper") || ItemName.contains("Rocket") || ItemName.contains("Pistol")
                    || ItemName.contains("Rifle")) {
                    return true;
                }
            }
        }
        return false;
    }

    void EquipPickaxe()
    {
        if (!Pawn || !Pawn->CurrentWeapon) return;
        if (!Pawn->CurrentWeapon->WeaponData->IsA(UFortWeaponMeleeItemDefinition::StaticClass()))
        {
            for (size_t i = 0; i < PC->Inventory->Inventory.ReplicatedEntries.Num(); i++)
            {
                if (PC->Inventory->Inventory.ReplicatedEntries[i].ItemDefinition->IsA(UFortWeaponMeleeItemDefinition::StaticClass()))
                {
                    Pawn->EquipWeaponDefinition((UFortWeaponItemDefinition*)PC->Inventory->Inventory.ReplicatedEntries[i].ItemDefinition, PC->Inventory->Inventory.ReplicatedEntries[i].ItemGuid);
                    break;
                }
            }
        }
    }

    void SimpleSwitchToWeapon() {
        if (!HasGun()) return;
        if (!Pawn || !Pawn->CurrentWeapon || !Pawn->CurrentWeapon->WeaponData || !PC || !PC->Inventory || bIsDead) return;
        if (!Pawn->CurrentWeapon->WeaponData->IsA(UFortWeaponMeleeItemDefinition::StaticClass())) return;

        for (size_t i = 0; i < PC->Inventory->Inventory.ReplicatedEntries.Num(); i++)
        {
            auto& Entry = PC->Inventory->Inventory.ReplicatedEntries[i];
            if (Entry.ItemDefinition) {
                std::string ItemName = Entry.ItemDefinition->Name.ToString();
                if (ItemName.contains("Shotgun") || ItemName.contains("SMG") || ItemName.contains("Assault")
                    || ItemName.contains("Grenade") || ItemName.contains("Sniper") || ItemName.contains("Rocket") || ItemName.contains("Pistol")) {
                    Pawn->EquipWeaponDefinition((UFortWeaponItemDefinition*)Entry.ItemDefinition, Entry.ItemGuid);
                    break;
                }
            }
        }
    }

    // Умное переключение оружия по дистанции
    void SmartSwitchWeaponForDistance(float DistanceToTarget) {
        if (!Pawn || !PC || !PC->Inventory || !HasGun()) return;

        float CurrentTime = UGameplayStatics::GetDefaultObj()->GetTimeSeconds(UWorld::GetWorld());
        if (CurrentTime - LastWeaponSwitchTime < 2.0f) return;

        FGuid BestGuid;
        UFortWeaponItemDefinition* BestDef = nullptr;
        float BestPriority = -1.0f;

        for (size_t i = 0; i < PC->Inventory->Inventory.ReplicatedEntries.Num(); i++)
        {
            auto& Entry = PC->Inventory->Inventory.ReplicatedEntries[i];
            if (!Entry.ItemDefinition || Entry.ItemDefinition->IsA(UFortWeaponMeleeItemDefinition::StaticClass()))
                continue;

            std::string ItemName = Entry.ItemDefinition->Name.ToString();
            float Priority = 0.0f;

            if (ItemName.contains("Shotgun")) {
                if (DistanceToTarget < 1000.0f) Priority = 100.0f;
                else Priority = 0.0f;
            }
            else if (ItemName.contains("SMG")) {
                if (DistanceToTarget < 2000.0f) Priority = 90.0f;
                else Priority = 20.0f;
            }
            else if (ItemName.contains("Assault") || ItemName.contains("Rifle")) {
                if (DistanceToTarget >= 800.0f) Priority = 95.0f;
                else Priority = 70.0f;
            }
            else if (ItemName.contains("Sniper")) {
                if (DistanceToTarget > 3000.0f) Priority = 100.0f;
                else Priority = 10.0f;
            }
            else if (ItemName.contains("Rocket")) {
                if (DistanceToTarget > 1000.0f) Priority = 80.0f;
                else Priority = 15.0f;
            }
            else if (ItemName.contains("Pistol")) Priority = 30.0f;
            else continue;

            if (Priority > BestPriority) {
                BestPriority = Priority;
                BestGuid = Entry.ItemGuid;
                BestDef = (UFortWeaponItemDefinition*)Entry.ItemDefinition;
            }
        }

        if (BestDef && BestPriority > 0.0f) {
            Pawn->EquipWeaponDefinition(BestDef, BestGuid);
            LastWeaponSwitchTime = CurrentTime;
        }
    }

    // Прицеливание с предсказанием движения
    void AimAtTargetWithLead(AActor* Target) {
        if (!Target || !Pawn || !PC) return;

        FVector MyLoc = Pawn->K2_GetActorLocation();
        FVector TargetLoc = Target->K2_GetActorLocation();
        FVector TargetVelocity = Target->GetVelocity();
        
        float Distance = UKismetMathLibrary::GetDefaultObj()->Vector_Distance(MyLoc, TargetLoc);
        float BulletSpeed = 15000.0f;
        float TimeToTarget = Distance / BulletSpeed;
        
        // Предсказываем позицию цели
        FVector PredictedLoc;
        PredictedLoc.X = TargetLoc.X + (TargetVelocity.X * TimeToTarget);
        PredictedLoc.Y = TargetLoc.Y + (TargetVelocity.Y * TimeToTarget);
        PredictedLoc.Z = TargetLoc.Z + (TargetVelocity.Z * TimeToTarget) + 30.0f; // Немного выше для компенсации гравитации
        
        FRotator AimRot = UKismetMathLibrary::GetDefaultObj()->FindLookAtRotation(MyLoc, PredictedLoc);
        
        PC->SetControlRotation(AimRot);
        PC->K2_SetActorRotation(AimRot, true);
    }

    bool ShouldPickup(AFortPickup* Pickup) {
        if (!Pickup) return false;
        auto Def = Pickup->PrimaryPickupItemEntry.ItemDefinition;
        if (!Def) return false;
        if (Def->IsA(UFortAmmoItemDefinition::StaticClass())) return false;
        return true;
    }

    void Pickup(AFortPickup* Pickup) {
        if (!Pickup) return;
        GiveItemBot(Pickup->PrimaryPickupItemEntry.ItemDefinition, Pickup->PrimaryPickupItemEntry.Count, Pickup->PrimaryPickupItemEntry.LoadedAmmo);
        Pickup->PickupLocationData.bPlayPickupSound = true;
        Pickup->PickupLocationData.FlyTime = 0.3f;
        Pickup->PickupLocationData.ItemOwner = Pawn;
        Pickup->PickupLocationData.PickupGuid = Pickup->PrimaryPickupItemEntry.ItemGuid;
        Pickup->PickupLocationData.PickupTarget = Pawn;
        Pickup->OnRep_PickupLocationData();
        Pickup->bPickedUp = true;
        Pickup->OnRep_bPickedUp();
    }

    void PickupAllItemsInRange(float Range = 320.f) {
        if (!Pawn || !PC) return;
        static auto PickupClass = AFortPickupAthena::StaticClass();
        TArray<AActor*> Array;
        UGameplayStatics::GetDefaultObj()->GetAllActorsOfClass(UWorld::GetWorld(), PickupClass, &Array);
        for (size_t i = 0; i < Array.Num(); i++)
        {
            if (!Array[i] || Array[i]->bHidden) continue;
            if (!ShouldPickup((AFortPickupAthena*)Array[i])) continue;
            if (Array[i]->GetDistanceTo(Pawn) < Range)
                Pickup((AFortPickupAthena*)Array[i]);
        }
        Array.Free();
    }

    ABuildingActor* FindNearestChest()
    {
        static auto ChestClass = StaticLoadObject<UClass>("/Game/Building/ActorBlueprints/Containers/Tiered_Chest_Athena.Tiered_Chest_Athena_C");
        static auto FactionChestClass = StaticLoadObject<UClass>("/Game/Building/ActorBlueprints/Containers/Tiered_Chest_Athena_FactionChest.Tiered_Chest_Athena_FactionChest_C");
        TArray<AActor*> Array;
        TArray<AActor*> FactionChests;
        UGameplayStatics::GetDefaultObj()->GetAllActorsOfClass(UWorld::GetWorld(), ChestClass, &Array);
        UGameplayStatics::GetDefaultObj()->GetAllActorsOfClass(UWorld::GetWorld(), FactionChestClass, &FactionChests);

        AActor* NearestChest = nullptr;

        for (size_t i = 0; i < Array.Num(); i++)
        {
            AActor* Chest = Array[i];
            if (Chest->bHidden && Chest != TargetLootable) continue;
            if (!NearestChest || Chest->GetDistanceTo(Pawn) < NearestChest->GetDistanceTo(Pawn))
                NearestChest = Array[i];
        }

        for (size_t i = 0; i < FactionChests.Num(); i++)
        {
            AActor* Chest = FactionChests[i];
            if (Chest->bHidden && Chest != TargetLootable) continue;
            if (!NearestChest || Chest->GetDistanceTo(Pawn) < NearestChest->GetDistanceTo(Pawn))
                NearestChest = FactionChests[i];
        }
        Array.Free();
        FactionChests.Free();
        return (ABuildingActor*)NearestChest;
    }

    AFortPickupAthena* FindNearestPickup()
    {
        static auto PickupClass = AFortPickupAthena::StaticClass();
        TArray<AActor*> Array;
        UGameplayStatics::GetDefaultObj()->GetAllActorsOfClass(UWorld::GetWorld(), PickupClass, &Array);
        AActor* NearestPickup = nullptr;

        for (size_t i = 0; i < Array.Num(); i++)
        {
            if (Array[i]->bHidden && Array[i] != TargetLootable) continue;
            if (!ShouldPickup((AFortPickupAthena*)Array[i])) continue;
            if (!NearestPickup || Array[i]->GetDistanceTo(Pawn) < NearestPickup->GetDistanceTo(Pawn))
                NearestPickup = Array[i];
        }

        Array.Free();
        return (AFortPickupAthena*)NearestPickup;
    }

    FVector FindNearestPlayerOrBot() {
        auto GameMode = (AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode;
        AActor* NearestPlayer = nullptr;

        for (size_t i = 0; i < GameMode->AlivePlayers.Num(); i++)
        {
            if (!GameMode->AlivePlayers[i]->Pawn) continue;
            if (!NearestPlayer || GameMode->AlivePlayers[i]->Pawn->GetDistanceTo(Pawn) < NearestPlayer->GetDistanceTo(Pawn))
            {
                AFortPlayerStateAthena* PS = (AFortPlayerStateAthena*)GameMode->AlivePlayers[i]->PlayerState;
                if (PS->TeamIndex != PlayerState->TeamIndex)
                    NearestPlayer = GameMode->AlivePlayers[i]->Pawn;
            }
        }

        for (size_t i = 0; i < GameMode->AliveBots.Num(); i++)
        {
            if (!GameMode->AliveBots[i]->Pawn || GameMode->AliveBots[i]->Pawn == Pawn) continue;
            if (!NearestPlayer || GameMode->AliveBots[i]->Pawn->GetDistanceTo(Pawn) < NearestPlayer->GetDistanceTo(Pawn))
            {
                AFortPlayerStateAthena* PS = (AFortPlayerStateAthena*)GameMode->AliveBots[i]->PlayerState;
                if (PS->TeamIndex != PlayerState->TeamIndex)
                    NearestPlayer = GameMode->AliveBots[i]->Pawn;
            }
        }

        for (size_t i = 0; i < FactionBots.size(); i++) {
            if (!FactionBots[i]->Pawn) continue;
            if (!NearestPlayer || FactionBots[i]->Pawn->GetDistanceTo(Pawn) < NearestPlayer->GetDistanceTo(Pawn))
                NearestPlayer = FactionBots[i]->Pawn;
        }

        return NearestPlayer ? NearestPlayer->K2_GetActorLocation() : FVector();
    }

    // ИСПРАВЛЕН БАГ: было AlivePlayers[i] вместо AliveBots[i]
    AActor* GetNearestPlayerActor() {
        auto GameMode = (AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode;
        AActor* NearestPlayer = nullptr;
        float MinDist = FLT_MAX;

        for (size_t i = 0; i < GameMode->AlivePlayers.Num(); i++)
        {
            if (!GameMode->AlivePlayers[i]->Pawn) continue;
            AFortPlayerStateAthena* PS = (AFortPlayerStateAthena*)GameMode->AlivePlayers[i]->PlayerState;
            if (!PS || PS->TeamIndex == PlayerState->TeamIndex) continue;

            float Dist = GameMode->AlivePlayers[i]->Pawn->GetDistanceTo(Pawn);
            if (Dist < MinDist) {
                MinDist = Dist;
                NearestPlayer = GameMode->AlivePlayers[i]->Pawn;
            }
        }

        for (size_t i = 0; i < GameMode->AliveBots.Num(); i++)
        {
            if (!GameMode->AliveBots[i]->Pawn || GameMode->AliveBots[i]->Pawn == Pawn) continue;
            AFortPlayerStateAthena* PS = (AFortPlayerStateAthena*)GameMode->AliveBots[i]->PlayerState;
            if (!PS || PS->TeamIndex == PlayerState->TeamIndex) continue;

            float Dist = GameMode->AliveBots[i]->Pawn->GetDistanceTo(Pawn);
            if (Dist < MinDist) {
                MinDist = Dist;
                NearestPlayer = GameMode->AliveBots[i]->Pawn;
            }
        }

        for (size_t i = 0; i < FactionBots.size(); i++) {
            if (!FactionBots[i]->Pawn || FactionBots[i]->Pawn == Pawn) continue;
            float Dist = FactionBots[i]->Pawn->GetDistanceTo(Pawn);
            if (Dist < MinDist) {
                MinDist = Dist;
                NearestPlayer = FactionBots[i]->Pawn;
            }
        }

        return NearestPlayer;
    }

    void UpdateLootableReservation(AActor* Lootable, bool RemoveReservation) {
        if (RemoveReservation && !TargetLootable) return;
        if (!RemoveReservation) {
            if (!Lootable) return;
            TargetLootable = Lootable;
            Lootable->bHidden = true;
        }
        else {
            TargetLootable->bHidden = false;
            TargetLootable = nullptr;
        }
    }

    void __fastcall GiveLategameLoadout()
    {
        if (UFortItemDefinition* AssaultRifleDef = Inventory::LoadWeapon(Assault_rifle))
        {
            GiveItemBot(AssaultRifleDef, 1, Looting::GetClipSize(AssaultRifleDef));
            if (auto* RangedDef = (UFortWeaponRangedItemDefinition*)AssaultRifleDef)
            {
                UFortWorldItemDefinition* AmmoDef = RangedDef->GetAmmoWorldItemDefinition_BP();
                if (AmmoDef) GiveItemBot(AmmoDef, 200, 0);
            }
        }

        if (UFortItemDefinition* ShotGunDef = Inventory::LoadWeapon(Shotgun))
        {
            GiveItemBot(ShotGunDef, 1, Looting::GetClipSize(ShotGunDef));
            if (auto* RangedDef = (UFortWeaponRangedItemDefinition*)ShotGunDef)
            {
                UFortWorldItemDefinition* AmmoDef = RangedDef->GetAmmoWorldItemDefinition_BP();
                if (AmmoDef) GiveItemBot(AmmoDef, 120, 0);
            }
        }

        if (UFortItemDefinition* RandomDef = Inventory::LoadWeapon(Mixed))
        {
            GiveItemBot(RandomDef, 1, Looting::GetClipSize(RandomDef));
            if (auto* RangedDef = (UFortWeaponRangedItemDefinition*)RandomDef)
            {
                UFortWorldItemDefinition* AmmoDef = RangedDef->GetAmmoWorldItemDefinition_BP();
                if (AmmoDef) GiveItemBot(AmmoDef, 120, 0);
            }
        }

        if (auto Consumable1Def = Inventory::LoadWeapon(Consumables))
            GiveItemBot(Consumable1Def, 3, 0);
        if (auto Consumable2Def = Inventory::LoadWeapon(Consumables))
            GiveItemBot(Consumable2Def, 3, 0);
        if (auto TrapDef = Inventory::LoadWeapon(Traps))
            GiveItemBot(TrapDef, 3, 0);

        static UFortItemDefinition* WoodDef = StaticLoadObject<UFortItemDefinition>("/Game/Items/ResourcePickups/WoodItemData.WoodItemData");
        GiveItemBot(WoodDef, 500, 0);
        static UFortItemDefinition* StoneDef = StaticLoadObject<UFortItemDefinition>("/Game/Items/ResourcePickups/StoneItemData.StoneItemData");
        GiveItemBot(StoneDef, 500, 0);
        static UFortItemDefinition* MetalDef = StaticLoadObject<UFortItemDefinition>("/Game/Items/ResourcePickups/MetalItemData.MetalItemData");
        GiveItemBot(MetalDef, 500, 0);
    }
};

class BotsBTService_AIEvaluator {
public:
    bool IsStressed(AFortPlayerPawnAthena* Pawn, ABP_PhoebePlayerController_C* PC) {
        if (Pawn->GetHealth() <= 75) return true;
        return false;
    }

public:
    void Tick(PlayerBot* bot) {
        FVector BotPos = bot->Pawn->K2_GetActorLocation();
        FVector Vel = bot->Pawn->GetVelocity();
        float Speed = sqrtf(Vel.X * Vel.X + Vel.Y * Vel.Y);

                if (bot->BotState >= EBotState::Landed && bot->BotState != EBotState::MovingToSafeZone) {
            auto GameStateLocal = (AFortGameStateAthena*)UWorld::GetWorld()->GameState;
            if (GameStateLocal && GameStateLocal->SafeZoneIndicator) {
                float DistToCenter = UKismetMathLibrary::GetDefaultObj()->Vector_Distance(
                    BotPos, GameStateLocal->SafeZoneIndicator->NextCenter);
                float ZoneRadius = GameStateLocal->SafeZoneIndicator->Radius;
                // Если бот за пределами зоны (с небольшим запасом 200 юнитов для плавности)
                if (DistToCenter > ZoneRadius + 200.f) {
                    bot->BotState = EBotState::MovingToSafeZone;
                    return; // Сразу выходим, давая MovingToSafeZone взять управление
                }
            }
        }

        if (bot->tick_counter % 60 == 0) {
            AActor* NearestChest = bot->FindNearestChest();
            AActor* NearestPickup = (AActor*)bot->FindNearestPickup();

            if (NearestChest && NearestPickup) {
                AActor* Nearest = NearestChest;
                ELootableType NearestLootable = ELootableType::Chest;
                if (NearestChest->GetDistanceTo(bot->Pawn) > NearestPickup->GetDistanceTo(bot->Pawn)) {
                    NearestLootable = ELootableType::Pickup;
                    Nearest = NearestPickup;
                }
                if (bot->TargetLootable) {
                    if (Nearest != bot->TargetLootable) {
                        bot->UpdateLootableReservation(nullptr, true);
                        bot->UpdateLootableReservation(Nearest, false);
                        bot->LootTargetStartTime = 0.f;
                        bot->LastLootTargetDistance = 0.f;
                    }
                }
                else {
                    bot->UpdateLootableReservation(Nearest, false);
                    bot->LootTargetStartTime = 0.f;
                    bot->LastLootTargetDistance = 0.f;
                }
                bot->TargetLootableType = NearestLootable;
            }

               if (bot->BotState != EBotState::LookingForPlayers) {
        bot->NearestPlayerActor = bot->GetNearestPlayerActor();
        }
    }   

        if (bot->tick_counter % 60 == 0) {
            bot->PC->K2_ClearFocus();
        }

        bot->bIsStressed = IsStressed(bot->Pawn, bot->PC);

        if (bot->bIsCurrentlyStrafing) {
            bot->ForceStrafe(false);
        }

        // Спринт каждые 40 тиков
        if ((bot->tick_counter % 40 == 0) && Speed >= 50 && bot->BotState > EBotState::Landed) {
            bot->Run();
        }
    }
};

class BotsBTService_Warmup{
public:
    void DetermineBotWarmupChoice(PlayerBot* bot) {
        if (UKismetMathLibrary::GetDefaultObj()->RandomBool()) {
            bot->BotWarmupChoice = EBotWarmupChoice::Emote;
        }
        else {
            bot->BotWarmupChoice = EBotWarmupChoice::MoveToPlayerEmote;
        }
    }

public:
    void Tick(PlayerBot* bot) {
        if (bot->BotWarmupChoice == EBotWarmupChoice::MAX) {
            DetermineBotWarmupChoice(bot);
        }
        else if (bot->BotWarmupChoice == EBotWarmupChoice::Emote) {
            if (bot->tick_counter % 300 == 0) {
                bot->Emote();
            }
        }
        else {
            if (bot->tick_counter % 150 == 0) {
                bot->NearestPlayerActor = bot->GetNearestPlayerActor();
                auto BotPos = bot->Pawn->K2_GetActorLocation();
                if (bot->NearestPlayerActor) {
                    FVector Nearest = bot->NearestPlayerActor->K2_GetActorLocation();
                    if (!Nearest.IsZero()) {
                        float Dist = UKismetMathLibrary::GetDefaultObj()->Vector_Distance(BotPos, Nearest);
                        if (Dist < 200.f + rand() % 300) {
                            bot->LookAt(bot->NearestPlayerActor);
                            if (UKismetMathLibrary::GetDefaultObj()->RandomBool()) {
                                bot->Emote();
                            }
                        }
                        else {
                            bot->LookAt(bot->NearestPlayerActor);
                            bot->PC->MoveToActor(bot->NearestPlayerActor, 100, true, false, true, nullptr, true);
                        }
                    }
                }
            }
        }
    }
};

class BotsBTService_AIDropZone {
public:
    void ChooseDropZone(PlayerBot* bot) {
        if (DropZoneLocations.empty()) return;
        bot->TargetDropZone = DropZoneLocations[rand() % DropZoneLocations.size()];
        bot->TargetDropZone.X += UKismetMathLibrary::GetDefaultObj()->RandomFloatInRange(-800.f, 800.f);
        bot->TargetDropZone.Y += UKismetMathLibrary::GetDefaultObj()->RandomFloatInRange(-800.f, 800.f);
    }

public:
    void Tick(PlayerBot* bot) {
        auto GameState = (AFortGameStateAthena*)UWorld::GetWorld()->GameState;
        auto Math = (UKismetMathLibrary*)UKismetMathLibrary::StaticClass()->DefaultObject;
        auto GameMode = (AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode;

        if (bot->TargetDropZone.IsZero()) {
            ChooseDropZone(bot);
            return;
        }

        if (bot->BotState == EBotState::Bus) {
            bot->Pawn->SetShield(0);
            if (bot->bHasJumpedFromBus) {
                bot->BotState = EBotState::Skydiving;
                return;
            }

            if (!bot->bHasThankedBusDriver && GameState->GamePhase == EAthenaGamePhase::Aircraft && Math->RandomBoolWithWeight(0.0005f))
            {
                bot->bHasThankedBusDriver = true;
                bot->PC->ThankBusDriver();
            }

            AActor* Bus = GameState->GetAircraft(0);
            if (!Bus) return;

            FVector BusLocation = Bus->K2_GetActorLocation();
            FVector DropTarget = bot->TargetDropZone;
            DropTarget.Z = BusLocation.Z;

            if (GameState->GamePhase > EAthenaGamePhase::Aircraft) {
                Log("Force Jump");
                bot->Pawn->K2_TeleportTo(DropTarget, {});
                bot->Pawn->BeginSkydiving(true);
                bot->BotState = EBotState::Skydiving;
                bot->bHasJumpedFromBus = true;
                return;
            }

            float DistanceToDrop = Math->Vector_Distance(BusLocation, DropTarget);

            if (DistanceToDrop < bot->ClosestDistToDropZone) {
                bot->ClosestDistToDropZone = DistanceToDrop;
            }
            else {
                if (!bot->bHasThankedBusDriver && Math->RandomBoolWithWeight(0.5f)) {
                    bot->bHasThankedBusDriver = true;
                    bot->PC->ThankBusDriver();
                }
                if (Math->RandomBoolWithWeight(0.75)) {
                    bot->Pawn->K2_TeleportTo(GameState->GetAircraft(0)->K2_GetActorLocation(), {});
                    bot->Pawn->BeginSkydiving(true);
                    bot->BotState = EBotState::Skydiving;
                    bot->bHasJumpedFromBus = true;
                }
            }
            return;
        }

        auto BotPos = bot->Pawn->K2_GetActorLocation();
        if (!bot->TargetDropZone.IsZero()) {
            bot->TargetDropZone.Z = BotPos.Z;
        }

        // ИСПРАВЛЕНО: Быстрый прыжок с автобуса через AddMovementInput
        if (bot->BotState == EBotState::Skydiving) {
            if (!bot->Pawn->bIsSkydiving) {
                bot->BotState = EBotState::Gliding;
            }

            if (!bot->TargetDropZone.IsZero()) {
                // Поворачиваем к цели
                auto TestRot = Math->FindLookAtRotation(BotPos, bot->TargetDropZone);
                bot->PC->SetControlRotation(TestRot);
                bot->PC->K2_SetActorRotation(TestRot, true);

                // Движение вперёд + вниз
                FVector Forward = bot->Pawn->GetActorForwardVector();
                FVector Down = Math->NegateVector(bot->Pawn->GetActorUpVector());
                FVector MoveDir;
                MoveDir.X = Forward.X + Down.X * 0.7f;
                MoveDir.Y = Forward.Y + Down.Y * 0.7f;
                MoveDir.Z = Forward.Z + Down.Z * 1.2f;
                bot->Pawn->AddMovementInput(MoveDir, 1.5f, true);
            }
        }
        else if (bot->BotState == EBotState::Gliding) {
            if (bot->Pawn->bIsSkydiving) {
                bot->BotState = EBotState::Skydiving;
            }

            FVector Vel = bot->Pawn->GetVelocity();
            float Speed = Vel.Z;
            if (Speed == 0.f || bot->Pawn->bIsInWaterVolume) {
                bot->BotState = EBotState::Landed;
            }

            if (!bot->TargetDropZone.IsZero()) {
                auto TestRot = Math->FindLookAtRotation(BotPos, bot->TargetDropZone);
                bot->PC->SetControlRotation(TestRot);
                bot->PC->K2_SetActorRotation(TestRot, true);

                // Движение вперёд при глидировании
                FVector Forward = bot->Pawn->GetActorForwardVector();
                bot->Pawn->AddMovementInput(Forward, 1.2f, true);
            }
        }
    }
};

class BotsBTService_Loot {
public:
    void Tick(PlayerBot* bot) {
        if (!bot->TargetLootable) {
            AActor* NewChest = bot->FindNearestChest();
            AActor* NewPickup = (AActor*)bot->FindNearestPickup();
            if (NewChest || NewPickup) {
                bool bPickChest = NewChest && (!NewPickup || 
                    NewChest->GetDistanceTo(bot->Pawn) <= NewPickup->GetDistanceTo(bot->Pawn));
                AActor* NewTarget = bPickChest ? NewChest : NewPickup;
                bot->UpdateLootableReservation(NewTarget, false);
                bot->TargetLootableType = bPickChest ? ELootableType::Chest : ELootableType::Pickup;
            } else {
                bot->BotState = EBotState::LookingForPlayers;
            }
            return;
        }

        if (bot->TargetLootable && !bot->TargetLootable->bActorIsBeingDestroyed) {

            FVector BotLoc = bot->Pawn->K2_GetActorLocation();
            if (!BotLoc.IsZero()) {
                float Dist = UKismetMathLibrary::GetDefaultObj()->Vector_Distance(BotLoc, bot->TargetLootable->K2_GetActorLocation());
                auto BotPos = bot->Pawn->K2_GetActorLocation();
                auto TestRot = UKismetMathLibrary::GetDefaultObj()->FindLookAtRotation(BotPos, bot->TargetLootable->K2_GetActorLocation());
                bot->PC->SetControlRotation(TestRot);
                bot->PC->K2_SetActorRotation(TestRot, true);
                bot->LookAt(bot->TargetLootable);

                float CurrentTime = UGameplayStatics::GetDefaultObj()->GetTimeSeconds(UWorld::GetWorld());

                if (bot->TargetLootable) {
                    FVector BotLoc2 = bot->Pawn->K2_GetActorLocation();
                    float Dist2 = UKismetMathLibrary::GetDefaultObj()->Vector_Distance(BotLoc2, bot->TargetLootable->K2_GetActorLocation());
                    if (bot->LootTargetStartTime == 0.f) {
                        bot->LootTargetStartTime = CurrentTime;
                        bot->LastLootTargetDistance = Dist2;
                    }
                    float Elapsed = CurrentTime - bot->LootTargetStartTime;

                    if ((Elapsed > 8.f && Dist2 > bot->LastLootTargetDistance - 100.f)) {
                        bot->UpdateLootableReservation(nullptr, true);
                        bot->TargetLootable = nullptr;
                        bot->LootTargetStartTime = 0.f;
                        return;
                    }
                    if (Dist2 < 300.f) {
                        bot->LootTargetStartTime = 0.f;
                    }
                    bot->LastLootTargetDistance = Dist2;
                }

                if (Dist < 300.f) {
                    bot->PC->StopMovement();
                    // УБРАНА СТРЕЛЬБА ПРИ ЛУТАНИИ
                    bot->Pawn->PawnStopFire(0);

                    if (!bot->TimeToNextAction || !bot->Pawn->bStartedInteractSearch && bot->TargetLootableType == ELootableType::Chest) {
                        bot->TimeToNextAction = CurrentTime;
                        bot->Pawn->bStartedInteractSearch = true;
                        bot->Pawn->OnRep_StartedInteractSearch();
                    }
                    else if (CurrentTime - bot->TimeToNextAction >= 1.5f && bot->TargetLootableType == ELootableType::Chest) {
                        Looting::SpawnLoot((ABuildingContainer*)bot->TargetLootable);
                        bot->TargetLootable->bHidden = true;
                        bot->TargetLootable = nullptr;
                        AFortPickup* Pickup = bot->FindNearestPickup();
                        if (Pickup)
                        {
                            bot->PickupAllItemsInRange();
                            bot->SimpleSwitchToWeapon();
                        }
                        bot->Pawn->bStartedInteractSearch = false;
                        bot->Pawn->OnRep_StartedInteractSearch();
                        bot->TimeToNextAction = 0;
                        bot->BotState = EBotState::LookingForPlayers;
                    }
                    else if (bot->TargetLootableType == ELootableType::Pickup) {
                        AFortPickup* Pickup = bot->FindNearestPickup();
                        if (Pickup) {
                            bot->Pickup(Pickup);
                        }
                        bot->TimeToNextAction = 0;
                        bot->BotState = EBotState::LookingForPlayers;
                    }
                }
                else {
                    bot->PC->MoveToActor(bot->TargetLootable, 50, true, false, true, nullptr, true);
                    if (bot->tick_counter % 30 == 0) {
                        bot->Run();
                    }
                }
            }
        }
    }
};

namespace PlayerBots {
    void FreeDeadBots() {
        for (size_t i = 0; i < PlayerBotArray.size();)
        {
            if (PlayerBotArray[i]->bIsDead) {
                delete PlayerBotArray[i];
                PlayerBotArray.erase(PlayerBotArray.begin() + i);
                Log("Freed a dead bot from the array!");
            }
            else {
                ++i;
            }
        }
    }

    void SpawnPlayerBots(AActor* SpawnLocator, EBotState StartingState = EBotState::Warmup, AFortPlayerControllerAthena* TeamPC = nullptr)
    {
        if (!Globals::bBotsEnabled)
            return;

        auto GameState = (AFortGameStateAthena*)UWorld::GetWorld()->GameState;
        static auto BotBP = StaticLoadObject<UClass>("/Game/Athena/AI/Phoebe/BP_PlayerPawn_Athena_Phoebe.BP_PlayerPawn_Athena_Phoebe_C");
        static UBehaviorTree* BehaviorTree = StaticLoadObject<UBehaviorTree>("/Game/Athena/AI/Phoebe/BehaviorTrees/BT_Phoebe.BT_Phoebe");
        static UBlackboardData* BlackboardData = StaticLoadObject<UBlackboardData>("/Game/Athena/AI/Phoebe/BehaviorTrees/BB_Phoebe.BB_Phoebe");

        if (!BotBP || !BehaviorTree)
            return;

        PlayerBot* bot = new PlayerBot{};
        AFortGameModeAthena* GameMode = (AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode;
        static auto BotMutator = (AFortAthenaMutator_Bots*)GameMode->ServerBotManager->CachedBotMutator;

        bot->Pawn = BotMutator->SpawnBot(BotBP, SpawnLocator, SpawnLocator->K2_GetActorLocation(), SpawnLocator->K2_GetActorRotation(), false);

        if (!bot->Pawn || !bot->Pawn->Controller)
            return;

        bot->PC = Cast<ABP_PhoebePlayerController_C>(bot->Pawn->Controller);
        bot->PlayerState = Cast<AFortPlayerStateAthena>(bot->PC->PlayerState);

        if (!bot->PC || !bot->PlayerState)
            return;

            // Если бот спавнится не к игроку в тиммейты, даем ему уникальную команду (для Соло)
            if (!TeamPC && Misc::MaxPlayersOnTeam <= 1) {
            uint8 OldTeamIdx = bot->PlayerState->TeamIndex; // Сохраняем старый индекс
            bot->PlayerState->TeamIndex = Misc::NextIdx++;
            bot->PlayerState->SquadId = bot->PlayerState->TeamIndex;
            bot->PlayerState->OnRep_TeamIndex(OldTeamIdx); // Передаем старый индекс
            bot->PlayerState->OnRep_SquadId();
        }

        if (!CIDs.empty() && UKismetMathLibrary::GetDefaultObj()->RandomBoolWithWeight(1)) {
            auto CID = CIDs[UKismetMathLibrary::GetDefaultObj()->RandomIntegerInRange(0, CIDs.size() - 1)];
            if (CID->HeroDefinition)
            {
                if (CID->HeroDefinition->Specializations.IsValid())
                {
                    for (size_t i = 0; i < CID->HeroDefinition->Specializations.Num(); i++)
                    {
                        UFortHeroSpecialization* Spec = StaticLoadObject<UFortHeroSpecialization>(UKismetStringLibrary::GetDefaultObj()->Conv_NameToString(CID->HeroDefinition->Specializations[i].ObjectID.AssetPathName).ToString());
                        if (Spec)
                        {
                            for (size_t j = 0; j < Spec->CharacterParts.Num(); j++)
                            {
                                UCustomCharacterPart* Part = StaticLoadObject<UCustomCharacterPart>(UKismetStringLibrary::GetDefaultObj()->Conv_NameToString(Spec->CharacterParts[j].ObjectID.AssetPathName).ToString());
                                if (Part)
                                {
                                    bot->PlayerState->CharacterData.Parts[(uintptr_t)Part->CharacterPartType] = Part;
                                }
                            }
                        }
                    }
                }
            }
            if (CID) {
                bot->PC->CosmeticLoadoutBC.Character = CID;
            }
        }
        if (!Backpacks.empty() && UKismetMathLibrary::GetDefaultObj()->RandomBoolWithWeight(0.5)) {
            auto Backpack = Backpacks[UKismetMathLibrary::GetDefaultObj()->RandomIntegerInRange(0, Backpacks.size() - 1)];
            for (size_t j = 0; j < Backpack->CharacterParts.Num(); j++)
            {
                UCustomCharacterPart* Part = Backpack->CharacterParts[j];
                if (Part)
                {
                    bot->PlayerState->CharacterData.Parts[(uintptr_t)Part->CharacterPartType] = Part;
                }
            }
        }
        if (!Gliders.empty()) {
            auto Glider = Gliders[UKismetMathLibrary::GetDefaultObj()->RandomIntegerInRange(0, Gliders.size() - 1)];
            bot->PC->CosmeticLoadoutBC.Glider = Glider;
        }
        if (!Contrails.empty() && UKismetMathLibrary::GetDefaultObj()->RandomBoolWithWeight(0.95)) {
            auto Contrail = Contrails[UKismetMathLibrary::GetDefaultObj()->RandomIntegerInRange(0, Contrails.size() - 1)];
            bot->PC->CosmeticLoadoutBC.SkyDiveContrail = Contrail;
        }
        for (size_t i = 0; i < Dances.size(); i++)
        {
            bot->PC->CosmeticLoadoutBC.Dances.Add(Dances.at(i));
        }

        bot->Pawn->CosmeticLoadout = bot->PC->CosmeticLoadoutBC;
        bot->Pawn->OnRep_CosmeticLoadout();

        if (Pickaxes.empty()) {
            Log("Pickaxes array is empty!");
            UFortWeaponMeleeItemDefinition* PickDef = StaticLoadObject<UFortWeaponMeleeItemDefinition>("/Game/Athena/Items/Weapons/WID_Harvest_Pickaxe_Athena_C_T01.WID_Harvest_Pickaxe_Athena_C_T01");
            if (PickDef) {
                bot->GiveItemBot(PickDef);
                auto Entry = bot->GetEntry(PickDef);
                bot->Pawn->EquipWeaponDefinition((UFortWeaponMeleeItemDefinition*)Entry->ItemDefinition, Entry->ItemGuid);
            }
            else {
                Log("Default Pickaxe dont exist!");
            }
        }
        else {
            auto PickDef = Pickaxes[UKismetMathLibrary::GetDefaultObj()->RandomIntegerInRange(0, Pickaxes.size() - 1)];
            if (!PickDef)
            {
                Log("Cooked!");
                UFortWeaponMeleeItemDefinition* PickDef = StaticLoadObject<UFortWeaponMeleeItemDefinition>("/Game/Athena/Items/Weapons/WID_Harvest_Pickaxe_Athena_C_T01.WID_Harvest_Pickaxe_Athena_C_T01");
                if (PickDef) {
                    bot->GiveItemBot(PickDef);
                    auto Entry = bot->GetEntry(PickDef);
                    bot->Pawn->EquipWeaponDefinition((UFortWeaponMeleeItemDefinition*)Entry->ItemDefinition, Entry->ItemGuid);
                }
                else {
                    Log("Default Pickaxe dont exist!");
                }
            }
            else {
                if (PickDef && PickDef->WeaponDefinition)
                {
                    bot->GiveItemBot(PickDef->WeaponDefinition);
                }
                auto Entry = bot->GetEntry(PickDef->WeaponDefinition);
                if (Entry) {
                    bot->Pawn->EquipWeaponDefinition((UFortWeaponItemDefinition*)Entry->ItemDefinition, Entry->ItemGuid);
                }
            }
        }

        if (BotDisplayNames.size() != 0) {
            std::srand(static_cast<unsigned int>(std::time(0)));
            int randomIndex = std::rand() % BotDisplayNames.size();
            std::string rdName = BotDisplayNames[randomIndex];
            BotDisplayNames.erase(BotDisplayNames.begin() + randomIndex);

            int size_needed = MultiByteToWideChar(CP_UTF8, 0, rdName.c_str(), (int)rdName.size(), NULL, 0);
            std::u16string wideString(size_needed, 0);
            MultiByteToWideChar(CP_UTF8, 0, rdName.c_str(), (int)rdName.size(), &wideString[0], size_needed);

            FString CVName = FString(wideString.c_str());
            GameMode->ChangeName(bot->PC, CVName, true);
            bot->DisplayName = CVName;
            bot->PlayerState->OnRep_PlayerName();
        }

        for (auto SkillSet : bot->PC->DigestedBotSkillSets)
        {
            if (!SkillSet)
                continue;
            if (auto AimingSkill = Cast<UFortAthenaAIBotAimingDigestedSkillSet>(SkillSet))
                bot->PC->CacheAimingDigestedSkillSet = AimingSkill;
            if (auto AttackingSkill = Cast<UFortAthenaAIBotAttackingDigestedSkillSet>(SkillSet))
                bot->PC->CacheAttackingSkillSet = AttackingSkill;
            if (auto HarvestSkill = Cast<UFortAthenaAIBotHarvestDigestedSkillSet>(SkillSet))
                bot->PC->CacheHarvestDigestedSkillSet = HarvestSkill;
            if (auto InventorySkill = Cast<UFortAthenaAIBotInventoryDigestedSkillSet>(SkillSet))
                bot->PC->CacheInventoryDigestedSkillSet = InventorySkill;
            if (auto LootingSkill = Cast<UFortAthenaAIBotLootingDigestedSkillSet>(SkillSet))
                bot->PC->CacheLootingSkillSet = LootingSkill;
            if (auto MovementSkill = Cast<UFortAthenaAIBotMovementDigestedSkillSet>(SkillSet))
                bot->PC->CacheMovementSkillSet = MovementSkill;
            if (auto PerceptionSkill = Cast<UFortAthenaAIBotPerceptionDigestedSkillSet>(SkillSet))
                bot->PC->CachePerceptionDigestedSkillSet = PerceptionSkill;
            if (auto PlayStyleSkill = Cast<UFortAthenaAIBotPlayStyleDigestedSkillSet>(SkillSet))
                bot->PC->CachePlayStyleSkillSet = PlayStyleSkill;
        }

        bot->PC->BehaviorTree = BehaviorTree;
        bot->PC->RunBehaviorTree(BehaviorTree);
        bot->PC->UseBlackboard(BehaviorTree->BlackboardAsset, &bot->PC->Blackboard);
        bot->PC->UseBlackboard(BehaviorTree->BlackboardAsset, &bot->PC->Blackboard1);

        static auto Name1 = UKismetStringLibrary::Conv_StringToName(TEXT("AIEvaluator_Global_GamePhaseStep"));
        static auto Name2 = UKismetStringLibrary::Conv_StringToName(TEXT("AIEvaluator_Global_GamePhase"));
        bot->PC->Blackboard->SetValueAsEnum(Name1, (uint8)EAthenaGamePhaseStep::Warmup);
        bot->PC->Blackboard->SetValueAsEnum(Name2, (uint8)EAthenaGamePhase::Warmup);

        bot->PC->PathFollowingComponent->MyNavData = ((UAthenaNavSystem*)UWorld::GetWorld()->NavigationSystem)->MainNavData;
        bot->PC->PathFollowingComponent->OnNavDataRegistered(((UAthenaNavSystem*)UWorld::GetWorld()->NavigationSystem)->MainNavData);

        bot->PlayerState->OnRep_CharacterData();
        bot->Pawn->CapsuleComponent->SetGenerateOverlapEvents(true);
        bot->Pawn->CharacterMovement->bCanWalkOffLedges = true;
        bot->Pawn->SetMaxHealth(100);
        bot->Pawn->SetHealth(100);
        bot->Pawn->SetMaxShield(100);
        bot->Pawn->SetShield(0);
        bot->Pawn->bCanBeDamaged = true;

        bot->BotState = StartingState;

        if (TeamPC) {
            AFortPlayerStateAthena* TeamPS = (AFortPlayerStateAthena*)TeamPC->PlayerState;
            uint8 OldTeamIdx = bot->PlayerState->TeamIndex;
            bot->PlayerState->bIsABot = false;
            bot->PlayerState->TeamIndex = TeamPS->TeamIndex;
            bot->PlayerState->SquadId = TeamPS->SquadId;
            bot->PlayerState->OnRep_TeamIndex(OldTeamIdx);
            bot->PlayerState->OnRep_SquadId();
            TeamPS->PlayerTeam->TeamMembers.Add(bot->PC);
            bot->PlayerState->PlayerTeam = TeamPS->PlayerTeam;
            bot->PlayerState->OnRep_PlayerTeam();
            TeamPS->OnRep_PlayerTeam();
            bot->PlayerState->bIsABot = true;
        }

        PlayerBotArray.push_back(bot);
    }

    void Tick() {
        if (!PlayerBotArray.empty()) {
            if (UKismetMathLibrary::GetDefaultObj()->RandomBoolWithWeight(0.001f))
            {
                FreeDeadBots();
            }
        }
        else {
            return;
        }

        auto GameState = (AFortGameStateAthena*)UWorld::GetWorld()->GameState;
        auto Math = (UKismetMathLibrary*)UKismetMathLibrary::StaticClass()->DefaultObject;
        auto GameMode = (AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode;

        for (auto bot : PlayerBotArray) {
            if (!bot || !bot->Pawn || !bot->PC || !bot->PlayerState)
                continue;

            if (GameState->GamePhase <= EAthenaGamePhase::Warmup) {
                if (bot->tick_counter <= 150) {
                    bot->tick_counter++;
                    continue;
                }
            }

            if (bot->BotState > EBotState::Bus) {
                BotsBTService_AIEvaluator Evaluator;
                Evaluator.Tick(bot);
            }

            if (bot->BotState == EBotState::Warmup) {
                BotsBTService_Warmup Warmup;
                Warmup.Tick(bot);
            }
            else if (bot->BotState == EBotState::PreBus) {
                bot->Pawn->SetHealth(100);
                bot->Pawn->SetShield(100);
                if (!bot->bHasThankedBusDriver && UKismetMathLibrary::GetDefaultObj()->RandomBoolWithWeight(0.0005f))
                {
                    bot->bHasThankedBusDriver = true;
                    bot->PC->ThankBusDriver();
                }
            }
            else if (bot->BotState == EBotState::Bus || bot->BotState == EBotState::Skydiving || bot->BotState == EBotState::Gliding) {
                if (Globals::LateGame && UKismetMathLibrary::GetDefaultObj()->RandomBoolWithWeight(0.5f)) {
                    if (bot->bHasJumpedFromBus) {
                        bot->BotState = EBotState::LookingForPlayers;
                        return;
                    }
                    bot->Pawn->K2_TeleportTo(GameState->GetAircraft(0)->K2_GetActorLocation(), {});
                    bot->Pawn->BeginSkydiving(true);
                    bot->GiveLategameLoadout();
                    bot->BotState = EBotState::LookingForPlayers;
                    bot->bHasJumpedFromBus = true;
                    return;
                }
                BotsBTService_AIDropZone DropZoneEv;
                DropZoneEv.Tick(bot);
            }
            else if (bot->BotState == EBotState::Landed) {
                bot->BotState = EBotState::Looting;
            }
            else if (bot->BotState == EBotState::Fleeing) {
                FVector BotLoc = bot->Pawn->K2_GetActorLocation();
                FVector Nearest = bot->FindNearestPlayerOrBot();
                if (!Nearest.IsZero()) {
                    float Dist = Math->Vector_Distance(BotLoc, Nearest);
                    if (bot->HasGun()) {
                        bot->BotState = EBotState::LookingForPlayers;
                        continue;
                    }
                    if (Dist < 3000.f) {
                        auto TestRot = Math->FindLookAtRotation(Nearest, BotLoc);
                        bot->PC->SetControlRotation(TestRot);
                        bot->PC->K2_SetActorRotation(TestRot, true);
                        bot->Pawn->AddMovementInput(bot->Pawn->GetActorForwardVector(), 1.2f, true);
                    }
                    else {
                        bot->BotState = EBotState::Looting;
                    }
                }
                else {
                    bot->BotState = EBotState::Looting;
                }
            }
            else if (bot->BotState == EBotState::Looting) {
                BotsBTService_Loot LootAI;
                LootAI.Tick(bot);
            }
            // УЛУЧШЕННЫЙ БОЙ: аим с предсказанием, стрельба по всем, спринт
            else if (bot->BotState == EBotState::LookingForPlayers) {
                if (!bot->HasGun()) {
                    bot->BotState = EBotState::Looting;
                    continue;
                }
                if (bot->IsPickaxeEquiped()) {
                    bot->SimpleSwitchToWeapon();
                    if (bot->IsPickaxeEquiped()) {
                        bot->BotState = EBotState::Looting;
                    }
                    continue;
                }

                // Обновляем цель каждые 20 тиков
                if (bot->tick_counter % 20 == 0) {
                    AActor* NewTarget = bot->GetNearestPlayerActor();
                    if (NewTarget != bot->NearestPlayerActor) {
                        // Смена цели — сбрасываем счётчики анти-стрельбы
                        bot->NoApproachTicks = 0;
                        bot->LastTargetDist = 0.f;
                    }
                    bot->NearestPlayerActor = NewTarget;
                }

                FVector BotLoc = bot->Pawn->K2_GetActorLocation();

                // === АНТИ-ЗАСТРЕВАНИЕ: проверка движения бота ===
                FVector BotVel = bot->Pawn->GetVelocity();
                float BotSpeed = sqrtf(BotVel.X * BotVel.X + BotVel.Y * BotVel.Y);
                if (BotSpeed < 30.f) {
                    bot->NoMoveTicks++;
                } else {
                    bot->NoMoveTicks = 0;
                }
                // Бот стоит 3 секунды (90 тиков при 30 тиков/сек) → застрял
                if (bot->NoMoveTicks > 90) {
                    bot->NoMoveTicks = 0;
                    bot->PC->StopMovement();
                    bot->Pawn->PawnStopFire(0);
                    bot->NearestPlayerActor = nullptr;
                    bot->BotState = EBotState::Looting;
                    continue;
                }

                if (bot->NearestPlayerActor && !bot->NearestPlayerActor->bActorIsBeingDestroyed) {
                    FVector Nearest = bot->NearestPlayerActor->K2_GetActorLocation();;          

                    if (!Nearest.IsZero()) {
                        float Dist = Math->Vector_Distance(BotLoc, Nearest);

                        // === АНТИ-СТРЕЛЬБА СКВОЗЬ СТЕНЫ: проверка прогресса сближения ===
                        if (bot->LastTargetDist > 0.f) {
                            if (Dist >= bot->LastTargetDist - 50.f) {
                                bot->NoApproachTicks++;  // дистанция не уменьшается
                            } else {
                                bot->NoApproachTicks = 0;
                            }
                        }
                        bot->LastTargetDist = Dist;

                        // Цель недоступна 3 секунды → сброс
                        if (bot->NoApproachTicks > 90) {
                            bot->NoApproachTicks = 0;
                            bot->LastTargetDist = 0.f;
                            bot->Pawn->PawnStopFire(0);
                            bot->NearestPlayerActor = nullptr;
                            bot->PC->StopMovement();
                            continue;
                        }

                        // Прицеливание
                        FRotator Rot = Math->FindLookAtRotation(BotLoc, Nearest);
                        bot->PC->SetControlRotation(Rot);
                        bot->PC->K2_SetActorRotation(Rot, true);

                        // Стрельба только если есть прогресс сближения (не за стеной)
                        if (Dist < 5000.f && bot->NoApproachTicks < 60) {
                            bot->Pawn->PawnStartFire(0);
                        }
                        else {
                            bot->Pawn->PawnStopFire(0);
                        }

                        // Strafe иногда
                        if (Math->RandomBoolWithWeight(0.03f)) {
                            bot->ForceStrafe(true);
                        }

                        // Движение
                        if (Dist < 1000.f) {
                            FVector BackVector = bot->Pawn->GetActorForwardVector() * -1.0f;
                            bot->Pawn->AddMovementInput(BackVector, 1.1f, true);
                        }
                        else if (!bot->bIsStressed) {
                            bot->PC->MoveToActor(bot->NearestPlayerActor, Math->RandomFloatInRange(400, 1500), true, false, true, nullptr, true);
                        }
                        else {
                            bot->Pawn->AddMovementInput((bot->Pawn->GetActorForwardVector() * -1.0f), 1.2f, true);
                        }
                    }
                }
                else {
                    bot->Pawn->PawnStopFire(0);
                }
            }
                else if (bot->BotState == EBotState::MovingToSafeZone) {
                    FVector BotLoc = bot->Pawn->K2_GetActorLocation();

                    // Проверяем в зоне ли бот
                    bool bInZone = true;
                    if (GameState && GameState->SafeZoneIndicator) {
                        float DistToCenter = Math->Vector_Distance(BotLoc, GameState->SafeZoneIndicator->NextCenter);
                        // Запас 200 юнитов, как в Evaluator, для предотвращения осцилляции
                        if (DistToCenter > GameState->SafeZoneIndicator->Radius + 200.f) {
                            bInZone = false;
                        }
                    }

                    if (!bInZone) {
                        // ВНЕ ЗОНЫ: бежим в зону, ИГНОРИРУЕМ врагов
                        if (GameState && GameState->SafeZoneIndicator) {
                            FVector ZoneCenter = GameState->SafeZoneIndicator->NextCenter;

                            // Поворачиваемся к зоне
                            FRotator RotToZone = Math->FindLookAtRotation(BotLoc, ZoneCenter);
                            bot->PC->SetControlRotation(RotToZone);
                            bot->PC->K2_SetActorRotation(RotToZone, true);

                            // MoveToLocation через NavMesh
                            bot->PC->MoveToLocation(ZoneCenter, GameState->SafeZoneIndicator->Radius, true, false, false, true, nullptr, true);

                            // Спринт каждый тик
                            bot->Run();

                            // Если бот застрял — пытаемся выбраться прыжком и рывком
                            FVector Vel = bot->Pawn->GetVelocity();
                            float Speed = sqrtf(Vel.X * Vel.X + Vel.Y * Vel.Y);
                            if (Speed < 30.f && bot->tick_counter % 120 == 0) {
                                bot->Pawn->Jump();
                                FVector SideDir = bot->Pawn->GetActorRightVector() * (Math->RandomBool() ? 1.f : -1.f);
                                bot->Pawn->AddMovementInput(SideDir, 2.f, true);
                            }
                        }
                    }
                    else {
                        // В ЗОНЕ: можно искать врагов или лутаться
                        if (bot->NearestPlayerActor) {
                            float Dist = Math->Vector_Distance(BotLoc, bot->NearestPlayerActor->K2_GetActorLocation());
                            if (Dist < 4000.f) {
                                if (!bot->HasGun()) {
                                    bot->BotState = EBotState::Looting;
                                }
                                else {
                                    bot->BotState = EBotState::LookingForPlayers;
                                }
                            }
                        }
                        else {
                            // В зоне, нет врагов — патрулируем к центру
                            if (GameState && GameState->SafeZoneIndicator) {
                                bot->PC->MoveToLocation(GameState->SafeZoneIndicator->NextCenter, GameState->SafeZoneIndicator->Radius, true, false, false, true, nullptr, true);
                            }
                        }
                    }
                }
                else if (bot->BotState == EBotState::Stuck) {
                    if (!bot->IsPickaxeEquiped()) {
                        bot->EquipPickaxe();
                    }

                    if (!bot->Pawn->bIsCrouched && Math->RandomBoolWithWeight(0.025f)) {
                        bot->Pawn->Crouch(false);
                    }

                    if (Math->RandomBoolWithWeight(0.05f)) {
                        bot->Pawn->UnCrouch(false);
                        bot->Pawn->Jump();
                    }

                    if (bot->StuckBuildingActor && !bot->StuckBuildingActor->bActorIsBeingDestroyed) {
                        if (bot->StuckBuildingActor->GetHealth() <= 1) {
                            bot->Pawn->PawnStopFire(0);
                            bot->StuckBuildingActor = nullptr;
                            bot->BotState = bot->CachedBotState;
                        }
                        else {
                            bot->LookAt(bot->StuckBuildingActor);
                            bot->Pawn->PawnStartFire(0);
                        }
                    }
                    else {
                        bot->Pawn->PawnStopFire(0);
                        bot->ForceStrafe(true);
                        bot->BotState = bot->CachedBotState;
                    }
                }

            bot->tick_counter++;
        }
    }
}