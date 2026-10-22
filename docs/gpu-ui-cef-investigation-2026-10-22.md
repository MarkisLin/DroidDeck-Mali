# Steam UI GPU Rendering Investigation (kbase / ANGLE / CEF)

Date: 2026-10-22
Device: Redmi K Pad 25079RPDCC (MT8799, Mali-G925-Immortalis MC12, `gpu_id 0xd8300015`, pan_arch v13, kbase CSF UAPI 1.30, Android 16, no root)
Driver under test: `PanVK_Kbase_G615_v11_main186_p1` (reference build, 25,013,984 B)

## 1. Goal

Make the Steam UI inside the guest render through the GPU (PanVK on kbase) instead of
llvmpipe, while keeping the software path as a working fallback.

## 2. Two pipelines

Current (software, working):

```
Steam CEF (-cef-disable-gpu, llvmpipe) -> X11 window on Xvfb :7 -> XWD framebuffer
  -> xpresent -> app Wayland compositor -> SurfaceView
```

Target (this investigation):

```
Steam CEF (GL) -> ANGLE (--use-gl=angle) -> Vulkan -> PanVK/kbase -> surface -> present
```

## 3. Layer-by-layer results (all measured on device)

| Layer | Test | Result |
|---|---|---|
| kbase / panvk | vkCreateDevice, submit+fence+readback, full suite | 5/5; readback 0/1024 mismatch; suite 53 pass / 1 fail / 19 skip |
| kbase WSI | X11 swapchain on Xvfb, 4 formats x 4 images x 6 frames | `WSI_EXT_FAILS=0` |
| ANGLE -> Vulkan -> panvk | X11 window + swapchain + fullscreen draws | 40/40 frames, 118.5 fps, PASS |
| zink (GL over Vulkan) | X11 window + swapchain + texture uploads | 60/60 frames, 114 fps, PASS |
| ANGLE stress | 4.8 GB textures, 200 shaders, 4 concurrent contexts, shared-context churn | all PASS |
| ANGLE soak | 2 concurrent contexts | 129,720 / 122,100 frames, zero kbase timeout, zero CONTEXT_LOST |
| CEF on our driver | hook-injected flags + ICD env + steamrtarm64 libs | 8 steamwebhelper processes; GPU process `panvk=7`, `swrast=0`, `llvmpipe=0` |

Key renderer strings:

```
GL_RENDERER: ANGLE (ARM, Vulkan 1.4.363 (Mali-G925-Immortalis MC12 (0xD8300015)), panvk-26.2.99)
GL_RENDERER: zink Vulkan 1.4(Mali-G925-Immortalis MC12 (MESA_PANVK))
```

## 4. The three independent blockers inside CEF

### 4.1 ANGLE-Vulkan is not in this CEF build

```
[gl_factory.cc(120)] Requested GL implementation (gl=egl-angle,angle=vulkan)
  not found in allowed implementations:
  [(gl=egl-angle,angle=default),(gl=egl-gles2,angle=none)]
```

### 4.2 ANGLE-GL needs GLX, which Xvfb cannot provide

```
[angle_platform_impl] Display.cpp:1086 (initialize):
  ANGLE Display::initialize error 12289: glXQueryExtensionsString returned NULL

glxinfo: Error: couldn't find RGB GLX visual or fbconfig   (DRI3 not available)
```

### 4.3 Native EGL is rejected by CEF's passthrough check

```
[FATAL:gpu_init.cc(593)] Passthrough is not supported, GL is egl, ANGLE is
[gpu_process_host.cc(1002)] GPU process exited unexpectedly: exit_code=5
```

Consequence: flags that start CEF (`--use-gl=egl`) kill the GPU process; flags that are
accepted (`--use-gl=angle`, i.e. ANGLE default backend) start CEF but never present a pixel.

## 5. Attempt matrix

| Configuration | CEF | Observation |
|---|---|---|
| `-cef-disable-gpu` (baseline) | runs | UI visible; llvmpipe software rendering |
| `--use-gl=egl --use-cmd-decoder=passthrough` | 7 helpers | GPU process FATAL, exit_code=5 |
| `--use-gl=angle --use-angle=vulkan` | rejected | not in allowed implementations |
| `--use-gl=angle --use-angle=gl/gles/opengl/opengles` | does not start | `(gl=none,angle=none)` or GLX failure |
| `--use-gl=angle` + `ANGLE_DEFAULT_PLATFORM=vulkan` | 8 helpers | CEF runs on panvk, but framebuffer stays black |
| ... + `--disable-gpu-compositing` | 8 helpers | still black |
| ... + `--disable-gpu-rasterization` | 8 helpers | still black |
| ... + `--in-process-gpu` | 8 helpers | still black |
| ozone=wayland | 8 helpers | no gbm/drm calls, no output |
| ozone=x11 + DAMAGE-less Xvfb | - | XvfbHost retry reclaims :7, not effective |
| gamescope path | gamescope not started | no output |
| fake DRM render node (shim) | called | only `DRM_IOCTL_VERSION`, nothing further |
| GBM shim over dma-heap | loaded | CEF never calls `gbm_*` |

## 6. A/B control (E1)

Same environment, only the CEF GPU switch differs:

| Group | Flags | steamwebhelper | Xvfb/xpresent | Framebuffer |
|---|---|---|---|---|
| A (baseline) | `-cef-disable-gpu` | 8 | running | has pixels |
| B (experiment) | `--use-gl=angle` + `ANGLE_DEFAULT_PLATFORM=vulkan` | 8 | running | black (cursor only) |

Both groups start, both connect to the same X environment; only A produces pixels.
Therefore the break is inside CEF's own draw/composite step, not in process start,
X connection, or driver loading.

## 7. Ruled out by earlier project records

From the Mali fork README: without root there is no DRM render node, so the guest has
`/dev/mali0` but no `/dev/dri/renderD128`. **Xwayland's glamor needs EGL + GBM + a render
node**, and Chromium/CEF needs an EGL context on the surface; neither is available.
That is why the working stack renders into a software X server and presents the framebuffer
through the GPU compositor. (Re-proposing Xwayland is therefore not a new option.)

## 8. Where the remaining work would be

| Direction | Feasibility | Note |
|---|---|---|
| Compare Steam client channels / CEF builds | low cost, inconclusive so far | client download stalled repeatedly; only untested avenue |
| Binary-patch `steamwebhelper` allow-list | possible, fragile | lost on every client update |
| Full DRM/GBM/EGL emulation layer | very large | would have to satisfy Xwayland glamor semantics |
| Accept software CEF | immediate | current state |
| GPU on the compositing side (scale / frame-gen / colour) | viable | does not change that CEF rasterises on CPU |

## 9. Assets produced by this investigation

| File | Purpose | State |
|---|---|---|
| `tools/gpustub/hook.c` | CEF flag/env injection (exec at the `.sh` and binary level), CPU affinity, Vulkan env | effective |
| `tools/gpustub/fakedrm.c` | DRM render-node probe: answers VERSION/GET_CAP, logs the rest and returns ENOTTY | called by CEF |
| `tools/gpustub/gbm_shim.c` | GBM replacement over `/dev/dma_heap`, real exportable dma-bufs | builds, loads |
| `tools/linuxfs/overlay/usr/local/bin/droiddeck-session` | A-mode gate, zink self-check with software fallback, overridable args/channel | verified |
| `tools/linuxfs/overlay/usr/local/bin/droiddeck-steamos` | llvmpipe only for the software path, configurable ozone, zink backend | verified |

Enable A-mode inside the guest with `touch /root/.gpu-ui`; override the CEF arguments with
`/root/.gpu-ui.args` and the client channel with `/root/.gpu-ui.channel`. When the zink probe
fails the launcher falls back to software CEF instead of leaving a black screen.

## 10. Conclusion

The lower layers are usable and were each verified independently: the driver, ANGLE-Vulkan,
zink and X11 window surfaces all work. CEF can also be made to run on the PanVK driver.

The blocker is the CEF build shipped with the Steam client: it exposes no usable GL backend
in this environment (no ANGLE-Vulkan entry, ANGLE-GL requires a GLX stack this guest cannot
provide, native EGL is rejected by its own passthrough check). Until a CEF build with
ANGLE-Vulkan is available, hardware rendering of the Steam UI is not reachable here, and
software CEF remains the supported path.