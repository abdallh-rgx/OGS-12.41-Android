// NativeResolver.h — PC-RVA keyed resolution of the engine's internal
// (non-reflected) C++ functions for the Android 12.41 build.
//
// The PC source hardcodes `ImageBase + 0x<PC RVA>` for a set of internal
// engine functions. On Android those addresses differ; instead of hardcoding
// Android RVAs (no reference binary), every site keeps the PC RVA as a
// semantic key (OGS_RESOLVE / OGS_HOOK / OGS_RETADDR_IS) and this resolver
// maps each key to the Android address at bring-up:
//
//   1. KNOWN      — exact verified offsets (ProcessEvent, GObjects, ...)
//   2. ERBIUM     — the Erbium-Android v6 finders (runtime chains: version-
//                   clustered patterns, string anchors, native thunk scans)
//   3. VTABLE     — UE-source vtable slots seeded from an anchored function
//   4. EXEC-ANCHOR— BL xref out of a UFunction exec wrapper (SDK anchors)
//   5. STRING     — UE 4.25 log-string anchors via RT::StringAnchorFunc
#pragma once

#include <cstdint>

namespace NativeResolver
{
	// Resolve every registered key; logs the resolution table. Called at
	// bring-up (after libUE4.so is mapped, before hooks install).
	void Init();

	// Resolve one PC RVA key to an absolute Android address (0 on failure).
	uint64_t Resolve(uint64_t PcRva);

	// Register a PC RVA -> resolution rule (called by OGS_RESOLVE at static
	// init via the resolver table; also used for the hook macro).
	uint64_t Register(uint64_t PcRva);
}

// ─── the source-side macros (implemented in NativeResolver.h for use across
//     TUs — the definitions live in NativeResolver.cpp) ────────────────────
//
// OGS_RESOLVE(0xRVA)  -> function pointer value (uintptr_t) for a call site
// OGS_HOOK(0xRVA, Detour, OgRef)   -> install inline hook at the resolved addr
// OGS_HOOK_NOREF(0xRVA, Detour)    -> same, without an original pointer
// OGS_RETADDR_IS(0xVA) -> return-address check against resolved PC site
// OGS_RETURN_ADDRESS() -> __builtin_return_address(0)

#include "Hooks.h"
