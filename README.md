# DroidDeck-Mali

Run Steam Big Picture and Windows games on **MediaTek Mali** Android tablets.

This is a fork of [DroidDeck](https://github.com/Droid-Deck/DroidDeck) that adds a Mali path: a
self-built **PanVK (kbase)** Vulkan driver plus an **Xvfb + presenter** display pipeline, because
Mali devices without root have no DRM render node and therefore no window-level GL/EGL.

> Experimental. Developed and tested on a **Redmi K Pad (25079RPDCC)**: MT8799, Mali-G925-Immortalis
> MC12 (`0xd8300015`, pan_arch 13, kbase CSF UAPI 1.30), Android 16, bootloader locked, **no root**.
> Not affiliated with Valve, MediaTek, or the upstream DroidDeck project.

## Why a separate display pipeline

On a locked Mali device the guest gets `/dev/mali0` but no `/dev/dri/renderD128`. PanVK can render
through kbase, yet:

* Xwayland's glamor needs EGL + GBM + a render node → **no GLX** → Wine's `winex11.drv` cannot
  create a window → Steam's VGUI asserts.
* Chromium/CEF on Wayland needs an EGL context on the surface → **only the surfaceless platform
  exists** → CEF never submits a buffer.

The working stack therefore renders into a software X server and moves pixels to the GPU compositor:

```
Steam / Windows game (Proton ARM64 → Wine → FEX → DXVK)
  → Xvfb :7 (swrast GLX, framebuffer in /tmp/xvfb-fb)
  → xpresent (mmap framebuffer → XDamage dirty rects → wl_shm → wl_surface)
  → DroidDeck's Wayland compositor (Vulkan on the Mali vendor driver)
  → SurfaceView

Input: compositor wl_pointer/wl_keyboard → xpresent → XTEST → Xvfb → Steam/game
Window management: xraise/xhide (Xvfb has no window manager)
```

## What this fork changes

* Bundles `XvfbHost` (starts Xvfb from a fake-root proot process, because `-nolock` needs root)
  and `WindowTools` (installs `xraise`/`xhide` into the runtime).
* `droiddeck-steamos` gets an Xvfb branch: waits for the X socket and framebuffer, starts the
  presenter, launches the Steam client with the software-CEF flags, and runs the minimal window
  manager that hides the Steam UI while a game window is up.
* Games stay on the **same display** as the Steam client: the fork sets `BL_XWAYLAND_COUNT=1`, so
  Steam does not move each game to its own Xwayland. That keeps the Steam overlay, focus and
  controller routing in one X server.
* A self-built PanVK driver (`PanVK_Kbase_G615_v11_main186_p1`, Mesa `5a07217f` + 179 upstream
  patches + a local watchdog re-arm) is imported through the normal runtime-driver UI.
* UI: GPU support recognises an imported PanVK driver ("GPU supported"), the official FPS HUD shows
  the compositor's real presented frame rate, and the diagnostics page shows the active Linux
  driver and the Xwayland mode.

## The story behind the driver

The driver is not written from scratch: it is Mesa's PanVK kbase path, cross-built and carried with
two local patches. Getting there took several wrong turns (a fake `meta.json`, a copied patch series,
a misread ring dump). `docs/DRIVER-JOURNEY.md` records what failed, what the evidence actually said,
and what is still broken:

* `p8-common-gates` SIGSEGV on RC3 and later builds — unresolved,
* the P1 watchdog patch exists but has no reproducer that proves it helps,
* no bionic build, so PanProbe cannot load the driver,
* G925 on MT8799 was never in upstream's validation matrix,
* no dmabuf/GBM export, which is why the display path copies frames instead of sharing them.

If you can help with any of these, open an issue with a reproducible, checkable result. The build
recipe, patches and scripts are in `driver/`.

## Contributors

* Maintainer: @<github-handle> — device work, app, testing
* **DeepSeek** (AI pair programmer) — driver research, debugging, display path and documentation

## Contributing

See `CONTRIBUTING.md`. Reproducible reports and small, evidence-backed patches are the most useful
contributions; the roadmap lives at the end of `docs/DRIVER-JOURNEY.md`.

## Requirements

* Android 9+ device with a Mali GPU that kbase exposes (`/dev/mali0`) and a matching PanVK build.
* No root; about 9 GB free for the Linux runtime and Steam.
* You own a Steam account and the games you run. **Nothing from Valve is bundled.** The runtime
  downloads the Steam client itself; Proton is installed by Steam.

## Build

Requirements: JDK 17, Android SDK (compileSdk 34), NDK 30, and the Gradle wrapper in this repo.

Guest scripts are hashed into `runtime-inputs.json`. After changing anything under
`tools/linuxfs/overlay/`, record the new hashes before building:

```bash
python3 tools/release/runtime_inputs.py record
```

Windows (what this fork is developed with):

```powershell
powershell -File tools/release/build-mt8799.ps1 -Project . -Log build.log
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Any platform: `./gradlew :app:assembleDebug` after the record step.

## Install

1. Install the APK.
2. Import the PanVK driver zip through **Components → GPU drivers → Import .zip** (runtime driver).
3. Open a session with `BL_STEAM_XVFB=1` in
   `Android/data/dev.hayash1.steamoslite/files/droiddeck-env`. A typical file:

   ```
   MESA_LOG=android,file
   PANVK_KBASE_GPU_SEMAPHORE_WAITS=0
   BL_NO_GAMESCOPE=1
   BL_STEAM_SOFT_CEF=1
   BL_STEAM_XVFB=1
   ```

   The app defaults to `BL_XWAYLAND_COUNT=1`; do not raise it unless you also provide a second
   display and presenter.

## Known limitations

* Steam's CEF renders with the software rasteriser: the UI sits at ~13–16 fps; the presenter itself
  runs at ~59 fps and XDamage skips ~80 % of the copies.
* No GPU acceleration for CEF and no GLX for XWayland (both need a DRM render node).
* The Steam client and the game share one Xvfb. Window placement is done by our minimal window
  manager, not by a desktop WM.
* Desktop compositing is software; expect the limits the upstream project documents for Adreno.

## Credits and licence

GPL-3.0, inherited from DroidDeck. This fork's changes are released under the same licence; see
[LICENSE](LICENSE) and [NOTICE.md](NOTICE.md) for third-party components.

Built on the work of the [DroidDeck](https://github.com/Droid-Deck/DroidDeck) project, Valve's
Steam/Proton (not bundled), Mesa's PanVK/kbase support, PRoot, gamescope, and the WinNative,
Bannerlator and uruntime projects. Steam and Proton belong to Valve Corporation.
