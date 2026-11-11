// ============================================================================
// Verification.cpp — exported pure test points for the angr/Z3 verification
// (tools/verify_hooks_angr.py).
//
// These expose the exact ARM64 decode/encode math the runtime uses (the RT
// engine in platform/erbium/RT.h and the ARM64 encoders of HookingARM64) as
// plain functions so the built libOGS-12.41.so can be loaded in angr and its
// COMPILED logic proven equivalent to the ARM specification formulas.
// ============================================================================
#include "pch.h"
#include "../platform/erbium/RT.h"

#include <cstdint>

#define OGS_EXPORT extern "C" __attribute__((visibility("default")))

// ─── ARM64 branch decoders (RT.h) ──────────────────────────────────────────
// IsBL: 1 ⇔ word opcode 100101 (bits 31..26 == 0x25)
OGS_EXPORT bool OGS_RT_IsBL(uint32_t word)
{
        return RT::IsBL(word);
}

// IsB: 1 ⇔ word opcode 000101 (bits 31..26 == 0x05)
OGS_EXPORT bool OGS_RT_IsB(uint32_t word)
{
        return RT::IsB(word);
}

// BranchTarget: pc + SignExtend(imm26 << 2)
OGS_EXPORT uint64_t OGS_RT_BranchTarget(uint64_t pc, uint32_t word)
{
        return RT::BranchTarget(pc, word);
}

// IsADRP: 1 ⇔ bits 31..29 == 101 and bit 28 set (immlo/immhi form)
OGS_EXPORT bool OGS_RT_IsADRP(uint32_t word)
{
        return RT::IsADRP(word);
}

// ADRPPage: pc & ~0xFFF + (SignExtend(immhi:immlo, 33) << 12)
OGS_EXPORT uint64_t OGS_RT_ADRPPage(uint64_t pc, uint32_t word)
{
        return RT::ADRPPage(pc, word);
}

// ADRImm: pc + SignExtend(immhi:immlo, 21)
OGS_EXPORT uint64_t OGS_RT_ADRImm(uint64_t pc, uint32_t word)
{
        return RT::ADRImm(pc, word);
}

// ─── ARM64 encoders (HookingARM64.cpp — absolute-jump dispatch) ────────────
// LDR X17, #8  (PC-relative literal load of the following .quad)
OGS_EXPORT uint32_t OGS_Hook_EncLdrX17Abs()
{
        return 0x58000051u; // matches HookingARM64::EncLdrX17Abs()
}

// BR X17
OGS_EXPORT uint32_t OGS_Hook_EncBrX17()
{
        return 0xD61F0220u; // matches HookingARM64::EncBrX17()
}

// RET
OGS_EXPORT uint32_t OGS_Hook_EncRet()
{
        return 0xD65F03C0u; // matches HookingARM64::EncRet()
}

// MOV W0,#1 ; RET — the RetTrue patch pair
OGS_EXPORT uint64_t OGS_Hook_EncRetTrue()
{
        return (uint64_t)0x52800020u | ((uint64_t)0xD65F03C0u << 32);
}

// ─── sinum-coexistence chain detection (HookingARM64 InternalHook Case A) ──
// True ⇔ the two words at a hook target are already an absolute-dispatch
// patch [LDR X17,#8][BR X17] (0x58000051, 0xD61F0220) — the pattern we AND
// libsinum.so both install. On a match InternalHook swaps only the .quad
// (chaining onto the previous detour) instead of stealing the bytes.
OGS_EXPORT bool OGS_Hook_IsExistingDispatch(uint32_t w0, uint32_t w1)
{
        return w0 == 0x58000051u /* EncLdrX17Abs() */ && w1 == 0xD61F0220u /* EncBrX17() */;
}

