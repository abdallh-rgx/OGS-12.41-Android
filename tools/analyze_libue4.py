#!/usr/bin/env python3
"""
analyze_libue4.py — offline ELF analyzer for Fortnite Android's libUE4.so /
libUnreal.so. Produces a baked-offsets header consumed by the Android port,
replacing upstream's runtime x64 pattern scanning (Finders.cpp) with
per-version constants — the same philosophy as Owen.c's offsets table.

Phases:
  phase 1: ELF sanity, arch check, symbol table dump, version string,
           UTF-16 string anchors, section map
  phase 2: APS2 packed-relocation decode (Android-10 flag semantics) →
           GOT slot map + .data.rel.ro vtable map;
           ARM64 ADRP+ADD/ADRP+LDR cross-reference scanner;
           BL-target function boundary table;
           GIsClient / GIsServer / GIsCommandlet / GAllowCommandlet* /
           GEngine discovery via the "AllowCommandletRendering" anchor
           (ARM64 equivalent of upstream Finders.cpp FindGIsClient/Server);
           JNI export map (nativeConsoleCommand etc.)

Usage:
  python3 scripts/analyze_libue4.py --apk fortnite.apk
  python3 scripts/analyze_libue4.py --so libUnreal.so --out android/Generated/offsets.h

Tested against Fortnite 21.30.0-CL-21088273 Android (arm64-v8a, NDK r21e).
"""
import argparse
import array
import json
import os
import re
import struct
import sys

ELF_MAGIC = b"\x7fELF"
TEXT_LIMIT = 400 * 1024 * 1024

# ---------------------------------------------------------------------------
# Android 10 (Q) packed-relocation flag semantics — bionic android10-release.
# NOTE: master bionic has HAS_ADDEND/GROUPED_BY_ADDEND SWAPPED vs android10.
# ---------------------------------------------------------------------------
REL_BY_INFO = 1
REL_BY_OFFSET_DELTA = 2
REL_GROUPED_BY_ADDEND = 4
REL_HAS_ADDEND = 8
R_AARCH64_RELATIVE = 1027

# ARM64 instruction decode helpers ------------------------------------------


def adrp_decode(pc, word):
    """Return (rd, target_page) if word is ADRP else None."""
    if (word & 0x9F000000) != 0x90000000:
        return None
    rd = word & 0x1F
    immlo = (word >> 29) & 3
    immhi = (word >> 5) & 0x7FFFF
    imm = (immhi << 2) | immlo
    if imm & (1 << 20):
        imm -= (1 << 21)
    return rd, ((pc & ~0xFFF) + (imm << 12)) & ((1 << 64) - 1)


def add_imm_decode(word):
    """Return (rd, rn, imm12) if word is ADD (immediate, 64-bit) else None."""
    if (word & 0xFFC00000) != 0x91000000:
        return None
    imm12 = (word >> 10) & 0xFFF
    if (word >> 22) & 3:
        imm12 <<= 12
    return word & 0x1F, (word >> 5) & 0x1F, imm12


def ldrx_imm_decode(word):
    """Return (rt, rn, imm) if word is LDR X (unsigned offset) else None."""
    if (word & 0xFFC00000) != 0xF9400000:
        return None
    return word & 0x1F, (word >> 5) & 0x1F, ((word >> 10) & 0xFFF) << 3


def strb_imm_decode(word):
    """Return (rt, rn, imm) if word is STRB (unsigned offset) else None."""
    if (word & 0xFFC00000) != 0x39000000:
        return None
    return word & 0x1F, (word >> 5) & 0x1F, (word >> 10) & 0xFFF


def ldrb_imm_decode(word):
    if (word & 0xFFC00000) != 0x39400000:
        return None
    return word & 0x1F, (word >> 5) & 0x1F, (word >> 10) & 0xFFF


class Sleb:
    __slots__ = ("buf", "pos")

    def __init__(self, buf, pos):
        self.buf, self.pos = buf, pos

    def sleb(self):
        result = 0
        shift = 0
        buf, pos = self.buf, self.pos
        while True:
            b = buf[pos]
            pos += 1
            result |= (b & 0x7F) << shift
            shift += 7
            if not (b & 0x80):
                if b & 0x40:
                    result -= (1 << shift)
                self.pos = pos
                return result


def decode_aps2(buf, start):
    """Exact port of Android-10 bionic packed_reloc_iterator. Returns list of
    (r_offset, r_info, r_addend). Memory-heavy — caller filters on the fly."""
    assert buf[start:start + 4] == b"APS2", "not an APS2 section"
    d = Sleb(buf, start + 4)
    relocation_count = d.sleb()
    r_offset = d.sleb()
    r_info = 0
    r_addend = 0
    out = []
    idx = 0
    while idx < relocation_count:
        gs = d.sleb()
        gf = d.sleb()
        grod = 0
        if gf & REL_BY_OFFSET_DELTA:
            grod = d.sleb()
        if gf & REL_BY_INFO:
            r_info = d.sleb()
        if (gf & REL_HAS_ADDEND) and (gf & REL_GROUPED_BY_ADDEND):
            r_addend += d.sleb()
        elif not (gf & REL_HAS_ADDEND):
            r_addend = 0
        for _ in range(gs):
            if gf & REL_BY_OFFSET_DELTA:
                r_offset += grod
            else:
                r_offset += d.sleb()
            if not (gf & REL_BY_INFO):
                r_info = d.sleb()
            if (gf & REL_HAS_ADDEND) and not (gf & REL_GROUPED_BY_ADDEND):
                r_addend += d.sleb()
            out.append((r_offset, r_info, r_addend))
            idx += 1
    return out


def extract_lib_from_apk(apk_path):
    """Return (name, bytes) of the UE4/Unreal shared lib inside the APK/XAPK."""
    import zipfile
    zf = zipfile.ZipFile(apk_path)
    candidates = []
    for n in zf.namelist():
        base = os.path.basename(n)
        if base in ("libUE4.so", "libUnreal.so") and "arm64-v8a" in n:
            candidates.append((n, base))
    if not candidates:
        for n in zf.namelist():
            if "arm64-v8a" in n and n.endswith(".so"):
                candidates.append((n, os.path.basename(n)))
    if not candidates:
        raise RuntimeError("no arm64 native lib found in APK")
    candidates.sort(key=lambda c: 0 if c[1] == "libUE4.so" else 1)
    name, base = candidates[0]
    print(f"[analyze] extracting {name} ({zf.getinfo(name).file_size/1024/1024:.1f} MB)")
    return base, zf.read(name)


class Elf64:
    """Minimal ELF64 little-endian parser."""

    def __init__(self, data: bytes):
        self.data = data
        if data[:4] != ELF_MAGIC:
            raise RuntimeError("not an ELF")
        if data[4] != 2 or data[5] != 1:
            raise RuntimeError("not ELF64 LE")
        (self.e_type, self.e_machine, _ver, self.e_entry, self.e_phoff,
         self.e_shoff, self.e_flags, self.e_ehsize, self.e_phentsize,
         self.e_phnum, self.e_shentsize, self.e_shnum,
         self.e_shstrndx) = struct.unpack_from("<HHIQQQIHHHHHH", data, 16)
        self.sections = []
        for i in range(self.e_shnum):
            off = self.e_shoff + i * self.e_shentsize
            (sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size,
             sh_link, sh_info, sh_addralign, sh_entsize) = struct.unpack_from(
                "<IIQQQQIIQQ", data, off)
            self.sections.append(dict(name_off=sh_name, type=sh_type, flags=sh_flags,
                                      addr=sh_addr, offset=sh_offset, size=sh_size,
                                      link=sh_link, entsize=sh_entsize))
        if self.e_shstrndx < len(self.sections):
            strtab = self.sections[self.e_shstrndx]
            self._strtab = data[strtab["offset"]:strtab["offset"] + strtab["size"]]
        else:
            self._strtab = b""

    def sh_name(self, sec):
        end = self._strtab.find(b"\x00", sec["name_off"])
        return self._strtab[sec["name_off"]:end].decode(errors="ignore")

    def section(self, name):
        for s in self.sections:
            if self.sh_name(s) == name:
                return s
        return None

    def dynsyms(self):
        """Yield (name, value, size) from .dynsym."""
        symsec = self.section(".dynsym")
        if not symsec:
            return []
        strsec = self.sections[symsec["link"]]
        strblob = self.data[strsec["offset"]:strsec["offset"] + strsec["size"]]
        blob = self.data[symsec["offset"]:symsec["offset"] + symsec["size"]]
        out = []
        count = len(blob) // 24
        for i in range(count):
            st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from(
                "<IBBHQQ", blob, i * 24)
            if st_name == 0 or st_value == 0:
                continue
            end = strblob.find(b"\x00", st_name)
            name = strblob[st_name:end].decode(errors="ignore")
            if name:
                out.append((name, st_value, st_size))
        return out

    def find_utf16(self, text, rodata):
        """Find UTF-16LE string in .rodata; return vaddr or None."""
        needle = text.encode("utf-16-le") + b"\x00\x00"
        blob = self.data[rodata["offset"]:rodata["offset"] + rodata["size"]]
        i = blob.find(needle)
        if i < 0:
            return None
        return rodata["addr"] + i


def analyze(so_name, blob, version, out_path):
    print(f"[analyze] parsing {so_name} — {len(blob)/1024/1024:.1f} MB")
    elf = Elf64(blob)
    mach = {183: "aarch64", 40: "arm", 62: "x86_64", 3: "x86"}.get(elf.e_machine, str(elf.e_machine))
    print(f"[analyze] machine: {mach}  type: {elf.e_type}  sections: {elf.e_shnum}")
    if mach != "aarch64":
        print("[analyze] WARNING: expected aarch64!")

    report = {"lib": so_name, "version": version, "machine": mach,
              "sections": {elf.sh_name(s): hex(s["addr"]) for s in elf.sections if s["type"] != 0}}

    # 1) game version string anchor (addr==file offset in this link layout)
    m = re.search(rb"\+\+Fortnite\+Release-[\d.]+-CL-\d+", blob)
    report["release_string"] = m.group(0).decode() if m else None
    report["release_string_addr"] = m.start() if m else None
    print(f"[analyze] release string: {report['release_string']} @ {hex(m.start()) if m else '?'}")

    rodata = elf.section(".rodata")
    text = elf.section(".text")
    data_rel_ro = elf.section(".data.rel.ro")
    bss = elf.section(".bss")
    got = elf.section(".got") or elf.section(".got.plt")
    if not (rodata and text and bss):
        raise RuntimeError("missing .rodata/.text/.bss")
    TEXT_LO, TEXT_HI = text["addr"], text["addr"] + text["size"]

    # 2) dynamic symbols (JNI exports etc.)
    syms = elf.dynsyms()
    report["symbol_count"] = len(syms)
    jni_syms = {}
    for name, value, size in syms:
        if name.startswith(("Java_", "JNI_OnLoad", "ANativeActivity")):
            jni_syms[name] = value
    report["jni_exports"] = {k: hex(v) for k, v in sorted(jni_syms.items(), key=lambda kv: kv[1])}
    print(f"[analyze] symbols: {len(syms)} (jni exports: {len(jni_syms)})")

    # 3) APS2 packed relocations → GOT map + relro map
    got_map = {}
    relro_map = {}
    rela_dyn = elf.section(".rela.dyn")
    if rela_dyn:
        head = blob[rela_dyn["offset"]:rela_dyn["offset"] + 4]
        if head == b"APS2":
            print("[analyze] decoding APS2 packed relocations (Android-10 semantics)...")
            d = Sleb(blob, rela_dyn["offset"] + 4)
            relocation_count = d.sleb()
            r_offset = d.sleb()
            r_info = 0
            r_addend = 0
            idx = 0
            n_got = n_relro = 0
            got_lo, got_hi = got["addr"], got["addr"] + got["size"]
            rro_lo, rro_hi = data_rel_ro["addr"], data_rel_ro["addr"] + data_rel_ro["size"] if data_rel_ro else (0, 0)
            while idx < relocation_count:
                gs = d.sleb()
                gf = d.sleb()
                grod = 0
                if gf & REL_BY_OFFSET_DELTA:
                    grod = d.sleb()
                if gf & REL_BY_INFO:
                    r_info = d.sleb()
                if (gf & REL_HAS_ADDEND) and (gf & REL_GROUPED_BY_ADDEND):
                    r_addend += d.sleb()
                elif not (gf & REL_HAS_ADDEND):
                    r_addend = 0
                for _ in range(gs):
                    if gf & REL_BY_OFFSET_DELTA:
                        r_offset += grod
                    else:
                        r_offset += d.sleb()
                    if not (gf & REL_BY_INFO):
                        r_info = d.sleb()
                    if (gf & REL_HAS_ADDEND) and not (gf & REL_GROUPED_BY_ADDEND):
                        r_addend += d.sleb()
                    if (r_info & 0xFFFFFFFF) == R_AARCH64_RELATIVE:
                        if got_lo <= r_offset < got_hi:
                            got_map[r_offset] = r_addend
                            n_got += 1
                        elif rro_lo and rro_lo <= r_offset < rro_hi:
                            relro_map[r_offset] = r_addend
                            n_relro += 1
                    idx += 1
            print(f"[analyze] APS2: {relocation_count} relocs; got_map={n_got}, relro_map={n_relro}")
    report["got_entries"] = len(got_map)

    # 4) string anchors
    anchors = {}
    for text_anchor in ("AllowCommandletRendering", "AllowCommandletAudio",
                        "Artemis_Terrain", "Athena_Terrain", "Commandlet"):
        addr = elf.find_utf16(text_anchor, rodata)
        if addr:
            anchors[text_anchor] = addr
    report["utf16_anchors"] = {k: hex(v) for k, v in anchors.items()}
    print(f"[analyze] utf16 anchors: {report['utf16_anchors']}")

    # 5) xref scanner — find code refs to the AllowCommandletRendering anchor
    xrefs = []
    if "AllowCommandletRendering" in anchors:
        target = anchors["AllowCommandletRendering"]
        tpage, tlo = target & ~0xFFF, target & 0xFFF
        a = array.array("I")
        a.frombytes(blob[text["offset"]:text["offset"] + text["size"]])
        n = len(a)
        for i in range(n - 1):
            w = a[i]
            if (w & 0x9F000000) != 0x90000000:
                continue
            pc = TEXT_LO + i * 4
            r = adrp_decode(pc, w)
            if not r or r[1] != tpage:
                continue
            w2 = a[i + 1]
            ad = add_imm_decode(w2)
            if ad and ad[1] == r[0] and tpage + ad[2] == target:
                xrefs.append(pc)
                continue
            ld = ldrx_imm_decode(w2)
            if ld and ld[1] == r[0] and tpage + ld[2] == target:
                xrefs.append(pc)
        print(f"[analyze] AllowCommandletRendering xrefs: {[hex(x) for x in xrefs]}")

    # 6) GIs* discovery — around the anchor xref, emulate upstream's logic:
    #    nearby ADRP+GOT-LDR + STRB writes split into two classes:
    #      a) constant stores: 'mov w8,#1' (52800028) feeding 'strb w8,[glob]'
    #         → GIsClient (first), GIsServer (second) — UE source order
    #      b) parse-result stores: 'and w8,w0,#1' (12000008) feeding the store
    #         → GAllowCommandletRendering (first), GAllowCommandletAudio (second)
    #    Validated against UE 4.27 FEngineLoop::PreInit on 21.30 ground truth.
    gis = {}
    if xrefs:
        xref = xrefs[0]
        window_start = xref - 0x40
        regs = {}
        const_stores = []   # (pc, global) — store of a constant (mov #1)
        parse_stores = []   # (pc, global) — store of a ParseParam result
        last_def = {}       # reg -> 'const' | 'parse'
        for pc in range(window_start, xref + 0x60, 4):
            w = struct.unpack_from("<I", blob, pc)[0]
            r = adrp_decode(pc, w)
            if r:
                regs[r[0]] = ("page", r[1])
                continue
            ld = ldrx_imm_decode(w)
            if ld and ld[1] in regs and regs[ld[1]][0] == "page":
                addr = regs[ld[1]][1] + ld[2]
                if addr in got_map:
                    regs[ld[0]] = ("glob", got_map[addr])
                else:
                    regs[ld[0]] = ("raw", addr)
                continue
            # mov w8, #1  → next strb w8 is a constant store
            if w == 0x52800028:
                last_def[8] = "const"
                continue
            # and w8, w0, #1 → next strb w8 is a parse-result store
            if w == 0x12000008:
                last_def[8] = "parse"
                continue
            sb = strb_imm_decode(w)
            if sb and sb[1] in regs and regs[sb[1]][0] == "glob":
                glob = regs[sb[1]][1] + sb[2]
                kind = last_def.get(sb[0])
                if kind == "const":
                    const_stores.append((pc, glob))
                elif kind == "parse":
                    parse_stores.append((pc, glob))
                # NB: do NOT pop — a single 'mov w8,#1' feeds several stores
        gis["const_stores"] = [(hex(pc), hex(g)) for pc, g in const_stores]
        gis["parse_stores"] = [(hex(pc), hex(g)) for pc, g in parse_stores]
        if len(const_stores) >= 2:
            gis["GIsClient"] = hex(const_stores[0][1])
            gis["GIsServer"] = hex(const_stores[1][1])
        if len(parse_stores) >= 2:
            gis["GAllowCommandletRendering"] = hex(parse_stores[0][1])
            gis["GAllowCommandletAudio"] = hex(parse_stores[1][1])
    report["gis_analysis"] = gis
    print(f"[analyze] gis analysis: {gis}")

    # 7) GEngine — from nativeConsoleCommand's GOT double-deref
    gengine = None
    console_cmd = jni_syms.get("Java_com_epicgames_unreal_GameActivity_nativeConsoleCommand")
    if console_cmd:
        # within first 0x40 bytes, find: adrp xN, got_page; ldr xN, [xN, #slot]
        for pc in range(console_cmd, console_cmd + 0x40, 4):
            w = struct.unpack_from("<I", blob, pc)[0]
            r = adrp_decode(pc, w)
            if not r:
                continue
            w2 = struct.unpack_from("<I", blob, pc + 4)[0]
            ld = ldrx_imm_decode(w2)
            if ld and ld[1] == r[0]:
                slot = r[1] + ld[2]
                if slot in got_map:
                    gengine = got_map[slot]
                    break
    if gengine:
        report["GEngine"] = hex(gengine)
    print(f"[analyze] GEngine: {report.get('GEngine')}")


    # Owen.c redirect-hook offsets (libUnreal.so ProcessRequest/SetURL),
    # sourced from the Owen.c per-version tables. Extend as versions bake.
    OWEN_OFFSETS = {
        "++Fortnite+Release-21.30-CL-21088273": (0x089F02F0, 0x70, 0x089EDCF8),
    }

    # 8) write the baked offsets header
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "w") as f:
        f.write("// GENERATED by scripts/analyze_libue4.py — DO NOT EDIT\n")
        f.write(f"// lib: {so_name}  version: {version}\n")
        f.write(f"// release: {report.get('release_string')}\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("namespace Erbium::Baked {\n")
        f.write(f"inline constexpr const char* kLibName = \"{so_name}\";\n")
        f.write(f"inline constexpr const char* kRelease = \"{report.get('release_string') or version}\";\n")
        ident = lambda s: re.sub(r"\W+", "_", s)
        if gis.get("GIsClient"):
            f.write(f"inline constexpr uint64_t kGIsClient = {gis['GIsClient']};\n")
        if gis.get("GIsServer"):
            f.write(f"inline constexpr uint64_t kGIsServer = {gis['GIsServer']};\n")
        if gis.get("GAllowCommandletRendering"):
            f.write(f"inline constexpr uint64_t kGAllowCommandletRendering = {gis['GAllowCommandletRendering']};\n")
        if report.get("release_string_addr") is not None:
            f.write(f"inline constexpr uint64_t kReleaseString = {hex(report['release_string_addr'])}; // \"{report['release_string']}\"\n")
        f.write("inline constexpr const char* kConsoleCommandSymbol = \"Java_com_epicgames_unreal_GameActivity_nativeConsoleCommand\";\n")
        owen = OWEN_OFFSETS.get(report.get("release_string"))
        if owen:
            f.write(f"inline constexpr uint64_t kOwenProcessRequest = {hex(owen[0])};\n")
            f.write(f"inline constexpr uint64_t kOwenGetUrlField = {hex(owen[1])};\n")
            f.write(f"inline constexpr uint64_t kOwenSetUrl = {hex(owen[2])};\n")
        report["owen"] = owen and [hex(x) for x in owen]
        if gengine:
            f.write(f"inline constexpr uint64_t kGEngine = {hex(gengine)};\n")
        if console_cmd:
            f.write(f"inline constexpr uint64_t kNativeConsoleCommand = {hex(console_cmd)};\n")
        resume = jni_syms.get("Java_com_epicgames_unreal_GameActivity_nativeResumeMainInit")
        if resume:
            f.write(f"inline constexpr uint64_t kNativeResumeMainInit = {hex(resume)};\n")
        for name, addr in sorted(jni_syms.items(), key=lambda kv: kv[1]):
            f.write(f"inline constexpr uint64_t kSym_{ident(name)} = {hex(addr)}; // {name}\n")
        for text_anchor, addr in anchors.items():
            f.write(f"inline constexpr uint64_t kStr_{ident(text_anchor)} = {hex(addr)}; // \"{text_anchor}\"\n")

        # ── function bake (find82): merge + verify ────────────────────
        # If scripts/find82 produced functions-<ver>.json for this version,
        # re-emit the kFn_ constants from it (keeps the CI-generated header
        # identical to the committed one) and VERIFY every resolved address
        # lands inside .text of THIS binary (guards against version drift).
        bake_id = re.sub(r"^(\d+\.\d+).*", r"\1", version)
        fnjson = os.path.join(os.path.dirname(os.path.abspath(out_path)),
                              f"functions-{bake_id}.json")
        if os.path.isfile(fnjson):
            fand = json.load(open(fnjson))
            n_ok = n_bad = 0
            f.write("// ── function bake (find82)\n")
            f.write("// resolved by scripts/find82 — see functions-%s.json for provenance\n\n" % bake_id)
            for entry in sorted(fand.get("functions", []), key=lambda e: e["name"]):
                if entry.get("status") == "todo":
                    continue
                addr = int(entry["addr"], 16)
                if TEXT_LO <= addr < TEXT_HI:
                    f.write(f"inline constexpr uint64_t kFn_{entry['name']} = {hex(addr)}; // {entry['status']}\n")
                    n_ok += 1
                else:
                    print(f"[analyze] FUNCTION-BAKE MISMATCH: {entry['name']} "
                          f"{entry['addr']} outside .text of this binary!")
                    n_bad += 1
            f.write("\n")
            print(f"[analyze] function bake: {n_ok} verified in .text, {n_bad} MISMATCHED")
            if n_bad:
                raise RuntimeError(f"function bake verification failed ({n_bad} mismatched)")
        else:
            print(f"[analyze] no function bake found at {fnjson} (find82 not run for this version)")

        f.write("} // namespace Erbium::Baked\n")
    print(f"[analyze] wrote {out_path}")

    sidecar = os.path.splitext(out_path)[0] + ".json"
    with open(sidecar, "w") as f:
        json.dump(report, f, indent=1)
    print("[analyze] phase-2 analysis complete")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apk")
    ap.add_argument("--so")
    ap.add_argument("--version", default="unknown")
    ap.add_argument("--out", default="android/Generated/offsets.h")
    args = ap.parse_args()

    if args.so:
        name = os.path.basename(args.so)
        with open(args.so, "rb") as f:
            blob = f.read()
    elif args.apk:
        name, blob = extract_lib_from_apk(args.apk)
    else:
        print("need --apk or --so")
        return 2

    analyze(name, blob, args.version, args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
