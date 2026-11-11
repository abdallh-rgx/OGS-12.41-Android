// ============================================================================
// ListenServer.h — UE NM_ListenServer semantics for the Android port.
//
// Per the UnrealEngine definition, a Listen Server keeps the host's
// LocalPlayer alive (host plays) while accepting client connections:
//   - UEngine::CreateNetDriver(GameInstance, "GameNetDriver")
//   - UIpNetDriver::InitListen(NetDriver, Notify, URL(0.0.0.0:7777), bReuse, Error)
//   - UNetDriver::SetWorld(World)
//   - AGameSession::InitHost() + PauseBeaconRequests(false)
// The GUI's "join by IP" mode lets a second device connect to this listener.
// ============================================================================
#pragma once

#include "framework.h"

namespace ListenServer
{
	// Bring the listen server up (called before the map opens).
	void Setup();

	// Called right after `open Apollo_Terrain` (rebinds the NetDriver to the
	// new world — UE re-creates the world on map travel).
	void OnWorldLoaded();

	// Whether the listener is currently active.
	extern bool bActive;

	// The port the listener binds (matching the PC OGS default).
	constexpr uint16_t Port = 7777;
}
