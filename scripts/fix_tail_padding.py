#!/usr/bin/env python3
"""Automatic Itanium tail-padding fixer for the Dumper-7 SDK on clang/ARM64.

Why: the engine's Android build keeps MSVC-style base-class layout (derived
members start at sizeof(base), not dsize(base) — tail padding is NOT reused),
while clang (Itanium ABI) reuses non-POD base tail padding. The reflected
property offsets (from the dump — identical on both platforms) prove the
real layout matches the MSVC-style one.

How: parse clang's static_assert failures
        "__builtin_offsetof(SDK::CLASS, MEMBER) == Y" + "expression evaluates to 'X == Y'"
   For each class whose FIRST declared member is short (X < Y), inject
   explicit padding at the end of its immediate base class
        uint8 OGS_ClangTailPad[N]; // OGS-CLANG-TAILPAD
   which forces dsize(base) == sizeof(base) + needed bytes. Iterate until the
   build is clean (cascading offsets converge in a few rounds).
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path("/home/z/my-project")
CMAKE = str(Path.home() / ".venv/bin/cmake")
PAD_MARK = "OGS-CLANG-TAILPAD"


def build() -> str:
    r = subprocess.run([CMAKE, "--build", "build", "-j", "2"],
                       cwd=ROOT, capture_output=True, text=True, timeout=1800)
    return r.stdout + r.stderr


def parse_failures(log: str):
    """[(class, member, actual, expected)] — dedup, keep first member per class."""
    pat = re.compile(
        r"static assertion failed due to requirement '__builtin_offsetof\(SDK::(\w+), (\w+)\) == (\d+)'"
        r".*?expression evaluates to '(\d+) == (\d+)'",
        re.S)
    seen = {}
    for m in pat.finditer(log):
        cls, member, expected, actual, expected2 = m.group(1), m.group(2), int(m.group(3)), int(m.group(4)), int(m.group(5))
        if expected != expected2:
            continue
        if cls not in seen or member < seen[cls][0]:  # keep lexicographically-first member (declaration order)
            seen[cls] = (member, actual, expected)
    return {c: v for c, v in seen.items()}


def find_class_def(cls):
    """(file, base_class) — the definition 'class X ... : public Y ... {'."""
    inherit = re.compile(rf"^\s*class {cls}\b[^{{;]*?:\s*public\s+(\w+)", re.M)
    plain = re.compile(rf"^\s*class {cls}\b[^{{;]*\{{", re.M)
    for f in sorted((ROOT / "SDK/SDK").glob("*.hpp")):
        text = f.read_text(encoding="utf-8", errors="ignore")
        m = inherit.search(text)
        if m:
            return f, m.group(1), text
        if plain.search(text):
            return f, None, text
    return None, None, None


def inject_pad(path: Path, cls: str, pad: int) -> bool:
    """Insert trailing pad member right before the class's closing '};'."""
    text = path.read_text(encoding="utf-8")
    if f"{PAD_MARK}-{cls}" in text:
        return False
    # find the class body braces
    m = re.search(rf"class {cls}\b[^{{;]*\{{", text)
    if not m:
        return False
    start = m.end()  # after '{'
    depth = 1
    i = start
    while i < len(text) and depth:
        c = text[i]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        i += 1
    # i-1 is the matching '}'; expect ';' right after
    close = i - 1
    inject = (f"\n#ifdef __ANDROID__\n"
              f"\tuint8 OGS_ClangTailPad[{pad}]; // {PAD_MARK}-{cls} (Itanium tail-reuse fix)\n"
              f"#endif\n")
    text = text[:close] + inject + text[close:]
    path.write_text(text, encoding="utf-8")
    return True


def main():
    rounds = 0
    while rounds < 8:
        rounds += 1
        log = build()
        failures = parse_failures(log)
        if not failures:
            print(f"round {rounds}: no offsetof failures — clean")
            print("BUILD RESULT:", "OK" if "ninja: no work to do" in log or "Linking" in log or "Built target" in log else log[-500:])
            return 0
        print(f"round {rounds}: {len(failures)} failing classes: {list(failures)[:6]}...")
        changed = 0
        for cls, (member, actual, expected) in sorted(failures.items()):
            if actual == expected:
                continue
            diff = expected - actual
            if diff <= 0 or diff > 64:
                print(f"  SKIP {cls}: odd diff {diff}")
                continue
            _, base, _ = find_class_def(cls)
            if not base:
                print(f"  SKIP {cls}: no base (pad cannot be placed)")
                continue
            # the pad goes into the file where the BASE class is DEFINED
            base_file, base_base, _ = find_class_def(base)
            if not base_file:
                print(f"  SKIP {cls}: base {base} definition file not found")
                continue
            if inject_pad(base_file, base, diff):
                changed += 1
            else:
                print(f"  SKIP {cls}: base {base} already padded / not found")
        print(f"  injected pads: {changed}")
        if changed == 0:
            print("no progress — stopping")
            print(log[-3000:])
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
