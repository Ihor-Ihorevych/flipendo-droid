# PS1 Harry Potter in Flipendo (SwanStation) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Play the PS1 Harry Potter 1 and 2 inside Flipendo, from a PS1 tab in the launcher, with SwanStation built into the APK.

**Architecture:** SwanStation is built as a libretro core (`libswanstation_libretro.so`, arm64). A C++ libretro host on SDL3 (GLES context, audio stream, gamepads) loads it with `dlopen` and runs the frame loop in `Ps1Activity` (process `:ps1`), like HP3. The launcher gets PC/PS1 tabs; PS1 lists HP1 and HP2.

**Tech Stack:** Java (no Gradle, `build-apk.sh`), C++20, NDK 27.2, SDL3 (already shipped), libretro API, SwanStation core, PCSX-Redux OpenBIOS.

**Spec:** `docs/superpowers/specs/2026-11-05-ps1-swanstation-design.md`

## Global Constraints

- arm64-v8a only; `minSdk 29`, `targetSdk 35`; package `io.github.flipendo.spike`.
- PS1 runs in its own activity and process `:ps1`; PC games (HP1/HP2/HP3) must not change behaviour.
- OpenGL ES hardware renderer from the start; software renderer only as a fallback.
- BIOS: OpenBIOS (SHA-256 `fabe498fbf224e4721f12f31b6f5fe0659205e341dc4e5c5f91b9bd1a1011c57`, 524,288 bytes) bundled; optional user BIOS override.
- Never commit disc images, a Sony BIOS or game data; every shipped file must be GPL-compatible.
- Ask before downloading binaries or installing tools (the owner approved the SwanStation source fetch and the build).
- HP1 `MDF/MDS` is converted to `BIN/CUE`; HP2 `BIN/CUE` loads as is.

## Review Focus

- Disc folder missing or unreadable: the PS1 tab shows a clear message, no crash.
- MDF whose size is not a multiple of 2352: conversion refuses and says so.
- Phone rotated or app backgrounded during play: audio stops, state is kept, resume works.
- Memory card file corrupt or read-only: the game still starts, the user is told saves will not persist.
- BIOS override file wrong size: rejected, OpenBIOS used.

---

### Task 1: Spike — core builds and boots both games (throwaway)

**Files:**
- Create (outside the repo): `C:\Users\Ihor\hpdroid\ps1-spike\` (core clone, build output, test host)

**Interfaces:**
- Produces: a known-good `libswanstation_libretro.so` build recipe (flags, NDK path), and answers to: GLES renderer works with an SDL3 context? OpenBIOS boots HP1 and HP2? Memory-card saves persist?

- [ ] **Step 1: Clone `github.com/libretro/swanstation` into an empty `ps1-spike/swanstation` dir; record its licence file and the licences of bundled third-party code.**
- [ ] **Step 2: Build the libretro core for arm64 with NDK 27.2 (use the repo's Android/CMake or `Makefile` path); expected: `swanstation_libretro_android.so` produced.**
- [ ] **Step 3: Write a minimal desktop-or-device libretro host (SDL3, GLES) that loads the core, sets OpenBIOS as system BIOS and runs HP2's CUE for 600 frames; expected: non-black frames, audio samples produced.**
- [ ] **Step 4: Convert HP1's MDF to BIN/CUE and boot it the same way; expected: BIOS logo then game intro.**
- [ ] **Step 5: Save in-game to a memory card, quit, relaunch, load; expected: save survives. If not, record it and try a user-provided BIOS or save states.**
- [ ] **Step 6: Write the findings into `docs/ps1.md` (new) and amend the design if anything contradicts it. Commit `docs/` only.**

### Task 2: MDF → BIN/CUE converter

**Files:**
- Create: `android/java/io/github/flipendo/spike/Ps1DiscConverter.java`
- Test: `android/tests/Ps1DiscConverterTest.java`

**Interfaces:**
- Produces: `static File convertMdf(File mdf, File outDir) throws IOException` returning the generated `.cue`; `static boolean isSupported(File image)`.

- [ ] **Step 1: Write failing tests `convertsRaw2352Image`, `rejectsSizeNotMultipleOf2352`, `leavesBinCueUntouched`, using a generated fake image.**
- [ ] **Step 2: Run them (existing test runner for `android/tests`); expected FAIL, class missing.**
- [ ] **Step 3: Implement per the layout the `.mds` shows (Task 1 step 4 confirmed it); write a single-track MODE2/2352 CUE.**
- [ ] **Step 4: Run; expected PASS. Commit `feat(android): MDF to BIN/CUE converter for PS1 discs`.**

### Task 3: Libretro host (native)

**Files:**
- Create: `android/jni/ps1_host.cpp`, `android/jni/ps1_host.h`
- Modify: `android/CMakeLists.txt` (new `ps1host` shared library), `android/build-apk.sh` (pack core + libs)

**Interfaces:**
- Produces JNI: `Ps1Activity.nativeStart(String corePath, String discCue, String biosPath, String saveDir, int upscale)`, `nativeSetButton(int libretroButton, boolean down)`, `nativePause(boolean)`, `nativeStop()`.

- [ ] **Step 1: Implement environment callback, video (hw_render GLES3 via SDL3 context), audio batch → SDL audio stream, input poll, per the spike.**
- [ ] **Step 2: Build the APK; expected: `libps1host.so` and the core packed under `lib/arm64-v8a`.**
- [ ] **Step 3: On the phone, boot HP2 through a debug intent; expected: picture and sound. Install the APK without asking (owner standing instruction). Commit.**

### Task 4: Ps1Activity, input overlay, saves

**Files:**
- Create: `android/java/io/github/flipendo/spike/Ps1Activity.java`, `Ps1PadView.java`
- Modify: `android/AndroidManifest.xml` (activity, `android:process=":ps1"`)

**Interfaces:**
- Consumes: Task 3 JNI. Produces: intent extras `disc` (cue path), `slot`.

- [ ] **Step 1: Implement the activity (fullscreen, SDL3 surface), the on-screen pad (D-pad, ✕○□△, L1/L2/R1/R2, Start/Select) and gamepad mapping.**
- [ ] **Step 2: Memory card `.mcd` per game in app storage; one save-state slot in a pause menu; tests for the Review Focus items on card and pause.**
- [ ] **Step 3: Verify on the phone: play, background, resume, quit, relaunch with save intact. Commit.**

### Task 5: Launcher tabs and PS1 setup

**Files:**
- Modify: `android/java/io/github/flipendo/spike/SetupActivity.java` (tab bar, PS1 covers, PS1 folder pick, BIOS override setting), `android/res/`
- Test: `android/tests/` (folder detection and BIOS validation)

**Interfaces:**
- Consumes: `Ps1DiscConverter`, `Ps1Activity` extras. Produces: preference `ps1.dir`, `ps1.bios`, tab state kept apart from `game`.

- [ ] **Step 1: Failing tests `findsHp1MdfAndHp2Cue`, `missingFolderReportsError`, `rejectsBiosOfWrongSize`.**
- [ ] **Step 2: Implement tabs PC/PS1; PC unchanged. PS1 shows HP1 and HP2 covers; Play converts if needed and starts `Ps1Activity`.**
- [ ] **Step 3: Verify PC games launch exactly as before. Commit.**

### Task 6: Docs, licences, release

**Files:**
- Modify: `docs/ps1.md`, `docs/android.md`, `README.md`, `ROADMAP.md`, `CONTRIBUTING.md` if needed
- Create: `licenses/` entries for SwanStation, OpenBIOS (MIT, with source URL and hash)

- [ ] **Step 1: Document build recipe, OpenBIOS provenance, risks. Remove done roadmap items.**
- [ ] **Step 2: Release commit in the project's `chore(release)` style; do not push without permission.**
