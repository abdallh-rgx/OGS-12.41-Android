#!/usr/bin/env python3
"""Restore symlinks after zip extraction (python zipfile drops them).

Reads the original zip, walks every entry whose unix mode has S_IFLNK,
and replaces the extracted plain file with a proper symlink.
"""
import os
import stat
import sys
import zipfile

ZIP = "/tmp/ndk.zip"
ROOT = "/tmp/ndk-extract"

def main() -> int:
    z = zipfile.ZipFile(ZIP)
    fixed = 0
    skipped = 0
    for info in z.infolist():
        mode = info.external_attr >> 16
        if not (mode & stat.S_IFLNK) == stat.S_IFLNK:
            continue
        target = z.read(info).decode()
        dst = os.path.join(ROOT, info.filename)
        try:
            if os.path.islink(dst) or os.path.isfile(dst):
                os.remove(dst)
            elif os.path.isdir(dst):
                skipped += 1
                continue
            os.symlink(target, dst)
            fixed += 1
        except OSError:
            skipped += 1
    print(f"symlinks restored: {fixed}, skipped: {skipped}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
