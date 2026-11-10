# Erbium — Android ARM64 Port (In Progress)

Fork of [plooshi/Erbium](https://github.com/plooshi/Erbium) being ported to run **inside the Fortnite Android client (ARM64)**, so a single phone/tablet/emulator can **host and play** (LAN or solo).

> The Windows x64 codebase stays untouched on `main`. All Android work lives on the `android` branch.

---

## Architecture (Target)

```
┌─────────────────────────── Android device (ARM64) ───────────────────────────┐
│  Fortnite APK (patched via apktool)                                          │
│  ├── smali: onCreate() → System.loadLibrary("erbium")                        │
│  ├── lib/arm64-v8a/liberbium.so   ← THIS PORT (server, from Erbium C++ src)  │
│  │     • constructor → waits for libUE4.so/libUnreal.so                      │
│  │     • SDK::Init (GObjects/GNames via ARM64 anchors)                       │
│  │     • flips GIsClient/GIsServer, opens Athena_Terrain                     │
│  │     • hooks TickFlush etc. via embedded ARM64 Dobby (same approach as     │
│  │       Owen.c from Voltronite)                                             │
│  │     • listens 0.0.0.0:7777 (UDP) — LAN players join via Wi-Fi             │
│  └── lib/arm64-v8a/libowen.so     ← redirect hook (Owen.c, upstream)          │
│        • redirects Epic HTTP/EOS to backend at 127.0.0.1:3551                 │
└──────────────────────────────────────────────────────────────────────────────┘
         │ adb reverse (emulator) or same Wi-Fi
   Voltronite backend (Bun, :3551) + matchmaker (:8080) — fake Epic services
```

### Component map

| Concern | Windows x64 (upstream) | Android ARM64 (this port) |
|---|---|---|
| Entry | DLL injection → `DllMain` | `System.loadLibrary` → `__attribute__((constructor))` |
| Hooking | MinHook | Embedded ARM64 Dobby (as in Owen.c) |
| Module/scan | Memcury (WinAPI) | `/proc/self/maps` + `dl_iterate_phdr` |
| Patterns | x64 byte patterns (Finders.cpp) | offline analyzer → baked offsets (`android/Generated/*`) |
| Console/GUI | console + ImGui | logcat only (`tag: Erbium`) |
| Patches | `0xC3`/`mov eax,1` x64 | `RET`/`MOV W0,#1` ARM64 |
| Crash reporter | custom SEH | disabled v1 (tombstones via logcat) |

### Version support (v1 target)

**`18.40.0-CL-18167774`** (Chapter 2 Season 8, Nov 2021):
- Fully supported by upstream Erbium (3.4 → 19.40).
- Owen.c (Voltronite fork) already ships UE-HTTP hook offsets for `CL-18167774`.
- Outside the PartyHub problem range (12.61–15.50).
- Uptodown file id: `4114108` (142.8 MB APK).

Full matrix later: latest of every season (see `android/uptodown_versions.json` for all 370 Uptodown version ids).

---

## Repo layout (android branch)

```
android/                    build + platform layer
  CMakeLists.txt            NDK build (arm64-v8a) → liberbium.so
  src/entry.cpp             constructor, libUE4 wait loop, logcat bootstrap
  src/alog.h                __android_log wrappers
  src/win32_compat.h        Sleep/CreateThread/VirtualProtect/... shims
  src/platform.cpp          maps parsing, module base, mprotect helpers
  uptodown_versions.json    370 versions scraped from Uptodown API
scripts/
  analyze_libue4.py         offline ELF analyzer → baked offsets header
  get_apk.py                APK fetcher (direct URL / archive.org)
  patch_apk.sh              apktool decode → onCreate smali patch → build → sign
.github/workflows/
  smoke.yml                 x86_64: NDK cross-compile check (fast)
  bringup.yml               arm64: emulator + APK + crash detection (WIP)
docs/ANDROID-PORT.md        this file
```

## CI strategy

Two runner types, because tools and targets differ:

1. **`ubuntu-latest` (x86_64)** — compile `liberbium.so` with the standard NDK (NDK has no linux-arm64 host build). Fast, native.
2. **`ubuntu-24.04-arm` (ARM64)** — run the Android 10 (API 29) ARM64 emulator (QEMU, no KVM, SwiftShader GPU), install the patched APK, stream logcat, detect crashes/tombstones. Tailscale (`secrets.TAILSCALE_AUTHKEY`) lets you SSH into the runner live.

## Status

- [x] Fork with full upstream history (`main` untouched)
- [x] Actions enabled, `TAILSCALE_AUTHKEY` secret set
- [x] Android platform skeleton + NDK build
- [x] Smoke workflow (cross-compile)
- [ ] APK pipeline (analyze → patch → sign) validated on 18.40
- [ ] Erbium core compiled for Android (Finders/Hooking port)
- [ ] Emulator bring-up (app boots, liberbium loads, engine init)
- [ ] Backend integration (Voltronite + Owen.c, adb reverse)
- [ ] Solo match entry on emulator
- [ ] Multi-version matrix

---

## v2 — FULL server port (this branch: `android-port`)

The complete Erbium gameserver now compiles and runs on Android ARM64 — not
just the v1 flip-and-open milestone:

| Concern | Implementation |
|---|---|
| Entry | `entry.cpp` → `ErbiumAndroidMain()` → full `Main()` (SDK → hooks → flip → terrain) |
| SDK bootstrap | `android/erbium/SDKBootstrap.cpp` — version-string detect + baked fast-path + universal runtime chains |
| Finders | `android/erbium/Finders-Android.cpp` — the Erbium-Android **v6** catalog: 89 functions × 11 versions, version-clustered ARM64 patterns (755 verified-unique) + RT:: chains for everything else |
| Hooking | `android/erbium/HookingARM64.cpp` — native ARM64 inline hooks (trampoline + PC-relative relocation), RET/RetTrue/Nop/InvertBranch patches |
| Memcury | `android/erbium/Memcury.h` — ELF64 scanner (same API as Windows PE version) + FindFunctionCall/FindNameRef |
| Strings | UE-on-Android uses UTF-8 TCHAR — `FString` is `TArray<char>` with converting ctors; FName ctor takes `const char*` |
| Alloc/Realloc | runtime discovery of the FMemory::Realloc wrapper (GMalloc → vtable slot; slot 5 on UE4-5.2, slot 7 on UE5.3+) |
| GUI/curl | compiled out (logcat `tag:Erbium`; webhook disabled) |

### Baked versions (fast path; chains cover all others)

`5.41 · 7.40 · 9.41 · 13.40 · 17.50 · 18.40 · 19.10 · 21.30 · 22.40 · 23.50 · 30.00`

Each build bakes one version (`-DERBIUM_TARGET_VERSION=…`); at runtime the
library verifies the loaded game's release string — if it differs, every
offset falls back to the runtime chains (string anchors → wrapper scans →
ADRP extraction), so ONE binary works on any 5.x→30.00 build.

### Build

```bash
cmake -B build -S android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DERBIUM_TARGET_VERSION=21.30
cmake --build build
```

CI: `.github/workflows/android-full-server.yml` builds all 11 versions and
uploads per-version artifacts.

### The mining data

`android/Generated/sdk-*.h` are generated from real-binary analysis
(11 reference ARM64 builds from the public FNAndroidBuildsArchive):
`android/erbium/bake.py` consumes the mining JSON + the v6 catalog.
