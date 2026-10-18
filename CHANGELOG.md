# Changelog
## v0.4.3-sl1 (2026-10-18)

Proton failures become diagnosable.

* `droiddeck-proton-run` captures Proton's stderr into `/tmp/droiddeck-proton-<AppID>.log`; the script
  ships in the APK and is staged at session start, so an in-place update is enough;
* analysis of the L4D2 (AppID 550) launch-then-exit path added to `docs/PROTON-32BIT.md`;
* known limitations: L4D2 not playable yet (32-bit support to verify); Silksong in-game input still
  unresolved.

Update in place: same package id and signature, `versionCode 16`.
## v0.4.2-sl1 (2026-10-18)

Self-healing update: runtime pieces now travel with the APK, so a fresh install starts SteamOS
without a runtime-bundle update.

Application:

* `Xvfb`, `xpresent`, the session scripts and `libgpuhook.so` ship as APK assets and are staged into
  the Linux environment when a session starts; `usr/share/X11/xkb` is added only when missing;
* the imported PanVK driver is selected automatically when no driver was chosen yet;
* imported drivers keep `libvulkan_panfrost.so` and `icd.json` points at the written file;
* GPU detection accepts older imports through `meta.json`;
* games installed during a session are mapped to `droiddeck-proton-arm64` within about ten seconds;
* CEF stays pinned to the two fastest cores (`BL_CPU_MASK=0xc0`).

Update in place: same package id and signature, `versionCode 15`; data and runtime are preserved.
## v0.4.1-sl1 (2026-10-18)

Follow-up release: display-pipeline robustness and a CPU-affinity optimisation.

Application:

* **imported PanVK drivers keep their real library name.** The installer used to rename
  `libvulkan_panfrost.so` to `libvulkan_freedreno.so`; `icd.json` now points at the file that was
  actually written, so the runtime and the UI agree on what was imported;
* **GPU status also recognises drivers imported before this fix.** Detection now accepts a
  `libvulkan_panfrost.so` *or* a `meta.json` that names PanVK / panfrost, so a device that imported
  the driver with the old naming shows "supported" instead of "unsupported";
* **CEF is pinned to the two fastest cores by default** (`BL_CPU_MASK=0xc0`). Steam launches its
  helper with `taskset 0x7c`, which mixes little cores with big cores and leaves the fastest core
  unused; the session rewrites that mask at exec time. Measured on the K Pad with a fixed scroll
  workload: 12.5–12.8 fps before, 15.4 fps after (+15–20%), with no change to resolution or image
  quality. `BL_CPU_MASK` overrides the default.

Runtime requirements:

* the display stack needs `Xvfb` and the project's `xpresent` presenter. Devices whose runtime
  bundle predates this release must install both, otherwise Steam reports
  `Unable to open X11 display` and the session never presents a frame.

## v0.4.0-sl1 (2026-10-17)

First runnable build on the Redmi K Pad (MT8799 / Mali-G925).

Application:

* single-Xwayland configuration (`BL_XWAYLAND_COUNT=1`); games and the Steam client share one
  display, so overlay, focus and controller routing stay consistent;
* GPU status recognises an imported PanVK driver instead of reporting "unsupported";
* the official FPS HUD reads the compositor's presented frame rate; the duplicate counter was removed;
* diagnostics page shows the active runtime driver and the Xwayland mode;
* application label changed to DroidDeck.

Driver:

* self-built PanVK kbase ICD: Mesa `5a07217f` + 179 upstream `csf-v11` patches;
* subqueue timeout protection and Vulkan 1.4 core command aliases;
* packaged as `DroidDeck-PanVK-G925-main186-p1.zip`.

Display path:

* Xvfb `:7` + `xpresent` + the Wayland compositor; `xraise`/`xhide` for window management;
* sources for the X tools published under `tools/xtools/`.

Known limitations: see the "Current limitations" section of the README.

## Unreleased

* `p8-common-gates` SIGSEGV on recent builds — investigation pending.
* bionic build for the on-device probe (PanProbe).
* performance work on the copy path and frame scheduling.
