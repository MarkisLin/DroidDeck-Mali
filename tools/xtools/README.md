# Guest X tools

Small X11 programs used by the Xvfb display path. They are cross-built for the Linux runtime
(aarch64, glibc) and installed into the guest as `/root/xpresent`, `/root/xraise`, `/root/xhide`,
`/root/xwindows`, `/root/xwin`.

| Tool | Purpose |
|---|---|
| `xpresent` | Maps the Xvfb framebuffer (`/tmp/xvfb-fb/Xvfb_screen0`), tracks damage with XDamage and presents the surface through `wl_shm` to the DroidDeck compositor. Also forwards pointer/keyboard events back with XTEST. |
| `xraise` | Minimal window management: find the game window (`--game`), raise it, set `_NET_ACTIVE_WINDOW`. |
| `xhide` | `map` / `unmap` the Steam UI window while a game window is up. |
| `xwindows` | Lists the windows on the display (debugging). |
| `xwin` | Prints the focused/active window (debugging). |
| `dri3probe` | Lists the X extensions the display offers (DRI3/DRI2/PRESENT/RANDR/MIT-SHM); use it to check whether a display can hand out GPU buffers. |

## Build

```sh
CC=aarch64-linux-gnu-gcc sh build.sh
```

Requires the X11, Xext and Xdamage development headers for the target. On Ubuntu, install
`libx11-dev:arm64 libxext-dev:arm64 libxdamage-dev:arm64` (multiarch) or use a sysroot that has them.

## Deploy

Copy the binaries into the guest and make them executable:

```sh
adb shell "run-as dev.hayash1.steamoslite cp /data/local/tmp/xpresent files/linuxfs/root/xpresent"
```

`SessionFiles.stage` installs the copies that ship inside the APK; the binaries under
`tools/linuxfs/overlay/usr/local/bin/` are the packaged versions.
