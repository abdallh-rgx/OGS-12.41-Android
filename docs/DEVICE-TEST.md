# Erbium Android — Device Test Guide / دليل الاختبار على الجهاز

**Status / الحالة:** v1 stack is CI-green through launch. The patched APK now
contains everything a full match needs:

1. `System.loadLibrary("erbium")` in the launcher `onCreate`
2. **The 4-finger console** — `GameActivity.dispatchTouchEvent` sees 4 pointers
   → `forceShowConsoleWindow_test()` → `AndroidThunkJava_ShowConsoleWindow`
   (اضغط الشاشة بأربع أصابع معاً ويظهر الكونسول)
3. Owen.c redirect → Voltronite (runtime-configurable, see below)
4. Erbium bootstrap: engine wait → **GO gate** → `GIsClient=false /
   GIsServer=true` → `open Artemis_Terrain`

---

## Runtime config files (adb, no rebuild) / ملفات الإعداد وقت التشغيل

| File on device | Purpose |
|---|---|
| `/data/local/tmp/erbium.backend` | Backend URL the game redirects to. **Emulator + backend on the PC →** `adb shell "echo http://10.0.2.2:3551 > /data/local/tmp/erbium.backend"`. **Voltronite in Termux on the phone →** `http://127.0.0.1:3551`. Missing = compiled-in default (127.0.0.1:3551). |
| `/data/local/tmp/erbium.go` | **GO signal.** Erbium will NOT flip/open the map until this file exists — login must happen first, otherwise the map loads with no player (لا لاعب = لا يُحتسب). Touch it once you are logged in and sitting in the lobby. |

---

## One-command full test on YOUR phone / أمر واحد للاختبار الكامل على هاتفك

With the phone connected via adb (USB or Wi-Fi) and Voltronite running
(Termux on the phone, or on your PC with `adb reverse tcp:3551 tcp:3551`):

```bash
# from the repo root, on your PC
ADB_SERIAL=<device-serial> bash scripts/android/gameflow_test.sh \
    --apk fortnite-erbium-patched.apk \
    --backend http://127.0.0.1:3551 \
    --user YourName
```

The driver does the WHOLE human flow, in order — and never force-stops the
game (خروج جزئي فقط، بدون خروج كامل):

1. installs + launches
2. waits for the **internal update to complete** (backend traffic goes quiet;
   tolerates the game restarting itself)
3. **login**: backgrounds the game (HOME, still alive) → opens
   `http://<backend>/login` in Chrome → types the username → taps
   *Continue* → the page fires `com.epicgames.fortnite://authorize/?code=...`
   → the game returns to the foreground **by itself** → logged in
   (اضغط login ثم ارجع للعبة بدون أن تفعل شيئاً)
4. waits for the lobby to settle
5. `touch /data/local/tmp/erbium.go` → Erbium flips the flags + opens the map
6. waits for the **match internal loading to COMPLETE** (loading screens
   animate; the match counts only once the scene renders — loading frames,
   black voids and under-the-map views DO NOT COUNT)
7. **player verification**: in-match screenshot + brightness/color analysis +
   4-finger console `getall PlayerController Location` / `Pawn` (position
   evidence in `artifacts/shots/`)
8. prints a PASS/FAIL verdict; everything lands in `artifacts/`

Every screenshot, the full logcat and a digest are kept in `artifacts/` —
attach them when reporting results.

### Notes
* On an unrooted phone the automated 4-finger tap may not be possible —
  the driver warns and you can tap with 4 real fingers when it reaches
  phase 7. On a rooted phone / `google_apis` emulator it uses `sendevent`.
* Already logged in from a previous run? Add `--skip-login`.
* The same script runs in CI (`android-bringup` workflow) against the
  emulator with `--backend http://10.0.2.2:3551`.

---

## Manual milestones in logcat / المعالم في logcat

```bash
adb logcat -c && adb logcat -s Erbium Erbium-Owen
```

```
Erbium  : === Erbium Android bootstrap ===
Erbium  : engine module: libUnreal.so base=0x...
Erbium-Owen: UE hooks installed: ProcessRequest=0x89f02f0 ...
Erbium  : release probe OK: ++Fortnite+Release-21.30-CL-21088273
Erbium-Owen: redirect: https://...epicgames.com/... -> http://127.0.0.1:3551/...   ← every backend request
Erbium  : GEngine = 0x...
Erbium  : waiting for GO file: /data/local/tmp/erbium.go       ← armed; log in now
Erbium  : GO received — settling 3s before flag flip...
Erbium  : POST-FLIP: GIsClient=0 GIsServer=1   ← the process is now a game server
Erbium  : dispatching console command: open Artemis_Terrain
```

## Manual login flow (if you prefer hands-on) / تسجيل الدخول يدوياً

1. Launch the game, let the internal update finish (لا تخرج من اللعبة)
2. Open Chrome → `http://127.0.0.1:3551/login` (or `http://10.0.2.2:3551/login`
   on an emulator whose backend runs on the PC)
3. Type a username → **Continue** → the game opens by itself
4. Go back to the game — you are logged in
5. `adb shell touch /data/local/tmp/erbium.go` → the flip + map open happens
6. 4-finger press → console → `getall PlayerController Location` to verify
   the pawn position

## Report back / أرسل لنا

Attach: `artifacts/VERDICT.txt`, `artifacts/logcat-digest.txt` and the
`artifacts/shots/` folder (especially `ingame_pov_*`, `console_getall_*`).
