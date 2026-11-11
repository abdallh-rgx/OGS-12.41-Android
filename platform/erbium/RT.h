// ============================================================================
// RT.h — Erbium-Android v6 runtime brute-force engine (extracted verbatim from
// Finders-Android-FULL-v6.cpp PART 0). Works on any libUE4.so/libUnreal.so
// 5.x→30.00 after load: ELF phdr parsing, UTF-16/ASCII string search (literal
// pools + .rodata), ADR/ADRP refs, de-outline, .eh_frame next-function,
// vtable scans, natives pairs, GIs discovery.
// Requires: pch-android.h included first (ImageBase, alog, SDK types).
// ============================================================================
#pragma once
namespace RT
{
    static uint64_t g_ModBase = 0, g_ModEnd = 0;
    static uint64_t g_TextStart = 0, g_TextEnd = 0;      // executable range
    static uint64_t g_DataStart = 0, g_DataEnd = 0;      // writable range (vtables/GOT/natives)

    inline void Init()
    {
        if (g_ModBase)
            return;
        g_ModBase = ModuleBase;
        // parse ELF64 program headers to find exec + writable segments
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
            if (flags & 1) // X
            {
                if (!g_TextStart || s < g_TextStart) g_TextStart = s;
                if (e > g_TextEnd) g_TextEnd = e;
            }
            if (flags & 2) // W
            {
                if (!g_DataStart || s < g_DataStart) g_DataStart = s;
                if (e > g_DataEnd) g_DataEnd = e;
            }
        }
        if (!g_TextStart) { g_TextStart = g_ModBase; g_TextEnd = g_ModEnd; }
    }

    inline bool InText(uint64_t a) { return a >= g_TextStart && a < g_TextEnd; }
    inline bool InData(uint64_t a) { return a >= g_DataStart && a < g_DataEnd; }
    inline bool InMod(uint64_t a) { return a >= g_ModBase && a < g_ModEnd; }


    // ---- string scan (UTF-16LE across whole module: .text literal pools + .rodata)
    inline uint64_t FindWStr(const wchar_t* needle)
    {
        RT::Init();
        size_t len = wcslen(needle);
        if (!len)
            return 0;
        // Android wchar_t is UTF-32 (4 bytes); the binary stores UTF-16LE.
        // Convert the needle to UTF-16LE first (BMP-safe; surrogates folded).
        uint16_t u16[512];
        size_t n16 = 0;
        for (size_t i = 0; i < len && n16 < 511; i++)
        {
            uint32_t cp = (uint32_t)needle[i];
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
        size_t nbytes = n16 * 2 + 2; // include the LNULL terminator
        auto* p = (const uint8_t*)u16;
        uint8_t first = p[0], second = p[1];
        for (uint64_t a = g_ModBase + 0x1000; a + nbytes < g_ModEnd; a += 2)
        {
            auto* m = (const uint8_t*)a;
            if (m[0] != first || m[1] != second)
                continue;
            bool ok = true;
            for (size_t i = 2; i < nbytes; i++)
                if (m[i] != p[i]) { ok = false; break; }
            if (ok)
                return a;
        }
        return 0;
    }

    inline uint64_t FindAStr(const char* needle)
    {
        RT::Init();
        size_t len = strlen(needle);
        if (!len)
            return 0;
        for (uint64_t a = g_ModBase + 0x1000; a + len < g_ModEnd; a += 1)
        {
            auto* m = (const uint8_t*)a;
            if (m[0] != (uint8_t)needle[0])
                continue;
            bool ok = true;
            for (size_t i = 1; i < len; i++)
                if (m[i] != (uint8_t)needle[i]) { ok = false; break; }
            if (ok && m[len] == 0)
                return a;
        }
        return 0;
    }

    // ---- ARM64 decoders ----
    // ADRP Xd, imm : (instr & 0x9F000000) == 0x90000000
    inline bool IsADRP(uint32_t i) { return (i & 0x9F000000) == 0x90000000; }
    inline uint64_t ADRPPage(uint64_t pc, uint32_t i)
    {
        uint64_t imm = ((uint64_t)(i & 0x00FFFFE0) >> 3) | ((i >> 29) & 3);
        if (imm & 0x100000) imm |= 0xFFFFFFFFFFF00000ULL; // sign-extend 21 bits
        return (pc & ~0xFFFULL) + (imm << 12);
    }
    inline uint8_t ADRPRd(uint32_t i) { return i & 0x1F; }
    // ADR Xd, imm : (instr & 0x9F000000) == 0x10000000
    inline bool IsADR(uint32_t i) { return (i & 0x9F000000) == 0x10000000; }
    inline uint64_t ADRImm(uint64_t pc, uint32_t i)
    {
        uint64_t imm = ((uint64_t)(i & 0x00FFFFE0) >> 3) | ((i >> 29) & 3);
        if (imm & 0x100000) imm |= 0xFFFFFFFFFFF00000ULL;
        return pc + imm;
    }
    // ADD Xd, Xn, #imm12<<0 : (i & 0xFF800000) == 0x91000000
    inline bool IsADDImm(uint32_t i) { return (i & 0xFF800000) == 0x91000000; }
    inline uint32_t ADDImm12(uint32_t i) { return (i >> 10) & 0xFFF; }
    // LDR Xt, [Xn, #imm12*8] : (i & 0xFFC00000) == 0xF9400000
    inline bool IsLDR64(uint32_t i) { return (i & 0xFFC00000) == 0xF9400000; }
    inline uint32_t LDR64Off(uint32_t i) { return ((i >> 10) & 0xFFF) * 8; }
    // LDRB Wt, [Xn, #imm12] : (i & 0xFFC00000) == 0x39400000
    inline bool IsLDRB(uint32_t i) { return (i & 0xFFC00000) == 0x39400000; }
    inline uint32_t LDRBOff(uint32_t i) { return (i >> 10) & 0xFFF; }
    // STRB Wt, [Xn, #imm12] : (i & 0xFFC00000) == 0x39000000
    inline bool IsSTRB(uint32_t i) { return (i & 0xFFC00000) == 0x39000000; }
    // B imm26 : (i & 0xFC000000) == 0x14000000 ; BL : 0x94000000
    inline bool IsB(uint32_t i) { return (i & 0xFC000000) == 0x14000000; }
    inline bool IsBL(uint32_t i) { return (i & 0xFC000000) == 0x94000000; }
    inline uint64_t BranchTarget(uint64_t pc, uint32_t i)
    {
        int64_t imm = (int32_t)((i & 0x03FFFFFF) << 6) >> 6; // sign-extend 26
        return pc + (imm << 2);
    }
    // STP X29, X30, [SP, #-imm]! : (i & 0xFFC07FFF) == 0xA9807BFD
    // STP X29, X30, [SP, #imm]  : (i & 0xFFC07FFF) == 0xA9007BFD
    inline bool IsSTPX29X30(uint32_t i) { return (i & 0xFFC07FFF) == 0xA9807BFD || (i & 0xFFC07FFF) == 0xA9007BFD; }
    // SUB SP, SP, #imm : (i & 0xFF8003FF) == 0xD10003FF
    inline bool IsSUBSP(uint32_t i) { return (i & 0xFF8003FF) == 0xD10003FF; }
    // PACIASP: 0xD503233F
    inline bool IsPACIASP(uint32_t i) { return i == 0xD503233F; }

    // ---- refs to an address inside .text (ADRP+ADD / ADR / ADRP+LDR-GOT) ----
    inline void RefsTo(uint64_t target, uint64_t* out, uint32_t* outN, uint32_t maxOut)
    {
        RT::Init();
        *outN = 0;
        if (!target || !g_TextStart)
            return;
        uint64_t start = g_TextStart;
        uint64_t end = g_TextEnd;
        // fast prefilter: ADRP whose page == target page (±1 page for ADR)
        uint64_t tpage = target & ~0xFFFULL;
        for (uint64_t pc = start; pc + 8 < end && *outN < maxOut; pc += 4)
        {
            uint32_t i = *(uint32_t*)pc;
            if (IsADRP(i))
            {
                uint64_t page = ADRPPage(pc, i);
                uint8_t rd = ADRPRd(i);
                if (page != tpage && page != tpage + 0x1000 && page + 0x1000 != tpage)
                    continue;
                // scan next 8 instructions for ADD/LDR with same rd
                for (uint64_t q = pc + 4; q < pc + 40 && q + 4 < end; q += 4)
                {
                    uint32_t j = *(uint32_t*)q;
                    if (IsADRP(j) && ADRPRd(j) == rd)
                        break; // register redefined
                    if (IsADDImm(j) && ((j >> 5) & 0x1F) == rd)
                    {
                        uint64_t addr = page + ADDImm12(j);
                        if (addr == target)
                        {
                            out[(*outN)++] = pc;
                            break;
                        }
                        break; // ADD consumed the ADRP
                    }
                    if (IsLDR64(j) && ((j >> 5) & 0x1F) == rd)
                    {
                        // GOT-indirect: *(page + off) == target ?
                        uint64_t slot = page + LDR64Off(j);
                        if (RT::InMod(slot) && *(uint64_t*)slot == target)
                        {
                            out[(*outN)++] = pc;
                            break;
                        }
                        break;
                    }
                }
            }
            else if (IsADR(i) && ADRImm(pc, i) == target)
            {
                out[(*outN)++] = pc;
            }
        }
    }

    // ---- de-outline: a stub is a tiny function that only B's to the real one ----

    // bounds helpers (for the SDK bootstrap)
    inline void TextBounds(uint64_t* s, uint64_t* e) { RT::Init(); *s = g_TextStart; *e = g_TextEnd; }
    inline void ModuleBounds(uint64_t* s, uint64_t* e) { RT::Init(); *s = g_ModBase; *e = g_ModEnd; }

    // first code reference to a wide string — requires FindWStr/RefsTo above
    inline uint64_t FirstStringRef(const wchar_t* needle)
    {
        uint64_t strAddr = RT::FindWStr(needle);
        if (!strAddr)
            return 0;
        uint64_t refs[64];
        uint32_t nRefs = 0;
        RT::RefsTo(strAddr, refs, &nRefs, 64);
        return nRefs ? refs[0] : 0;
    }

    inline uint64_t Deoutline(uint64_t addr)
    {
        RT::Init();
        if (!RT::InText(addr))
            return 0;
        for (int hops = 0; hops < 4; hops++)
        {
            uint32_t i = *(uint32_t*)addr;
            if (IsB(i))
            {
                uint64_t t = BranchTarget(addr, i);
                if (RT::InText(t) && t != addr)
                {
                    addr = t;
                    continue;
                }
            }
            break;
        }
        return addr;
    }

    // ---- prologue backscan: from an address inside a function, find its start ----
    inline uint64_t BackscanPrologue(uint64_t addr, uint64_t maxDist = 0x900)
    {
        RT::Init();
        if (!RT::InText(addr))
            return 0;
        uint64_t best = 0;
        uint64_t lo = addr > maxDist ? addr - maxDist : g_TextStart;
        for (uint64_t p = addr & ~3ULL; p >= lo; p -= 4)
        {
            uint32_t i = *(uint32_t*)p;
            if (IsSTPX29X30(i) || IsPACIASP(i))
            {
                best = p;
                // keep scanning: the LOWEST prologue marker wins (function start)
                if (IsSTPX29X30(i))
                {
                    // verify: previous instruction (if any) is also prologue-ish or it IS the start
                    if (p < addr && p - 4 >= lo)
                    {
                        uint32_t prev = *(uint32_t*)(p - 4);
                        if (IsSTPX29X30(prev) || IsPACIASP(prev) || IsSUBSP(prev))
                            continue; // part of a bigger prologue — keep going down
                    }
                    // STP X29,X30 followed by typical prologue body → accept lowest chain
                    break;
                }
            }
            else if (IsSUBSP(i) && !best)
            {
                best = p; // fallback: SUB SP alone (leaf)
            }
            // stop if we hit an obvious RET (end of previous function)
            if ((i & 0xFFFFFC1F) == 0xD65F0000 || (i & 0xFFFFFFE0) == 0xD65F0000) // RET/RETAA
            {
                if (!best)
                    best = p + 4;
                break;
            }
        }
        return best;
    }

    // ---- full string-anchor chain: str → refs → deoutline → prologue ----
    // Tries each string; returns the first function whose body references it.
    inline uint64_t StringAnchorFunc(const wchar_t* const* strs, int nStrs, int refIndex = 0)
    {
        RT::Init();
        for (int s = 0; s < nStrs; s++)
        {
            uint64_t strAddr = RT::FindWStr(strs[s]);
            if (!strAddr)
                continue;
            uint64_t refs[64];
            uint32_t nRefs = 0;
            RT::RefsTo(strAddr, refs, &nRefs, 64);
            for (uint32_t r = 0; r < nRefs; r++)
            {
                uint64_t a = RT::Deoutline(refs[r]);
                uint64_t f = RT::BackscanPrologue(a);
                if (f)
                {
                    if (refIndex == 0)
                        return f;
                    refIndex--;
                }
            }
        }
        return 0;
    }

    inline uint64_t StringAnchorFunc(const wchar_t* s0)
    {
        const wchar_t* arr[1] = { s0 };
        return RT::StringAnchorFunc(arr, 1);
    }

    // ---- runtime vtable primitives ----
    // find every 8-aligned slot in writable data equal to func
    inline uint32_t VtSlotsWith(uint64_t func, uint64_t* slots, uint32_t maxSlots)
    {
        RT::Init();
        uint32_t n = 0;
        if (!g_DataStart || !func)
            return 0;
        for (uint64_t a = g_DataStart; a + 8 <= g_DataEnd && n < maxSlots; a += 8)
            if (*(uint64_t*)a == func)
                slots[n++] = a;
        return n;
    }

    // walk a vtable slot array backwards to its start (stop at non-text ptr / big gap)
    inline uint64_t VtStartOf(uint64_t slot)
    {
        RT::Init();
        uint64_t s = slot;
        int guard = 0;
        while (guard++ < 8192)
        {
            if (s < g_DataStart + 8)
                break;
            uint64_t v = *(uint64_t*)(s - 8);
            if (!RT::InText(v))
                break;
            // vtable entries are dense: previous slot usually within ±32MB of func
            s -= 8;
        }
        return s;
    }

    // neighbor of anchor's vtable slot by delta (majority across slots)
    inline uint64_t VtNeighbor(uint64_t anchorFunc, int delta)
    {
        RT::Init();
        if (!anchorFunc)
            return 0;
        uint64_t slots[96];
        uint32_t n = RT::VtSlotsWith(anchorFunc, slots, 96);
        uint64_t votes[96];
        uint32_t nv = 0;
        for (uint32_t i = 0; i < n; i++)
        {
            uint64_t t = (delta >= 0) ? slots[i] + 8 * (uint64_t)delta
                                      : slots[i] - 8 * (uint64_t)(-(int64_t)delta);
            if (!RT::InData(t))
                continue;
            uint64_t v = *(uint64_t*)t;
            if (!RT::InText(v))
                continue;
            bool seen = false;
            for (uint32_t j = 0; j < nv; j++)
                if (votes[j] == v) { seen = true; break; }
            if (!seen && nv < 96)
                votes[nv++] = v;
        }
        if (!nv)
            return 0;
        return votes[0]; // all valid votes agree in practice (same class hierarchy)
    }

    // index of func in its (majority) vtable — for *Vft() finders
    inline uint32_t VtIndexOf(uint64_t func, uint64_t* outVt = nullptr)
    {
        RT::Init();
        if (!func)
            return 0;
        uint64_t slots[96];
        uint32_t n = RT::VtSlotsWith(func, slots, 96);
        uint32_t bestIdx = 0, bestCnt = 0;
        uint64_t bestVt = 0;
        for (uint32_t i = 0; i < n; i++)
        {
            uint64_t vs = RT::VtStartOf(slots[i]);
            uint32_t idx = (uint32_t)((slots[i] - vs) / 8);
            uint32_t cnt = 0;
            for (uint32_t j = 0; j < n; j++)
            {
                uint64_t vs2 = RT::VtStartOf(slots[j]);
                uint32_t idx2 = (uint32_t)((slots[j] - vs2) / 8);
                if (idx2 == idx)
                    cnt++;
            }
            if (cnt > bestCnt || (cnt == bestCnt && idx < bestIdx))
            {
                bestIdx = idx;
                bestCnt = cnt;
                bestVt = vs;
            }
        }
        if (outVt)
            *outVt = bestVt;
        return bestIdx;
    }

    // read vtable method by index using anchor's vtable (majority)
    inline uint64_t VtMethodAt(uint64_t anchorFunc, uint32_t index)
    {
        RT::Init();
        if (!anchorFunc)
            return 0;
        uint64_t slots[96];
        uint32_t n = RT::VtSlotsWith(anchorFunc, slots, 96);
        for (uint32_t i = 0; i < n; i++)
        {
            uint64_t vs = RT::VtStartOf(slots[i]);
            uint64_t t = vs + 8ull * index;
            if (RT::InData(t))
            {
                uint64_t v = *(uint64_t*)t;
                if (RT::InText(v))
                    return v;
            }
        }
        return 0;
    }

    // ---- natives table: {char* Name, uintptr Thunk} pairs in resolved data ----
    inline uint64_t FindNativeThunk(const char* name)
    {
        RT::Init();
        if (!g_DataStart)
            return 0;
        size_t len = strlen(name);
        for (uint64_t a = g_DataStart; a + 16 <= g_DataEnd; a += 8)
        {
            uint64_t namePtr = *(uint64_t*)a;
            uint64_t fnPtr = *(uint64_t*)(a + 8);
            if (!RT::InText(fnPtr) || !RT::InMod(namePtr))
                continue;
            // name must be ASCII, null-terminated, exact match
            const char* m = (const char*)namePtr;
            bool ok = true;
            for (size_t i = 0; i < len; i++)
                if (m[i] != name[i]) { ok = false; break; }
            if (ok && m[len] == 0)
                return fnPtr;
        }
        return 0;
    }

    // ---- native orig-climb: exec thunk → BL → التنفيذ الأصلي ----
    // الـ exec thunk ينادي الـ impl مبكراً في جسمه (FFrame setup ثم BL impl).
    inline uint64_t NativeOrig(const char* name)
    {
        uint64_t thunk = RT::FindNativeThunk(name);
        if (!thunk)
            return 0;
        uint64_t t = RT::Deoutline(thunk);
        for (uint64_t pc = t; pc < t + 0x140; pc += 4)
        {
            uint32_t i = *(uint32_t*)pc;
            if (IsBL(i))
            {
                uint64_t callee = BranchTarget(pc, i);
                if (callee == t || !RT::InText(callee))
                    continue;
                // الـ impl: دالة أكبر من الـ stub، تبدأ بـ prologue صالح
                uint64_t cand = RT::Deoutline(callee);
                if (cand != t && RT::InText(cand))
                    return cand;
            }
        }
        return t; // fallback: الـ thunk نفسه (نقطة هوك ExecHook صالحة)
    }

    // ---- BL-crawl: callees of a function ----
    inline uint32_t BLCallees(uint64_t from, uint64_t* out, uint32_t maxOut, uint64_t window = 0x100)
    {
        RT::Init();
        uint32_t n = 0;
        if (!RT::InText(from))
            return 0;
        for (uint64_t pc = from; pc < from + window && n < maxOut; pc += 4)
        {
            uint32_t i = *(uint32_t*)pc;
            if (IsBL(i))
            {
                uint64_t t = BranchTarget(pc, i);
                if (RT::InText(t))
                {
                    bool seen = false;
                    for (uint32_t j = 0; j < n; j++)
                        if (out[j] == t) { seen = true; break; }
                    if (!seen)
                        out[n++] = RT::Deoutline(t);
                }
            }
            // RET في نهاية دالة صغيرة → توقف إذا تجاوزنا نافذة صغيرة (اختياري: لا نتوقف — outlined helpers)
        }
        return n;
    }

    // ---- caller-walk: كل من يستدعي target عبر BL ----
    inline uint32_t BLCallersTo(uint64_t target, uint64_t* out, uint32_t maxOut)
    {
        RT::Init();
        uint32_t n = 0;
        if (!target || !g_TextStart)
            return 0;
        for (uint64_t pc = g_TextStart; pc + 4 < g_TextEnd && n < maxOut; pc += 4)
        {
            uint32_t i = *(uint32_t*)pc;
            if (IsBL(i) && BranchTarget(pc, i) == target)
            {
                uint64_t f = RT::BackscanPrologue(pc);
                if (f)
                {
                    bool seen = false;
                    for (uint32_t j = 0; j < n; j++)
                        if (out[j] == f) { seen = true; break; }
                    if (!seen)
                        out[n++] = f;
                }
            }
        }
        return n;
    }

    // ---- عدد حفظات السجلات في المقدمة (مقياس "ضخامة" الدالة) ----
    inline uint32_t PrologueSaveCount(uint64_t fn)
    {
        RT::Init();
        if (!RT::InText(fn))
            return 0;
        uint32_t saves = 0;
        for (uint64_t pc = fn; pc < fn + 0x40; pc += 4)
        {
            uint32_t w = *(uint32_t*)pc;
            // STP Xt1,Xt2,[SP,#imm](|!) — أي زوج إلى SP
            if ((w & 0xFFC003E0) == 0xA90003E0 || (w & 0xFFC003E0) == 0xA98003E0)
                saves++;
        }
        return saves;
    }

    // ---- callee ضخم (أكثر حفظات) مثل AttemptDeriveFromURL من GetNetMode ----
    inline uint64_t BigCalleeFrom(uint64_t caller, uint32_t minSaves = 3)
    {
        uint64_t c[48];
        uint32_t n = RT::BLCallees(caller, c, 48, 0x80);
        uint64_t best = 0;
        uint32_t bestSaves = minSaves ? minSaves - 1 : 0;
        for (uint32_t i = 0; i < n; i++)
        {
            uint32_t s = RT::PrologueSaveCount(c[i]);
            if (s > bestSaves)
            {
                bestSaves = s;
                best = c[i];
            }
        }
        return best;
    }

    // ---- caller ضخم لدالة (ServerReplicateActors يُنادي ReplicateActor) ----
    inline uint64_t BigCallerOf(uint64_t target, uint32_t minSaves = 4)
    {
        uint64_t c[64];
        uint32_t n = RT::BLCallersTo(target, c, 64);
        uint64_t best = 0;
        uint32_t bestSaves = minSaves ? minSaves - 1 : 0;
        for (uint32_t i = 0; i < n; i++)
        {
            uint32_t s = RT::PrologueSaveCount(c[i]);
            if (s > bestSaves)
            {
                bestSaves = s;
                best = c[i];
            }
        }
        return best;
    }

    // ---- الشكل: LDRB Wt,[X0]; TBZ; LDR X0,[X0,#imm]; CBNZ; RET (بوصف GetSparseClassData) ----
    inline uint64_t TinySparseAccessorFrom(uint64_t caller)
    {
        uint64_t c[48];
        uint32_t n = RT::BLCallees(caller, c, 48, 0x200);
        for (uint32_t i = 0; i < n; i++)
        {
            uint64_t f = c[i];
            uint32_t ldrbX0 = 0, ldrX0X0 = 0, rets = 0, ins = 0;
            for (uint64_t pc = f; pc < f + 0x20; pc += 4)
            {
                uint32_t w = *(uint32_t*)pc;
                if (w == 0xD65F03C0u) { rets++; break; }
                if (IsLDRB(w) && ((w >> 5) & 0x1F) == 0)
                    ldrbX0++;
                if (IsLDR64(w) && ((w >> 5) & 0x1F) == 0 && (w & 0x1F) == 0)
                    ldrX0X0++;
                ins++;
            }
            if (rets == 1 && ldrX0X0 >= 1 && ldrbX0 >= 1 && ins <= 6)
                return f;
        }
        return 0;
    }

    // ---- الشكل: LDRSW + ADD ..,LSL#3 (حلقة WorldContexts) — GetWorldContext ----
    inline uint64_t WorldContextCalleeFrom(uint64_t caller)
    {
        uint64_t c[48];
        uint32_t n = RT::BLCallees(caller, c, 48, 0x100);
        for (uint32_t i = 0; i < n; i++)
        {
            uint64_t f = c[i];
            bool hasLdrsw = false, hasAddLsl3 = false;
            for (uint64_t pc = f; pc < f + 0x40; pc += 4)
            {
                uint32_t w = *(uint32_t*)pc;
                // LDRSW Xt,[Xn,#imm]: 0xB9800000|..
                if ((w & 0xFFC00000) == 0xB9800000)
                    hasLdrsw = true;
                // ADD Xd,Xn,Xm,LSL#3: 0x8B000C00 mask imm6=3<<10
                if ((w & 0xFF200C00) == 0x8B000C00)
                    hasAddLsl3 = true;
                if (w == 0xD65F03C0u)
                    break;
            }
            if (hasLdrsw && hasAddLsl3)
                return f;
        }
        return 0;
    }

    // ---- الشكل: نداء افتراضي واحد صغير (CallPreReplication من ReplicateActor) ----
    // LDR X8,[X0] (vtable) + LDR Xn,[X8,#off] + BLR Xn — دالة صغيرة بنداء افتراضي واحد
    inline uint64_t VirtCallCalleeFrom(uint64_t caller)
    {
        uint64_t c[48];
        uint32_t n = RT::BLCallees(caller, c, 48, 0x180);
        for (uint32_t i = 0; i < n; i++)
        {
            uint64_t f = c[i];
            uint32_t blrs = 0, ldrVft = 0, ins = 0;
            bool earlyRet = false;
            for (uint64_t pc = f; pc < f + 0x60; pc += 4)
            {
                uint32_t w = *(uint32_t*)pc;
                if (w == 0xD65F03C0u)
                    break;
                ins++;
                if ((w & 0xFFFFFC1F) == 0xD63F0000 && ((w >> 5) & 0x1F) != 31)
                    blrs++; // BLR Xn (ليس BLR LZCNT-free reserved)
                if (IsLDR64(w) && ((w >> 5) & 0x1F) == 0)
                    ldrVft++;
                if (ins > 24)
                    break;
            }
            if (blrs == 1 && ldrVft >= 1 && ins >= 4 && ins <= 24)
                return f;
        }
        return 0;
    }

    // ---- GIs global discovery (ARM64 port of Erbium's AllowCommandletRendering scan)
    // Mirrors Erbium Windows: scan BACKWARD from the string-ref instruction collecting
    // bool-global accesses (ADRP+LDRB/STRB, incl. GOT-indirect); skip=2 → GIsClient,
    // skip=1 → GIsServer (identical ordering to Finders.cpp).
    inline uint64_t GIsFromAnchor(int skip)
    {
        RT::Init();
        uint64_t sAddr = RT::FindWStr(L"AllowCommandletRendering");
        if (!sAddr)
            sAddr = RT::FindWStr(L"llowCommandletRendering");
        if (!sAddr)
            return 0;
        uint64_t refs[16];
        uint32_t nRefs = 0;
        RT::RefsTo(sAddr, refs, &nRefs, 16);
        for (uint32_t r = 0; r < nRefs; r++)
        {
            uint64_t refInstr = RT::Deoutline(refs[r]);
            // scan backward up to 0x100 bytes (like Erbium's i < 0x100 loop)
            uint64_t g[16];
            uint32_t ng = 0;
            for (uint64_t pc = refInstr; pc + 4 > refInstr - 0x100 && ng < 16; pc -= 4)
            {
                uint32_t i = *(uint32_t*)pc;
                if ((IsLDRB(i) || IsSTRB(i)))
                {
                    // find the ADRP defining the base register within the previous 12 instrs
                    uint8_t rn = (i >> 5) & 0x1F;
                    uint64_t base = 0;
                    for (uint64_t q = pc - 4; q + 4 > pc - 52; q -= 4)
                    {
                        uint32_t j = *(uint32_t*)q;
                        if (IsADRP(j) && ADRPRd(j) == rn)
                        {
                            base = ADRPPage(q, j);
                            break;
                        }
                        if (IsLDR64(j) && ADRPRd(j) != rn && ((j >> 5) & 0x1F) == rn)
                        {
                            // GOT-indirect: ADRP Xk,GOT; LDR Xk,[Xk,#off]; LDRB ..[Xk]
                            uint64_t k = q;
                            for (uint64_t w = k - 4; w + 4 > k - 52; w -= 4)
                            {
                                uint32_t u = *(uint32_t*)w;
                                if (IsADRP(u) && ADRPRd(u) == ((j >> 5) & 0x1F))
                                {
                                    uint64_t slot = ADRPPage(w, u) + LDR64Off(j);
                                    if (RT::InMod(slot))
                                        base = *(uint64_t*)slot;
                                    break;
                                }
                            }
                            break;
                        }
                    }
                    if (base)
                    {
                        uint64_t addr = base + LDRBOff(i);
                        if (RT::InData(addr))
                        {
                            bool seen = false;
                            for (uint32_t j = 0; j < ng; j++)
                                if (g[j] == addr) { seen = true; break; }
                            if (!seen)
                                g[ng++] = addr; // collected in backward order (like Erbium)
                        }
                    }
                }
            }
            if ((uint32_t)(skip + 1) <= ng)
                return g[skip];
        }
        return 0;
    }


    // ========================================================================
    // v6 additions — .eh_frame FDEs / GNames / LogV2 / native CHILF / chains
    // ========================================================================

    // ---- v6: exact function bounds via .eh_frame FDE table ----
    // Verified: next-FDE(IBA) == IBA_next GT on 13.40/17.50/18.40/21.30/22.40/23.50
    //           + 10.40 (Erbium reference build) via STAT-string chain.
#ifndef ERBIUM_V6_MAX_FDES
#define ERBIUM_V6_MAX_FDES 1200000
#endif
    static uint64_t v6_FdeStarts[ERBIUM_V6_MAX_FDES];
    static uint32_t v6_FdeCount = 0;
    static bool v6_FdesReady = false;

    inline bool EhFrameInit()
    {
        if (v6_FdesReady)
            return v6_FdeCount > 0;
        v6_FdesReady = true;
        RT::Init();
        if (!g_ModBase)
            return false;
        auto* ehdr = (uint8_t*)g_ModBase;
        if (*(uint32_t*)ehdr != 0x464C457F)
            return false;
        uint16_t phentsize = *(uint16_t*)(ehdr + 54);
        uint16_t phnum = *(uint16_t*)(ehdr + 56);
        uint8_t* ph = ehdr + *(uint64_t*)(ehdr + 32);
        uint64_t ehStart = 0, ehSize = 0;
        for (int i = 0; i < phnum; i++, ph += phentsize)
        {
            if (*(uint32_t*)ph == 0x6474E550) // PT_GNU_EH_FRAME
            {
                ehStart = g_ModBase + *(uint64_t*)(ph + 16);
                ehSize = *(uint64_t*)(ph + 40);
                break;
            }
        }
        if (!ehStart || !ehSize || ehStart < g_ModBase)
            return false;
        // walk CIE/FDE chain; FDE pc_begin assumed DW_EH_PE_pcrel|sdata4 (Android norm),
        // every entry validated to land inside .text — abort silently on mismatch.
        uint64_t p = ehStart, end = ehStart + ehSize;
        uint32_t bad = 0;
        while (p + 4 <= end && v6_FdeCount < ERBIUM_V6_MAX_FDES)
        {
            uint32_t len32 = *(uint32_t*)p;
            if (!len32)
                break;
            uint64_t q = p + 4, entryEnd;
            if (len32 == 0xFFFFFFFF)
            {
                uint64_t len64 = *(uint64_t*)q;
                entryEnd = q + 8 + len64;
                q += 8;
            }
            else
                entryEnd = q + len32;
            if (entryEnd > end || entryEnd <= q)
                break;
            uint32_t cieId = *(uint32_t*)q;
            q += 4;
            if (cieId != 0) // FDE (CIE ref back-offset)
            {
                int32_t rel = *(int32_t*)q;
                uint64_t start = (uint64_t)q + (int64_t)rel; // pcrel to this field
                uint32_t range = *(uint32_t*)(q + 4);
                if (RT::InText(start) && range && range < (uint32_t)(g_TextEnd - g_TextStart))
                    v6_FdeStarts[v6_FdeCount++] = start;
                else if (++bad > 64)
                    break; // wrong encoding — bail
            }
            p = entryEnd;
        }
        // sort (FDEs are normally ordered; safeguard for merged sections)
        for (uint32_t i = 1; i < v6_FdeCount; i++)
        {
            uint64_t k = v6_FdeStarts[i];
            uint32_t j = i;
            while (j > 0 && v6_FdeStarts[j - 1] > k)
            {
                v6_FdeStarts[j] = v6_FdeStarts[j - 1];
                j--;
            }
            v6_FdeStarts[j] = k;
        }
        return v6_FdeCount > 0;
    }

    // smallest function start strictly after `addr` (exact .eh_frame bounds)
    inline uint64_t NextFunction(uint64_t addr)
    {
        if (!RT::EhFrameInit() || !RT::InText(addr))
            return 0;
        uint32_t lo = 0, hi = v6_FdeCount;
        while (lo < hi)
        {
            uint32_t mid = lo + (hi - lo) / 2;
            if (v6_FdeStarts[mid] <= addr)
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo < v6_FdeCount ? v6_FdeStarts[lo] : 0;
    }

    // ---- v6: mapped-range validation (for heap pointers: GNames blocks) ----
    struct V6Range { uint64_t lo, hi; };
    static V6Range v6_Ranges[8192];
    static int v6_nRanges = -1;

    inline bool V6LoadMaps()
    {
        if (v6_nRanges >= 0)
            return v6_nRanges > 0;
        v6_nRanges = 0;
        FILE* f = fopen("/proc/self/maps", "r");
        if (!f)
            return false;
        char line[512];
        while (fgets(line, sizeof(line), f) && v6_nRanges < 8192)
        {
            uint64_t lo, hi;
            char perms[8];
            if (sscanf(line, "%lx-%lx %7s", &lo, &hi, perms) == 3)
                if (perms[0] == 'r')
                    v6_Ranges[v6_nRanges++] = { lo, hi };
        }
        fclose(f);
        return v6_nRanges > 0;
    }
    inline bool V6Readable(uint64_t p)
    {
        if (!RT::V6LoadMaps())
            return RT::InMod(p); // fallback: module only
        for (int i = 0; i < v6_nRanges; i++)
            if (p >= v6_Ranges[i].lo && p < v6_Ranges[i].hi)
                return true;
        return false;
    }

    // ---- v6: old-UE (4.19–4.22) GNames brute-force scanner ----
    // The pre-NamePool name system: FName::GNames = array of FNameEntry* block
    // pointers (bss). Block 0 starts with FNameEntry "None" (u16 hdr 0x0004 +
    // "None" + 2 pad), followed by the EName table entries.
    // Returns the address of the blocks array (the NamePool-era equivalent).
    inline uint64_t FindGNames()
    {
        RT::Init();
        if (!g_DataStart)
            return 0;
        for (uint64_t p = g_DataStart; p + 64 <= g_DataEnd; p += 8)
        {
            uint64_t blk = *(uint64_t*)p;
            if (blk < 0x1000 || (blk & 3))
                continue;
            if (!RT::V6Readable(blk))
                continue;
            // entry 0 must be "None"
            uint16_t h0 = *(uint16_t*)blk;
            if (h0 != 0x0004) // len=4, non-wide
                continue;
            if (memcmp((void*)(blk + 2), "None", 4) != 0)
                continue;
            // entry 1 (at +8: hdr(2)+4 chars+pad(2)) must be a sane FNameEntry
            uint64_t e1 = blk + 8;
            if (!RT::V6Readable(e1 + 2))
                continue;
            uint16_t h1 = *(uint16_t*)e1;
            uint16_t len1 = h1 & 0x7FFF;
            if (len1 < 3 || len1 > 128)
                continue;
            // spot-check characters of entry 1 (ANSI or wide)
            if (h1 & 0x8000)
            {
                uint16_t c0 = *(uint16_t*)(e1 + 2), c1 = *(uint16_t*)(e1 + 4);
                if (c0 < 0x20 || c0 > 0x7E || c1 < 0x20 || c1 > 0x7E)
                    continue;
            }
            else
            {
                uint8_t c0 = *(uint8_t*)(e1 + 2), c1 = *(uint8_t*)(e1 + 3);
                if (c0 < 0x20 || c0 > 0x7E || c1 < 0x20 || c1 > 0x7E)
                    continue;
            }
            // second block pointer (index 1) — vote for array-ness
            uint64_t blk1 = *(uint64_t*)(p + 8);
            if (blk1 && RT::V6Readable(blk1))
            {
                uint16_t g1 = *(uint16_t*)blk1;
                if ((g1 & 0x7FFF) >= 1 && (g1 & 0x7FFF) <= 1024)
                    return p; // strong: two valid blocks
            }
        }
        return 0;
    }

    // ---- v6: UE5.4/5.5 LogV2 descriptor string→function chain ----
    // Verified on 32.00 (UE5.5, real binary): the log string sits in .rodata,
    // a 0x20-aligned descriptor in .data points at it via relocation, and the
    // logging code references the descriptor base with ADRP+ADD.
    inline uint64_t LogV2StringAnchorFunc(const wchar_t* const* strs, int nStrs)
    {
        RT::Init();
        for (int s = 0; s < nStrs; s++)
        {
            uint64_t strAddr = RT::FindWStr(strs[s]);
            if (!strAddr)
                continue;
            // slots whose VALUE == strAddr → descriptor fields
            uint64_t slots[64];
            uint32_t ns = RT::VtSlotsWith(strAddr, slots, 64);
            for (uint32_t k = 0; k < ns; k++)
            {
                uint64_t desc = slots[k] & ~0x1Full; // 0x20-aligned descriptor base
                uint64_t refs[64];
                uint32_t nRefs = 0;
                RT::RefsTo(desc, refs, &nRefs, 64);
                for (uint32_t r = 0; r < nRefs; r++)
                {
                    uint64_t a = RT::Deoutline(refs[r]);
                    uint64_t f = RT::BackscanPrologue(a);
                    if (f)
                        return f;
                }
            }
        }
        return 0;
    }

    // ---- v6: NATIVE ClientHasInitializedLevelFor (UE semantics, exact) ----
    // The game's virtual is inlined/transformed on many Android builds (verified:
    // the v4 21.30 "pattern" cell was a false positive — a 3-arg wrapper calling a
    // byte counter). This native implementation mirrors Erbium's own modern path
    // (NetDriver.cpp reads UNetConnection+ClientWorldPackageNameOffset directly).
    // UObject layout (stable across UE4.19–UE5.5 shipping):
    //   +0x18 NamePrivate (FName index in low 32 bits)  +0x20 OuterPrivate
    inline bool NativeClientHasInitializedLevelFor(const void* InConnection, const void* World)
    {
        if (!InConnection)
            return false;
        if (!World)
            return true; // no world restriction (matches Erbium pattern#3 semantics)
        float v = VersionInfo.FortniteVersion;
        uint64_t off = v >= 24.0f ? 0x1820 : v >= 21.20f ? 0x1708 : v >= 20.20f ? 0x16b8 : 0x1698;
        uint64_t pkg = (uint64_t)World;
        for (int i = 0; i < 32; i++)
        {
            uint64_t outer = *(uint64_t*)(pkg + 0x20); // GetOutermost walk
            if (!outer)
                break;
            pkg = outer;
        }
        uint32_t connName = *(uint32_t*)((uint64_t)InConnection + off); // ClientWorldPackageName
        uint32_t pkgName = *(uint32_t*)(pkg + 0x18);                    // WorldPackage->NamePrivate
        return connName == pkgName;
    }

    // ---- v6: CHILF vtable+shape chain (best-effort; native fallback guarantees) ----
    // UNetConnection-family vtables contain IsNetReady (findable everywhere via the
    // existing chains). Scan nearby slots for a small bool-returning 2-arg function
    // that walks [x1]+0x20 (GetOutermost) or reads [xN+0x18] (NamePrivate compare).
    inline bool V6ChilfShape(uint64_t f)
    {
        if (!RT::InText(f))
            return false;
        uint32_t n = 0x140 / 4;
        bool usesX1 = false, has20 = false, has18 = false, hasCset = false, hasRet = false;
        for (uint32_t i = 0; i < n; i++)
        {
            uint32_t w = *(uint32_t*)(f + i * 4);
            if (w == 0xD65F03C0) { hasRet = true; break; } // ret
            uint32_t op = w & 0xFFC00000;
            if (op == 0xB9400000 || op == 0xF9400000) // ldr
            {
                uint32_t rn = (w >> 5) & 0x1F, rt = w & 0x1F;
                uint64_t imm = ((w >> 10) & 0xFFF) * ((op == 0xF9400000) ? 8 : 4);
                if (rn == 1 || rt == 1) usesX1 = true;
                if (rn != 0 && imm == 0x18 && (op == 0xB9400000)) has18 = true;
                if (rn == 1 && imm == 0x20 && (op == 0xF9400000)) has20 = true;
            }
            else
            {
                uint32_t op10 = w & 0x7FE00C00; // opcode + fixed bits 10-11 (Rm/cond/Rn/Rd masked out)
                if (op10 == 0x1A800400 /*CSINC: cset/cinc*/ || op10 == 0x1A800000 /*CSEL*/ ||
                    op10 == 0x5A000400 /*CSINV: csetm*/ || op10 == 0x5A000000 /*CSNEG: cneg*/)
                    hasCset = true;
            }
            if (op == 0x14000000 || op == 0x94000000)
                continue; // branches don't stop the scan
        }
        return usesX1 && hasCset && (has20 || has18) && hasRet;
    }

    // inr = IsNetReady address (resolved by the caller via FindIsNetReady())
    inline uint64_t ChilfVtableChain(uint64_t inr)
    {
        if (!inr || !RT::InText(inr))
            return 0;
        uint64_t slots[96];
        uint32_t ns = RT::VtSlotsWith(inr, slots, 96);
        for (uint32_t s = 0; s < ns; s++)
        {
            // walk back to vtable start (contiguous text pointers)
            uint64_t vt = slots[s];
            while (vt - 8 >= g_DataStart && RT::InText(*(uint64_t*)(vt - 8)))
                vt -= 8;
            for (int delta = -48; delta <= 48; delta++)
            {
                uint64_t slot = slots[s] + (int64_t)delta * 8;
                if (slot < vt || !RT::InData(slot))
                    continue;
                uint64_t cand = *(uint64_t*)slot;
                if (!cand || !RT::InText(cand) || cand == inr)
                    continue;
                if (RT::V6ChilfShape(cand))
                    return cand;
            }
        }
        return 0;
    }

    // ---- v6: CreateNetDriverWorldContext runtime chain ----
    // Real semantic (Windows-verified on 21.30/22.20/27.11): UEngine::CreateNetDriver
    // iterates Engine->NetDriverDefinitions (16-byte stride: {FName Name, FName
    // DriverClassName, UClass*}), comparing each def's 32-bit FName with the arg.
    // ARM64 loop forms: pointer+0x10 stride, or counter loop with +0/+4 field loads.
    inline bool V6CndShape(uint64_t f)
    {
        if (!RT::InText(f))
            return false;
        bool pairK = false, has16 = false, cmpArg = false;
        uint64_t kOffs[8];
        int nk = 0;
        for (uint32_t i = 0; i < 0x300 / 4; i++)
        {
            uint32_t w = *(uint32_t*)(f + i * 4);
            if (w == 0xD65F03C0)
                break;
            uint32_t op = w & 0xFFC00000;
            if (op == 0xF9400000 || op == 0xB9400000) // ldr x/w imm
            {
                uint32_t rn = (w >> 5) & 0x1F;
                uint64_t imm = ((w >> 10) & 0xFFF) * ((op == 0xF9400000) ? 8 : 4);
                if (rn == 0 && nk < 8)
                    kOffs[nk++] = imm; // member loads from this
            }
            else if (op == 0x91000000) // add imm
            {
                uint64_t imm = ((w >> 10) & 0xFFF);
                if (imm == 0x10)
                    has16 = true;
            }
            else if ((w & 0xFFE0001F) == 0x6B00001F) // cmp (register)
            {
                uint32_t rm = (w >> 16) & 0x1F, rn = (w >> 5) & 0x1F;
                if (rm == 2 || rn == 2 || rm == 3 || rn == 3)
                    cmpArg = true; // compares against the FName arg
            }
        }
        for (int i = 0; i < nk; i++)
            for (int j = 0; j < nk; j++)
                if (kOffs[i] != kOffs[j] && (kOffs[i] - kOffs[j] == 8 || kOffs[j] - kOffs[i] == 8) &&
                    kOffs[i] >= 0x100 && kOffs[j] >= 0x100)
                    pairK = true; // Data/Num member pair deep in UEngine
        return pairK && has16 && cmpArg;
    }

    inline uint64_t CndChain()
    {
        RT::Init();
        uint64_t hits[16];
        uint32_t nh = 0;
        for (uint64_t a = g_TextStart; a + 0x300 < g_TextEnd && nh < 16; a += 4)
        {
            // cheap prefilter: add xN, xN, #0x10 then later ldr w,[xN,#4]-ish
            uint32_t w = *(uint32_t*)a;
            if ((w & 0xFFC00000) != 0x91000000 || ((w >> 10) & 0xFFF) != 0x10)
                continue;
            if ((w >> 5 & 0x1F) != (w & 0x1F)) // add xN,xN
                continue;
            uint64_t f = RT::BackscanPrologue(a, 0x200);
            if (!f || !RT::InText(f))
                continue;
            bool dup = false;
            for (uint32_t k = 0; k < nh; k++)
                if (hits[k] == f) { dup = true; break; }
            if (dup)
                continue;
            if (RT::V6CndShape(f))
                hits[nh++] = f;
        }
        return nh ? hits[0] : 0;
    }

} // namespace RT

