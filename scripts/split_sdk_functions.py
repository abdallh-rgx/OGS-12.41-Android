#!/usr/bin/env python3
"""split_sdk_functions.py — split the huge Dumper-7 *_functions.cpp files into
smaller TUs so each clang invocation fits in memory (the sandbox has 4 GB;
FortniteGame_functions.cpp alone needs > 4 GB as one TU).

Splits at "// Function <Package>.<Class>.<Name>" boundaries into ~700 KB
chunks; every chunk repeats the original include preamble.
"""
import re
from pathlib import Path

SDK = Path("/home/z/my-project/SDK/SDK")
OUT = SDK / "split"

TARGET = 700_000  # bytes per chunk


def split_file(path: Path):
    text = path.read_text(encoding="utf-8", errors="ignore")

    # strip the trailing namespace close (re-appended per part)
    stripped = text.rstrip()
    if stripped.endswith("}"):
        # remove exactly the last namespace-closing brace
        idx = stripped.rfind("}")
        text = stripped[:idx] + "\n"

    # preamble: everything up to the first "// Function " marker
    m = re.search(r"^// Function ", text, re.M)
    if not m:
        return
    preamble = text[:m.start()]

    # body split into function blocks
    blocks = re.split(r"(?m)(?=^// Function )", text[m.start():])

    chunks = []
    cur = []
    cur_size = 0
    for b in blocks:
        if cur_size and cur_size + len(b) > TARGET:
            chunks.append("".join(cur))
            cur = []
            cur_size = 0
        cur.append(b)
        cur_size += len(b)
    if cur:
        chunks.append("".join(cur))

    if len(chunks) <= 1:
        return

    stem = path.stem  # e.g. FortniteGame_functions
    OUT.mkdir(exist_ok=True)
    for i, chunk in enumerate(chunks):
        (OUT / f"{stem}_part{i:03d}.cpp").write_text(
            preamble + "\n// === split part %d/%d ===\n" % (i + 1, len(chunks)) + chunk
            + "\n} // namespace SDK\n",
            encoding="utf-8")
    print(f"{path.name}: {len(chunks)} chunks")
    path.unlink()  # replaced by the chunks
    return [str(OUT / f"{stem}_part{i:03d}.cpp") for i in range(len(chunks))]


def main():
    for name in ["AIModule_functions.cpp", "CoreUObject_functions.cpp",
                 "Engine_functions.cpp", "FortniteGame_functions.cpp",
                 "GameplayAbilities_functions.cpp", "UMG_functions.cpp"]:
        p = SDK / name
        if p.exists() and p.stat().st_size > TARGET:
            split_file(p)


if __name__ == "__main__":
    main()
