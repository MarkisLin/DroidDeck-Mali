# Why the Steam UI's software rasteriser cannot be replaced: a full investigation

> 中文版： [UI-ACCELERATION-POSTMORTEM.md](UI-ACCELERATION-POSTMORTEM.md)

This document records why the goal "move the Steam UI from CPU rasterisation to GPU rendering"
stalled, and the reasoning behind every step along the way.
The conclusion first: **the driver is not the problem, and we are not short of approaches — we
simply do not get the control that would make Steam's CEF call GL.**

---

## 1. Starting point and constraints

The goal was simple: the Steam UI runs at only 13–16 fps, so we wanted to replace the CPU
rasteriser with GPU rendering to make the interface smoother.

The device constraints come first, because every later judgement follows from them:

- Redmi K Pad, MT8799 / Dimensity 9400+, Mali-G925-Immortalis MC12;
- Android 16, locked bootloader, **no root**;
- no `/dev/dri/renderD128` (no DRM render node), only `/dev/dri/card0` (the display controller,
  owned by `system:system 0660`, which an app cannot open);
- the GPU is reachable only through `/dev/mali0` (kbase) as userspace Vulkan (PanVK).

---

## 2. Why the UI is software-rendered today

Steam's interface is drawn by CEF (Chromium Embedded Framework). Hardware acceleration needs a
"GL/EGL on a window surface" path; on this device that path breaks in three places:

1. Chromium's GPU compositing on X11 needs **DRI3** (to share GPU buffers);
2. with no render node in the guest, X11's EGL can only fall back to **llvmpipe** (CPU raster);
3. we currently pass `-cef-disable-gpu` to CEF so that anything appears at all, which tells it
   outright not to use the GPU.

So "software rasteriser" is the sum of two layers: **the environment lacks the capability** (no
DRI3), and **we disable it ourselves** (disable-gpu).

---

## 3. Investigation path and evidence (in order)

### 3.1 The driver is not the bottleneck (the first piece of good news)

- `vulkaninfo` shows PanVK supporting the full WSI set:
  `VK_KHR_xcb_surface` / `xlib_surface` / `wayland_surface` / `headless_surface`;
- `vkcube` presents 60 consecutive frames on Xvfb and exits 0;
- on the game side, 84–91 fps with `kbase: timeout = 0` and `MESA: error = 0`.

Conclusion: the Vulkan presentation path works, so the planned "add WSI to PanVK" work is not
needed.

### 3.2 The GL layer has no hardware

- `eglinfo`: the X11, Wayland and Surfaceless platforms are all **llvmpipe**;
- with `MESA_LOADER_DRIVER_OVERRIDE=zink`, only the **Device (offscreen) platform** gets
  `zink Vulkan 1.4 (Mali-G925 via MESA_PANVK)`; as soon as a window surface is requested it falls
  back to llvmpipe.

The reason: Zink's window presentation depends on Vulkan WSI plus dma-buf export, and kbase does
not provide exportable buffers.

### 3.3 DRI3 does not exist at all (the decisive fact)

From our own probe, `tools/xtools/dri3probe`:

```
DRI3       ABSENT
DRI2       ABSENT
PRESENT    ABSENT
RANDR      present
MIT-SHM    present
```

Confirmed against `AHardwareBuffer`: gralloc's dma-buf **cannot be mmapped by the guest**
(`ENODEV`), so the pixels can only be read and presented on the host side.

### 3.4 CEF's GPU process: what it loads, whom it obeys

- Reading `/proc/<gpu-process>/maps`: it maps
  `~/.local/share/Steam/steamrtarm64/libEGL.so` and `libGLESv2.so` (**Steam's own ANGLE**),
  plus Mesa's `/usr/lib/libEGL.so.1.1.0`;
- in other words, replacing GL means replacing the behaviour of those two libraries.

### 3.5 The first wall: Steam restores its own files

Replacing `steamrtarm64/libEGL.so` with our stub makes Steam verify and **restore** it on start-up
(size back to 283472, timestamp back to the release date); replacing the `steamwebhelper` binary
with a wrapper script is restored the same way.

Conclusion: **file replacement is not sustainable**.

### 3.6 The second wall: the argument allow-list

Steam drops arguments such as `-cef-use-gl` / `-cef-use-angle`. Intercepting `execve` / `posix_spawn`
and appending `--use-gl=egl` the moment Steam starts the helper does reach the **browser and utility**
processes (`type=browser use-gl=1`).

But the process that actually calls EGL is the **GPU process**:

```
pid=16093 type=browser      use-gl=1
pid=16171 type=gpu-process  use-gl=0   ← dropped when Chromium rebuilds the command line
```

The GPU process is forked from the zygote and never goes through `execve`, so a process hook cannot
inject anything. Appending `--ozone-platform=wayland` is equally ineffective.

### 3.7 The third wall: the library is loaded, but nothing calls it

Redirecting the absolute-path ANGLE load to our stub through a `dlopen` interception gives:

- our `libEGL.so` is loaded into **6 CEF processes** (proven by the constructor log);
- **not a single EGL function call** (`stub.log` stays empty).

Because the GPU process, facing "no GL switch and no DRI3", simply gives up on initialising GL.

---

## 4. What the channel we built ourselves achieved

Having established that CEF cannot be controlled, we still built "our own GPU channel" from
scratch, as the foundation of another route:

| Stage | Content | Verification |
|---|---|---|
| M0 | `@droiddeck-gpu` abstract socket, version handshake | `caps=gles=3.2;ahb=1;transport=abstract` |
| M1 | the host allocates an `AHardwareBuffer`, hands the fd to the guest over `SCM_RIGHTS`; the host renders into the AHB with **vendor GL** (EGL + `EGLImage`); `SurfaceControl` puts it on screen | `gl renderer=Mali-G925-Immortalis MC12`, magenta/green/blue frames appear on screen |
| M2 | guest `libEGL`/`libGLESv2` stubs (`tools/gpustub/`), serialising clear+swap onto the channel | `swap 1..3 ok`, the screen shows the requested colour |

A few traps we hit along the way (all fixed, all worth remembering):

- `eglCreateImageKHR` cannot be cast straight from an AHB; `eglGetNativeClientBufferANDROID` has to
  come first;
- the AHB's `EGLImage`/texture/FBO **must be cached per buffer**; rebuilding them each frame wedges
  on the second frame;
- an AHB's dma-buf **can only be presented by SurfaceControl**; the compositor's
  `vkp_image_from_dmabuf` cannot import it (all four DRM formats × supported modifiers failed);
- intercepting `dlsym` under a global `LD_PRELOAD` segfaults `/bin/sleep` (recursion), so it was
  removed.

---

## 5. Why this channel cannot save CEF

The channel answers "who do we render for": it lets **programs we write ourselves** use the GPU.

But CEF wants "call GL inside its own GPU process", and all we control is:

- files (restored by Steam);
- environment variables and preloaded libraries (the library can be changed, but the GPU process
  never initialises GL);
- process launch arguments (the GPU process never goes through exec).

**What is missing is control, not implementation.** No matter how many more GL stubs we write, they
will not be called.

---

## 5.1 Checking the external proposals (2026-10-18)

A circulated research document (`Steam_UI_GPU加速_GitHub调研与解决方案.docx`) proposed two leads we
had not tried; both were checked on this device:

**Lead A: raw dma-buf X11 WSI (funnymdzz/mesa's PanVK kbase variant, `WSI_X11_TERMUX=1` /
`PANVK_KBASE_DRI3=raw`), together with the private DRI3 protocol of Termux:X11 / Winlator.**

- Blocker 1: that approach allocates dma-bufs through `/dev/dma_heap/*`. On this device
  `/dev/dma_heap/{system,mtk_mm,...}` are `0444`, so the app uid **can only read, not write**, and
  the allocation ioctl cannot be issued;
- Blocker 2: it also needs an X server implementing the private DRI3 protocol (Termux:X11 is an
  Android app, not part of our session);
- Blocker 3: even with both in place, CEF still does not initialise GL (see below), so it would only
  give the GPU to "other GL programs", not to the Steam UI.
- A possible substitute: use the `AHardwareBuffer` we can already allocate (gralloc dma-buf) instead
  of dma-heap, but the same blockers 2 and 3 apply.

**Lead B: `-cef-force-glx` (the switch mentioned in steam-for-linux issue #12694 as restoring
hardware acceleration).**

- Measured: after passing `-cef-force-glx` to Steam, **no CEF process command line contains it**
  (browser / utility / gpu-process all report `force-glx=0`), so it is filtered by the same Steam
  allow-list;
- it therefore cannot serve as a back door into CEF.

Neither lead changes the conclusion of section 3; the document itself notes that these are "external
approaches not verified on this device".

## 5.2 A viable optimisation that keeps every pixel: CPU affinity (shipped)

With hardware acceleration blocked, one optimisation remains that changes neither resolution nor
CEF: **move CEF's raster threads off the "mixed cores" and onto "big cores only".**

- the device has 8 cores, capacity `502/502/472/502` (little cores 0–3) and `871/880/871/1024`
  (big cores 4–7);
- Steam launches the helper with `taskset 0x7c`, i.e. CPUs 2–6: two little cores and missing the
  fastest big core 7;
- our hook rewrites `taskset 0x7c` to `0xf0` (CPUs 4–7, all big cores) at `execve` time;
  `BL_CPU_MASK` can override it, and the default is `0xf0`.

**Controlled comparison** (fresh session + first 30 seconds of scrolling, the same `xscroll` load,
counting the compositor's `frames on screen`):

| Affinity | Frame rate |
|---|---|
| `0x7c` (Steam default, CPUs 2–6 including little cores) | 12.5–12.8 fps |
| `0xf0` (CPUs 4–7, all big cores) | 13.2–16.1 fps |
| **`0xc0` (CPUs 6–7, the two fastest cores)** | **15.4 fps (most stable; now the default)** |
| `0x80` (CPU 7 only, a single core) | 1.9 fps (rasterising is multithreaded; one core is not enough) |

Note: the deeper the scroll position and the heavier the page, the lower the frame rate, so absolute
numbers are only comparable across sessions under the same "first scroll" protocol; measurements on
deep pages are not valid. The default is `0xc0`: it takes only the two fastest cores and leaves the
rest for games.

The stable gain is about **15–20%**, with no change to pixels or image quality. It also confirms that
the bottleneck really is CEF's CPU rasteriser rather than the copy chain: `xpresent` already uses
XDamage dirty rectangles, the same XRGB8888 format, and skips undamaged regions, so the copy path
has no obvious waste.

## 6. Ways out and their preconditions

| Direction | Precondition | Cost |
|---|---|---|
| Accept the software UI and optimise elsewhere | none | limited gain (lower resolution / fewer animations, roughly 13→18 fps) |
| Unlock/root to get a DRM render node | bootloader can be unlocked | violates the project's "no root" premise; would solve DRI3 outright |
| Support from Valve | CEF exposing a GL backend switch or a render hook | out of our hands; waiting only |
| Our own render process (using the channel we built) | no CEF needed | equivalent to rewriting the Steam UI; not realistic |
| An ARM SteamOS (such as Armada) | needs DRM + gamescope | a different product line, not a patch for this project |

---

## 7. Retrospective: if we started over

1. **Verify control before investing in implementation**: whether CEF can be injected or replaced
   should have been answered by a one-hour experiment on day one, not after the channel was finished;
2. **Recognise the "vendor closed loop"**: the Steam client verifies and restores its own files by
   design, not as a bug, so any approach that depends on modifying files should be cut immediately;
3. **Separate "driver capability" from "consumer willingness"**: PanVK is perfectly usable, but the
   consumer (CEF's GPU process) chooses not to use it;
4. **Put the evidence on disk**: every key conclusion here comes from `/proc/<pid>/maps`,
   `cef_log.txt` and our own probes rather than guesswork, which is what makes the "this way is
   blocked" conclusion reviewable.

---

## 8. How to reproduce

- driver and WSI: `vulkaninfo`, `vkcube`, `dri3probe` (`tools/xtools/`);
- channel: `GpuChannel` (`app/src/main/cpp/waylandcomp/src/gpu_channel.c`) plus
  `tools/gpuchannel/gpuclient.c`;
- guest GL stubs: `tools/gpustub/`;
- CEF-side evidence: `/proc/<gpu-process>/maps`, `~/.local/share/Steam/logs/cef_log.txt`, and the
  command line of `steamwebhelper`.

Related design document: [UI-ACCELERATION.md](UI-ACCELERATION.md).
