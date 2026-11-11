// ============================================================================
// HookingARM64.cpp — ARM64 implementation of the Erbium Hooking namespace,
// v2: Dobby-backed (merged from sinum).
//
// v1 shipped a hand-written inline-hook engine with TWO fatal relocation
// bugs (the on-device crash — see tools/verify_hooks_angr.py regression
// section for the mathematical post-mortem):
//
//   BUG-1  the ADRP decoder dropped immlo (bits 30:29) — every ADRP with
//          immlo != 0 (3 out of 4 ADRPs) relocated to the WRONG page in the
//          trampoline, so calling the original through the trampoline
//          dereferenced a wild address. ProcessEvent alone calls its
//          trampoline on every event.
//   BUG-2  the B/BL out-of-range veneer emitted `LDR X17,#8 ; BR X17` but
//          appended the target quad at the END of the trampoline — the LDR
//          actually reads the two words right after the BR, i.e. the next
//          relocated instructions, so the "target" was garbage.
//
// v2 removes the custom engine entirely and delegates to Dobby — the exact
// prebuilt third_party/dobby/libdobby-arm64.a that libsinum shipped and
// that is PROVEN on-device against this very libUE4.so on a PAC-enabled
// ARMv9 device (sinum hooked curl_easy_setopt through it, crash-free).
//
// What remains ours are the direct in-place patches that never relocate
// instructions (mathematically safe, encodings angr/Z3-verified):
//   Null / RetTrue / Nop / InvertBranch / VirtualSwap
// ============================================================================
#include "pch-android.h"

#include "dobby.h"

#include <map>
#include <cstdint>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace Hooking
{
    // ─── bookkeeping ─────────────────────────────────────────────────────────
    struct FHookRecord
    {
        void* detour;   // for re-install (EnableHook)
        bool enabled;
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

    // ─── ARM64 encodings (direct patches only — no relocation involved) ─────
    static uint32_t EncNop() { return 0xD503201F; }
    static uint32_t EncRet() { return 0xD65F03C0; }
    static uint32_t EncMovW0_1() { return 0x52800020; }

    // ─── public API ──────────────────────────────────────────────────────────

    // Inline hook a function at `Ptr`: every call is routed to `Detour`;
    // `*Original` (optional) receives a callable trampoline that executes
    // the displaced original prologue and jumps back.
    bool InternalHook(uintptr_t Ptr, void* Detour, void** Original)
    {
        if (!Ptr || !Detour)
            return false;

        // already hooked by us → refuse (re-hooking a Dobby entry through a
        // second DobbyHook would relocate the first dispatch — the exact
        // double-hook corruption the sinum merge removed)
        if (g_Hooks.count(Ptr))
        {
            LOGW("Hooking: %llx already hooked — refusing double hook",
                (unsigned long long)Ptr);
            return false;
        }

        // Dobby: full ARM64 inline-hook engine (instruction relocation,
        // near trampolines, PAC-aware). The vendored prebuilt is the exact
        // binary libsinum used crash-free on this device/game.
        int Result = DobbyHook((void*)Ptr, (dobby_dummy_func_t)Detour,
            Original ? (dobby_dummy_func_t*)Original : nullptr);
        if (Result != 0)
        {
            LOGW("Hooking: DobbyHook(%llx) failed (%d)", (unsigned long long)Ptr, Result);
            return false;
        }

        FHookRecord Rec;
        Rec.detour = Detour;
        Rec.enabled = true;
        g_Hooks[Ptr] = Rec;
        return true;
    }

    bool EnableHook(uintptr_t Ptr)
    {
        auto it = g_Hooks.find(Ptr);
        if (it == g_Hooks.end())
            return false;
        if (it->second.enabled)
            return true;
        // re-install the recorded detour (v1 stored the trampoline here and
        // jumped straight into it — fixed)
        void* Dummy = nullptr;
        if (DobbyHook((void*)Ptr, (dobby_dummy_func_t)it->second.detour,
                (dobby_dummy_func_t*)&Dummy) != 0)
            return false;
        it->second.enabled = true;
        return true;
    }

    bool DisableHook(uintptr_t Ptr)
    {
        auto it = g_Hooks.find(Ptr);
        if (it == g_Hooks.end() || !it->second.enabled)
            return false;
        if (DobbyDestroy((void*)Ptr) != 0)
            return false;
        it->second.enabled = false;
        return true;
    }

    // ─── direct in-place patches (no instruction relocation — spec encodings,
    //     proven by tools/verify_hooks_angr.py V7 + the encoder test points) ─

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

    // Allocate an RWX region (API compat with the Erbium shellcode helpers).
    uint8_t* AllocateShellcodeRegion(void* targetAddr, size_t SizeOfShellcode)
    {
        void* p = mmap(nullptr, SizeOfShellcode, PROT_READ | PROT_WRITE | PROT_EXEC,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        return (p && p != MAP_FAILED) ? (uint8_t*)p : nullptr;
    }

    // ARM64 has no rel32 E9-style patch distinct from the standard inline
    // hook — same engine.
    void InternalRel32Hook(uintptr_t _Ptr, void* _Detour, void** _Orig)
    {
        InternalHook(_Ptr, _Detour, _Orig);
    }

    // Swap a vtable slot (pure data write — POSIX, not the v1 Win32 compat).
    void VirtualSwap(void** VTable, int Index, void* Detour, void** Original)
    {
        if (!VTable)
            return;
        SetRWX((uintptr_t)&VTable[Index], 8);
        if (Original)
            *Original = VTable[Index];
        VTable[Index] = Detour;
        FlushICache((void*)&VTable[Index], 8);
    }
} // namespace Hooking
