# PORTING.md — the 1:1 port map (PC OGS 12.41 → Android ARM64)

Every functional element of the PC source (`upstream/OGS-12.41-main/`) and how it
maps to this repository. The PC's `ImageBase + 0x…` hardcoded offsets become
**PC-RVA keys** resolved at runtime by `src/NativeResolver.cpp`.

## Entry & flow (dllmain.cpp → src/main.cpp)

| PC | Android |
|---|---|
| `DllMain` → `CreateThread(Main)` | `System.loadLibrary("OGS-12.41")` → `JNI_OnLoad` **only** → worker thread (single-shot guard). **No `__attribute__((constructor))`** — constructors run under the linker's write lock inside `call_constructors`, before libUE4.so exists in this process (see the tombstone post-mortem below) |
| `InitConsole` (AllocConsole + `OGS_log.txt`) | `Log()` → logcat (`-s OGS`) + `/storage/emulated/0/Android/data/com.epicgames.fortnite1241/files/OGS-12.41.log` |
| `WaitForLogin` (Frontend + InProgress) | identical (SDK-resolved, no offsets) |
| `Hook()` (all modules) | identical, via `OGS_HOOK(pcRva, …)` |
| `*(bool*)(ImageBase+0x804B659/0x804B65A)` GIs flip | `FindGIsClient()/FindGIsServer()` (Erbium-Android v6 chains) |
| `LoadWorld` → `open Apollo_Terrain` + `LocalPlayers.Remove(0)` | `open Apollo_Terrain` (identical); **`LocalPlayers.Remove(0)` NOT ported — Listen Server: the host keeps playing** (`NM_ListenServer`), `ListenServer::Setup()` opens the NetDriver instead |
| — | `Gui::InitTouchDetection()` + `Gui::InitRenderer()` (4-finger/2s gesture → menu) |

## Internal functions (PC RVA → Android resolution)

`src/NativeResolver.cpp` — the resolution table. Rules: **Known** (verified
Android RVA), **Erbium** (v6 runtime-chains finder), **String** (UE 4.25
log-string anchor), **VtNeighbor/thunk** (vtable/native-thunk chain).

| PC RVA | Function | Rule |
|---|---|---|
| 0x2E13BF0 | ProcessEvent | **Known** — `0x79ECBFC` (verified: dump + OffsetsInfo + MobileDumper-7 log "PEOffset") |
| 0x4573990 | CreateNetDriver | Erbium `FindCreateNetDriverWorldContext` |
| 0xD44C40 | InitListen | Erbium `FindInitListen` |
| 0x42C2B20 | SetWorld | Erbium `FindSetWorld` |
| 0x6B19E0 | GiveAbility | Erbium `FindGiveAbility` |
| 0x6D6DD0 | AbilitySpecConstructor | Erbium `FindConstructAbilitySpec` |
| 0x6B33F0 | InternalTryActivateAbility | Erbium `FindInternalTryActivateAbility` |
| 0x6B1B00 | GiveAbilityAndActivateOnce | Erbium `FindGiveAbilityAndActivateOnce` |
| 0x18848F0 | PickSupplyDropLocation | Erbium `FindPickSupplyDropLocation` |
| 0x1B951B0 | ReplaceBuildingActor | Erbium `FindReplaceBuildingActor` |
| 0x1E57790 | CantBuild | Erbium `FindCantBuild` |
| 0x2363FF0 | ApplyCharacterCustomization | Erbium `FindApplyCharacterCustomization` |
| 0x18ECBB0 | RemoveFromAlivePlayers | Erbium `FindRemoveFromAlivePlayers` |
| 0x45C9D90 | WorldGetNetMode | Erbium `FindGetNetMode` |
| 0x4155600 | KickPlayer | Erbium `FindKickPlayer` |
| 0x18E6B20 | PickTeam | Erbium `FindPickTeam` |
| 0x4576310 | TickFlush | Erbium `FindTickFlush` |
| 0x42C3ED0 | ServerReplicateActors | Erbium `FindServerReplicateActors` |
| 0x1023F60 | GetMaxTickRate | Erbium `FindGetMaxTickRate` |
| 0x18F9BB0 | StartAircraftPhase | Erbium `FindStartAircraftPhase` |
| 0x1B46D00 | SpawnLoot | Erbium `FindSpawnLoot` |
| 0x3FF94E0 | RegisterComponentWithWorld | **String** — verified UE 4.25 anchor |
| 0x2E1DD00/0x2E1DD30 | FFrame::StepCompiledIn (both) | thunk chain (optional — Inventory only) |
| 0x2E1C4B0/0x2E1D7A0 | StaticFindObject/StaticLoadObject | thunk chain |
| 0xD446F0 / 0x20DFAD0 | InitHost / PauseBeaconRequests | best-effort (declared but never called on PC) |
| 0x18C35B0, 0x19D93F0, 0x108D740, 0x2DBCBA0, 0x1E23840, 0x3262100, 0x2D95DC0 | Fortnite internals | best-effort string anchors + RT chains (each logs its resolution) |
| PC.h / module hooks (0x29B5C80, 0x1C8EA00, 0x1583360, 0x22A08C0, 0x197F6D0, 0x1B6C232, 0x6853B0, 0x18F6250, 0x18E07D0, 0x18E0730, 0x18FD350, 0x163C760, 0x163C300, 0x1637F50, 0x49932C0, 0x260C490, 0x1E69600, 0x1CFC810, 0x1CFC8D0, 0x4640A30 …) | module internals | Erbium finders + vtable/thunk chains (resolution table, each logged) |

Every UFunction call (99% of the module code) resolves through the SDK's
`StaticClass()->GetFunction()` + `ProcessEvent` (vtable idx 0x43) — identical
to the PC SDK mechanics, no per-function offsets needed.

## Platform adaptations

| PC | Android |
|---|---|
| `wchar_t` / `L""` | `char16_t` / `u""` (UE-on-Android 12.41 TCHAR is UTF-16 — confirmed by the on-device dump) |
| `std::wstring` | `std::u16string` |
| MinHook `MH_CreateHook` | `OGS_HOOK(pcRva,…)` → `Hooking::InternalHook` (ARM64 inline hooks w/ trampoline + relocation — `platform/erbium/HookingARM64.cpp`) |
| `MH_EnableHook(MH_ALL_HOOKS)` | hooks install enabled |
| Windows APIs (`Sleep`, `VirtualProtect`, `__int64`, `MultiByteToWideChar`, …) | `platform/erbium/compat/win32_compat.h` |
| `_ReturnAddress()` | `__builtin_return_address(0)` |
| `unsigned __int64` etc. | `uint64_t` (transformed by `scripts/port_modules.py`) |
| `AllocConsole`/`printf` | logcat + on-device log file |
| console-less server GUI | **in-game GUI** (ImGui over GLES2, `eglSwapBuffers` hook) with 4-finger/2s touch gesture (`AInputQueue_getEvent` hook) |
| dedicated server (LocalPlayer removed) | **listen server** (LocalPlayer kept, `InitListen` 0.0.0.0:7777, join by IP from the GUI) |

## SDK adaptations (SDK/ — the PC Dumper-7 SDK, patched)

- `SDK/SDK/Basic.hpp` — Android offsets (`GObjects=0x10249200`, `GNames=0x101B5EC0`,
  `GWorld=0x10191070`, `ProcessEvent=0x79ECBFC`, vtable idx `0x43`), module base
  via `dl_iterate_phdr`, chunked `TUObjectArray` (+0x10 pad), `FNamePool`
  decode via GNames, real Android `FField`/`FProperty` layout (FField 0x30 —
  no ObjFlags; FProperty 0x70; ArrayDim@0x30/ElementSize@0x34/Flags@0x38/Offset@0x44),
  `#undef EM_MAX` (bionic errno collision), InvalidUseOfTDelegate completeness.
- `SDK/UnrealContainers.hpp` — `FString : TArray<char16_t>` on Android, libc
  allocator fallback + `OGS_SetEngineMemoryFunctions`, TSet friend fix.
- `SDK/PropertyFixup.hpp` — portable `uint8_t`.
- `SDK/SDK/split/` — the huge SDK function TUs split into memory-sized parts
  (`scripts/split_sdk_functions.py`).
- `scripts/fix_tail_padding.py` — auto-fixes the Itanium tail-padding-reuse
  divergence (forces dsize==sizeof on bases so reflected offsets match the
  real Android binary layout).

## Verification (all green)

- `tools/verify_offsets_z3.py` — 34 Z3 proofs (offset algebra vs the on-device dump).
- `tools/verify_hooks_angr.py` — angr symbolic proofs that the **compiled**
  ARM64 decoders/encoders match the ARM spec for **all** encodings.

## Tombstone post-mortem — the sinum init model (merged)

Two device tombstones against `com.epicgames.fortnite1241` drove the init
rework; `libsinum.so` (loaded in the same APK) never crashed, and the fix is
precisely sinum's init model:

1. **Tombstone #1 (deadlock)** — a worker spawned from an
   `__attribute__((constructor))` ran `dlopen`/module resolution while the
   dynamic linker still held its write lock in `call_constructors`;
   `libsinum.so`'s own `dlopen` blocked forever on the same lock (futex in
   `NonPI::MutexLockWithTimeout`).
2. **Tombstone #2 (SIGSEGV `[0+0x130]`)** — the PC-port static initializers
   (Bosses.h's `auto GameState = UWorld::GetWorld()->GameState;` etc.,
   `Tick.h`'s `OGS_RESOLVE(...)` pointer, PC.h's
   `GetModuleHandleA(0) + RVA`) executed inside `_GLOBAL__sub_I_main.cpp`
   at `dlopen` time — **before `libUE4.so` is even mapped** (the patched
   APK loads us as the first statement of `GameActivity.onCreate`).
   `GetWorld()` returned null → `ldr x8, [x0, #0x130]` → SEGV. On PC these
   ran at DLL attach **into an already-running game** — that is the one
   semantic difference of the 1:1 port, now closed.

**The model now (verified on the built .so — `.init_array` contains data
construction only, zero engine/dlopen calls):**

- `JNI_OnLoad` is the ONLY load-time entry (ART calls it after `dlopen`
  returns — the linker lock is released). No constructor attribute.
- The worker first **polls `/proc/self/maps`** for `libUE4.so`/`libUnreal.so`
  (pure file reads — no `dl_iterate_phdr`, no linker lock), then waits for
  `UEngine::GetEngine()`, and only then resolves/initializes anything.
- The PC attach-time initializers are deferred to
  `Bosses::Init()` / `Tick::Hook()` / the `PC.h` vtable capture — called
  from `Main()` after the engine is up, in the PC order.
- **sinum coexistence on the same hooks**: `HookingARM64::InternalHook`
  detects an already-patched target (`[LDR X17,#8][BR X17][.quad prev]` or
  a far `B` veneer) and **chains** — it swaps only the detour quad and
  hands the caller the previous detour as the "original" (our detour →
  sinum's detour → its trampoline → the real function), instead of
  clobbering sinum's bytes. Proven by angr/Z3 V8
  (`OGS_Hook_IsExistingDispatch` ⇔ the dispatch-pattern equality, all
  word pairs).
- Latent resolver bugs fixed with it: the Tick rule-key shift
  (`TickFlush=0x42C3ED0`, `GetMaxTickRate=0x4576310`,
  `ServerReplicateActors=0x1023F60` — verified against the PC original),
  the SpawnBot key typo (`0x19E9B10`, was `0x018E9B10`), plus documented
  Zero rules for the Misc.h nullFunc site (`0x3CA10C0`) and the SpawnBot
  caller check (`0x1A4153F`).
