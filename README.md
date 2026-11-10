# OGS-12.41-Android

**1:1 Android (ARM64) port of OGS 12.41** — the PC Fortnite v12.41 game server — as an injectable
`libOGS-12.41.so` loaded by the Fortnite Android 12.41 APK (`System.loadLibrary("OGS-12.41")` in `onCreate`).

The host device **hosts and plays at the same time** (UE `NM_ListenServer` semantics — the LocalPlayer
is kept alive, unlike the PC dedicated build which destroys it).

## Feature summary

| PC (OGS 12.41) | This port (Android ARM64) |
|---|---|
| DllMain + CreateThread | `__attribute__((constructor))` + `std::thread` (via `System.loadLibrary`) |
| MinHook | Native ARM64 inline hooks (`platform/erbium/HookingARM64.cpp`, trampoline + PC-relative relocation) |
| `ImageBase + 0x…` hardcoded offsets | Runtime resolution: SDK anchors (verified `GObjects/GNames/GWorld/ProcessEvent`) + Erbium-Android v6 RT chains (work on any 5.x→30.00 build) |
| Console log + `OGS_log.txt` | logcat (`OGS`) + `/storage/emulated/0/Android/data/com.epicgames.fortnite1241/files/OGS-12.41.log` |
| `AllocConsole` GUI-less server | **In-game English GUI** — press the screen with **4 fingers for 2 seconds** to open; pick any game mode, or **join another player by IP** |
| Dedicated server (`GIsServer`, LocalPlayer removed) | **Listen server** — `NM_ListenServer`: host + play; friends join over Wi-Fi/LAN by IP |

## Verified 12.41 Android offsets (from on-device dump, `SDK-AndroidReference/MobileDumper7.log`)

```cpp
GObjects       = 0x10249200   // FChunkedFixedUObjectArray, Objects @ +0x10, chunks of 0x10000, FUObjectItem 0x18
GNames         = 0x101B5EC0   // FNamePool, Blocks @ +0x40, BlocksBit = 16, entry stride 2, UTF-16 supported
GWorld         = 0x10191070
GEngine        = 0x0DFDA008
ProcessEvent   = 0x079ECBFC   // UObject vtable index 0x43 (67)
```

## Build (GitHub Actions does this for you)

The `.github/workflows/android-build.yml` workflow builds on every push and uploads
`libOGS-12.41.so` (ARM64, NDK r27c) as an artifact.

Local build:

```bash
cmake -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j 2
```

## Inject into the APK

```bash
tools/patch_apk.sh Fortnite-12.41.apk libOGS-12.41.so   # apktool → loadLibrary patch → sign
```

## Layout

```
SDK/            Dumper-7 SDK of the PC 12.41 build (parameterized) — patched for Android
                (Basic.hpp offsets/TUObjectArray/FName-pool + UnrealContainers FString char16_t)
SDK-AndroidReference/  On-device 12.41 dump: logs, GObjects dump, NameIndices, usmap/idmap
src/            The 1:1 ported OGS modules + entry + ListenServer + 4-finger GUI
platform/       Erbium-Android v6 runtime: RT chains, Memcury, ARM64 hooking, win32 compat
tools/          APK patcher, anchor analyzer, angr/Z3 hook verification
upstream/       Untouched PC reference source (OGS-12.41)
```

## Verification — angr + Z3 (all green)

**`tools/verify_offsets_z3.py`** — 34 Z3 proofs of the offset algebra against the recorded
on-device dump values (all `unsat` — no counterexamples): runtime addresses, chunked
TUObjectArray bijection + bounds, FNamePool block decode (spot-checked against every
NameIndices.h entry), FName entry header math, ProcessEvent vtable slot, FProperty layout.

**`tools/verify_hooks_angr.py`** — angr loads the **built `libOGS-12.41.so`** and symbolically
proves the **compiled** ARM64 hook engine (the exact RT decoders + encoders the runtime uses,
exported by `src/Verification.cpp`) equivalent to the ARM architecture specification for **all**
encodings: IsBL/IsB/IsADRP classifiers, BranchTarget / ADRPPage / ADRImm PC-relative math,
and the hook dispatch encoding (`LDR X17,#8 ; BR X17 ; .quad detour`).

```
python3 tools/verify_offsets_z3.py                 # 34 proofs — all PASS
python3 tools/verify_hooks_angr.py build/libOGS-12.41.so   # V1–V7 — all PASS
```

## Inject into the APK

```bash
tools/patch_apk.sh Fortnite-12.41.apk build/libOGS-12.41.so OGS-12.41-patched.apk
adb install -r OGS-12.41-patched.apk
adb logcat -s OGS
```

## Status

- [x] Full Dumper-7 SDK compiles for Android ARM64 (5,617 files, verified offsets)
- [x] All 21 OGS PC modules ported 1:1 (hooks keyed by PC RVA, resolved at runtime)
- [x] Listen server (NM_ListenServer — host keeps playing, 0.0.0.0:7777)
- [x] 4-finger / 2-second touch gesture → English GUI (mode select + join by IP)
- [x] Log file: `/storage/emulated/0/Android/data/com.epicgames.fortnite1241/files/OGS-12.41.log`
- [x] CI: GitHub Actions builds on every push → artifact `libOGS-12.41-arm64`
- [x] Z3 + angr mathematical verification suites (all passing)
- [ ] On-device smoke test on a real 12.41 install (needs the APK + device)
