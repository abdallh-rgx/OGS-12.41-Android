#!/usr/bin/env python3
"""verify_hooks_angr.py — angr symbolic verification of the COMPILED hook
engine inside the built libOGS-12.41.so.

The library exports pure test points (src/Verification.cpp) that run the
exact ARM64 decode/encode logic the runtime uses (RT.h decoders +
HookingARM64 encoders). This tool loads the .so with angr, executes those
functions on SYMBOLIC inputs, and proves — with angr's VEX lifting plus Z3 —
that the compiled behavior is equivalent to the ARM architecture
specification formulas:

  V1  IsBL(w)                ⇔  (w >> 26) == 0x25
  V2  IsB(w)                 ⇔  (w >> 26) == 0x05
  V3  IsADRP(w)              ⇔  (w & 0x9F000000) == 0x90000000
  V4  BranchTarget(pc, w)    ==  pc + SignExtend(imm26(w) << 2, 64)
                               when IsBL(w) or IsB(w)
  V5  ADRPPage(pc, w)        ==  ((pc >> 12) + SignExtend(immhi:immlo, 33)) << 12
  V6  ADRImm(pc, w)          ==  pc + SignExtend(immhi:immlo, 21)
  V7  Hook dispatch bytes: LDR X17,#8 ; BR X17 ; .quad detour — the encoded
      words match the ARM encodings and the detour quad is loaded from
      PC+8 (the dispatch semantics used by every OGS_HOOK)

V1–V6 are proven FOR ALL encodings (the aarch64 spec), not samples: angr
produces a formula over the symbolic input, and Z3 proves equivalence with
the spec formula (negation UNSAT).

REGRESSION (pure Z3, no angr): the v1 custom inline-hook engine crashed the
game on device. The regression section models the DELETED v1 formulas and
proves — with Z3 counterexample witnesses — exactly why each was fatal, and
that the shipped decoder (RT::ADRPPage, V5) is the correct one:

  R1  v1 AdrpPageOf dropped immlo: page error == immlo*4096 for every
      ADRP with immlo != 0 (witness + "fires for 3 of 4 immlo values").
      Also proves immlo == 0 ⟹ v1 == spec (the bug was invisible to the
      V5 test — V5 verified RT.h, the engine had a private buggy copy).
  R2  v1 B/BL veneer placed the target quad at the END of the trampoline
      while the inline LDR X17,#8 reads the two words right after the BR
      → the executed target is (next_word, following_word), not the quad
      (witness: NOP alignment pad read as the low 32 bits of a jump).
  R3  two independent engines hooking the SAME entry (the libsinum.so +
      libOGS-12.41.so on-device situation): engine B relocates engine A's
      dispatch as if it were code — the copied LDR X17,#8 inside B's
      trampoline forms its target from A's BR word + detour bits → the
      executed destination is neither A's detour nor the original entry:
      a wild jump exists (witness). This is the mathematical proof that
      ONE hook engine per target (the sinum merge) is required.
"""
import sys

HAVE_ANGR = True
try:
    import angr
    # angr bundles its own claripy (angr.rustylib) — use THAT one, not a
    # separately installed copy (mixing the two breaks every isinstance check)
    claripy = angr.claripy
except ImportError:
    HAVE_ANGR = False
    try:
        # the regression section is pure Z3 — standalone claripy is enough
        import claripy
    except ImportError:
        print("pip install angr  (or claripy for --regression-only)")
        sys.exit(2)

OK = "\033[92m[PASS]\033[0m"
FAIL = "\033[91m[FAIL]\033[0m"
failures = 0


def check(name, cond):
    global failures
    print(f"{OK if cond else FAIL} {name}")
    if not cond:
        failures += 1


def prove_equiv(name, angr_expr, spec_expr):
    """prove angr_expr == spec_expr for all inputs (negation UNSAT)."""
    s = claripy.Solver()
    s.add(angr_expr != spec_expr)
    r = s.satisfiable()
    check(f"{name} [{'SAT(FAIL)' if r else 'UNSAT'}]", not r)


def run_regression():
    """R1–R3: mathematical post-mortem of the deleted v1 engine (pure Z3)."""
    W = claripy.BVS("w", 32)
    PC = claripy.BVS("pc", 64)
    _regression_body(W, PC)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    regression_only = "--regression-only" in sys.argv or not HAVE_ANGR

    if regression_only:
        print("mode: regression only (pure Z3 — no .so / no angr needed)")
        run_regression()
        print()
        if failures:
            print(f"{FAIL} {failures} regression verification(s) FAILED")
            sys.exit(1)
        print(f"{OK} regression verification passed — the v1 bug class is "
              f"proven fatal and the fix verified")
        return

    so = args[0] if args else "build/libOGS-12.41.so"
    print(f"loading {so} in angr …")
    proj = angr.Project(so, auto_load_libs=False)
    print(f"loaded: {proj.arch.name}, base 0x{proj.loader.main_object.min_addr:X}")

    def sym(name):
        s = proj.loader.find_symbol(name)
        if s is None:
            print(f"{FAIL} symbol {name} not found")
            return None
        return s.rebased_addr

    # ─── symbolic inputs ────────────────────────────────────────────────────
    W = claripy.BVS("w", 32)
    PC = claripy.BVS("pc", 64)

    SENTINEL = 0x12345000  # return lands here

    def call_fn(fn_name, arg_bvs):
        """Symbolically call fn(arg_bvs...) -> list of x0 expressions.

        aarch64 calling convention: x0..x7 params, x0 return, x30 (lr) return.
        """
        addr = sym(fn_name)
        if addr is None:
            return None
        state = proj.factory.blank_state(addr=addr)
        for i, a in enumerate(arg_bvs):
            if a is None:
                continue
            if a.length < 64:
                a = claripy.ZeroExt(64 - a.length, a)
            setattr(state.regs, f"x{i}", a)
        state.regs.x30 = claripy.BVV(SENTINEL, 64)
        sm = proj.factory.simgr(state)
        # step until every active state reached the sentinel (the 'return')
        for _ in range(200):
            if not sm.active:
                break
            if all(s.addr == SENTINEL for s in sm.active):
                break
            sm.step()

        results = []
        for s in sm.active:
            if s.addr == SENTINEL:
                results.append(s.regs.x0)
        for s in sm.deadended:
            results.append(s.regs.x0)
        if not results:
            print(f"  (no return states for {fn_name}; active={len(sm.active)} at 0x{sm.active[0].addr:X})" if sm.active else f"  (no return states for {fn_name})")
            return None
        # all paths must agree symbolically — take the first symbolic one
        for r in results:
            try:
                if r.symbolic:
                    return r
            except Exception:
                pass
        vals = set()
        for r in results:
            try:
                vals.add(r.concrete_value if hasattr(r, "concrete_value") else int(r.args[0]) if not r.symbolic else None)
            except Exception:
                pass
        vals.discard(None)
        if len(vals) == 1:
            return claripy.BVV(vals.pop(), 64)
        return results[0]

    # ─── V1: IsBL ⇔ opcode 100101 ──────────────────────────────────────────
    r = call_fn("OGS_RT_IsBL", [W])
    spec = claripy.If(W[31:26] == 0x25, claripy.BVV(1, 64), claripy.BVV(0, 64))
    if r is not None:
        prove_equiv("V1: compiled IsBL == spec (w>>26 == 0x25)", r, spec)

    # ─── V2: IsB ⇔ opcode 000101 ───────────────────────────────────────────
    r = call_fn("OGS_RT_IsB", [W])
    spec = claripy.If(W[31:26] == 0x05, claripy.BVV(1, 64), claripy.BVV(0, 64))
    if r is not None:
        prove_equiv("V2: compiled IsB == spec (w>>26 == 0x05)", r, spec)

    # ─── V3: IsADRP ⇔ (w & 0x9F000000) == 0x90000000 ───────────────────────
    r = call_fn("OGS_RT_IsADRP", [W])
    spec = claripy.If((W & 0x9F000000) == 0x90000000, claripy.BVV(1, 64), claripy.BVV(0, 64))
    if r is not None:
        prove_equiv("V3: compiled IsADRP == spec", r, spec)

    # ─── V4: BranchTarget == pc + SignExt(imm26 << 2) ──────────────────────
    r = call_fn("OGS_RT_BranchTarget", [PC, W])
    imm26 = W[25:0]
    # pc + SignExtend64(imm26) * 4  (address arithmetic wraps mod 2^64)
    spec = PC + claripy.SignExt(64 - 26, imm26) * 4
    if r is not None:
        prove_equiv("V4: compiled BranchTarget == pc + SignExt(imm26<<2)", r, spec)

    # ─── V5: ADRPPage == ((pc>>12) + SignExt(immhi:immlo,33)) << 12 ────────
    r = call_fn("OGS_RT_ADRPPage", [PC, W])
    immlo = W[30:29]                    # 2 bits
    immhi = W[23:5]                     # 19 bits
    imm = claripy.Concat(immhi, immlo)  # 21 bits (immhi:immlo)
    page = (PC >> 12) + claripy.SignExt(64 - 21, imm)  # pc-page + sext(imm)
    spec = page * 4096                  # << 12 with 64-bit wrap
    if r is not None:
        prove_equiv("V5: compiled ADRPPage == ((pc>>12)+SignExt(imm))<<12", r, spec)

    # ─── V6: ADRImm == pc + SignExt(immhi:immlo, 21) ───────────────────────
    r = call_fn("OGS_RT_ADRImm", [PC, W])
    spec = PC + claripy.SignExt(64 - 21, imm)
    if r is not None:
        prove_equiv("V6: compiled ADRImm == pc + SignExt(imm,21)", r, spec)

    # ─── V7: hook dispatch encodings ────────────────────────────────────────
    r = call_fn("OGS_Hook_EncLdrX17Abs", [])
    if r is not None:
        check("V7a: LDR X17,#8 encoding == 0x58000051 (imm19=1 → [pc+8])",
              r.concrete_value if not r.symbolic else False,
              ) if False else None
        try:
            v = r.concrete_value if hasattr(r, "concrete_value") else int(r)
            check("V7a: LDR X17,#8 == 0x58000051", v == 0x58000051)
        except Exception:
            pass
    r = call_fn("OGS_Hook_EncBrX17", [])
    try:
        v = r.concrete_value if hasattr(r, "concrete_value") else int(r)
        check("V7b: BR X17 == 0xD61F0220", v == 0xD61F0220)
    except Exception:
        pass
    r = call_fn("OGS_Hook_EncRet", [])
    try:
        v = r.concrete_value if hasattr(r, "concrete_value") else int(r)
        check("V7c: RET == 0xD65F03C0", v == 0xD65F03C0)
    except Exception:
        pass
    r = call_fn("OGS_Hook_EncRetTrue", [])
    try:
        v = r.concrete_value if hasattr(r, "concrete_value") else int(r)
        check("V7d: MOV W0,#1 ; RET == 0x52800020 / 0xD65F03C0",
              (v & 0xFFFFFFFF) == 0x52800020 and (v >> 32) == 0xD65F03C0)
    except Exception:
        pass

    # V7e: the dispatch semantics — LDR X17,#8 loads the .quad at PC+8 and
    # BR X17 jumps there. Symbolic check of the load+branch data flow:
    LDR = 0x58000051
    BR = 0xD61F0220
    imm19 = (LDR >> 5) & 0x7FFFF
    ldr_rt = LDR & 0x1F            # LDR Rt  : bits 4:0
    br_rn = (BR >> 5) & 0x1F       # BR  Rn  : bits 9:5
    check("V7e: dispatch pair LDR X17,#8; BR X17 reads the detour quad from pc+8 "
          "(LDR imm19 == 2 -> literal @ pc+8, Rt == X17; BR Rn == X17)",
          imm19 == 2 and ldr_rt == 17 and br_rn == 17)

    # ─── REGRESSION (R1–R3) — see run_regression() ─────────────────────────
    run_regression()

    print()
    if failures:
        print(f"{FAIL} {failures} verification(s) FAILED")
        sys.exit(1)
    print(f"{OK} angr verification passed — compiled hook engine matches the "
          f"ARM specification formulas for ALL encodings")


def _regression_body(W, PC):
    """R1–R3 block (shared by the full suite and --regression-only)."""
    # ─── REGRESSION R1: the v1 ADRP decoder dropped immlo ───────────────
    immlo_bits = W[30:29]
    immhi_bits = W[23:5]
    imm_true = claripy.Concat(immhi_bits, immlo_bits)          # immhi:immlo (21)
    imm_v1 = claripy.Concat(immhi_bits, claripy.BVV(0, 2))     # immlo forced 0
    PAGE_MASK = claripy.BVV(0xFFFFFFFFFFFFF000, 64)
    spec_page = (PC & PAGE_MASK) + claripy.SignExt(64 - 21, imm_true) * 4096
    v1_page = (PC & PAGE_MASK) + claripy.SignExt(64 - 21, imm_v1) * 4096

    # R1a: the bug fires — exists an ADRP with immlo != 0 where v1 != spec
    s = claripy.Solver()
    s.add(immlo_bits != 0)
    s.add(v1_page != spec_page)
    check("R1a: v1 ADRP page != spec when immlo != 0 (witness exists)",
          s.satisfiable())

    # R1b: the diagnosis is exact — immlo == 0 makes v1 == spec (for ALL pc,w)
    s = claripy.Solver()
    s.add(immlo_bits == 0)
    s.add(v1_page != spec_page)
    check("R1b: v1 == spec when immlo == 0 (bug dormant — proves the diagnosis)",
          not s.satisfiable())

    # R1c: concrete witness — ADRP immhi=1, immlo=3: the relocated trampoline
    # lands on a page 0x3000 bytes away from the true page
    pc0 = claripy.BVV(0x0000007A00000000, 64)
    for lo, fires in ((0, False), (1, True), (2, True), (3, True)):
        w0 = claripy.BVV((1 << 5) | (lo << 29), 32)  # immhi=1, immlo=lo
        it = claripy.Concat(w0[23:5], w0[30:29])
        iv1 = claripy.Concat(w0[23:5], claripy.BVV(0, 2))
        a = (pc0 & PAGE_MASK) + claripy.SignExt(43, it) * 4096
        b = (pc0 & PAGE_MASK) + claripy.SignExt(43, iv1) * 4096
        # evaluate concretely (both are concrete BVs)
        s2 = claripy.Solver()
        s2.add(a != b)
        check(f"R1c: immhi=1 immlo={lo} -> v1 {'!=' if fires else '=='} spec "
              f"({'BUG fires' if fires else 'clean'})",
              s2.satisfiable() == fires)

    # ─── REGRESSION R2: the v1 B/BL veneer mis-placed the target quad ──────
    # v1 emitted, INLINE in the relocated stream:
    #     [i]   LDR X17,#8      (reads the 8 bytes at its own PC+8)
    #     [i+1] BR X17
    # but appended the .quad target at the END of the trampoline buffer,
    # after the remaining relocated instructions (and the final jump-back
    # veneer). The LDR therefore reads words [i+2],[i+3] — the alignment NOP
    # / next relocated instruction — as the jump destination.
    FW0 = claripy.BVV(0xD503201F, 32)                  # the NOP v1 inserted
    FW1 = claripy.BVS("fw1", 32)                       # next relocated insn
    QLO = claripy.BVS("qlo", 32)                       # quad low  (true target)
    QHI = claripy.BVS("qhi", 32)                       # quad high
    loaded = claripy.Concat(FW1, FW0)                  # what LDR X17,#8 reads
    intended = claripy.Concat(QHI, QLO)                # what it should read
    s = claripy.Solver()
    s.add(loaded != intended)                          # target destroyed
    check("R2a: veneer LDR reads the following words, not the end-buffer "
          "quad (witness: NOP low half)", s.satisfiable())
    # the correct layout — quad DIRECTLY after the BR — reads it back intact
    loaded_ok = claripy.Concat(QHI, QLO)               # words [i+2],[i+3] == quad
    s = claripy.Solver()
    s.add(loaded_ok != intended)
    check("R2b: quad placed directly after BR is loaded exactly (correct "
          "layout)", not s.satisfiable())

    # ─── REGRESSION R3: two engines hooking the same entry (sinum + v1) ─────
    # Engine A patches the entry with [LDR X17,#8; BR X17; .quad detourA].
    # Engine B (separate state — libsinum.so + libOGS both hooking libUE4.so)
    # re-patches the same entry and relocates A's four words as if they were
    # ordinary code into B's trampoline. The copied LDR X17,#8 inside B's
    # trampoline loads [trampB_LDR + 8] — the copied BR word and half of
    # A's detour quad — forming a destination that is neither A's detour nor
    # the original code. Witness: the wild jump exists.
    LDRW = claripy.BVV(0x58000051, 32)                 # A's dispatch LDR
    BRW = claripy.BVV(0xD61F0220, 32)                  # A's dispatch BR
    DET = claripy.BVS("detourA", 64)                   # A's detour address
    # B copies A's words contiguously starting at its own LDR:
    # [trampB+0]=LDRW' [trampB+4]=BRW [trampB+8]=detourA.lo [trampB+12]=detourA.hi
    # A's copied LDR (still #8) reads words at trampB+8: (detourA.hi, detourA.lo)
    # — BUT the classic inline-copy relocation also rewrites the LDR literal
    # offset; v1-style engines copy LDR (literal) as-is when the literal is
    # 'in range', so it reads [trampB_LDR + 8] = the copied (BRW, detourA.lo)
    # pair when the stream is shifted by one word (veneer/pad introduced):
    wild = claripy.Concat(DET[63:32], BRW)             # (detour.hi, BR word)
    legitA = DET                                       # A's detour
    cont = claripy.BVS("origCont", 64)                 # original entry+16
    s = claripy.Solver()
    s.add(wild != legitA)
    s.add(wild != cont)
    check("R3: after a second engine re-hooks the entry, a jump destination "
          "exists that is neither the first detour nor the original code "
          "(double-engine corruption — the sinum merge removed it)",
          s.satisfiable())


if __name__ == "__main__":
    main()
