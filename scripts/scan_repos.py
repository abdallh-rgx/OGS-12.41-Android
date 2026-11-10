#!/usr/bin/env python3
"""Scan the user's GitHub repos for libUE4.so / APK / 12.41 assets."""
import json
import os
import sys
import urllib.request

TOKEN = os.environ.get("GH_TOKEN", "")
HDRS = {"Authorization": f"token {TOKEN}", "User-Agent": "scan"}

REPOS = [
    "owen", "rearm", "Optimize", "test", "idk", "Simple-OGFN-launcher",
    "Tellurium", "Midnight", "Voltronite", "Sinum", "Erbium", "erbium-build",
    "FModel-Android", "Dumper7Android", "UE4-Mobile-Dumper",
    "ipa-loader-android", "agent-ops", "fn35-loader", "fn35-ios-emulator",
    "12.41-Android-GameServer",
]

KEYS = ("libue4", "libogsn", ".so", "12.41", ".apk", "libdobby", "dobby")


def api(url):
    req = urllib.request.Request(url, headers=HDRS)
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def main():
    for repo in REPOS:
        try:
            d = api(f"https://api.github.com/repos/abdallh-rgx/{repo}/git/trees/HEAD?recursive=1")
            tree = d.get("tree", [])
            hits = [t["path"] for t in tree
                    if any(k in t["path"].lower() for k in KEYS)
                    and t["type"] == "blob"]
            status = f"TRUNCATED({len(tree)}+)" if d.get("truncated") else f"{len(tree)} items"
            print(f"{repo:28s} [{status}] -> {hits[:8]}")
        except Exception as e:  # noqa: BLE001
            print(f"{repo:28s} ERR {e}")
        sys.stdout.flush()


if __name__ == "__main__":
    main()
