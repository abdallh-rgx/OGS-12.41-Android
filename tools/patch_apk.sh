#!/usr/bin/env bash
# ============================================================================
# patch_apk.sh — inject libOGS-12.41.so into the Fortnite 12.41 Android APK.
#
# Usage:
#   ./tools/patch_apk.sh <Fortnite-12.41.apk> [libOGS-12.41.so] [out.apk]
#
# Steps:
#   1. apktool decode
#   2. patch GameActivity.onCreate smali → System.loadLibrary("OGS-12.41")
#   3. drop lib/arm64-v8a/libOGS-12.41.so
#   4. apktool build + zipalign + apksigner (debug or a supplied keystore)
#
# Requires: apktool, zipalign, apksigner (Android build-tools), a Java keystore.
#   keystore:  keytool -genkey -v -keystore ogs.keystore -alias ogs \
#              -keyalg RSA -keysize 2048 -validity 10000
# ============================================================================
set -euo pipefail

APK="${1:?usage: patch_apk.sh <in.apk> [lib.so] [out.apk]}"
LIB="${2:-build/libOGS-12.41.so}"
OUT="${3:-OGS-12.41-patched.apk}"
KS="${KS:-ogs.keystore}"
KS_ALIAS="${KS_ALIAS:-ogs}"
KS_PASS="${KS_PASS:-android}"

command -v apktool >/dev/null || { echo "apktool not found (https://apktool.org)"; exit 1; }
command -v zipalign >/dev/null || { echo "zipalign not found (Android build-tools)"; exit 1; }
command -v apksigner >/dev/null || { echo "apksigner not found (Android build-tools)"; exit 1; }
[ -f "$LIB" ] || { echo "library not found: $LIB"; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "[1/5] apktool decode $APK"
apktool d -f -q -o "$WORK/app" "$APK"

echo "[2/5] patching smali: System.loadLibrary(\"OGS-12.41\") in onCreate"
# The activity that runs first is Epic's GameActivity (com.epicgames.unreal.GameActivity).
PATCHED=0
for f in $(grep -rl "onCreate" "$WORK/app/smali"*/com/epicgames/unreal/ 2>/dev/null || true); do
    # skip lines that already load the library
    if grep -q 'loadLibrary("OGS-12.41")' "$f"; then PATCHED=1; continue; fi
    python3 - "$f" <<'PY'
import re, sys
path = sys.argv[1]
src = open(path, encoding="utf-8").read()
if 'loadLibrary("OGS-12.41")' in src:
    sys.exit(0)
# inject right after the first invoke of onCreate's super (or at the top of onCreate)
pat = re.compile(r'(\.method\s+public\s+onCreate\(Landroid/os/Bundle;\)V\s*\n)')
m = pat.search(src)
if not m:
    sys.exit(1)
load = ('    const-string v0, "OGS-12.41"\n'
        '    invoke-static {v0}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V\n')
# choose a free register: scan a few lines after the method start for v0 usage
after = src[m.end():m.end()+400]
reg = 'v0'
for r in ('v0', 'v1', 'v2', 'v3'):
    if not re.search(rf'\b{r}\b', after.split('.end method')[0][:400]):
        reg = r
        break
load = load.replace('v0', reg)
src = src[:m.end()] + '\n' + load + src[m.end():]
open(path, 'w', encoding='utf-8').write(src)
PY
    if [ $? -eq 0 ]; then PATCHED=1; fi
done
[ "$PATCHED" = "1" ] || { echo "no GameActivity smali patched — check the APK"; exit 1; }
echo "      loadLibrary patched"

echo "[3/5] adding lib/arm64-v8a/libOGS-12.41.so"
mkdir -p "$WORK/app/lib/arm64-v8a"
cp "$LIB" "$WORK/app/lib/arm64-v8a/libOGS-12.41.so"

echo "[4/5] apktool build"
apktool b -q -o "$WORK/app-unsigned.apk" "$WORK/app"

echo "[5/5] zipalign + sign"
zipalign -f -p 4 "$WORK/app-unsigned.apk" "$WORK/app-aligned.apk"
if [ ! -f "$KS" ]; then
    echo "      (no keystore — creating debug keystore ogs.keystore)"
    keytool -genkeypair -keystore "$KS" -alias "$KS_ALIAS" -keyalg RSA -keysize 2048 \
        -validity 10000 -storepass "$KS_PASS" -keypass "$KS_PASS" \
        -dname "CN=OGS Android, OU=OGS, O=OGS, L=, ST=, C=" >/dev/null 2>&1
fi
apksigner sign --ks "$KS" --ks-key-alias "$KS_ALIAS" --ks-pass "pass:$KS_PASS" \
    --key-pass "pass:$KS_PASS" --out "$OUT" "$WORK/app-aligned.apk"

echo
echo "DONE: $OUT"
echo "install: adb install -r $OUT"
echo "logcat:  adb logcat -s OGS     # bring-up logs"
echo "log file: /storage/emulated/0/Android/data/com.epicgames.fortnite1241/files/OGS-12.41.log"
