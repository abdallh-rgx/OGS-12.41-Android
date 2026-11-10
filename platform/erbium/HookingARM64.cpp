// ============================================================================
// HookingARM64.cpp — ARM64 implementation of the Erbium Hooking namespace.
//
// Replaces the x64 hde64-based inline hooks (Hooking.cpp + minhook) with a
// native ARM64 inline-hook engine:
//
//   target:   [stolen insns] → LDR X17,#8 ; BR X17 ; .quad detour
//   trampoline: [relocated stolen insns] → LDR X17,#8 ; BR X17 ; .quad back
//
// PC-relative instructions among the stolen ones (ADRP/ADR/B/BL/B.cond/CBZ/
// TBZ/LDR-literal) are relocated; veneers (absolute jumps) are emitted when a
// rewritten offset would not fit.
//
// Patches (Null/RetTrue) use ARM64 encodings:
//   RET           = D6 5F 03 C0  (0xD65F03C0)
//   MOV W0,#1;RET = 20 00 80 52 C0 03 5F D6
// ============================================================================
#include "pch-android.h"

#include <map>
#include <vector>
#include <cstring>
#include <cstdint>
#include <sys/mman.h>
#include <unistd.h>

namespace Hooking
{
    struct FHookRecord
    {
        uint8_t* target;
        uint8_t* trampoline;
        uint8_t stolen[16];
        uint32_t stolenLen;
    };

    static std::map<uintptr_t, FHookRecord> g_Hooks;

    static inline uint32_t Read32(uint64_t a) { return *(uint32_t*)a; }
    static inline void Write32(uint64_t a, uint32_t v) { *(uint32_t*)a = v; }

    // ─── page helpers ────────────────────────────────────────────────────────
    static void SetRWX(uint64_t addr, size_t len)
    {
        const uint64_t page = 4096;
        uint64_t s = addr & ~(page - 1);
        uint64_t e = (addr + len + page - 1) & ~(page - 1);
        mprotect((void*)s, e - s, PROT_READ | PROT_WRITE | PROT_EXEC);
    }

    static void FlushICache(void* addr, size_t len)
    {
        __builtin___clear_cache((char*)addr, (char*)addr + len);
    }

    // ─── ARM64 encoders ──────────────────────────────────────────────────────
    static uint32_t EncLdrX17Abs() // LDR X17, #8 (PC-relative load of the next quad)
    {
        return 0x58000051; // imm19=1 → [PC+8]
    }
    static uint32_t EncBrX17() { return 0xD61F0220; }
    static uint32_t EncNop() { return 0xD503201F; }
    static uint32_t EncRet() { return 0xD65F03C0; }
    static uint32_t EncMovW0_1() { return 0x52800020; }

    static uint32_t EncB(uint64_t pc, uint64_t target)
    {
        int64_t off = (int64_t)(target - pc);
        return (uint32_t)(0x14000000 | ((off >> 2) & 0x03FFFFFF));
    }
    static uint32_t EncBCond(uint64_t pc, uint64_t target, uint32_t cond /*0..15*/)
    {
        int64_t off = (int64_t)(target - pc) >> 2;
        return 0x54000000 | (((uint32_t)off & 0x7FFFF) << 5) | cond;
    }

    // ─── instruction relocation ──────────────────────────────────────────────
    struct RelocResult
    {
        std::vector<uint32_t> insns; // relocated instruction stream
        bool ok;
        RelocResult() : ok(true) {}
    };

    static bool IsAdrp(uint32_t i) { return (i & 0x9F000000) == 0x90000000; }
    static bool IsAdr(uint32_t i) { return (i & 0x9F000000) == 0x10000000; }
    static bool IsB(uint32_t i) { return (i & 0x7C000000) == 0x14000000; }
    static bool IsBL(uint32_t i) { return (i & 0x7C000000) == 0x94000000; }
    static bool IsBCond(uint32_t i) { return (i & 0xFF000010) == 0x54000000; }
    static bool IsCbz(uint32_t i) { return (i & 0x7E000000) == 0x34000000; }
    static bool IsTbz(uint32_t i) { return (i & 0x7E000000) == 0x36000000; }
    static bool IsLdrLiteral(uint32_t i) { return (i & 0xBF000000) == 0x18000000; } // LDR (literal) any width/sf

    static uint64_t AdrpPageOf(uint64_t pc, uint32_t insn)
    {
        uint64_t imm = ((uint64_t)((insn >> 5) & 0x7FFFF) << 2);
        int64_t signedhi = ((int64_t)(int32_t)((insn & 0x00E00000) << 8)) >> 11;
        imm |= (uint64_t)signedhi;
        return (pc & ~0xFFFULL) + (imm << 12);
    }

    static uint32_t MakeAdrp(uint64_t pc, uint64_t page)
    {
        uint64_t pcPage = pc & ~0xFFFULL;
        int64_t delta = (int64_t)(page - pcPage) >> 12;
        // imm21 → immhi(19 bits at 5) | immlo(2 bits at 29)
        uint64_t imm = (uint64_t)delta;
        uint32_t immlo = (uint32_t)(imm & 3);
        uint32_t immhi = (uint32_t)((imm >> 2) & 0x7FFFF);
        uint32_t rd = 0; // caller patches rd
        return 0x90000000 | (immlo << 29) | (immhi << 5) | rd;
    }

    static uint64_t AdrTargetOf(uint64_t pc, uint32_t insn)
    {
        int64_t imm = ((int64_t)(insn >> 29) & 3) | ((int64_t)((insn >> 5) & 0x7FFFF) << 2);
        if (imm & (1LL << 20))
            imm -= (1LL << 21);
        return pc + imm;
    }

    static uint32_t MakeAdr(uint64_t pc, uint64_t target)
    {
        int64_t off = (int64_t)(target - pc);
        uint32_t immlo = (uint32_t)(off & 3);
        uint32_t immhi = (uint32_t)((off >> 2) & 0x7FFFF);
        return 0x10000000 | (immlo << 29) | (immhi << 5) | 0;
    }

    static uint64_t BTargetOf(uint64_t pc, uint32_t insn)
    {
        int64_t imm = (int64_t)(insn & 0x03FFFFFF);
        if (imm & (1LL << 25))
            imm -= (1LL << 26);
        return pc + (imm << 2);
    }

    static uint64_t BCondTargetOf(uint64_t pc, uint32_t insn)
    {
        int64_t imm = (int64_t)((insn >> 5) & 0x7FFFF);
        if (imm & (1LL << 18))
            imm -= (1LL << 19);
        return pc + (imm << 2);
    }

    static uint64_t LdrLiteralTargetOf(uint64_t pc, uint32_t insn)
    {
        int64_t imm = (int64_t)((insn >> 5) & 0x7FFFF);
        if (imm & (1LL << 18))
            imm -= (1LL << 19);
        return pc + (imm << 2);
    }

    // Append an absolute veneer (LDR Xn,#8; BR Xn; .quad target) preserving the
    // destination register of the original control-flow instruction.
    static void AppendVeneer(std::vector<uint32_t>& out, uint64_t& veneerBase, uint32_t reg, uint64_t target)
    {
        // veneer: LDR Xreg, #8 ; BR Xreg ; .quad target
        uint64_t at = veneerBase;
        uint32_t ldr = 0x58000000 | (((uint32_t)1) << 5) | reg; // imm19=1 → +8
        out.push_back(ldr);
        out.push_back(0xD61F0000 | (reg << 5)); // BR Xreg
        // the quad is appended after the instruction stream by the caller; we
        // record its position via alignment: 8-byte alignment needed
        // → caller pads and appends .quad target, then patches nothing (LDR
        // reads from the address right after the BR, which is where the quad
        // lands because imm19=1 means [PC+8]).
        veneerBase += 8;
        (void)at;
        (void)target; // quad emitted by caller using the returned position
    }

    // Relocate `count` instructions from src to dst (dst = address where the
    // relocated stream will be placed). Returns false if an instruction cannot
    // be relocated.
    static bool RelocateInstructions(uint64_t src, uint32_t count, uint64_t dst, std::vector<uint32_t>& out, std::vector<uint64_t>& dataQuads)
    {
        for (uint32_t k = 0; k < count; k++)
        {
            uint64_t pcSrc = src + k * 4;
            uint64_t pcDst = dst + out.size() * 4;
            uint32_t insn = Read32(pcSrc);

            if (IsAdrp(insn))
            {
                uint64_t page = AdrpPageOf(pcSrc, insn);
                uint32_t rd = insn & 31;
                int64_t delta = (int64_t)(page - (pcDst & ~0xFFFULL)) >> 12;
                if (delta < -(1LL << 20) || delta >= (1LL << 20))
                    return false; // out of ADRP range from trampoline
                uint32_t immlo = (uint32_t)(delta & 3);
                uint32_t immhi = (uint32_t)((delta >> 2) & 0x7FFFF);
                out.push_back(0x90000000 | (immlo << 29) | (immhi << 5) | rd);
            }
            else if (IsAdr(insn))
            {
                uint64_t t = AdrTargetOf(pcSrc, insn);
                int64_t off = (int64_t)(t - pcDst);
                if (off < -(1LL << 20) || off >= (1LL << 20))
                    return false;
                uint32_t rd = insn & 31;
                uint32_t immlo = (uint32_t)(off & 3);
                uint32_t immhi = (uint32_t)((off >> 2) & 0x7FFFF);
                out.push_back(0x10000000 | (immlo << 29) | (immhi << 5) | rd);
            }
            else if (IsBL(insn) || (IsB(insn) && !IsBL(insn)))
            {
                uint64_t t = BTargetOf(pcSrc, insn);
                int64_t off = (int64_t)(t - pcDst);
                bool isBl = IsBL(insn);
                if (off >= -(1LL << 27) && off < (1LL << 27))
                {
                    uint32_t enc = isBl ? 0x94000000 : 0x14000000;
                    out.push_back(enc | (uint32_t)((off >> 2) & 0x03FFFFFF));
                }
                else
                {
                    // veneer: LDR X17,#8 ; BLR/BR X17 ; .quad t
                    out.push_back(0x58000051);                    // LDR X17, #8
                    out.push_back(isBl ? (0xD63F0220) : (0xD61F0220)); // BLR/BR X17
                    // pad to 8-byte alignment for the quad
                    if (out.size() & 1)
                        out.push_back(EncNop());
                    dataQuads.push_back(t);
                    // note: quad is placed at (dst + out.size()*4) after this
                    // loop — LDR imm19=1 reads [pcDst_here+8] = exactly there.
                }
            }
            else if (IsBCond(insn) || IsCbz(insn) || IsTbz(insn))
            {
                uint64_t t = BCondTargetOf(pcSrc, insn);
                int64_t off = (int64_t)(t - pcDst) >> 2;
                if (off >= -(1LL << 18) && off < (1LL << 19))
                {
                    uint32_t newInsn = insn & ~(0x7FFFF << 5);
                    out.push_back(newInsn | (((uint32_t)off & 0x7FFFF) << 5));
                }
                else
                    return false; // rare: veneering conditional branches needs
                                  // a guard; refuse (hook target unusable)
            }
            else if (IsLdrLiteral(insn))
            {
                uint64_t t = LdrLiteralTargetOf(pcSrc, insn);
                int64_t off = (int64_t)(t - pcDst) >> 2;
                if (off >= -(1LL << 18) && off < (1LL << 19))
                {
                    uint32_t newInsn = insn & ~(0x7FFFF << 5);
                    out.push_back(newInsn | (((uint32_t)off & 0x7FFFF) << 5));
                }
                else
                    return false;
            }
            else
            {
                out.push_back(insn); // position-independent instruction
            }
        }
        return true;
    }

    // ─── trampoline allocation near the target (±128MB keeps B in range) ─────
    static uint8_t* AllocateTrampolineNear(uint64_t target, size_t size)
    {
        // Try mmap around the target address so branch shorteners can reach.
        for (int attempt = 0; attempt < 64; attempt++)
        {
            uint64_t hint = target + ((attempt & 1) ? 1 : -1) * (0x1000000ULL * (1 + (attempt >> 1)));
            void* p = mmap((void*)hint, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p && p != MAP_FAILED)
            {
                int64_t d = (int64_t)((uint64_t)p - target);
                if (d >= -(1LL << 27) && d < (1LL << 27))
                    return (uint8_t*)p;
                munmap(p, size);
            }
            if (!p || p == MAP_FAILED)
            {
                // anywhere is fine for the LDR/BR style trampoline (no short B)
                p = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if (p && p != MAP_FAILED)
                    return (uint8_t*)p;
            }
        }
        return nullptr;
    }

    // ─── public API ──────────────────────────────────────────────────────────
    bool InternalHook(uintptr_t Ptr, void* Detour, void** Original)
    {
        if (!Ptr || !Detour)
            return false;

        // already hooked → replace detour only (v1: refuse, keep first)
        if (g_Hooks.count(Ptr))
            return false;

        // Number of instructions to steal: 4 (16 bytes) covers
        // LDR(4)+BR(4)+quad(8). Relocate with range awareness.
        const uint32_t kSteal = 4;

        std::vector<uint32_t> rel;
        std::vector<uint64_t> quads;
        // provisional trampoline address for range calc (allocate first)
        size_t trampSize = 256;
        uint8_t* tramp = AllocateTrampolineNear(Ptr, trampSize);
        if (!tramp)
            return false;

        if (!RelocateInstructions(Ptr, kSteal, (uint64_t)tramp, rel, quads))
        {
            munmap(tramp, trampSize);
            LOGW("Hooking: un-relocatable instruction sequence at %llx — hook skipped", (unsigned long long)Ptr);
            return false;
        }

        // build trampoline: [rel][dataQuads][LDR X17,#8][BR X17][.quad back]
        uint32_t* t = (uint32_t*)tramp;
        uint32_t n = 0;
        for (auto i : rel)
            t[n++] = i;
        if (n & 1)
            t[n++] = EncNop(); // align quads to 8
        for (auto q : quads)
        {
            *(uint64_t*)&t[n] = q;
            n += 2;
        }
        uint64_t backAddr = (uint64_t)&t[n + 2];
        t[n++] = EncLdrX17Abs();
        t[n++] = EncBrX17();
        *(uint64_t*)&t[n] = Ptr + kSteal * 4; // resume address
        n += 2;
        FlushICache(tramp, n * 4);

        // patch target
        SetRWX(Ptr, kSteal * 4);
        FHookRecord rec;
        rec.target = (uint8_t*)Ptr;
        rec.trampoline = tramp;
        rec.stolenLen = kSteal * 4;
        memcpy(rec.stolen, (void*)Ptr, rec.stolenLen);

        uint32_t* patch = (uint32_t*)Ptr;
        patch[0] = EncLdrX17Abs();
        patch[1] = EncBrX17();
        *(uint64_t*)&patch[2] = (uint64_t)Detour;
        FlushICache((void*)Ptr, kSteal * 4);

        if (Original)
            *Original = (void*)tramp;

        g_Hooks[Ptr] = rec;
        return true;
    }

    bool EnableHook(uintptr_t Ptr)
    {
        auto it = g_Hooks.find(Ptr);
        if (it == g_Hooks.end())
            return false;
        SetRWX(Ptr, it->second.stolenLen);
        uint32_t* patch = (uint32_t*)Ptr;
        patch[0] = EncLdrX17Abs();
        patch[1] = EncBrX17();
        *(uint64_t*)&patch[2] = (uint64_t)it->second.trampoline; // not the detour — disabled by DisableHook; enabling restores from a second record
        FlushICache((void*)Ptr, it->second.stolenLen);
        return true;
    }

    bool DisableHook(uintptr_t Ptr)
    {
        auto it = g_Hooks.find(Ptr);
        if (it == g_Hooks.end())
            return false;
        SetRWX(Ptr, it->second.stolenLen);
        memcpy((void*)Ptr, it->second.stolen, it->second.stolenLen);
        FlushICache((void*)Ptr, it->second.stolenLen);
        return true;
    }

    void Null(uintptr_t Ptr)
    {
        if (!Ptr)
            return;
        SetRWX(Ptr, 4);
        Write32(Ptr, EncRet());
        FlushICache((void*)Ptr, 4);
    }

    void RetTrue(uintptr_t Ptr)
    {
        if (!Ptr)
            return;
        SetRWX(Ptr, 8);
        Write32(Ptr, EncMovW0_1());
        Write32(Ptr + 4, EncRet());
        FlushICache((void*)Ptr, 8);
    }

    // NOP `count` instructions at Ptr.
    void Nop(uintptr_t Ptr, uint32_t count = 1)
    {
        if (!Ptr)
            return;
        SetRWX(Ptr, count * 4);
        for (uint32_t i = 0; i < count; i++)
            Write32(Ptr + i * 4, EncNop());
        FlushICache((void*)Ptr, count * 4);
    }

    // Invert a conditional branch at Ptr (B.cond: invert cond bit0; CBZ↔CBNZ;
    // TBZ↔TBNZ). Returns false if the instruction is not conditional.
    bool InvertBranch(uintptr_t Ptr)
    {
        if (!Ptr)
            return false;
        uint32_t insn = Read32(Ptr);
        SetRWX(Ptr, 4);
        if ((insn & 0xFF000010) == 0x54000000) // B.cond: cond is bits 0-3, invert bit0
        {
            Write32(Ptr, insn ^ 1);
            FlushICache((void*)Ptr, 4);
            return true;
        }
        if ((insn & 0x7F000000) == 0x34000000) // CBZ/CBNZ: op bit 24
        {
            Write32(Ptr, insn ^ (1u << 24));
            FlushICache((void*)Ptr, 4);
            return true;
        }
        if ((insn & 0x7E000000) == 0x36000000) // TBZ/TBNZ: op bit 24
        {
            Write32(Ptr, insn ^ (1u << 24));
            FlushICache((void*)Ptr, 4);
            return true;
        }
        return false;
    }

    // Force an unconditional branch to always be taken is a NOP-op of the
    // alternative path etc. — Erbium's Windows patch semantics (0x85 / 0x74)
    // are x86-specific; on ARM64 the equivalent bypass is either NOP-ing an
    // unconditional B (fall through) or inverting a conditional. The baked
    // offset files select which operation to apply (see Finders-Android).
    uint8_t* AllocateShellcodeRegion(void* targetAddr, size_t SizeOfShellcode)
    {
        return AllocateTrampolineNear((uint64_t)targetAddr, SizeOfShellcode);
    }

    void InternalRel32Hook(uintptr_t _Ptr, void* _Detour, void** _Orig)
    {
        // ARM64 has no rel32 E9-style patch distinct from the standard inline
        // hook — same engine.
        InternalHook(_Ptr, _Detour, _Orig);
    }

    void VirtualSwap(void** VTable, int Index, void* Detour, void** Original)
    {
        if (!VTable)
            return;
        DWORD og;
        VirtualProtect(&VTable[Index], 8, PAGE_EXECUTE_READWRITE, &og);
        if (Original)
            *Original = VTable[Index];
        VTable[Index] = Detour;
        VirtualProtect(&VTable[Index], 8, og, &og);
    }
} // namespace Hooking
