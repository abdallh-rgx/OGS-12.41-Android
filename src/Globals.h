#pragma once
// (xkeycheck.h removed — MSVC header)

namespace Globals {
	inline bool bIsProdServer = false;

	inline bool bCreativeEnabled = false;
	inline bool bSTWEnabled = false;
	inline bool bEventEnabled = false;

	inline bool bBossesEnabled = true;
	inline bool bBotsEnabled = true;

	inline bool bUseLegacyAI_MANG = true; // Keep this on true bro, I am NOT doing this!

	inline bool LateGame = false;
	inline bool Automatics = true;
	inline bool BattleLab = false;
	inline bool Blitz = false;
	inline bool StormKing = false;
	inline bool Arsenal = false;
	inline bool TeamRumble = false;
	inline bool SolidGold = false;
	inline bool UnVaulted = false;
	inline bool Siphon = false;
	inline bool Arena = false;

	inline int MaxBotsToSpawn = 100;
	inline int MinPlayersForEarlyStart = 100;
}
