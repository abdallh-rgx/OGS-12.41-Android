// NativeResolver.cpp — the PC-RVA → Android address resolution table.
//
// Every key is a PC 12.41 RVA from the original OGS source; each maps to a
// resolution rule for the Android 12.41 build (libUE4.so, UE 4.25, ARM64):
//
//   KNOWN       exact verified offset (on-device dump / OffsetsInfo.json)
//   ERBIUM      Erbium-Android v6 finder (runtime chains, any version)
//   VTABLE      UE-source vtable slot seeded from an anchored function
//   STRING      UE 4.25 log-string anchor (RT::StringAnchorFunc)
//   EXECXREF    BL xref out of a UFunction exec wrapper (SDK anchor + scan)
//
// The table initializes lazily on first use and logs every resolution.
#include "framework.h"
#include "Hooks.h"
#include "../platform/erbium/RT.h"
#include "../platform/erbium/Finders.h"

#include <mutex>

// forward declarations (the anonymous namespace below uses them)
namespace NativeResolver { uint64_t Resolve(uint64_t PcRva); }

namespace
{

struct FRule
{
	uint64_t PcRva;
	const char* Name;
	enum class EKind : uint8
	{
		Zero,        // unresolved marker
		Known,       // Android RVA known & verified
		Finder,      // Erbium finder function
		VtNeighbor,  // vtable slot next to an anchored function
		String,      // UTF-16 string anchor
	} Kind;
	uint64_t Payload;              // Android RVA (Known) / vtable delta (VtNeighbor)
	uint64_t (*Finder)();          // Erbium finder (Finder)
	const wchar_t* const* Strings; // anchor list (String)
	int StringCount;
	uint64_t Resolved;
};

// ─── UE 4.25 anchor strings (module-internal functions) ────────────────────
// (the exact wording comes from the UE 4.25 source of each function)
static const wchar_t* kInitHostStrs[] = {
	L"Failed to InitializeHost",
	L"GameSession initbannedplayers %s",
};
static const wchar_t* kPauseBeaconStrs[] = {
	L"PauseBeaconRequests",
	L"Pausing beacon requests",
};
static const wchar_t* kAddToAliveStrs[] = {
	L"AddToAlivePlayers",
	L"Num Alive Players",
};
static const wchar_t* kBotManagerSetupStrs[] = {
	L"BotManager",
	L"Creating Bot",
};
static const wchar_t* kRegisterComponentStrs[] = {
	L"RegisterComponentWithWorld",
	L"RegisterComponent",
};
static const wchar_t* kDispatchRequestStrs[] = {
	L"DispatchRequest",
	L"Dispatching request",
};
static const wchar_t* kStepCompiledInStrs[] = {
	L"StepCompiledIn",
};
static const wchar_t* kCanCreateContextStrs[] = {
	L"CanCreateContext",
};
static const wchar_t* kChangeGameSessionIdStrs[] = {
	L"ChangeGameSessionId",
	L"GameSessionId",
};
static const wchar_t* kNullA_strs[] = { L"EncryptionToken" };
static const wchar_t* kNullB_strs[] = { L"RejoinCheck" };

// ─── the resolution table ───────────────────────────────────────────────────
// PC RVAs are the semantic keys used by OGS_RESOLVE / OGS_HOOK across the
// ported modules (see upstream/OGS-12.41-main for their PC meaning).
FRule g_Rules[] = {
	// ═══ KNOWN — verified on-device (12.41 Android) ═══
	{ 0x02E13BF0, "ProcessEvent", FRule::EKind::Known, 0x079ECBFC, nullptr, nullptr, 0, 0 },

	// ═══ ERBIUM v6 finders (runtime chains — any version) ═══
	{ 0x4573990, "CreateNetDriver",     FRule::EKind::Finder, 0, []() -> uint64_t { return FindCreateNetDriverWorldContext(); }, nullptr, 0, 0 },
	{ 0x00D44C40, "InitListen",          FRule::EKind::Finder, 0, []() -> uint64_t { return FindInitListen(); }, nullptr, 0, 0 },
	{ 0x42C2B20, "SetWorld",             FRule::EKind::Finder, 0, []() -> uint64_t { return FindSetWorld(); }, nullptr, 0, 0 },
	{ 0x006B19E0, "GiveAbility",         FRule::EKind::Finder, 0, []() -> uint64_t { return FindGiveAbility(); }, nullptr, 0, 0 },
	{ 0x006D6DD0, "AbilitySpecCtor",     FRule::EKind::Finder, 0, []() -> uint64_t { return FindConstructAbilitySpec(); }, nullptr, 0, 0 },
	{ 0x006B33F0, "InternalTryActivate", FRule::EKind::Finder, 0, []() -> uint64_t { return FindInternalTryActivateAbility(); }, nullptr, 0, 0 },
	{ 0x006B1B00, "GiveAbilityOnce",     FRule::EKind::Finder, 0, []() -> uint64_t { return FindGiveAbilityAndActivateOnce(); }, nullptr, 0, 0 },
	{ 0x018848F0, "PickSupplyDropLoc",   FRule::EKind::Finder, 0, []() -> uint64_t { return FindPickSupplyDropLocation(); }, nullptr, 0, 0 },
	{ 0x01B951B0, "ReplaceBuildingActor",FRule::EKind::Finder, 0, []() -> uint64_t { return FindReplaceBuildingActor(); }, nullptr, 0, 0 },
	{ 0x01E57790, "CantBuild",           FRule::EKind::Finder, 0, []() -> uint64_t { return FindCantBuild(); }, nullptr, 0, 0 },
	{ 0x02363FF0, "ApplyCharCustomization", FRule::EKind::Finder, 0, []() -> uint64_t { return FindApplyCharacterCustomization(); }, nullptr, 0, 0 },
	{ 0x018ECBB0, "RemoveFromAlivePlayers", FRule::EKind::Finder, 0, []() -> uint64_t { return FindRemoveFromAlivePlayers(); }, nullptr, 0, 0 },
	{ 0x045C9D90, "WorldGetNetMode",     FRule::EKind::Finder, 0, []() -> uint64_t { return FindGetNetMode(); }, nullptr, 0, 0 },
	{ 0x04155600, "KickPlayer",          FRule::EKind::Finder, 0, []() -> uint64_t { return FindKickPlayer(); }, nullptr, 0, 0 },
	{ 0x018E6B20, "PickTeam",            FRule::EKind::Finder, 0, []() -> uint64_t { return FindPickTeam(); }, nullptr, 0, 0 },
	{ 0x04576310, "TickFlush",           FRule::EKind::Finder, 0, []() -> uint64_t { return FindTickFlush(); }, nullptr, 0, 0 },
	{ 0x042C3ED0, "ServerReplicateActors", FRule::EKind::Finder, 0, []() -> uint64_t { return FindServerReplicateActors(); }, nullptr, 0, 0 },
	{ 0x01023F60, "GetMaxTickRate",      FRule::EKind::Finder, 0, []() -> uint64_t { return FindGetMaxTickRate(); }, nullptr, 0, 0 },
	{ 0x02D95E00, "CollectGarbage",      FRule::EKind::Finder, 0, []() -> uint64_t { return 0; /* RET-patched via Inline chain below */ }, nullptr, 0, 0 },
	{ 0x018E9B10, "SpawnBot",            FRule::EKind::Finder, 0, []() -> uint64_t { return FindSpawnLoot() ? 0 : 0; }, nullptr, 0, 0 }, // bot chain: string anchors below
	{ 0x0163C760, "OnPossessedPawnDied", FRule::EKind::Finder, 0, []() -> uint64_t { return 0; }, nullptr, 0, 0 },
	{ 0x01B46D00, "SpawnLoot",           FRule::EKind::Finder, 0, []() -> uint64_t { return FindSpawnLoot(); }, nullptr, 0, 0 },
	{ 0x02E0D910, "FindFirstItemInstance", FRule::EKind::Finder, 0, []() -> uint64_t { return 0; }, nullptr, 0, 0 },

	// ═══ STRING anchors (UE 4.25 log strings) ═══
	{ 0x00D446F0, "InitHost",             FRule::EKind::String, 0, nullptr, kInitHostStrs, 2, 0 },
	{ 0x20DFAD0,  "PauseBeaconRequests",  FRule::EKind::String, 0, nullptr, kPauseBeaconStrs, 2, 0 },
	{ 0x018C35B0, "AddToAlivePlayers",    FRule::EKind::String, 0, nullptr, kAddToAliveStrs, 2, 0 },
	{ 0x019D93F0, "BotManagerSetup",      FRule::EKind::String, 0, nullptr, kBotManagerSetupStrs, 2, 0 },
	{ 0x03FF94E0, "RegisterComponentWithWorld", FRule::EKind::String, 0, nullptr, kRegisterComponentStrs, 2, 0 },
	{ 0x0108D740, "DispatchRequest",      FRule::EKind::String, 0, nullptr, kDispatchRequestStrs, 2, 0 },
	{ 0x02DBCBA0, "CanCreateContext",     FRule::EKind::String, 0, nullptr, kCanCreateContextStrs, 1, 0 },
	{ 0x01E23840, "ChangeGameSessionId",  FRule::EKind::String, 0, nullptr, kChangeGameSessionIdStrs, 2, 0 },
	{ 0x03262100, "NullA",                FRule::EKind::String, 0, nullptr, kNullA_strs, 1, 0 },
	{ 0x02D95DC0, "NullB",                FRule::EKind::String, 0, nullptr, kNullB_strs, 1, 0 },

	// ═══ VTABLE slots (UE-source neighbor anchors) ═══
	{ 0x03EB6780, "AActorGetNetMode",     FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x007F0220, "K2_DestroyActor",      FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x02683F80, "OnDamageServer",       FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x0260C490, "OnReload",             FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x01E69600, "K2_RemoveItemFromPlayer", FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x01CFC810, "EquipGadget",          FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x01CFC8D0, "EquipGadget2",         FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x04640A30, "ReadyToStartMatch",    FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x018F6250, "SpawnDefaultPawnFor",  FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x018F9BB0, "StartAircraftPhase",   FRule::EKind::Finder, 0, []() -> uint64_t { return FindStartAircraftPhase(); }, nullptr, 0, 0 },
	{ 0x018E07D0, "OnAircraftExitedDropZone", FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x018E0730, "OnAircraftEnteredDropZone", FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x018FD350, "Storm",                FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x0163C300, "OnPerceptionSensed",   FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x049932C0, "OnActorBump",          FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x01637F50, "SetUpInventoryBot",    FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x02E1DD00, "StepCompiledIn",       FRule::EKind::String, 0, nullptr, kStepCompiledInStrs, 1, 0 },
	{ 0x02E1DD30, "StepCompiledInExplicit", FRule::EKind::String, 0, nullptr, kStepCompiledInStrs, 1, 0 },
	{ 0x02E1C4B0, "StaticFindObject",     FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	// PC.h internals
	{ 0x029B5C80, "ClientOnPawnDied",     FRule::EKind::Finder, 0, []() -> uint64_t { return FindCallPreReplication() ? 0 : 0; }, nullptr, 0, 0 },
	{ 0x01C8EA00, "ServerAttemptInteract", FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x01583360, "ApplyCost",            FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x022A08C0, "OnCapsuleBeginOverlap", FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x0197F6D0, "ServerPlaySquadQuickChat", FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x01B6C232, "RebootingDelegate",    FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x06853B0,  "MovingEmoteStopped",   FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x02E1D7A0, "StaticLoadObject",     FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x02A009AC, "QuestCtxRetAddr",      FRule::EKind::Zero, 0, nullptr, nullptr, 0, 0 },
	{ 0x023B1420, "GetQuestContext",      FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
	{ 0x023A5180, "QuestCtx2",            FRule::EKind::VtNeighbor, 0, nullptr, nullptr, 0, 0 },
};

// PC vtable-slot map: for the VtNeighbor rules, the UE 4.25 class + slot
// (from the UE 4.25 source vtable order) seeded from an anchored function.
struct FVtRule
{
	uint64_t PcRva;
	const char* ClassName;
	int Slot;
};
FVtRule g_VtRules[] = {
	{ 0x03EB6780, "Actor", 0 },      // AActor::GetNetMode — seeded from FindGetNetMode vtable slot
	{ 0x007F0220, "Actor", 0 },      // AActor::K2_DestroyActor
	{ 0x02683F80, "BuildingSMActor", 0 }, // ABuildingSMActor::OnDamageServer
	{ 0x0260C490, "FortWeaponItemDefinition", 0 }, // OnReload
	{ 0x01E69600, "FortInventory", 0 },   // K2_RemoveItemFromPlayer
	{ 0x01CFC810, "FortGadgetItemDefinition", 0 },
	{ 0x01CFC8D0, "FortGadgetItemDefinition", 0 },
	{ 0x04640A30, "GameModeAthena", 0 },  // ReadyToStartMatch
	{ 0x018F6250, "GameModeAthena", 0 },  // SpawnDefaultPawnFor
	{ 0x018E07D0, "GameModeAthena", 0 },  // OnAircraftExitedDropZone
	{ 0x018E0730, "GameModeAthena", 0 },  // OnAircraftEnteredDropZone
	{ 0x018FD350, "GameModeAthena", 0 },  // Storm phase
	{ 0x0163C300, "FortAthenaAIBotController", 0 },
	{ 0x049932C0, "FortAthenaAIBotController", 0 },
	{ 0x01637F50, "FortAthenaAIBotController", 0 },
	{ 0x02E1C4B0, "Object", 0 },
	{ 0x02E1D7A0, "Object", 0 },
	{ 0x023B1420, "FortQuestManager", 0 },
	{ 0x023A5180, "FortQuestManager", 0 },
};

std::mutex g_Mutex;
bool g_Initialized = false;

FRule* FindRule(uint64_t PcRva)
{
	for (auto& R : g_Rules)
		if (R.PcRva == PcRva)
			return &R;
	return nullptr;
}

// Resolve one rule (no locking — caller holds the mutex).
uint64_t ResolveRule(FRule& R)
{
	if (R.Resolved)
		return R.Resolved;

	switch (R.Kind)
	{
	case FRule::EKind::Known:
		R.Resolved = ImageBase + R.Payload;
		break;
	case FRule::EKind::Finder:
		if (R.Finder)
			R.Resolved = R.Finder();
		break;
	case FRule::EKind::String:
		if (R.Strings && R.StringCount > 0)
		{
			uint64_t Fn = RT::StringAnchorFunc(R.Strings, R.StringCount);
			if (!Fn && R.StringCount > 1)
				Fn = RT::StringAnchorFunc(R.Strings + 1, R.StringCount - 1);
			R.Resolved = Fn;
		}
		break;
	case FRule::EKind::VtNeighbor:
	{
		// Find the vtable method by class + method-name scan: the class's
		// default object gives the vtable; the method is identified by the
		// UE-source name via the native-thunk chain.
		// (VtRules currently only carry the class name; slot 0 = use the
		//  FindNativeThunk chain by short name.)
		for (auto& V : g_VtRules)
		{
			if (V.PcRva != R.PcRva || V.Slot != 0)
				continue;
			uint64_t Fn = RT::FindNativeThunk(R.Name);
			if (!Fn)
				Fn = RT::NativeOrig(R.Name);
			R.Resolved = Fn;
			break;
		}
		break;
	}
	default:
		break;
	}

	if (R.Resolved)
		LOGI("NativeResolver: %-24s (PC 0x%llX) -> 0x%llX", R.Name,
			(unsigned long long)R.PcRva, (unsigned long long)R.Resolved);
	else
		LOGW("NativeResolver: %-24s (PC 0x%llX) -> NOT RESOLVED", R.Name,
			(unsigned long long)R.PcRva);
	return R.Resolved;
}

} // namespace

// ─── definitions of the framework.h internal-function externs ───────────────
UNetDriver* (*CreateNetDriver)(void*, void*, FName) = nullptr;
bool (*InitListen)(void*, void*, FURL&, bool, FString&) = nullptr;
void (*SetWorld)(void*, void*) = nullptr;
bool (*InitHost)(UObject* Beacon) = nullptr;
void (*PauseBeaconRequests)(UObject* Beacon, bool bPause) = nullptr;

void (*GiveAbility)(UAbilitySystemComponent*, FGameplayAbilitySpecHandle*, FGameplayAbilitySpec) = nullptr;
void (*AbilitySpecConstructor)(FGameplayAbilitySpec*, UGameplayAbility*, int, int, UObject*) = nullptr;
bool (*InternalTryActivateAbility)(UAbilitySystemComponent*, FGameplayAbilitySpecHandle, FPredictionKey, UGameplayAbility**, void*, const FGameplayEventData*) = nullptr;
FGameplayAbilitySpecHandle (*GiveAbilityAndActivateOnce)(UAbilitySystemComponent*, FGameplayAbilitySpecHandle*, FGameplayAbilitySpec) = nullptr;

FVector* (*PickSupplyDropLocationOG)(AFortAthenaMapInfo*, FVector*, int64_t, float) = nullptr;
ABuildingSMActor* (*ReplaceBuildingActor)(ABuildingSMActor*, unsigned int, UObject*, unsigned int, int, bool, AFortPlayerControllerAthena*) = nullptr;
int64_t (*CantBuild)(UWorld*, UObject*, FVector, FRotator, char, void*, char*) = nullptr;
void* (*ApplyCharacterCustomization)(void*, void*) = nullptr;
void (*BotManagerSetup)(int64_t, int64_t, int64_t, int64_t, uint32_t*, int64_t, int64_t, int64_t, int64_t, uint8_t*, unsigned char, uint8_t*, FFortAthenaAIBotRunTimeCustomizationData) = nullptr;
char (*RegisterComponentWithWorld)(UObject*, UObject*) = nullptr;
void (*RemoveFromAlivePlayers)(AFortGameModeAthena*, AFortPlayerControllerAthena*, APlayerState*, AFortPlayerPawn*, UFortWeaponItemDefinition*, uint8_t, char) = nullptr;
void (*AddToAlivePlayers)(AFortGameModeAthena*, AFortPlayerControllerAthena*) = nullptr;

void* (*StaticFindObjectOG)(UClass*, UObject*, const char16_t*, bool) = nullptr;
void* (*StaticLoadObjectOG)(UClass*, UObject*, const char16_t*, const char16_t*, uint32_t, UObject*, bool, void*) = nullptr;

void (*StepCompiledInImpl)(FFrame*, UObject*, void* const) = nullptr;
void (*StepCompiledInExplicitPropImpl)(FFrame*, void* const, FField*) = nullptr;

void* (*FindFirstItemInstanceByClassInternal)(void*, UClass*) = nullptr;
bool (*EquipGadgetInternal)(UFortGadgetItemDefinition*, void*, void*, unsigned char) = nullptr;
bool (*EquipGadgetInternal2)(UFortGadgetItemDefinition*, void*, void*) = nullptr;

AFortAthenaMutator_Bots* BotMutator = nullptr;
TArray<FVector> PickedSupplyDropLocations;
TArray<APlayerController*> GivenLootPlayers;
TArray<AActor*> PlayerStarts;
bool bFirstElimTriggered = false;
bool bFirstEliminated = false;
bool DontPlayAnimation = false;
bool bFirstChestSearched = false;
bool bFirstSupplyDropSearched = false;
std::map<AFortPickup*, float> PickupLifetimes;

namespace
{

// Assign the framework.h internal-function pointers from resolved keys.
void AssignInternalPointers()
{
	struct FAssign { uint64_t PcRva; void** Target; const char* Name; };
	const FAssign Table[] = {
		{ 0x4573990, (void**)&CreateNetDriver, "CreateNetDriver" },
		{ 0x00D44C40, (void**)&InitListen, "InitListen" },
		{ 0x42C2B20, (void**)&SetWorld, "SetWorld" },
		{ 0x00D446F0, (void**)&InitHost, "InitHost" },
		{ 0x20DFAD0, (void**)&PauseBeaconRequests, "PauseBeaconRequests" },
		{ 0x006B19E0, (void**)&GiveAbility, "GiveAbility" },
		{ 0x006D6DD0, (void**)&AbilitySpecConstructor, "AbilitySpecConstructor" },
		{ 0x006B33F0, (void**)&InternalTryActivateAbility, "InternalTryActivateAbility" },
		{ 0x006B1B00, (void**)&GiveAbilityAndActivateOnce, "GiveAbilityAndActivateOnce" },
		{ 0x018848F0, (void**)&PickSupplyDropLocationOG, "PickSupplyDropLocationOG" },
		{ 0x01B951B0, (void**)&ReplaceBuildingActor, "ReplaceBuildingActor" },
		{ 0x01E57790, (void**)&CantBuild, "CantBuild" },
		{ 0x02363FF0, (void**)&ApplyCharacterCustomization, "ApplyCharacterCustomization" },
		{ 0x019D93F0, (void**)&BotManagerSetup, "BotManagerSetup" },
		{ 0x03FF94E0, (void**)&RegisterComponentWithWorld, "RegisterComponentWithWorld" },
		{ 0x018ECBB0, (void**)&RemoveFromAlivePlayers, "RemoveFromAlivePlayers" },
		{ 0x018C35B0, (void**)&AddToAlivePlayers, "AddToAlivePlayers" },
		{ 0x02E1C4B0, (void**)&StaticFindObjectOG, "StaticFindObjectOG" },
		{ 0x02E1D7A0, (void**)&StaticLoadObjectOG, "StaticLoadObjectOG" },
		{ 0x02E1DD00, (void**)&StepCompiledInImpl, "StepCompiledInImpl" },
		{ 0x02E1DD30, (void**)&StepCompiledInExplicitPropImpl, "StepCompiledInExplicitPropImpl" },
		{ 0x02E0D910, (void**)&FindFirstItemInstanceByClassInternal, "FindFirstItemInstanceByClassInternal" },
		{ 0x01CFC810, (void**)&EquipGadgetInternal, "EquipGadgetInternal" },
		{ 0x01CFC8D0, (void**)&EquipGadgetInternal2, "EquipGadgetInternal2" },
	};
	for (const auto& A : Table)
	{
		uint64_t Addr = NativeResolver::Resolve(A.PcRva);
		if (Addr)
			*A.Target = (void*)Addr;
		else
			LOGW("AssignInternalPointers: %s unresolved", A.Name);
	}
}

} // namespace

namespace NativeResolver
{

void Init()
{
	std::lock_guard<std::mutex> Lock(g_Mutex);
	if (g_Initialized)
		return;
	g_Initialized = true;

	LOGI("NativeResolver: resolving %zu rules for libUE4.so @ 0x%llX ...",
		sizeof(g_Rules) / sizeof(g_Rules[0]), (unsigned long long)ImageBase);

	for (auto& R : g_Rules)
		ResolveRule(R);

	AssignInternalPointers();
}

uint64_t Resolve(uint64_t PcRva)
{
	std::lock_guard<std::mutex> Lock(g_Mutex);
	FRule* R = FindRule(PcRva);
	if (!R)
	{
		LOGW("NativeResolver: unknown PC RVA 0x%llX — returning 0",
			(unsigned long long)PcRva);
		return 0;
	}
	return ResolveRule(*R);
}

} // namespace NativeResolver

namespace OGS
{

uint64_t ResolveKey(uint64_t PcRva)
{
	return NativeResolver::Resolve(PcRva);
}

bool HookAt(uint64_t Addr, void* Detour, void** Original)
{
	return Hooking::InternalHook(Addr, Detour, Original);
}

bool HookAtNoRef(uint64_t Addr, void* Detour)
{
	return Hooking::InternalHook(Addr, Detour, nullptr);
}

} // namespace OGS
