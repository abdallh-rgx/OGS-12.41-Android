#!/usr/bin/env python3
"""
get_apk.py — fetch a Fortnite Android APK for the bring-up pipeline.

Source order:
  1. --url <direct url>        (anything curl can fetch: GitHub release asset,
                                archive.org file, personal hosting, ...)
  2. archive.org search        (works from GitHub runners; the search API is
                                blocked in some sandboxes but fine on CI)
  3. uptodown (metadata only)  — prints the version's Uptodown file id so a
                                human can fetch it in a browser (their file
                                endpoint is behind Cloudflare Turnstile).

Usage:
  python3 scripts/get_apk.py --version 18.40.0-18167774 --out artifacts/fortnite.apk
  python3 scripts/get_apk.py --url https://.../fortnite.apk --out artifacts/fortnite.apk
"""
import argparse
import json
import os
import re
import subprocess
import sys
import urllib.request

UPTODOWN_APP_ID = "725251"  # Fortnite on Uptodown
HERE = os.path.dirname(os.path.abspath(__file__))
VERSIONS_JSON = os.path.join(HERE, "..", "android", "uptodown_versions.json")


def http_get(url, timeout=60):
    req = urllib.request.Request(url, headers={"User-Agent": "erbium-ci/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def download_to(url, dest):
    print(f"[get_apk] downloading {url} -> {dest}")
    # curl handles redirects/big files better; fall back to urllib
    try:
        subprocess.run(["curl", "-fsSL", "--retry", "3", "-o", dest, url], check=True)
    except Exception:
        data = http_get(url, timeout=600)
        with open(dest, "wb") as f:
            f.write(data)
    size = os.path.getsize(dest)
    print(f"[get_apk] got {size/1024/1024:.1f} MB")
    if size < 10 * 1024 * 1024:
        print("[get_apk] WARNING: file is suspiciously small for a Fortnite APK")


def try_archive_org(version, dest):
    """Search archive.org for a Fortnite Android APK matching the version."""
    print(f"[get_apk] searching archive.org for {version} ...")
    queries = [
        f'fortnite android {version.split("-")[0]}',
        "fortnite android apk collection",
        "fortnite mobile apk",
    ]
    seen = set()
    for q in queries:
        try:
            url = (
                "https://archive.org/advancedsearch.php?q="
                + urllib.request.quote(q)
                + "&fl%5B%5D=identifier&fl%5B%5D=title&rows=50&output=json"
            )
            data = json.loads(http_get(url, timeout=90))
            for doc in data.get("response", {}).get("docs", []):
                ident = doc["identifier"]
                if ident in seen:
                    continue
                seen.add(ident)
                print(f"[get_apk] candidate item: {ident} :: {doc.get('title','')[:60]}")
                # list files of this item
                try:
                    meta = json.loads(http_get(f"https://archive.org/metadata/{ident}", timeout=60))
                    for fobj in meta.get("files", []):
                        name = fobj.get("name", "")
                        if name.lower().endswith((".apk", ".xapk")):
                            if version.split("-")[0] in name:
                                dl = f"https://archive.org/download/{ident}/{urllib.request.quote(name)}"
                                print(f"[get_apk] MATCH: {dl}")
                                download_to(dl, dest)
                                return True
                except Exception as e:
                    print(f"[get_apk] metadata fetch failed for {ident}: {e}")
        except Exception as e:
            print(f"[get_apk] archive.org query failed: {e}")
    return False


def uptodown_lookup(version):
    """Print Uptodown info for manual download (their file endpoint is Turnstile-gated)."""
    print(f"[get_apk] Uptodown lookup for {version}")
    if os.path.exists(VERSIONS_JSON):
        with open(VERSIONS_JSON) as f:
            rows = json.load(f)
        for r in rows:
            if r.get("v", "").startswith(version.split("-")[0]):
                print(
                    f"[get_apk]   {r['v']}  file_id={r['id']}  "
                    f"https://fortnite.en.uptodown.com/android/download/{r['id']}"
                )
        return True
    # fall back to live API
    try:
        for page in range(1, 25):
            url = f"https://fortnite.en.uptodown.com/android/apps/{UPTODOWN_APP_ID}/versions/{page}"
            data = json.loads(http_get(url, timeout=30))
            for row in data.get("data", []):
                if row.get("version", "").startswith(version.split("-")[0]):
                    print(
                        f"[get_apk]   {row['version']}  file_id={row['fileID']}  "
                        f"https://fortnite.en.uptodown.com/android/download/{row['fileID']}"
                    )
                    return True
    except Exception as e:
        print(f"[get_apk] uptodown api failed: {e}")
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default="18.40.0-18167774")
    ap.add_argument("--url", default="")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    os.makedirs(os.path.dirname(os.path.abspath(args.out)) or ".", exist_ok=True)

    if args.url:
        download_to(args.url, args.out)
        return 0

    if try_archive_org(args.version, args.out):
        return 0

    print("[get_apk] could not auto-download. Manual options:")
    uptodown_lookup(args.version)
    return 2


if __name__ == "__main__":
    sys.exit(main())
