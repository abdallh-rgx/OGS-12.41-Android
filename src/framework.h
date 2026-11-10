#pragma once

// ============================================================================
// framework.h — the Android port of OGS 12.41's framework.h.
//
// Differences from the PC original (everything else is a 1:1 port):
//   * pch + SDK come from platform/erbium/pch-android.h (win32 compat + the
//     full Dumper-7 SDK patched with the verified Android offsets)
//   * every 'ImageBase + 0x<PC RVA>' internal-function initialization is
//     resolved at runtime (NativeResolver) — PC RVAs kept as semantic keys
//   * wchar_t/L"" literals are char16_t/u"" on Android
// ============================================================================

#include "pch.h"
#include "Globals.h"
#include "Hooks.h"
#include "../platform/erbium/Finders.h"

using namespace SDK;

// Module base of libUE4.so (defined in main.cpp; the SDK also resolves it
// via InSDKUtils::GetImageBase()).
extern uint64_t ImageBase;

// ─── internal engine functions (PC: static ImageBase+RVA initializers) ─────
// Declared here; resolved & assigned in NativeResolver.cpp::Init().

class FFrame; // defined below (StepCompiledIn helpers reference it)

// Net / listen-server (Net.h, PC.h, GameMode.h)
extern UNetDriver* (*CreateNetDriver)(void*, void*, FName);
extern bool (*InitListen)(void*, void*, FURL&, bool, FString&);
extern void (*SetWorld)(void*, void*);
extern bool (*InitHost)(UObject* Beacon);
extern void (*PauseBeaconRequests)(UObject* Beacon, bool bPause);

// GameplayAbilities (Abilities.h)
extern void (*GiveAbility)(UAbilitySystemComponent*, FGameplayAbilitySpecHandle*, FGameplayAbilitySpec);
extern void (*AbilitySpecConstructor)(FGameplayAbilitySpec*, UGameplayAbility*, int, int, UObject*);
extern bool (*InternalTryActivateAbility)(UAbilitySystemComponent* AbilitySystemComp, FGameplayAbilitySpecHandle AbilityToActivate, FPredictionKey InPredictionKey, UGameplayAbility** OutInstancedAbility, void* OnGameplayAbilityEndedDelegate, const FGameplayEventData* TriggerEventData);
extern FGameplayAbilitySpecHandle (*GiveAbilityAndActivateOnce)(UAbilitySystemComponent* ASC, FGameplayAbilitySpecHandle*, FGameplayAbilitySpec);

// Fortnite gameplay internals
extern FVector* (*PickSupplyDropLocationOG)(AFortAthenaMapInfo* MapInfo, FVector* outLocation, int64_t Center, float Radius);
extern ABuildingSMActor* (*ReplaceBuildingActor)(ABuildingSMActor* BuildingSMActor, unsigned int a2, UObject* a3, unsigned int a4, int a5, bool bMirrored, AFortPlayerControllerAthena* PC);
extern int64_t (*CantBuild)(UWorld*, UObject*, FVector, FRotator, char, void*, char*);
extern void* (*ApplyCharacterCustomization)(void* a1, void* a2);
extern void (*BotManagerSetup)(int64_t BotManager, int64_t Pawn, int64_t BehaviorTree, int64_t a4, uint32_t* SkillLevel, int64_t a7, int64_t StartupInventory, int64_t BotNameSettings, int64_t a10, uint8_t* CanRespawnOnDeath, unsigned char BitFieldDataThing, uint8_t* CustomSquadId, FFortAthenaAIBotRunTimeCustomizationData InRuntimeBotData);
extern char (*RegisterComponentWithWorld)(UObject* Component, UObject* World);
extern void (*RemoveFromAlivePlayers)(AFortGameModeAthena*, AFortPlayerControllerAthena*, APlayerState*, AFortPlayerPawn*, UFortWeaponItemDefinition*, uint8_t DeathCause, char);
extern void (*AddToAlivePlayers)(AFortGameModeAthena* GameMode, AFortPlayerControllerAthena* Player);

// UObject internals (StaticFindObject / StaticLoadObject)
extern void* (*StaticFindObjectOG)(UClass*, UObject* Package, const char16_t* OrigInName, bool ExactClass);
extern void* (*StaticLoadObjectOG)(UClass* Class, UObject* InOuter, const char16_t* Name, const char16_t* Filename, uint32_t LoadFlags, UObject* Sandbox, bool bAllowObjectReconciliation, void*);

// FFrame step helpers
extern void (*StepCompiledInImpl)(FFrame*, UObject*, void* const);
extern void (*StepCompiledInExplicitPropImpl)(FFrame*, void* const, FField*);

// inventory internals (Inventory.h)
extern void* (*FindFirstItemInstanceByClassInternal)(void*, UClass*);
extern bool (*EquipGadgetInternal)(UFortGadgetItemDefinition*, void*, void*, unsigned char);
extern bool (*EquipGadgetInternal2)(UFortGadgetItemDefinition*, void*, void*);

extern AFortAthenaMutator_Bots* BotMutator;
extern TArray<FVector> PickedSupplyDropLocations;
extern TArray<APlayerController*> GivenLootPlayers;

extern TArray<AActor*> PlayerStarts;

extern bool bFirstElimTriggered;
extern bool bFirstEliminated;
extern bool DontPlayAnimation;

extern bool bFirstChestSearched;
extern bool bFirstSupplyDropSearched;

// text manipulation utils
namespace TextManipUtils {
	inline std::vector<std::string> SplitWhitespace(std::string const& input) {
		std::istringstream buffer(input);
		std::vector<std::string> ret;

		std::copy(std::istream_iterator<std::string>(buffer),
			std::istream_iterator<std::string>(),
			std::back_inserter(ret));
		return ret;
	}
}

// ─── logging: logcat + the on-device log file ───────────────────────────────
void Log(const std::string& msg);

template <typename DetourType>
inline void HookVTable(void* Base, int Idx, DetourType DetourIn, void** OG)
{
	void* Detour = reinterpret_cast<void*>(DetourIn);
	void** VTable = *(void***)Base;

	if (OG)
	{
		*OG = VTable[Idx];
	}

	DWORD oldProtection;
	VirtualProtect(&VTable[Idx], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtection);

	VTable[Idx] = Detour;

	VirtualProtect(&VTable[Idx], sizeof(void*), oldProtection, NULL);
}

// UFunction exec swap (PC original, verbatim)
template <typename T = void*, typename DetourType = void*>
__attribute__((always_inline)) inline void ExecHook(UFunction* func, DetourType detourIn, T& og = nullptr)
{
	if (!func)
		return;
	void* detour = reinterpret_cast<void*>(detourIn);
	if (!std::is_same_v<T, void*>)
		og = (T)func->ExecFunction;

	func->ExecFunction = reinterpret_cast<UFunction::FNativeFuncPtr>(detour);
}

// ─── FFrame (PC original; step helpers resolved at runtime) ────────────────
class FOutputDevice
{
public:
	bool bSuppressEventTag;
	bool bAutoEmitLineTerminator;
	uint8_t _Padding1[0x6];
};

class FFrame : public FOutputDevice
{
public:
	void** VTable;
	UFunction* Node;
	UObject* Object;
	uint8_t* Code;
	uint8_t* Locals;
	void* MostRecentProperty;
	uint8_t* MostRecentPropertyAddress;
	uint8_t _Padding1[0x40];
	FField* PropertyChainForCompiledIn;

public:
	void StepCompiledIn(void* const Result, bool ForceExplicitProp = false)
	{
		if (Code && !ForceExplicitProp)
		{
			StepCompiledInImpl(this, Object, Result);
		}
		else
		{
			FField* _Prop = PropertyChainForCompiledIn;
			PropertyChainForCompiledIn = _Prop->Next;
			StepCompiledInExplicitPropImpl(this, Result, _Prop);
		}
	}

	template <typename T>
	T& StepCompiledInRef()
	{
		T TempVal{};
		MostRecentPropertyAddress = nullptr;

		if (Code)
		{
			StepCompiledInImpl(this, Object, &TempVal);
		}
		else
		{
			FField* _Prop = PropertyChainForCompiledIn;
			PropertyChainForCompiledIn = _Prop->Next;
			StepCompiledInExplicitPropImpl(this, &TempVal, _Prop);
		}

		return MostRecentPropertyAddress ? *(T*)MostRecentPropertyAddress : TempVal;
	}
};

inline FQuat RotatorToQuat(FRotator Rotation)
{
	FQuat Quat;
	const float DEG_TO_RAD = 3.14159f / 180.0f;
	const float DIVIDE_BY_2 = DEG_TO_RAD / 2.0f;

	float SP = sin(Rotation.Pitch * DIVIDE_BY_2);
	float CP = cos(Rotation.Pitch * DIVIDE_BY_2);
	float SY = sin(Rotation.Yaw * DIVIDE_BY_2);
	float CY = cos(Rotation.Yaw * DIVIDE_BY_2);
	float SR = sin(Rotation.Roll * DIVIDE_BY_2);
	float CR = cos(Rotation.Roll * DIVIDE_BY_2);

	Quat.X = CR * SP * SY - SR * CP * CY;
	Quat.Y = -CR * SP * CY - SR * CP * SY;
	Quat.Z = CR * CP * SY - SR * SP * CY;
	Quat.W = CR * CP * CY + SR * SP * SY;

	return Quat;
}

template <typename T>
static inline T* StaticFindObject(std::u16string ObjectName)
{
	return (T*)StaticFindObjectOG(T::StaticClass(), nullptr, ObjectName.c_str(), false);
}

template<typename T>
inline T* Cast(UObject* Object)
{
	if (!Object || !Object->IsA(T::StaticClass()))
		return nullptr;
	return (T*)Object;
}

template<typename T = UObject>
static inline T* StaticLoadObject(const std::string& Name)
{
	std::u16string ConvName(Name.begin(), Name.end());

	T* Object = StaticFindObject<T>(ConvName);

	if (!Object)
	{
		Object = (T*)StaticLoadObjectOG(T::StaticClass(), nullptr, ConvName.c_str(), nullptr, 0, nullptr, false, nullptr);
	}

	return Object;
}

template<typename T>
T* GetDefaultObject()
{
	return (T*)T::StaticClass()->DefaultObject;
}

static inline FQuat FRotToQuat(FRotator Rotation) {
	FQuat Quat;
	const float DEG_TO_RAD = 3.14159f / 180.0f;
	const float DIVIDE_BY_2 = DEG_TO_RAD / 2.0f;

	float SP = sin(Rotation.Pitch * DIVIDE_BY_2);
	float CP = cos(Rotation.Pitch * DIVIDE_BY_2);
	float SY = sin(Rotation.Yaw * DIVIDE_BY_2);
	float CY = cos(Rotation.Yaw * DIVIDE_BY_2);
	float SR = sin(Rotation.Roll * DIVIDE_BY_2);
	float CR = cos(Rotation.Roll * DIVIDE_BY_2);

	Quat.X = CR * SP * SY - SR * CP * CY;
	Quat.Y = -CR * SP * CY - SR * CP * SY;
	Quat.Z = CR * CP * SY - SR * SP * CY;
	Quat.W = CR * CP * CY + SR * SP * SY;

	return Quat;
}

template<typename T>
inline T* SpawnActor(FVector Loc, FRotator Rot = FRotator(), AActor* Owner = nullptr, SDK::UClass* Class = T::StaticClass(), ESpawnActorCollisionHandlingMethod Handle = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn)
{
	FTransform Transform{};
	Transform.Scale3D = FVector{ 1,1,1 };
	Transform.Translation = Loc;
	Transform.Rotation = FRotToQuat(Rot);

	return (T*)UGameplayStatics::FinishSpawningActor(UGameplayStatics::BeginDeferredActorSpawnFromClass(UWorld::GetWorld(), Class, Transform, Handle, Owner), Transform);
}

template<typename T>
static inline T* SpawnAActor(FVector Loc = { 0,0,0 }, FRotator Rot = { 0,0,0 }, AActor* Owner = nullptr)
{
	FTransform Transform{};
	Transform.Scale3D = { 1,1,1 };
	Transform.Translation = Loc;
	Transform.Rotation = FRotToQuat(Rot);

	AActor* NewActor = UGameplayStatics::BeginDeferredActorSpawnFromClass(UWorld::GetWorld(), T::StaticClass(), Transform, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn, Owner);
	return (T*)UGameplayStatics::FinishSpawningActor(NewActor, Transform);
}

template<typename T>
static inline T* SpawnActorClass(FVector Loc = { 0,0,0 }, FRotator Rot = { 0,0,0 }, UClass* Class = nullptr, AActor* Owner = nullptr)
{
	FTransform Transform{};
	Transform.Scale3D = { 1,1,1 };
	Transform.Translation = Loc;
	Transform.Rotation = RotatorToQuat(Rot);

	AActor* NewActor = UGameplayStatics::BeginDeferredActorSpawnFromClass(UWorld::GetWorld(), Class, Transform, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn, Owner);
	return (T*)UGameplayStatics::FinishSpawningActor(NewActor, Transform);
}

template<typename T>
T* Actors(UClass* Class = T::StaticClass(), FVector Loc = {}, FRotator Rot = {}, AActor* Owner = nullptr)
{
	return SpawnActor<T>(Loc, Rot, Owner, Class);
}

inline AActor* DuplicateActor(AActor* Original)
{
	if (!Original) return nullptr;

	const FVector Location = Original->K2_GetActorLocation();
	const FRotator Rotation = Original->K2_GetActorRotation();
	const FTransform Transform = Original->GetTransform();

	auto* Class = Original->Class;
	auto* Owner = Original->GetOwner();

	auto* Duplicated = (AActor*)UGameplayStatics::FinishSpawningActor(
		UGameplayStatics::BeginDeferredActorSpawnFromClass(UWorld::GetWorld(), Class, Transform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn, Owner),
		Transform
	);

	if (Duplicated)
	{
		Duplicated->Name = Original->Name;
		Duplicated->Tags = Original->Tags;
	}

	FGameplayTag* OriginalFactionTag = (FGameplayTag*)((uintptr_t)Original + 0xB0);
	FGameplayTag* DuplicateFactionTag = (FGameplayTag*)((uintptr_t)Duplicated + 0xB0);

	if (OriginalFactionTag && DuplicateFactionTag)
	{
		*DuplicateFactionTag = *OriginalFactionTag;
	}

	return Duplicated;
}

inline AFortPickupAthena* SpawnPickup(UFortItemDefinition* ItemDef, int OverrideCount, int LoadedAmmo, FVector Loc, EFortPickupSourceTypeFlag SourceType, EFortPickupSpawnSource Source, AFortPawn* Pawn = nullptr)
{
	auto SpawnedPickup = Actors<AFortPickupAthena>(AFortPickupAthena::StaticClass(), Loc);
	SpawnedPickup->bRandomRotation = true;

	auto& PickupEntry = SpawnedPickup->PrimaryPickupItemEntry;
	PickupEntry.ItemDefinition = ItemDef;
	PickupEntry.Count = OverrideCount;
	PickupEntry.LoadedAmmo = LoadedAmmo;
	PickupEntry.ReplicationKey++;
	SpawnedPickup->OnRep_PrimaryPickupItemEntry();
	SpawnedPickup->PawnWhoDroppedPickup = Pawn;

	SpawnedPickup->TossPickup(Loc, Pawn, -1, true, false, SourceType, Source);

	SpawnedPickup->SetReplicateMovement(true);
	SpawnedPickup->MovementComponent = (UProjectileMovementComponent*)GetDefaultObject<UGameplayStatics>()->SpawnObject(UProjectileMovementComponent::StaticClass(), SpawnedPickup);

	if (SourceType == EFortPickupSourceTypeFlag::Container)
	{
		SpawnedPickup->bTossedFromContainer = true;
		SpawnedPickup->OnRep_TossedFromContainer();
	}

	return SpawnedPickup;
}

extern std::map<AFortPickup*, float> PickupLifetimes;
inline AFortPickupAthena* SpawnStack(APlayerPawn_Athena_C* Pawn, UFortItemDefinition* Def, int Count, bool giveammo = false, int ammo = 0)
{
	auto Statics = (UGameplayStatics*)UGameplayStatics::StaticClass()->DefaultObject;

	FVector Loc = Pawn->K2_GetActorLocation();
	AFortPickupAthena* Pickup = Actors<AFortPickupAthena>(AFortPickupAthena::StaticClass(), Loc);
	Pickup->bReplicates = true;
	PickupLifetimes[Pickup] = Statics->GetTimeSeconds(UWorld::GetWorld());
	Pickup->PawnWhoDroppedPickup = Pawn;
	Pickup->PrimaryPickupItemEntry.Count = Count;
	Pickup->PrimaryPickupItemEntry.ItemDefinition = Def;
	if (giveammo)
	{
		Pickup->PrimaryPickupItemEntry.LoadedAmmo = ammo;
	}
	Pickup->PrimaryPickupItemEntry.ReplicationKey++;

	Pickup->OnRep_PrimaryPickupItemEntry();
	Pickup->TossPickup(Loc, Pawn, 6, true, true, EFortPickupSourceTypeFlag::Other, EFortPickupSpawnSource::Unset);

	Pickup->MovementComponent = (UProjectileMovementComponent*)Statics->SpawnObject(UProjectileMovementComponent::StaticClass(), Pickup);
	Pickup->MovementComponent->bReplicates = true;
	((UProjectileMovementComponent*)Pickup->MovementComponent)->SetComponentTickEnabled(true);

	return Pickup;
}

inline static AFortPickupAthena* SpawnPickup(FFortItemEntry ItemEntry, FVector Location, EFortPickupSourceTypeFlag PickupSource = EFortPickupSourceTypeFlag::Other, EFortPickupSpawnSource SpawnSource = EFortPickupSpawnSource::Unset)
{
	auto Pickup = SpawnPickup(ItemEntry.ItemDefinition, ItemEntry.Count, ItemEntry.LoadedAmmo, Location, PickupSource, SpawnSource);
	return Pickup;
}

inline void ShowFoundation(ABuildingFoundation* BuildingFoundation) {
	if (!BuildingFoundation)
		return;

	BuildingFoundation->bServerStreamedInLevel = true;
	BuildingFoundation->DynamicFoundationType = EDynamicFoundationType::Static;
	BuildingFoundation->OnRep_ServerStreamedInLevel();

	BuildingFoundation->FoundationEnabledState = EDynamicFoundationEnabledState::Enabled;
	BuildingFoundation->DynamicFoundationRepData.EnabledState = EDynamicFoundationEnabledState::Enabled;
	BuildingFoundation->DynamicFoundationTransform = BuildingFoundation->GetTransform();
	BuildingFoundation->OnRep_DynamicFoundationRepData();
}

inline FVector PickSupplyDropLocation(SDK::AFortAthenaMapInfo* MapInfo, SDK::FVector Center, float Radius)
{
	if (!PickSupplyDropLocationOG)
		return SDK::FVector(0, 0, 0);

	const float MinDistance = 10000.0f;

	for (int i = 0; i < 20; i++)
	{
		SDK::FVector loc = FVector(0, 0, 0);
		PickSupplyDropLocationOG(MapInfo, &loc, (int64_t)&Center, Radius);

		bool bTooClose = false;
		for (const auto& other : PickedSupplyDropLocations)
		{
			float dx = loc.X - other.X;
			float dy = loc.Y - other.Y;
			float dz = loc.Z - other.Z;

			float distSquared = dx * dx + dy * dy + dz * dz;

			if (distSquared < MinDistance * MinDistance)
			{
				bTooClose = true;
				break;
			}
		}

		if (!bTooClose)
		{
			PickedSupplyDropLocations.Add(loc);
			return loc;
		}
	}

	return SDK::FVector(0, 0, 0);
}

template<typename T>
inline std::vector<T*> GetAllObjectsOfClass(UClass* Class = T::StaticClass())
{
	std::vector<T*> Objects{};

	for (int i = 0; i < UObject::GObjects->Num(); ++i)
	{
		UObject* Object = UObject::GObjects->GetByIndex(i);

		if (!Object)
			continue;

		if (Object->GetFullName().contains("Default"))
			continue;

		if (Object->GetFullName().contains("Test"))
			continue;

		if (Object->IsA(Class) && !Object->IsDefaultObject())
		{
			Objects.push_back((T*)Object);
		}
	}

	return Objects;
}

inline int CountActorsWithName(FName TargetName, UClass* Class)
{
	TArray<AActor*> FoundActors;
	auto* Statics = (UGameplayStatics*)UGameplayStatics::StaticClass()->DefaultObject;
	Statics->GetAllActorsOfClass(UWorld::GetWorld(), Class, &FoundActors);

	int Count = 0;
	for (AActor* Actor : FoundActors)
	{
		if (Actor && Actor->GetName() == TargetName.ToString())
			Count++;
	}
	return Count;
}

inline AFortPlayerControllerAthena* GetPCFromId(FUniqueNetIdRepl& ID)
{
	for (auto& PlayerState : UWorld::GetWorld()->GameState->PlayerArray)
	{
		auto PlayerStateAthena = Cast<AFortPlayerStateAthena>(PlayerState);
		if (!PlayerStateAthena)
			continue;
		if (PlayerStateAthena->AreUniqueIDsIdentical(ID, PlayerState->UniqueId))
			return Cast<AFortPlayerControllerAthena>(PlayerState->Owner);
	}

	return nullptr;
}

enum class EAccoladeEvent : uint8
{
	Kill,
	Search,
	MAX
};

inline UFortAccoladeItemDefinition* GetDefFromEvent(EAccoladeEvent Event, int Count, UObject* Object = nullptr)
{
	UFortAccoladeItemDefinition* Def = nullptr;

	switch (Event)
	{
	case EAccoladeEvent::Kill:
		if (Count == 1)
		{
			Def = StaticLoadObject<UFortAccoladeItemDefinition>("/Game/Athena/Items/Accolades/AccoladeId_014_Elimination_Bronze.AccoladeId_014_Elimination_Bronze");
		}
		else if (Count == 4)
		{
			Def = StaticLoadObject<UFortAccoladeItemDefinition>("/Game/Athena/Items/Accolades/AccoladeId_015_Elimination_Silver.AccoladeId_015_Elimination_Silver");
		}
		else if (Count == 8)
		{
			Def = StaticLoadObject<UFortAccoladeItemDefinition>("/Game/Athena/Items/Accolades/AccoladeId_016_Elimination_Gold.AccoladeId_016_Elimination_Gold");
		}
		break;
	case EAccoladeEvent::Search:
		if (Count == 3)
		{
			Def = StaticLoadObject<UFortAccoladeItemDefinition>("/Game/Athena/Items/Accolades/AccoladeId_008_SearchChests_Bronze.AccoladeId_008_SearchChests_Bronze");
		}
		else if (Count == 7)
		{
			Def = StaticLoadObject<UFortAccoladeItemDefinition>("/Game/Athena/Items/Accolades/AccoladeId_009_SearchChests_Silver.AccoladeId_009_SearchChests_Silver");
		}
		else if (Count == 12)
		{
			Def = StaticLoadObject<UFortAccoladeItemDefinition>("/Game/Athena/Items/Accolades/AccoladeId_010_SearchChests_Gold.AccoladeId_010_SearchChests_Gold");
		}
		break;
	case EAccoladeEvent::MAX:
		break;
	default:
		break;
	}

	return Def;
}

namespace EBTExecutionMode
{
	enum Type
	{
		SingleRun,
		Looped,
	};
}

namespace EBTActiveNode
{
	enum Type
	{
		Composite,
		ActiveTask,
		AbortingTask,
		InactiveTask,
	};
}

namespace EBTTaskStatus
{
	enum Type
	{
		Active,
		Aborting,
		Inactive,
	};
}
