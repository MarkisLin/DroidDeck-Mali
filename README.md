# DroidDeck on Redmi K Pad (Dimensity 9400+ / MT8799)

**Bringing SteamOS to a MediaTek Mali tablet — initial run achieved.**

[English](README.md) · [简体中文](README.zh-CN.md)

This is a fork of [DroidDeck](https://github.com/Droid-Deck/DroidDeck) developed on a **Redmi K Pad
(25079RPDCC)**, SoC **MT8799 / Dimensity 9400+**, GPU **Mali-G925-Immortalis MC12** (`0xd8300015`,
pan_arch 13, kbase CSF UAPI 1.30), Android 16, bootloader locked, **no root**. The goal was reached:
Steam Big Picture runs on the tablet, Windows games launch from the library, and input reaches the
game. It is experimental and far from finished — see the shortcomings below.

> Not affiliated with Valve, MediaTek, or the upstream DroidDeck project.

## Download

| What | Where |
|---|---|
| **Source** | this repository (`git clone`) |
| **APK** | [Releases](../../releases) → `DroidDeck-KPad-0.4.0-sl1-debug.apk` |
| **Driver** | [Releases](../../releases) → `DroidDeck-PanVK-G925-main186-p1.zip` |

The APK is debug-signed (a release key is not published); if another build with a different
signature is installed, uninstall it first. The driver zip is what **Components → GPU drivers →
Import .zip** expects (`libvulkan_panfrost.so` + `icd.json` + `meta.json`).

## Why this needs its own display path

Without root there is no DRM render node, so the guest has `/dev/mali0` but not
`/dev/dri/renderD128`. Consequences:

* Xwayland's glamor needs EGL + GBM + a render node → no GLX → Wine cannot create a window → Steam's
  VGUI asserts;
* Chromium/CEF on Wayland needs an EGL context on the surface → only the surfaceless platform exists
  → CEF never submits a buffer.

The stack that works:

```
Steam / Windows game (Proton ARM64 → Wine → FEX → DXVK)
  → Xvfb :7 (swrast GLX, framebuffer in /tmp/xvfb-fb)
  → xpresent (mmap → XDamage dirty rects → wl_shm → wl_surface)
  → DroidDeck's Wayland compositor (Vulkan on the Mali vendor driver)
  → SurfaceView

Input: compositor wl_pointer/wl_keyboard → xpresent → XTEST → Xvfb → Steam/game
Window management: xraise/xhide (Xvfb has no window manager)
```

Games stay on the **same display** as the Steam client (`BL_XWAYLAND_COUNT=1`), which keeps the Steam
overlay, focus and controller routing in one X server.

## How the driver came to be

The driver is Mesa's PanVK kbase path, cross-built in WSL2 with 179 upstream patches plus two local
ones (a watchdog re-arm and Vulkan 1.4 core-command aliases). The first attempts were wrong in an
instructive way: a hand-written `meta.json` claiming the wrong GPU, a copied patch series, and a
misread ring dump. The full story, the mistakes, the evidence and the lessons are in
[docs/DRIVER-JOURNEY.md](docs/DRIVER-JOURNEY.md); the build recipe is in [driver/](driver/).

## Known shortcomings

* `p8-common-gates` SIGSEGV on RC3 and later builds — unresolved;
* the P1 watchdog patch has no reproducer proving it helps;
* no bionic build, so PanProbe cannot load the driver on-device;
* G925 on MT8799 was never in upstream's validation matrix;
* no dmabuf/GBM export, so the display path copies frames instead of sharing them;
* Steam's CEF is software rasterised: UI at ~13–16 fps (presenter itself ~59 fps, XDamage skips ~80 %
  of copies);
* the game and the Steam client share one Xvfb; window placement is our minimal manager.

## Build

JDK 17, Android SDK (compileSdk 34), NDK 30. After changing `tools/linuxfs/overlay/**`:

```bash
python3 tools/release/runtime_inputs.py record
```

Windows: `powershell -File tools/release/build-mt8799.ps1 -Project . -Log build.log`
Any platform: `./gradlew :app:assembleDebug`

## Install

1. Install the APK, allow storage access.
2. **Components → GPU drivers → Import .zip** → choose the driver zip (runtime driver).
3. Put `BL_STEAM_XVFB=1` in `Android/data/dev.hayash1.steamoslite/files/droiddeck-env`:

   ```
   MESA_LOG=android,file
   PANVK_KBASE_GPU_SEMAPHORE_WAITS=0
   BL_NO_GAMESCOPE=1
   BL_STEAM_SOFT_CEF=1
   BL_STEAM_XVFB=1
   ```

4. Start the session and sign in to Steam. Steam downloads its client on first launch.

## Contributors

* Maintainer: **Markis Lin** — device work, app, testing
* **DeepSeek** (AI pair programmer) — driver research, debugging, display path, documentation

## Contributing

Issues with a reproducible, checkable result are the most useful. See [CONTRIBUTING.md](CONTRIBUTING.md);
the roadmap is at the end of [docs/DRIVER-JOURNEY.md](docs/DRIVER-JOURNEY.md).

## Licence

GPL-3.0 (inherited from DroidDeck); third-party components are listed in [NOTICE.md](NOTICE.md).
Steam and Proton belong to Valve Corporation and are not bundled.
