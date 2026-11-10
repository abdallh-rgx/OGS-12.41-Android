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

## Hook verification (angr / Z3)

`tools/verify_hooks_angr.py` symbolically verifies every runtime resolution rule the port uses
(BL/PC-relative math, ADRP page computation, trampoline dispatch), and `tools/verify_offsets.py`
Z3-proves the offset algebra (`runtime address == ImageBase + RVA`, chunked-array indexing,
FNamePool block decode) against the recorded dump values.
