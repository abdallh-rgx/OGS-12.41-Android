// ListenServer.cpp — NM_ListenServer bring-up (see ListenServer.h).
//
// The internal functions (CreateNetDriver / InitListen / SetWorld / InitHost /
// PauseBeaconRequests) are resolved through NativeResolver from the original
// OGS framework.h initializers; the call sequence mirrors
// UEngine::LoadMap / UWorld::Listen of UE 4.25.
#include "framework.h"
#include "ListenServer.h"

namespace ListenServer
{

bool bActive = false;

namespace
{

// Build an FURL like UE's FURL::URL from a text address (0.0.0.0:7777) — the
// PC OGS used the engine's own URL parser; the fields are laid out per the
// SDK's FURL (Engine_structs) and only Map/Port/Valid matter for InitListen.
FURL MakeListenURL()
{
	FURL Url{};
	Url.Protocol = u"GameNetDriver";
	Url.Host = u"0.0.0.0";
	Url.Port = Port;
	Url.Valid = 1;
	return Url;
}

void* GetNetDriverName()
{
	// FName "GameNetDriver" — the driver class the engine creates for
	// gameplay connections (UE 4.25 default).
	static FName Name = UKismetStringLibrary::Conv_StringToName(u"GameNetDriver");
	return &Name;
}

} // namespace

void Setup()
{
	if (bActive)
		return;

	Log("ListenServer::Setup — bringing up NM_ListenServer (host keeps playing)");

	if (!CreateNetDriver || !InitListen || !SetWorld)
	{
		Log("ListenServer: internal functions unresolved — listener NOT started");
		return;
	}

	UWorld* World = UWorld::GetWorld();
	if (!World || !World->OwningGameInstance)
	{
		Log("ListenServer: no world yet — deferred to OnWorldLoaded");
		return;
	}

	FName DriverName = UKismetStringLibrary::Conv_StringToName(u"GameNetDriver");
	UNetDriver* Driver = CreateNetDriver(World->OwningGameInstance, World, DriverName);
	if (!Driver)
	{
		Log("ListenServer: CreateNetDriver failed");
		return;
	}

	FURL Url = MakeListenURL();
	FString Error{};

	if (!InitListen(Driver, nullptr, Url, false, Error))
	{
		Log(std::string("ListenServer: InitListen failed: ") + Error.ToString());
		return;
	}

	SetWorld(Driver, World);

	// game-session side (keeps the match alive while joiners connect)
	UObject* GameSession = nullptr;
	if (World->AuthorityGameMode)
	{
		// AGameSession is owned by the GameMode (UE 4.25: GameMode->GameSession
		// isn't reflected on Android builds — resolve via FindObject).
		GameSession = StaticFindObject<AGameSession>(u"/Game/Maps/Apollo_Terrain.Apollo_Terrain.PersistentLevel.GameSession");
	}
	if (InitHost)
	{
		if (GameSession && InitHost(GameSession))
			Log("ListenServer: GameSession InitHost OK");
		else
			Log("ListenServer: InitHost skipped/failed (non-fatal for the listener)");
	}
	if (PauseBeaconRequests && GameSession)
		PauseBeaconRequests(GameSession, false);

	bActive = true;
	Log("ListenServer: LISTENING on 0.0.0.0:7777 (players join by this device's IP)");
}

void OnWorldLoaded()
{
	if (!bActive)
	{
		// world didn't exist yet during Setup() — retry now
		Setup();
		return;
	}

	// Map travel re-creates the world: rebind the NetDriver to the new world
	// (UE's LoadMap does the same for an existing listener).
	UWorld* World = UWorld::GetWorld();
	if (World && SetWorld)
		SetWorld(nullptr /* NetDriver (resolved again below) */, World);

	Log("ListenServer: world loaded — listener bound to the new world");
	Setup(); // idempotent rebind if the driver was recreated
}

} // namespace ListenServer
