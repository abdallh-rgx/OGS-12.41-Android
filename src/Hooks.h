// Hooks.h — OGS hook/resolve macros (PC-RVA keyed) + ARM64 hook facade over
// the Erbium-Android inline-hook engine (platform/erbium/HookingARM64.cpp).
#pragma once

#include <cstdint>

// clang/ARM64 replacement for MSVC's _ReturnAddress()
#define OGS_RETURN_ADDRESS() __builtin_return_address(0)

namespace Hooking
{
	// declared in platform/erbium/HookingARM64.cpp
	bool InternalHook(uintptr_t Ptr, void* Detour, void** Original);
	void Null(uintptr_t Ptr);
	void RetTrue(uintptr_t Ptr);
	void Nop(uintptr_t Ptr, uint32_t count = 1);
	bool InvertBranch(uintptr_t Ptr);
	void VirtualSwap(void** VTable, int Index, void* Detour, void** Original = nullptr);
}

namespace OGS
{
	// Resolve a PC RVA key to an absolute Android address (NativeResolver).
	uint64_t ResolveKey(uint64_t PcRva);

	// Raw inline-hook primitives (HookingARM64).
	bool HookAt(uint64_t Addr, void* Detour, void** Original);
	bool HookAtNoRef(uint64_t Addr, void* Detour);

	// PC-RVA keyed hook helpers (detour is templated: MSVC accepts implicit
	// function-pointer -> void* conversions, clang does not).
	template <typename OgType, typename DetourType>
	inline bool HookKeyWithRef(uint64_t PcRva, DetourType Detour, OgType& OgRef)
	{
		const uint64_t Addr = ResolveKey(PcRva);
		if (!Addr)
			return false;
		void* Orig = nullptr;
		if (!HookAt(Addr, reinterpret_cast<void*>(Detour), &Orig))
			return false;
		if (Orig)
			OgRef = reinterpret_cast<OgType>(Orig);
		return true;
	}

	template <typename DetourType>
	inline bool HookKey(uint64_t PcRva, DetourType Detour)
	{
		const uint64_t Addr = ResolveKey(PcRva);
		return Addr ? HookAtNoRef(Addr, reinterpret_cast<void*>(Detour)) : false;
	}
}

// resolved function-pointer value for call sites (cached lookup per key)
#define OGS_RESOLVE(pcRva) OGS::ResolveKey(pcRva)

// hook at the resolved address; keeps the original in ogRef
#define OGS_HOOK(pcRva, detour, ogRef) OGS::HookKeyWithRef(pcRva, detour, ogRef)

// hook at the resolved address without an original
#define OGS_HOOK_NOREF(pcRva, detour) OGS::HookKey(pcRva, detour)

// return-address equality against the Android address of a PC call site
#define OGS_RETADDR_IS(pcVa) ((uintptr_t)OGS_RETURN_ADDRESS() == OGS::ResolveKey(pcVa))
