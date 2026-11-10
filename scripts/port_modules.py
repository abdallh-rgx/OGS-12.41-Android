#!/usr/bin/env python3
"""port_modules.py — mechanical 1:1 transformation of the OGS PC module headers
to the Android port.

Transformations (each is semantics-preserving for the Android target):
  * L"..."           -> u"..."          (UE Android TCHAR is char16_t)
  * wchar_t          -> char16_t
  * std::wstring     -> std::u16string
  * MH_CreateHook((LPVOID)(ImageBase + 0xRVA), D, (LPVOID*)&OG);
                      -> OGS_HOOK(0xRVA, D, OG);
    (the PC RVA is kept as a semantic key; NativeResolver maps it to the
     resolved Android address at bring-up)
  * MH_Initialize/MH_EnableHook(MH_ALL_HOOKS) -> (no-op; Dobby-style hooks are
    installed enabled)
  * VirtualProtect/DWORD etc. -> provided by platform/erbium/compat
"""
import re
import sys
from pathlib import Path

SRC = Path("/home/z/my-project/upstream/OGS-12.41-main")
DST = Path("/home/z/my-project/src")

SKIP = {"framework.h", "dllmain.cpp", "Globals.h", "PC.h", "minhook"}

MH_HOOK = re.compile(
    r"MH_CreateHook\s*\(\s*\(LPVOID\)\s*\(ImageBase\s*\+\s*(0x[0-9A-Fa-f]+)\s*\)\s*,\s*([^,]+?)\s*,\s*\(LPVOID\*\)&(\w+)\s*\)\s*;")

MH_HOOK_NOREF = re.compile(
    r"MH_CreateHook\s*\(\s*\(LPVOID\)\s*\(ImageBase\s*\+\s*(0x[0-9A-Fa-f]+)\s*\)\s*,\s*([^,]+?)\s*,\s*nullptr\s*\)\s*;")

# static fn-ptr init: = decltype(Name)(ImageBase + 0xRVA)
FN_PTR_INIT = re.compile(
    r"decltype\s*\(\s*(\w+)\s*\)\s*\(\s*ImageBase\s*\+\s*(0x[0-9A-Fa-f]+)\s*\)")

RETADDR_EQ = re.compile(
    r"__int64\s*\(\s*_ReturnAddress\s*\(\s*\)\s*\)\s*==\s*ImageBase\s*\+\s*(0x[0-9A-Fa-f]+)")


def transform(text: str) -> str:
    # wide literals -> char16_t literals (L"..." -> u"...")
    text = re.sub(r'\bL"', 'u"', text)
    # wchar_t -> char16_t (word boundary; keeps 'wchar_t' inside comments readable)
    text = re.sub(r"\bwchar_t\b", "char16_t", text)
    text = re.sub(r"\bstd::wstring\b", "std::u16string", text)
    # wcslen etc. -> char16 equivalents
    text = re.sub(r"\bwcslen\b", "u16len", text)
    # _ReturnAddress() -> clang builtin via compat
    text = text.replace("_ReturnAddress()", "OGS_RETURN_ADDRESS()")
    # return-address equality checks -> resolved key compare
    text = RETADDR_EQ.sub(lambda m: f"OGS_RETADDR_IS({m.group(1)})", text)
    # static function-pointer initializers -> runtime resolution by PC-RVA key
    text = FN_PTR_INIT.sub(lambda m: f"decltype({m.group(1)})(OGS_RESOLVE({m.group(2)}))", text)
    # MH_CreateHook(ImageBase + 0xRVA, Detour, &OG); -> OGS_HOOK(RVA, Detour, OG);
    text = MH_HOOK.sub(lambda m: f"OGS_HOOK({m.group(1)}, {m.group(2).strip()}, {m.group(3)});", text)
    # MH_CreateHook(ImageBase + 0xRVA, Detour, nullptr); -> OGS_HOOK_NOREF(RVA, Detour);
    text = MH_HOOK_NOREF.sub(lambda m: f"OGS_HOOK_NOREF({m.group(1)}, {m.group(2).strip()});", text)
    # MH init/enables -> no-ops (hooks are installed enabled)
    text = text.replace("MH_Initialize();", "// (minhook init — Dobby-style hooks install enabled)")
    text = text.replace("MH_EnableHook(MH_ALL_HOOKS);", "// (minhook enable-all — hooks install enabled)")
    return text


def main():
    DST.mkdir(exist_ok=True)
    for f in sorted(SRC.glob("*.h")) + sorted(SRC.glob("*.cpp")):
        if f.name in SKIP:
            continue
        out = transform(f.read_text(encoding="utf-8", errors="ignore"))
        (DST / f.name).write_text(out, encoding="utf-8")
        print("ported", f.name)
    print("done")


if __name__ == "__main__":
    sys.exit(main())
