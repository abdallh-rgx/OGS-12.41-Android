// ============================================================================
// Memcury.h — Android/ELF64/ARM64 port of the upstream WinPE Memcury scanner.
// Same public API (Scanner::FindPattern / FindStringRef / ScanFor /
// RelativeOffset / AbsoluteOffset / Get / GetAs, ASM::pattern2bytes,
// PE::Address, ImageBase, bUE51) so the Erbium sources compile unmodified.
//
// ELF specifics handled here (from the Erbium-Android v6 analysis work):
//   • Module layout from program headers at ImageBase (PT_LOAD exec/writable)
//   • UTF-16 strings live in .text literal pools (referenced by ADR, ±1MiB)
//     or in .rodata (referenced by ADRP+ADD / ADRP+LDR-GOT)
//   • Version string "++Fortnite+Release-…-CL-…" in .rodata
// ============================================================================
#pragma once

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#include <algorithm>

// Set by the Android entry point (base of libUE4.so / libUnreal.so).
extern uint64_t ImageBase;

namespace Memcury
{
    inline bool bUE51 = false;

    namespace Globals
    {
        constexpr const bool bLogging = true;
        static const char* moduleName = nullptr;
    }

    // ─── ELF64 module layout ────────────────────────────────────────────────
    namespace ELF
    {
        struct Segment
        {
            uint64_t start, end; // absolute addresses
            uint32_t flags;      // PF_X 1 / PF_W 2 / PF_R 4
        };

        inline std::vector<Segment> g_Loads;
        inline uint64_t g_ModBase = 0, g_ModEnd = 0;
        inline bool g_Initialized = false;

        inline void Init()
        {
            if (g_Initialized)
                return;
            g_Initialized = true;
            g_ModBase = ImageBase;
            if (!g_ModBase)
                return;

            auto* ehdr = (uint8_t*)g_ModBase;
            if (*(uint32_t*)ehdr != 0x464C457F) // \x7fELF
                return;
            uint16_t phentsize = *(uint16_t*)(ehdr + 54);
            uint16_t phnum = *(uint16_t*)(ehdr + 56);
            uint8_t* ph = ehdr + *(uint64_t*)(ehdr + 32);
            for (int i = 0; i < phnum; i++, ph += phentsize)
            {
                uint32_t type = *(uint32_t*)(ph + 0);
                uint32_t flags = *(uint32_t*)(ph + 4);
                uint64_t vaddr = *(uint64_t*)(ph + 16);
                uint64_t memsz = *(uint64_t*)(ph + 40);
                if (type != 1 /*PT_LOAD*/)
                    continue;
                uint64_t s = g_ModBase + vaddr, e = s + memsz;
                if (g_ModEnd < e)
                    g_ModEnd = e;
                g_Loads.push_back({s, e, flags});
            }
        }

        inline bool IsExecutable(uint64_t addr)
        {
            for (auto& s : g_Loads)
                if (addr >= s.start && addr < s.end)
                    return (s.flags & 1) != 0;
            return false;
        }

        // executable range covering addr (or the single exec range)
        inline std::pair<uint64_t, uint64_t> TextRange()
        {
            Init();
            uint64_t s = 0, e = 0;
            for (auto& seg : g_Loads)
                if (seg.flags & 1)
                {
                    if (!s || seg.start < s)
                        s = seg.start;
                    if (seg.end > e)
                        e = seg.end;
                }
            return {s, e};
        }

        // whole module range
        inline std::pair<uint64_t, uint64_t> ModuleRange()
        {
            Init();
            return {g_ModBase, g_ModEnd};
        }

        inline bool IsInRange(uint64_t addr)
        {
            Init();
            return addr >= g_ModBase && addr < g_ModEnd;
        }
    } // namespace ELF

    namespace Util
    {
        template <typename T>
        constexpr static auto IsInRange(T value, T min, T max) -> bool
        {
            return value >= min && value < max;
        }

        inline auto GetModuleStartAndEnd() -> std::pair<uintptr_t, uintptr_t>
        {
            auto r = ELF::ModuleRange();
            return {r.first, r.second};
        }
    } // namespace Util

    namespace Safety
    {
        static auto FreezeCurrentThread() -> void
        {
            // best-effort: sleep forever
            for (;;)
                usleep(1000000);
        }
    } // namespace Safety

    namespace ASM
    {
        inline auto pattern2bytes(const char* pattern) -> std::vector<int>
        {
            auto bytes = std::vector<int>{};
            const auto start = const_cast<char*>(pattern);
            const auto end = const_cast<char*>(pattern) + strlen(pattern);

            for (auto current = start; current < end; ++current)
            {
                if (*current == '?')
                {
                    ++current;
                    if (*current == '?')
                        ++current;
                    bytes.push_back(-1);
                }
                else
                {
                    bytes.push_back(strtoul(current, &current, 16));
                }
            }
            return bytes;
        }
    } // namespace ASM

    namespace PE
    {
        inline auto SetCurrentModule(const char* moduleName) -> void
        {
            Globals::moduleName = moduleName;
        }

        inline auto GetModuleBase() -> uintptr_t
        {
            return ImageBase;
        }

        // ─── Windows-PE API compatibility shims (ELF-backed) ────────────────
        // Misc.cpp uses `PE::GetNTHeaders()->OptionalHeader.SizeOfImage`.
        struct PECompatOptionalHeader { uint32_t SizeOfImage; };
        struct PECompatNTHeaders { PECompatOptionalHeader OptionalHeader; };
        inline PECompatNTHeaders* GetNTHeaders()
        {
            ELF::Init();
            static PECompatNTHeaders hdr = []() -> PECompatNTHeaders
            {
                uint32_t size = 0;
                for (auto& s : ELF::g_Loads)
                    size = std::max(size, uint32_t(s.end - ELF::g_ModBase));
                return PECompatNTHeaders{size};
            }();
            return &hdr;
        }

        // Address kept under the PE:: namespace for source compat.
        class Address
        {
            uintptr_t _address;

        public:
            Address() { _address = 0; }
            Address(uintptr_t address) : _address(address) {}
            Address(void* address) : _address(reinterpret_cast<uintptr_t>(address)) {}

            auto operator=(uintptr_t address) -> Address
            {
                _address = address;
                return *this;
            }
            auto operator=(void* address) -> Address
            {
                _address = reinterpret_cast<uintptr_t>(address);
                return *this;
            }
            auto operator+(uintptr_t offset) -> Address { return Address(_address + offset); }

            bool operator>(uintptr_t offset) { return _address > offset; }
            bool operator>(Address address) { return _address > address._address; }
            bool operator<(uintptr_t offset) { return _address < offset; }
            bool operator<(Address address) { return _address < address._address; }
            bool operator>=(uintptr_t offset) { return _address >= offset; }
            bool operator>=(Address address) { return _address >= address._address; }
            bool operator<=(uintptr_t offset) { return _address <= offset; }
            bool operator<=(Address address) { return _address <= address._address; }
            bool operator==(uintptr_t offset) { return _address == offset; }
            bool operator==(Address address) { return _address == address._address; }
            bool operator!=(uintptr_t offset) { return _address != offset; }
            bool operator!=(Address address) { return _address != address._address; }

            // x64 semantics: read int32 at +offset, target = (addr+offset+4+off2)+imm
            auto RelativeOffset(uint32_t offset, uint32_t off2 = 0) -> Address
            {
                if (_address)
                    _address = ((_address + offset + 4 + off2) + *(int32_t*)(_address + offset));
                return *this;
            }

            auto AbsoluteOffset(uint32_t offset) -> Address
            {
                _address = _address + offset;
                return *this;
            }

            auto Get() -> uintptr_t { return _address; }

            template <typename T>
            auto GetAs() -> T
            {
                return reinterpret_cast<T>(_address);
            }

            auto IsValid() -> bool { return _address != 0; }
        };
    } // namespace PE

    // ─── ARM64 instruction decoding helpers ─────────────────────────────────
    namespace ARM64
    {
        inline bool IsAdrp(uint32_t insn) { return (insn & 0x9F000000) == 0x90000000; }
        inline bool IsAdr(uint32_t insn) { return (insn & 0x9F000000) == 0x10000000; }
        inline bool IsB(uint32_t insn) { return (insn & 0x7C000000) == 0x14000000; }      // B / BL
        inline bool IsBCond(uint32_t insn) { return (insn & 0xFF000010) == 0x54000000; }  // B.cond
        inline bool IsCbz(uint32_t insn) { return (insn & 0x7E000000) == 0x34000000; }    // CBZ/CBNZ
        inline bool IsTbz(uint32_t insn) { return (insn & 0x7E000000) == 0x36000000; }    // TBZ/TBNZ

        inline uint64_t AdrpTarget(uint64_t pc, uint32_t insn)
        {
            uint64_t imm = (uint64_t)((insn >> 5) & 0x7FFFF) << 2;
            uint64_t immhi_sign = (uint64_t)((int64_t)((int32_t)(insn & 0x00E00000) << 10) >> 11);
            imm |= immhi_sign;
            return (pc & ~0xFFFULL) + (imm << 12);
        }

        inline uint64_t AdrTarget(uint64_t pc, uint32_t insn)
        {
            int64_t imm = ((int64_t)(insn >> 29) & 3) | ((int64_t)((insn >> 5) & 0x7FFFF) << 2);
            // sign-extend 21 bits
            if (imm & (1LL << 20))
                imm -= (1LL << 21);
            return pc + imm;
        }

        inline uint64_t BranchTarget(uint64_t pc, uint32_t insn)
        {
            int64_t imm = (int64_t)(insn & 0x03FFFFFF);
            if (imm & (1LL << 25))
                imm -= (1LL << 26);
            return pc + (imm << 2);
        }

        inline uint64_t BCondTarget(uint64_t pc, uint32_t insn)
        {
            int64_t imm = (int64_t)((insn >> 5) & 0x7FFFF);
            if (imm & (1LL << 18))
                imm -= (1LL << 19);
            return pc + (imm << 2);
        }

        inline uint64_t CbzTarget(uint64_t pc, uint32_t insn)
        {
            int64_t imm = (int64_t)((insn >> 5) & 0x7FFFF);
            if (imm & (1LL << 18))
                imm -= (1LL << 19);
            return pc + (imm << 2);
        }

        // decode ADRP+ADD (page + offset) — returns target or 0
        inline uint64_t AdrpAddTarget(uint64_t adrpAddr)
        {
            uint32_t adrp = *(uint32_t*)adrpAddr;
            if (!IsAdrp(adrp))
                return 0;
            uint64_t page = AdrpTarget(adrpAddr, adrp);
            // look for ADD Xd, Xn, #imm in the next few instructions with same Xn
            uint32_t xn = (adrp >> 0) & 31;
            for (int i = 1; i <= 4; i++)
            {
                uint32_t add = *(uint32_t*)(adrpAddr + i * 4);
                // ADD (immediate) 64-bit: 1001000100 imm12 Rn Rd  → 0x91000000 mask 0x7F800000... exact: (add & 0x7F000000) == 0x11000000 with sf=1 → (add & 0xFF800000) == 0x91000000
                if ((add & 0xFF800000) == 0x91000000)
                {
                    uint32_t rn = (add >> 5) & 31, rd = add & 31;
                    if (rn == xn && rd == xn)
                    {
                        uint64_t imm = (add >> 10) & 0xFFF;
                        uint32_t shift = (add >> 22) & 3;
                        imm <<= (12 * shift);
                        return page + imm;
                    }
                }
            }
            return 0;
        }

        // decode ADRP+LDR (GOT-indirect) — returns the VALUE loaded (pointer at the GOT slot)
        inline uint64_t AdrpLdrGotValue(uint64_t adrpAddr)
        {
            uint32_t adrp = *(uint32_t*)adrpAddr;
            if (!IsAdrp(adrp))
                return 0;
            uint64_t page = AdrpTarget(adrpAddr, adrp);
            uint32_t xn = adrp & 31;
            for (int i = 1; i <= 3; i++)
            {
                uint32_t ldr = *(uint32_t*)(adrpAddr + i * 4);
                // LDR (immediate, unsigned offset) 64-bit: 1111100101 imm12 Rn Rt → (ldr & 0xFFC00000) == 0xF9400000
                if ((ldr & 0xFFC00000) == 0xF9400000)
                {
                    uint32_t rn = (ldr >> 5) & 31;
                    if (rn == xn)
                    {
                        uint64_t off = ((ldr >> 10) & 0xFFF) << 3; // 64-bit scale
                        uint64_t slot = page + off;
                        if (ELF::IsInRange(slot))
                            return *(uint64_t*)slot;
                    }
                }
            }
            return 0;
        }
    } // namespace ARM64

    // ─── string location on Android ─────────────────────────────────────────
    namespace Strings
    {
        // Find the string bytes; returns list of absolute addresses where the
        // string data lives (literal pools inside .text OR .rodata).
        inline std::vector<uint64_t> FindStringLocations(const void* str, bool wide)
        {
            std::vector<uint64_t> out;
            size_t len = wide ? (wcslen((const wchar_t*)str) + 1) * 2 : strlen((const char*)str) + 1;
            const uint8_t* needle = (const uint8_t*)str;

            auto [modStart, modEnd] = ELF::ModuleRange();
            if (!modStart || len < 2)
                return out;

            uint8_t first = needle[0];
            for (uint64_t p = modStart; p + len < modEnd; p++)
            {
                if (*(uint8_t*)p != first)
                    continue;
                if (memcmp((void*)p, needle, len) == 0)
                    out.push_back(p);
            }
            return out;
        }

        // Find a code instruction referencing any copy of the string.
        // Handles: ADR (literal pools inside .text, ±1MiB), ADRP+ADD (.rodata),
        // ADRP+LDR via GOT slot pointing at the string.
        // Returns the address of the FIRST referencing instruction.
        inline uint64_t FindStringRefAddress(const void* str, bool wide, int useRefNum = 0)
        {
            auto locs = FindStringLocations(str, wide);
            if (locs.empty())
                return 0;

            auto [textStart, textEnd] = ELF::TextRange();
            if (!textStart)
                return 0;

            int found = 0;
            for (uint64_t pc = textStart; pc + 4 <= textEnd; pc += 4)
            {
                uint32_t insn = *(uint32_t*)pc;
                if (ARM64::IsAdr(insn))
                {
                    uint64_t t = ARM64::AdrTarget(pc, insn);
                    for (auto l : locs)
                        if (t == l)
                        {
                            if (found++ == useRefNum)
                                return pc;
                        }
                }
                else if (ARM64::IsAdrp(insn))
                {
                    uint64_t t = ARM64::AdrpAddTarget(pc);
                    for (auto l : locs)
                        if (t && t == l)
                        {
                            if (found++ == useRefNum)
                                return pc;
                        }
                    if (!t)
                    {
                        uint64_t v = ARM64::AdrpLdrGotValue(pc);
                        for (auto l : locs)
                            if (v && v == l)
                            {
                                if (found++ == useRefNum)
                                    return pc;
                            }
                    }
                }
            }
            return 0;
        }

        // UTF-16 ref finder from a wchar_t* needle (Android wchar = UTF-32 →
        // converted); returns the referencing instruction address (for
        // FindNameRef / FindFunctionCall).
        inline uint64_t FindStringRefAddress16(const wchar_t* w, int useRefNum = 0)
        {
            uint16_t u16[512];
            size_t n16 = 0;
            for (size_t i = 0; w[i] && n16 < 511; i++)
            {
                uint32_t cp = (uint32_t)w[i];
                if (cp >= 0x10000)
                {
                    cp -= 0x10000;
                    u16[n16++] = (uint16_t)(0xD800 | (cp >> 10));
                    u16[n16++] = (uint16_t)(0xDC00 | (cp & 0x3FF));
                }
                else
                    u16[n16++] = (uint16_t)cp;
            }
            u16[n16] = 0;
            return FindStringRefAddress(u16, true, useRefNum);
        }

    } // namespace Strings

    class Scanner
    {
        PE::Address _address;

    public:
        Scanner(PE::Address address) : _address(address) {}
        Scanner(uintptr_t address = 0) : _address(address) {}

        static auto SetTargetModule(const char* moduleName) -> void
        {
            PE::SetCurrentModule(moduleName);
        }

        // Scan the executable segment(s) for a byte pattern ("AA BB ?? CC").
        static auto FindPattern(const char* signature, bool bWarnIfNotFound = true, bool /*bInRData*/ = false) -> Scanner
        {
            PE::Address add{(uintptr_t)0};

            ELF::Init();
            auto [start, end] = ELF::TextRange();
            if (!start || !signature)
                return Scanner(add);

            auto patternBytes = ASM::pattern2bytes(signature);
            const size_t s = patternBytes.size();
            if (!s)
                return Scanner(add);
            const int* d = patternBytes.data();

            // pick first concrete byte for a fast pre-filter
            int startingByte = d[0];
            uint32_t h = 1;
            while (startingByte == -1 && h < s)
                startingByte = d[h++];

            if (startingByte == -1) // all-wildcard: refuse
                return Scanner(add);

            const uint8_t* base = (const uint8_t*)start;
            const uint64_t size = end - start;

            for (uint64_t i = 0; i + s <= size; i++)
            {
                if (base[i] != (uint8_t)startingByte)
                    continue;

                bool found = true;
                for (size_t j = h; j < s; ++j)
                {
                    if (d[j] != -1 && base[i + j] != (uint8_t)d[j])
                    {
                        found = false;
                        break;
                    }
                }
                if (found)
                {
                    add = (uintptr_t)(base + i);
                    break;
                }
            }

            return Scanner(add);
        }

        // Supports wide and normal strings (pointers). On Android the string may
        // be referenced by ADR (literal pool in .text) or ADRP+ADD (.rodata) or
        // through a GOT slot — all handled in Strings::FindStringRefAddress.
        template <typename T = const wchar_t*>
        static auto FindStringRef(T string, bool bWarnIfNotFound = true, int useRefNum = 0, bool bIsInFunc = false, bool bSkunky = false) -> Scanner
        {
            constexpr auto bIsWide = std::is_same<T, const wchar_t*>::value;
            constexpr auto bIsChar = std::is_same<T, const char*>::value;

            uint64_t addr = 0;
            if constexpr (bIsWide || bIsChar)
            {
                // Android UE strings: wide literals are UTF-16LE in the binary,
                // narrow literals are plain ASCII/UTF-8.
                if constexpr (bIsWide)
                {
                    // convert wchar_t (UTF-32 on Android) → UTF-16LE bytes
                    const wchar_t* w = (const wchar_t*)string;
                    std::vector<uint16_t> u16;
                    for (; *w; ++w)
                        u16.push_back((uint16_t)*w);
                    u16.push_back(0);
                    addr = Strings::FindStringRefAddress(u16.data(), true, useRefNum);
                }
                else
                {
                    addr = Strings::FindStringRefAddress(string, false, useRefNum);
                }
            }
            else
            {
                // std::string / std::wstring overloads
                using CharT = typename T::value_type;
                if constexpr (std::is_same_v<CharT, wchar_t>)
                {
                    const std::wstring& w = string;
                    std::vector<uint16_t> u16(w.begin(), w.end());
                    u16.push_back(0);
                    addr = Strings::FindStringRefAddress(u16.data(), true, useRefNum);
                }
                else
                {
                    addr = Strings::FindStringRefAddress(string.c_str(), false, useRefNum);
                }
            }

            return Scanner(addr);
        }

        // Byte-window scan around the current address (same semantics as the
        // upstream ScanFor: forward/backward, skip N matches, Radius window).
        inline auto ScanFor(std::vector<uint8_t> opcodesToFind, bool forward = true, int toSkip = 0, int bytesToSkip = 1, int Radius = 2048, bool bIgnoreFF = false) -> Scanner
        {
            const auto scanBytes = _address.GetAs<std::uint8_t*>();
            if (!scanBytes)
                return *this;

            bool bFound = false;

            for (auto i = (forward ? bytesToSkip : -bytesToSkip); forward ? (i < Radius + bytesToSkip) : (i > -Radius - bytesToSkip); forward ? i++ : i--)
            {
                bool found = true;

                for (size_t k = 0; k < opcodesToFind.size() && found; k++)
                {
                    auto& currentOpcode = opcodesToFind[k];
                    if (bIgnoreFF && currentOpcode == 0xFF)
                        continue;
                    found = currentOpcode == scanBytes[i + k];
                }

                if (found)
                {
                    _address = (uintptr_t)&scanBytes[i];
                    if (toSkip != 0)
                        return ScanFor(opcodesToFind, forward, toSkip - 1);
                    bFound = true;
                    break;
                }
            }
            (void)bFound;
            return *this;
        }

        inline auto ScanFor(const char* pattern, bool forward = true, int toSkip = 0, int bytesToSkip = 1, int Radius = 2048) -> Scanner
        {
            auto scanBytes = _address.GetAs<std::uint8_t*>();
            if (!scanBytes)
                return *this;

            auto opcodesToFind = ASM::pattern2bytes(pattern);
            for (auto i = (forward ? bytesToSkip : -bytesToSkip); forward ? (i < Radius + bytesToSkip) : (i > -Radius - bytesToSkip); forward ? i++ : i--)
            {
                bool found = true;
                for (size_t k = 0; k < opcodesToFind.size() && found; k++)
                {
                    auto& currentOpcode = opcodesToFind[k];
                    if (currentOpcode == -1)
                        continue;
                    found = currentOpcode == scanBytes[i + k];
                }
                if (found)
                {
                    _address = (uintptr_t)&scanBytes[i];
                    if (toSkip != 0)
                        return ScanFor(pattern, forward, toSkip - 1);
                    break;
                }
            }
            return *this;
        }

        // ── ARM64-specific helpers (not in upstream API — used by the Android
        //    bootstrap and Finders) ──────────────────────────────────────────

        // Walk backwards from the current address to a function prologue:
        // STP X29,X30,[SP,#-x]! / SUB SP / PACIASP.
        static auto BackscanPrologue(uint64_t addr, int maxBytes = 0x800) -> uintptr_t
        {
            uint64_t start = addr & ~3ULL;
            for (int off = 0; off < maxBytes; off += 4)
            {
                uint64_t p = start - off;
                if (p < ImageBase)
                    break;
                uint32_t insn = *(uint32_t*)p;
                // STP X29, X30, [SP, #imm]! → mask FD 7B xx A9 with x29=29,x30=30
                if ((insn & 0xFFE07FFF) == 0xA9007BFD && ((insn >> 15) & 1))
                    return p;
                // PACIASP (D503233F)
                if (insn == 0xD503233F)
                    return p;
                // plain STP X29,X30,[SP,#imm] (no writeback)
                if ((insn & 0xFFC07FFF) == 0xA9007BFD)
                    return p;
            }
            return 0;
        }

        // If addr is an outlined stub (B to a thunk), follow it.
        static auto Deoutline(uint64_t addr) -> uint64_t
        {
            uint32_t insn = *(uint32_t*)addr;
            if (ARM64::IsB(insn) && !((insn >> 31) & 1)) // B (not BL)
                return ARM64::BranchTarget(addr, insn);
            return addr;
        }

        auto RelativeOffset(uint32_t offset, uint32_t off2 = 0) -> Scanner
        {
            _address.RelativeOffset(offset, off2);
            return *this;
        }

        auto AbsoluteOffset(uint32_t offset) -> Scanner
        {
            _address.AbsoluteOffset(offset);
            return *this;
        }

        auto Get() -> uintptr_t { return _address.Get(); }

        template <typename T>
        auto GetAs() -> T
        {
            return _address.GetAs<T>();
        }

        auto IsValid() -> bool { return _address.IsValid(); }

        auto Address() -> PE::Address { return _address; }
    };

} // namespace Memcury

// ─── Windows-Memcury global-scope APIs (game sources call these unqualified) ──
// ─── Windows-Memcury extra APIs used by the game sources ──────────────

// Find the code reference to a wide (or narrow) object/function NAME.
// On Android the name may be UTF-16 in .text literal pools / .rodata, or
// ASCII in the natives name table.
inline uintptr_t FindNameRef(const wchar_t* Name, int skip = 0, bool bWarn = true)
{
    (void)bWarn;
    uint64_t strAddr = Memcury::Strings::FindStringRefAddress16(Name, skip);
    return strAddr;
}

// Windows: NameRef → backward scan for x64 prologue. Android: NameRef →
// backscan to the ARM64 function start (the caller that references the name).
inline uintptr_t FindFunctionCall(const wchar_t* Name, const std::vector<uint8_t>& Bytes = {}, int skip = 0, bool bWarn = true)
{
    (void)Bytes; // x64 prologue bytes — N/A on ARM64 (backscan instead)
    uintptr_t nameRef = FindNameRef(Name, skip, bWarn);
    if (!nameRef)
        return 0;
    // walk back to the enclosing function prologue
    uint64_t best = 0;
    uint64_t lo = nameRef > 0x900 ? nameRef - 0x900 : Memcury::ELF::TextRange().first;
    for (uint64_t p = nameRef & ~3ULL; p >= lo; p -= 4)
    {
        uint32_t i = *(uint32_t*)p;
        bool stpx29 = (i & 0xFFE07FFF) == 0xA9007BFD || (i & 0xFFC07FFF) == 0xA9007BFD;
        bool paciasp = i == 0xD503233F;
        bool subsp = (i & 0xFF8003FF) == 0xD10003FF;
        if (stpx29 || paciasp)
            return p;
        if ((i & 0xFFFFFC1F) == 0xD65F0000) // RET — previous function ended
            return p + 4;
        if (subsp && !best)
            best = p;
    }
    return best;
}

inline bool IsNullSub(uint64_t Addr)
{
    if (!Addr)
        return true;
    uint32_t w = *(uint32_t*)Addr;
    return w == 0xD65F03C0 /* RET */ || (w & 0xFFFFFC1F) == 0xD65F0000;
}

