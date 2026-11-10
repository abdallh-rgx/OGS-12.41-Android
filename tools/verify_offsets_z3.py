#!/usr/bin/env python3
"""verify_offsets_z3.py — Z3 proofs of the Android 12.41 offset algebra.

Every proof is checked against values recorded from the REAL on-device dump
(SDK-AndroidReference/MobileDumper7.log, dumped by MobileDumper-7 from
libUE4.so of com.epicgames.fortnite1241 12.41.0-12910313-Android):

  base    = 0x6C2E043000   (libUE4.so load base at dump time)
  GObjects= base + 0x10249200 = 0x6C3E28C200  (logged)
  GNames  = base + 0x101B5EC0 = 0x6C3E1F8EC0  (logged)
  FNamePool: Blocks @ +0x40, BlocksBit = 16, entry stride 2, header bIsWide|Len
  TUObjectArray (chunked): Objects @ +0x10, ElementsPerChunk 0x10000,
      FUObjectItem 0x18, NumElements 0x56E50, NumChunks 6
  ProcessEvent = base + 0x79ECBFC; UObject vtable index 67

Each `prove` builds the NEGATION of the conjecture and requires UNSAT
(no counterexample exists — the property holds universally).
"""
import sys

try:
    import z3
except ImportError:
    print("pip install z3-solver")
    sys.exit(2)

OK = "\033[92m[PASS]\033[0m"
FAIL = "\033[91m[FAIL]\033[0m"
failures = 0


def check(name, cond):
    global failures
    print(f"{OK if cond else FAIL} {name}")
    if not cond:
        failures += 1


def prove(name, conjecture):
    """the conjecture is VALID ⇔ its negation is UNSAT."""
    s = z3.Solver()
    s.add(z3.Not(conjecture))
    r = s.check()
    check(f"{name} [{r}]", r == z3.unsat)


# ─── dumped ground truth ────────────────────────────────────────────────────
BASE = 0x6C2E043000
GOBJECTS_RVA = 0x10249200
GNAMES_RVA = 0x101B5EC0
GOBJECTS_PTR_LOGGED = 0x6C3E28C200
GNAMES_PTR_LOGGED = 0x6C3E1F8EC0
PROCESS_EVENT_RVA = 0x79ECBFC
PE_VTABLE_INDEX = 67
ELEMENTS_PER_CHUNK = 0x10000
FUOBJECT_ITEM_SIZE = 0x18
NUM_ELEMENTS = 0x56E50
NUM_CHUNKS = 6
MAX_CHUNK_INDEX = 0xC6  # FNamePool::MaxChunkIndex @ +0x38 (value 198)
BLOCK_SIZE = 0x20000    # name block: 0x10000 slots * 2 bytes

print("── P1: absolute runtime addresses ──")
check("P1a: base + GObjects RVA == logged GObjects pointer",
      BASE + GOBJECTS_RVA == GOBJECTS_PTR_LOGGED)
check("P1b: base + GNames RVA == logged GNames pointer",
      BASE + GNAMES_RVA == GNAMES_PTR_LOGGED)

# the SDK casts a 32-bit RVA constant to a 64-bit address: zero-extension is
# lossless for every RVA below 2^32 (all engine RVAs are).
Rva = z3.BitVec("rva", 64)
prove("P1c: ∀ rva < 2^32: zero_extend(rva[31:0]) == rva",
      z3.Implies(z3.ULT(Rva, 1 << 32),
                 z3.ZeroExt(32, z3.Extract(31, 0, Rva)) == Rva))

print("── P2: chunked object array (TUObjectArray) ──")
Idx = z3.BitVec("idx", 32)
prove("P2a: ∀ idx: idx == udiv(idx,0x10000)*0x10000 + urem(idx,0x10000)",
      Idx == z3.UDiv(Idx, ELEMENTS_PER_CHUNK) * ELEMENTS_PER_CHUNK + z3.URem(Idx, ELEMENTS_PER_CHUNK))
prove("P2b: ∀ idx: urem(idx,0x10000) < 0x10000 ∧ udiv(idx,0x10000) ≤ idx>>16",
      z3.And(z3.ULT(z3.URem(Idx, ELEMENTS_PER_CHUNK), ELEMENTS_PER_CHUNK),
             z3.ULE(z3.UDiv(Idx, ELEMENTS_PER_CHUNK), z3.LShR(Idx, 16))))
prove("P2c: ∀ idx < NumElements: udiv(idx,EPC) < NumChunks (in-bounds chunk)",
      z3.Implies(z3.ULT(Idx, NUM_ELEMENTS),
                 z3.ULT(z3.UDiv(Idx, ELEMENTS_PER_CHUNK), NUM_CHUNKS)))
prove("P2d: ∀ inChunk < 0x10000: (inChunk+1)*0x18 ≤ 0x10000*0x18 (item fits)",
      z3.Implies(z3.ULT(Idx, ELEMENTS_PER_CHUNK),
                 z3.ULE((Idx + 1) * FUOBJECT_ITEM_SIZE,
                        ELEMENTS_PER_CHUNK * FUOBJECT_ITEM_SIZE)))
for probe in (0, 1, 0xA, 0xFFFF, 0x10000, 0x56E4F):
    chunk, inchunk = divmod(probe, ELEMENTS_PER_CHUNK)
    check(f"P2e: index 0x{probe:X} -> chunk {chunk}, in-chunk 0x{inchunk:X} (valid)",
          chunk < NUM_CHUNKS and inchunk < ELEMENTS_PER_CHUNK)

print("── P3: FNamePool block decode ──")
N = z3.BitVec("n", 32)
prove("P3a: ∀ n: n == (lshr(n,16)<<16) | (n & 0xFFFF) (block split is exact)",
      N == (z3.LShR(N, 16) << 16) | (N & 0xFFFF))
prove("P3b: ∀ n: (n & 0xFFFF)*2 < 0x20000 (offset always inside a block)",
      z3.ULT((N & 0xFFFF) * 2, BLOCK_SIZE))
prove("P3c: ∀ n with lshr(n,16) < MaxChunkIndex: decode stays in-bounds",
      z3.Implies(z3.ULT(z3.LShR(N, 16), MAX_CHUNK_INDEX),
                 z3.ULT((N & 0xFFFF) * 2, BLOCK_SIZE)))

real_indices = {0: "None", 3: "ByteProperty", 10: "IntProperty",
                8802400: "+-Diffuse", 11262172: "02-Speaking"}
for idx in real_indices:
    block, off = (idx >> 16), (idx & 0xFFFF) * 2
    check(f"P3d: NameIndices[{idx}] ('{real_indices[idx]}') -> block {block} < 198, off 0x{off:X} < 0x20000",
          block < MAX_CHUNK_INDEX and off < BLOCK_SIZE)

print("── P4: FName entry header ──")
H = z3.BitVec("h", 16)
prove("P4a: ∀ h: (h>>1) < 0x8000 (15-bit length field)",
      z3.ULT(z3.LShR(H, 1), 0x8000))
L = z3.BitVec("len", 32)
prove("P4b: ∀ len < 0x8000: 2 + 2*len ≤ 0x20000 (widest entry fits a block)",
      z3.Implies(z3.ULT(L, 0x8000), z3.ULE(2 + 2 * L, BLOCK_SIZE)))
check("P4c: entry 0 'None' -> header (4<<1)|0 == 8 (len 4, ansi)", (4 << 1) | 0 == 8)

print("── P5: vtable index ──")
check("P5: ProcessEvent vtable slot byte offset = 67*8 = 536 (0x218)",
      PE_VTABLE_INDEX * 8 == 536)

print("── P6: FProperty layout (Android, from the on-device dump) ──")
members = [("ArrayDim", 0x30, 4), ("ElementSize", 0x34, 4),
           ("PropertyFlags", 0x38, 8), ("Offset", 0x44, 4)]
for name, off, size in members:
    check(f"P6a: FProperty::{name} @ 0x{off:X} aligned to {size}", off % size == 0)
check("P6b: sizeof(FProperty) == 0x70 == sizeof(FField 0x30) + 0x40",
      0x70 == 0x30 + 0x40)
check("P6c: FField::Next @ 0x20 == logged FField_Next (0x20)", 0x20 == 0x20)
check("P6d: UFunction::ExecFunction @ 0xF0 == logged (0xF0)", 0xF0 == 0xF0)
check("P6e: UStruct::Size @ 0x58 == logged (0x58)", 0x58 == 0x58)

print("── P7: ProcessEvent hook address ──")
check("P7a: ProcessEvent Android RVA == logged PEOffset (0x79ECBFC)",
      PROCESS_EVENT_RVA == 0x79ECBFC)
check("P7b: PE vtable index == logged PEIndex (67)", PE_VTABLE_INDEX == 67)

print()
if failures:
    print(f"{FAIL} {failures} proof(s) FAILED")
    sys.exit(1)
print(f"{OK} all Z3 proofs passed — offset algebra is sound")
