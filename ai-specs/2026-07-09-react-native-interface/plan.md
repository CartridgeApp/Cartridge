# Cartridge: React Native Interface for RetroArch

**Date:** 2026-07-09
**Status:** Locked — ready to execute

## 1. Goal

Replace RetroArch's built-in menu UI (XMB/Ozone/etc.) with a custom UI built in
React, while keeping RetroArch's emulation, video/audio drivers, and core
ecosystem completely intact and at full performance. One UI codebase should
serve:

1. **Android** (primary — Android handhelds are the target market)
2. **iOS** (phase 2)

Web/desktop rendering is tracked in a separate plan and is out of scope for
this document.

Cartridge is a **plugin invoked by an external launcher**, not a standalone
app with its own ROM library. The launcher owns content browsing/library UI
and tells Cartridge which core + content to load; Cartridge owns emulation,
the in-game quick menu, and settings. There is no ROM browser in scope here.

RetroArch's native UI is never rendered. Boot goes straight into our React UI;
gameplay renders through RetroArch's unmodified video pipeline; our menus
composite transparently above it.

## 2. Decisions locked in

| Decision | Choice |
|---|---|
| Platform order | Android → iOS. Every layer designed to be portable; web/desktop is a separate plan |
| Screen topology | **React Native owns the app.** RetroArch runs as a library rendering into a `SurfaceView` inside the RN view hierarchy; RN views composite above it (hardware compositor — zero cost to the emulator frame loop) |
| App model | **Launcher plugin, not a standalone app.** No ROM browser/library UI in Cartridge — an external launcher owns content discovery and tells Cartridge which core + content to load. Cartridge owns emulation, the in-game quick menu, and settings |
| FFI source of truth | **Hybrid codegen.** Settings catalog auto-generated from RetroArch's own data-driven tables; imperative command surface hand-curated in a small IDL |
| Dev workflow | **Expo custom dev client is a must-have.** Native side built once; UI iterated with fast refresh. Plus a TypeScript mock of the FFI for browser-only UI work |
| Upstream strategy | Fork stays mergeable. New code lives in an additive `cartridge/` tree; unavoidable edits to upstream files are tiny, `#ifdef`-guarded, and reproducible (extending the [customize-fork.sh](../../customize-fork.sh) philosophy) |
| Game input | Activity-level hotkey table intercepts a small set of reserved key combos (menu toggle, quick-menu summon) before they reach RetroArch; everything else forwards unmodified into the stock Android input driver, keeping controller hotplug/mapping logic upstream |
| Settings catalog generation | Runs from a **host-built headless x86_64 binary** (no emulator/device needed). Same binary is used locally (devs regenerate the committed snapshot after adding/changing a setting or merging upstream) and in CI (fails if the committed snapshot has drifted) |
| Config persistence | `retroarch.cfg` remains the single source of truth. RN reads/writes go through RetroArch's existing config system — no separate store, no sync layer |
| Quick-menu viewport capture | Captured once when the quick menu opens (on pause), not per-frame — avoids steady-state readback overhead |
| Storage access | Cartridge requests **full filesystem access** (not scoped storage/SAF). Not targeting Play Store, so no scoped-storage constraint. On boot, if access isn't granted, Cartridge guides the user to grant it before attempting to load content |

## 3. Current-state findings (what we're building on)

- **Android build:** ndk-build unity build. [pkg/android/phoenix-common/jni/Android.mk](../../pkg/android/phoenix-common/jni/Android.mk)
  compiles `griffin/griffin.c` + `griffin_cpp.cpp` into `libretroarch-activity.so`.
  The APK is the Gradle project in [pkg/android/phoenix/](../../pkg/android/phoenix/).
- **Activity model:** `RetroActivityCommon extends NativeActivity`
  ([pkg/android/phoenix-common/src/com/retroarch/browser/retroactivity/RetroActivityCommon.java](../../pkg/android/phoenix-common/src/com/retroarch/browser/retroactivity/RetroActivityCommon.java)),
  entry point `ANativeActivity_onCreate`. All lifecycle/window/input callbacks are
  handled in [frontend/drivers/platform_unix.c](../../frontend/drivers/platform_unix.c)
  via android_native_app_glue-style plumbing (`onNativeWindowCreated`, input queue, looper).
- **Imperative surface:** `command_event(enum event_command, void*)`
  ([command.h:351](../../command.h#L351)) with ~144 `CMD_EVENT_*` actions (load/close
  content, save states, screenshots, shader toggles, pause, reset, menu toggle…).
  There is also a string-based network command map in [command.c](../../command.c)
  (`action_map`) proving these operations are already callable from outside the menu.
- **Settings are data:** [configuration.c](../../configuration.c) builds
  bool/int/float/string tables (`populate_settings_bool/int/float/...`) mapping config
  keys → `settings_t` fields → defaults. `menu_setting_new()`
  ([menu/menu_setting.c:18275](../../menu/menu_setting.c#L18275)) constructs the full
  `rarch_setting_t` list — every setting with its type, range, step, default, enum
  values, and display strings — **enumerable at runtime**. This is the codegen goldmine.
- **Async operations are tasks:** core downloads ([tasks/task_core_updater.c](../../tasks/task_core_updater.c)
  + [core_updater_list.c](../../core_updater_list.c)), screenshots
  ([tasks/task_screenshot.c](../../tasks/task_screenshot.c)), content loading — all run
  on RetroArch's task queue with progress/completion callbacks we can hook.
- **Menu is compile-gated:** `HAVE_MENU` guards the entire menu system, so a
  menu-less build is an already-supported configuration.

## 4. Target architecture

```
┌─ Android APK ──────────────────────────────────────────────────┐
│  MainActivity (ReactActivity, Kotlin)                          │
│  launched by an external launcher's Intent (core + content     │
│  path extras); hotkey table intercepts reserved key combos      │
│  before they'd otherwise forward to RetroArch                   │
│  ┌─ React Native view tree ─────────────────────────────────┐  │
│  │   <RetroArchSurface/>   ← SurfaceView; RetroArch's        │  │
│  │                           GL/Vulkan driver renders here    │  │
│  │   <QuickMenu/> <Settings/>  ← React, composited above,     │  │
│  │                                transparent                 │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                 │
│  CartridgeModule (C++ TurboModule, JSI)                         │
│        │  generated bindings (one C++ impl for Android + iOS)   │
│        ▼                                                        │
│  cartridge_api  (hand-curated C shim, compiled with RetroArch   │
│        │         headers → upstream drift = compile error)      │
│        ▼                                                        │
│  libretroarch.so  (griffin build, RetroArch runloop on its own  │
│                    thread; commands marshaled onto it)          │
└─────────────────────────────────────────────────────────────────┘
```

### 4.1 Repo layout & upstream divergence strategy

```
cartridge/                      ← NEW, top-level, never touched by upstream merges
  api/
    cartridge.idl.ts            ← curated IDL for the imperative surface
    settings-catalog.json       ← generated snapshot of the settings catalog
    codegen/                    ← the generator (Node/TS)
  native/
    cartridge_api.h / .c        ← C shim: includes RetroArch headers, calls internals
    cartridge_events.c          ← event sink (C → JS event bridge)
    turbomodule/                ← generated + hand-written C++ JSI module
  app/                          ← Expo app (RN UI, prebuild config, dev client)
  android/                      ← Gradle glue: NDK build wiring, Kotlin host classes
  ios/                          ← (phase 2)
ai-specs/                       ← this plan and future specs
```

Rules for keeping the fork mergeable:

1. **Additive-first.** All Cartridge code lives under `cartridge/`. The NDK build
   *includes* upstream's `Android.mk` logic rather than editing it (our own
   `cartridge/android/jni/Android.mk` sets the same variables and appends our
   sources; if upstream's makefile drifts we take a small, obvious merge hit in
   one file).
2. **Guarded hooks.** Where we must touch upstream C (an event-sink hook, a
   library-mode entry tweak), the edit is a few lines wrapped in
   `#ifdef HAVE_CARTRIDGE`, so a diff against upstream shows exactly our
   footprint, and upstream merges rarely conflict.
3. **Drift detection is compile-time + CI.** `cartridge_api.c` includes real
   RetroArch headers and calls real internals — if upstream renames
   `command_event` or changes `rarch_setting_t`, **the build breaks loudly**.
   The settings catalog gets a CI job (see 4.4) that fails when the runtime
   catalog no longer matches the committed snapshot.
4. **customize-fork.sh stays the reproducible-deviation ledger** for
   non-code customizations (CI workflow pruning etc.).

### 4.2 Library-mode RetroArch on Android (the hard part)

Today RetroArch *is* the activity. We invert that: a plain Kotlin
`ReactActivity` hosts everything, and we drive RetroArch as a guest.

- **Entry:** RetroArch's real entry is effectively `rarch_main(argc, argv,
  android_app*)`. We add a small library-mode bootstrap in
  `cartridge_api.c` that constructs the `android_app` state RetroArch expects
  and runs the runloop on a dedicated **emulation thread** (RetroArch already
  assumes single-threaded ownership of its globals; we never call into it from
  other threads directly).
- **Window:** The RN view tree contains a `SurfaceView` (exposed to JS as
  `<RetroArchSurface/>`, a Fabric native component). Its `SurfaceHolder`
  callbacks call into the shim (`ANativeWindow_fromSurface`) and dispatch the
  equivalent of `APP_CMD_INIT_WINDOW` / `APP_CMD_TERM_WINDOW` /
  `APP_CMD_WINDOW_RESIZED` to RetroArch's looper — replacing what
  `NativeActivity` did, feeding the same code paths in
  [platform_unix.c](../../frontend/drivers/platform_unix.c). Video/audio drivers
  (GLES/Vulkan, AAudio/OpenSL) are untouched → **zero performance change** for
  gameplay; Android's hardware compositor layers RN views above the surface for
  free.
- **Lifecycle:** Activity `onPause/onResume/onDestroy` forward to the same
  command dispatch (`APP_CMD_PAUSE`, …). Surface destruction/recreation
  (rotation, backgrounding) is the highest-risk seam and gets a dedicated
  test matrix in M1.
- **Input:** Two paths, by design:
  - *Menu input* never reaches RetroArch — RN handles touches natively (views
    are above the surface).
  - *Game input* (touch overlay gamepad, physical controllers): the Activity's
    `dispatchKeyEvent`/`onGenericMotionEvent` first checks a small reserved
    hotkey table (menu toggle, quick-menu summon); a match is consumed there
    and never forwarded. Everything else forwards `MotionEvent`/`KeyEvent`
    unmodified into RetroArch's existing android input path — no custom
    native input driver — keeping controller hotplug/mapping logic upstream.
- **Asset bring-up:** The stock Java code (asset extraction, `UserPreferences`
  config bootstrap) is reused from
  [phoenix-common](../../pkg/android/phoenix-common/) where practical, called
  from our Kotlin host instead of `RetroActivityCommon`.
- **Boot flow:** MainActivity is launched by an external launcher's Intent
  carrying core + content path extras (Cartridge is never launched standalone
  by an end user). On boot, before doing anything else, it checks for full
  filesystem access (`MANAGE_EXTERNAL_STORAGE`); if not granted, it shows a
  guided prompt to grant it and defers content load until access is
  confirmed — no scoped-storage/SAF path, since Play Store is not a target.
  Build keeps `HAVE_MENU` **on** during development (debug escape hatch: a
  hidden dev toggle can summon Ozone for A/B checks), but the app boots
  RetroArch straight into the launcher-provided content with menu suppressed
  and our React UI showing. M4 ships a `HAVE_MENU=0` build to reclaim binary
  size and prove independence.

### 4.3 FFI layer: `cartridge_api` + C++ TurboModule

Three layers, each with one job:

1. **`cartridge_api.h/.c` (C, hand-curated core):** the only code that touches
   RetroArch internals. Small, stable, reviewable. Everything is async-safe:
   calls from the JS thread enqueue onto the emulation thread's looper (or
   RetroArch's task queue) and complete via callback. Sync fast-paths only for
   trivially thread-safe reads.
2. **C++ TurboModule (JSI, New Architecture):** one shared implementation for
   Android *and* iOS — this is why we use a C++ TurboModule rather than
   Kotlin/Swift modules. Marshals promises, emits events, hands frame buffers
   to JS as `ArrayBuffer`s without copies where possible.
3. **TypeScript client (`@cartridge/native`):** generated typed API + event
   hooks (`useSetting('video_smooth')`, `Cartridge.loadContent(...)`,
   `Cartridge.on('coreInstalled', …)`), plus a **mock implementation** with the
   same generated interface for browser/Expo Go UI development.

Initial API surface (curated IDL, ~40–60 operations):

- **Content/session:** loadContent(core, path) (core + path come from the launcher's launch Intent, or a later same-Activity handoff), closeContent, reset, pause/resume, fast-forward, save/load state (slot), rewind toggle
- **Settings:** getSetting(key), setSetting(key, value), resetSetting, apply/flush config — generic by key, typed in TS by the generated catalog
- **Cores:** list installed, fetch updater list, download/install/delete core (task-backed, progress events), core info/options
- **Shaders:** enable/disable, load preset, list presets, set parameters
- **Capture:** screenshot-to-file ([task_screenshot.c](../../tasks/task_screenshot.c)) and `captureViewport()` → pixel buffer for save-state thumbnails/blur-behind-menu effects
- **Events (C → JS):** contentLoaded/closed, taskProgress/taskFinished, settingChanged, notification messages (hook the msg queue), achievement events, fps/perf stats

No playlist/library/ROM-scanning surface — that's the launcher's responsibility, not Cartridge's.

Event delivery: a single `cartridge_events.c` sink — one `#ifdef
HAVE_CARTRIDGE` hook at the message-queue/task-callback chokepoints — fans out
to the TurboModule's event emitter on the JS thread.

### 4.4 Hybrid codegen pipeline

**Settings catalog (auto-generated):** rather than parsing C source, we exploit
the fact that `menu_setting_new()` builds the complete typed catalog at
runtime:

1. A dev-only shim function `cartridge_dump_settings_catalog()` walks the
   `rarch_setting_t` list and the [configuration.c](../../configuration.c)
   tables and emits `settings-catalog.json`: key, type, default, min/max/step,
   enum labels, category, description (from msg_hash). It runs against a
   **host-built headless x86_64 binary** — no Android emulator/device needed
   — so any dev can regenerate it locally in seconds.
2. The generator turns that JSON into TypeScript types (`SettingKey` union,
   per-key value types, metadata for auto-rendering settings screens — the
   React settings UI can be largely *generated from the catalog* too).
   `settings-catalog.json` is committed to the repo; local UI/codegen work
   reads the committed snapshot directly and doesn't need to regenerate it.
3. **CI drift check:** after any upstream merge (or whenever a dev might have
   forgotten to regenerate locally), CI rebuilds the same headless host
   binary, re-dumps the catalog, and fails if it differs from the committed
   snapshot → regenerate, review the diff, commit. Upstream adding/removing/
   retyping a setting is a visible, reviewed event.

**Imperative surface (curated, generated bindings):** the IDL
(`cartridge.idl.ts` — a small typed DSL, no new file format) is the single
source that generates, in one pass:

- the C shim *declarations* (`cartridge_api.h`) — implementations are
  hand-written and fail compilation if the header changes,
- the RN TurboModule spec (feeding RN's own codegen for JSI scaffolding),
- the C++ glue between TurboModule and C shim,
- the TS client + the TS mock skeleton.

Adding an API = edit IDL → run codegen → implement one C function → UI can
call it with types. This is the "single generation pass" you asked for.

### 4.5 Screen capture

- **Screenshots:** wrap the existing screenshot task; resolve a promise with the file path.
- **Live capture:** `video_driver_read_viewport()` into a shim-owned buffer,
  exposed zero-copy-ish as a JSI ArrayBuffer (RGB565/RGBA8888 + dimensions).
  Used for: pause-menu background blur, save-state thumbnails. Captured once
  when the quick menu opens (on pause) — never per-frame, always requested
  from JS.

## 5. Dev workflow

- `cartridge/app/` is an **Expo app using prebuild + a config plugin** that
  wires the NDK build (our `Android.mk` including phoenix-common's) and the
  TurboModule into the android project. `expo run:android` produces the
  **custom dev client**: RetroArch + shim baked in, JS served by Metro.
- Day-to-day UI work: fast refresh against the dev client on a device/handheld,
  or the **generated mock** in a browser (`expo start --web`) with fake cores,
  settings from the committed catalog snapshot, and canned screenshots.
- Native rebuilds only when the IDL or shim changes. CI (extending the existing
  Android workflow) builds the dev client APK + release APK and runs the
  catalog drift check.

## 6. Phases & milestones

**Phase 1 — Android (everything below), Phase 2 — iOS.** Web/desktop is a
separate plan, not scheduled here.

- **M0 – Library-mode spike (no RN).** A throwaway Kotlin activity with a
  `SurfaceView` boots `libretroarch.so` in library mode, loads a core+ROM
  passed by hardcoded path, renders and plays with sound.
  *Exit: gameplay at parity with stock APK on one handheld; rotation and
  pause/resume don't crash.* This retires the single biggest unknown first.
- **M1 – RN host + FFI skeleton.** Expo app with `<RetroArchSurface/>` Fabric
  component; C++ TurboModule with 3 hand-written calls (loadContent, pause,
  screenshot) + 1 event; hotkey-interception + stock-driver input forwarding
  implemented; Expo custom dev client working end-to-end.
  *Exit: press a React button to pause the running game; fast refresh works.*
- **M2 – Codegen v1.** IDL + generator producing shim header/TurboModule
  spec/TS client/mock; settings catalog dump (host build, local + CI) + drift
  CI; React settings screen (auto-rendered from catalog metadata) reads and
  writes real settings (e.g. toggle shaders, vsync, video smooth) live
  in-game.
  *Exit: a new IDL entry reaches JS with types via codegen alone; catalog CI
  red/green demo.*
- **M3 – Feature breadth.** Core browser + downloader with progress UI,
  launcher-Intent content handoff into `loadContent`, storage-permission
  onboarding gate, save states with captured thumbnails, in-game quick menu
  overlaying live gameplay, transparent-overlay polish (blur/dim via captured
  viewport).
  *Exit: daily-drivable frontend, launched by a test-harness launcher Intent,
  without ever seeing RetroArch's menu.*
- **M4 – Cut the cord + merge drill.** Ship build with `HAVE_MENU=0`; binary
  size and boot-time comparison; perform a real upstream merge and document the
  playbook (merge → customize-fork.sh → build → catalog regen → review).
  *Exit: upstream merge lands in < 1 day with all drift surfaced by tooling.*
- **M5 – iOS bring-up.** Same TurboModule C++ and shim; library-mode against
  the Apple frontend driver (Metal layer inside an RN view); Expo dev client
  for iOS.

## 7. Risks & mitigations

| Risk | Mitigation |
|---|---|
| Surface lifecycle (rotation, background, RN remount) crashes GL/Vulkan | M0/M1 test matrix; SurfaceView owned by a host view that outlives React remounts |
| RetroArch global state assumes one instance/thread | All FFI marshaled to the emulation thread via one queue; enforce with debug assertions in the shim |
| Input latency for touch gamepad through RN | Game input bypasses RN entirely (activity-level event forwarding into RetroArch's input driver) |
| Upstream merge pain in `Android.mk`/griffin | Own makefile includes rather than edits; merge drill in M4 makes the cost measurable early |
| Expo config plugin complexity for NDK | Prebuild once, keep the android project ejected-but-generated; plugin only patches gradle, doesn't own it |
| RN New Architecture churn | Pin RN/Expo SDK versions per milestone; TurboModule C++ API has been stable since RN 0.74+ |
| No scoped-storage path (full filesystem access) | Not targeting Play Store, so this is acceptable now; revisit if Play Store distribution is ever pursued |
