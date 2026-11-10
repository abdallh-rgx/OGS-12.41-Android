# OGS-12.41-Android

**1:1 Android (ARM64) port of OGS 12.41** — the PC Fortnite v12.41 game server — as an injectable
`libOGS-12.41.so` loaded by the Fortnite Android 12.41 APK (`System.loadLibrary("OGS-12.41")` in `onCreate`).

The host device **hosts and plays at the same time** (UE `NM_ListenServer` semantics — the LocalPlayer
is kept alive, unlike the PC dedicated build which destroys it).

## Feature summary

| PC (OGS 12.41) | This port (Android ARM64) |
|---|---|
| DllMain + CreateThread | `__attribute__((constructor))` + `std::thread` (via `System.loadLibrary`) |
| MinHook | **Dobby** (`third_party/dobby/libdobby-arm64.a`) — the exact engine libsinum used crash-free on-device; direct patches (RET/RetTrue/NOP/branch-invert) stay native |
| `ImageBase + 0x…` hardcoded offsets | Runtime resolution: SDK anchors (verified `GObjects/GNames/GWorld/ProcessEvent`) + Erbium-Android v6 RT chains (work on any 5.x→30.00 build) |
| Console log + `OGS_log.txt` | logcat (`OGS`) + `/storage/emulated/0/Android/data/com.epicgames.fortnite1241/files/OGS-12.41.log` |
| `AllocConsole` GUI-less server | **In-game English GUI** — press the screen with **4 fingers for 2 seconds** to open; pick any game mode, or **join another player by IP** |
| Dedicated server (`GIsServer`, LocalPlayer removed) | **Listen server** — `NM_ListenServer`: host + play; friends join over Wi-Fi/LAN by IP |
| (separate libsinum.so redirect) | **sinum merged in** — `curl_easy_setopt` hook redirects Epic traffic (ol/ak/on.epicgames.com, *.akamaized.net) to the private server + disables SSL verify (`src/Sinum.{h,cpp}`) |

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
proves the **compiled** ARM64 decode/encode math (the exact RT decoders + patch encoders the
runtime uses, exported by `src/Verification.cpp`) equivalent to the ARM architecture
specification for **all** encodings: IsBL/IsB/IsADRP classifiers, BranchTarget / ADRPPage /
ADRImm PC-relative math, and the hook dispatch encoding (`LDR X17,#8 ; BR X17 ; .quad detour`).

**Regression suite (R1–R3, pure Z3)** — the mathematical post-mortem of the v1 inline-hook
engine that crashed the game on-device (see the root-cause section below):

```
python3 tools/verify_hooks_angr.py --regression-only          # R1–R3 — all PASS (no angr needed)
python3 tools/verify_hooks_angr.py build/libOGS-12.41.so      # V1–V7 + R1–R3
python3 tools/verify_offsets_z3.py                            # 34 proofs — all PASS
```

## Root cause: why the first on-device run crashed (tombstone post-mortem)

Three independent faults, all fixed by the sinum merge:

1. **Two libraries, two hook engines, one `libUE4.so`.** `libsinum.so` (Dobby) and
   `libOGS-12.41.so` (custom v1 engine) both inline-hooked the same library. Proof **R3**:
   a second engine re-hooking an entry the first one already patched relocates the first
   engine's dispatch *as if it were code* — the resulting jump destination is provably
   neither the first detour nor the original code → wild jump → crash.
2. **The v1 engine's ADRP relocation was wrong.** Its private decoder dropped `immlo`
   (bits 30:29), so 3 out of 4 ADRPs relocated to a page `immlo × 0x1000` bytes off —
   any trampoline call through such an ADRP dereferenced a wild address (`ProcessEvent`
   alone calls its trampoline on *every* event). Proofs **R1a–R1c** (witness + exact
   diagnosis: the bug is dormant only when `immlo == 0`). The V1–V7 suite had verified
   `RT.h`'s decoder — the engine carried its own private, buggy copy.
3. **A loader deadlock.** sinum's constructor thread called `dlopen("libUE4.so", RTLD_NOW)`
   immediately at library load — racing the main thread's own loading (visible in the
   tombstone: sinum's worker stuck in `dlopen` → linker `MutexLockWithTimeout`, while
   `libOGS`'s worker waits for the engine). Adding `libOGS-12.41.so` to the APK shifted
   the timing and tipped the race into the wedge.

The merge (single library, single Dobby engine, `RTLD_NOLOAD` after engine-up, no
constructor-time `dlopen`) removes all three. **Remove `libsinum.so` from the APK** —
`libOGS-12.41.so` now performs the redirect itself (same host, same rules; change it in
`src/Sinum.cpp` → `kRedirectHost`).

## Inject into the APK

```bash
tools/patch_apk.sh Fortnite-12.41.apk build/libOGS-12.41.so OGS-12.41-patched.apk
adb install -r OGS-12.41-patched.apk
adb logcat -s OGS
```

**If your APK previously shipped `libsinum.so`: delete `lib/arm64/libsinum.so` and the
`System.loadLibrary("sinum")` line** — both jobs (Epic-traffic redirect + game-server hooks)
are now done by `libOGS-12.41.so` alone. Two libraries hooking `libUE4.so` = guaranteed
corruption (proof R3).

## Status

- [x] Full Dumper-7 SDK compiles for Android ARM64 (5,617 files, verified offsets)
- [x] All 21 OGS PC modules ported 1:1 (hooks keyed by PC RVA, resolved at runtime)
- [x] Listen server (NM_ListenServer — host keeps playing, 0.0.0.0:7777)
- [x] 4-finger / 2-second touch gesture → English GUI (mode select + join by IP)
- [x] Log file: `/storage/emulated/0/Android/data/com.epicgames.fortnite1241/files/OGS-12.41.log`
- [x] CI: GitHub Actions builds on every push → artifact `libOGS-12.41-arm64`
- [x] Z3 + angr mathematical verification suites (all passing)
- [x] **sinum merged in** — single library, single hook engine (Dobby), redirect included
- [x] First on-device crash root-caused (R1–R3) and fixed
- [ ] On-device re-test of the merged build (drop the new artifact into the APK)
