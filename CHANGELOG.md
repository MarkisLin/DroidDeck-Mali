# Changelog

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
