# PanVK kbase driver for Mali (MT8799 / G925 class)

The runtime driver this fork imports is a **self-built PanVK (kbase) ICD** for devices whose vendor
DDK is not usable and which have no DRM render node. This directory holds the patches, build
scripts, profile and metadata needed to reproduce it.

## What is built

* Mesa `5a07217f034b3e50d8c7c7794f97a2df1742613b`
* upstream PanVK kbase series, profile `g615-v11-csf` (179 patches applied, zero drift)
* two local patches:
  * `patches/p1-watchdog-rearm.patch` — subqueue timeout protection: kbase marks a subqueue lost
    after 10 s without progress; the patch extends that window while `extract`/`seqno`/
    `stream_progress` still advance (capped).
  * `patches/p2-core-command-aliases.patch` — export the Vulkan 1.4 core names
    `vkCmdBindDescriptorSets2` / `vkCmdPushConstants2` as aliases of the KHR entry points.
* output: `libvulkan_panfrost.so` (glibc, aarch64) + `icd.json` + `meta.json`, packaged as a zip that
  the app's **Components → GPU drivers → Import .zip** flow accepts.

## Build (WSL2)

The scripts assume Ubuntu 24.04 in WSL2, `gcc-aarch64-linux-gnu`, meson ≥ 1.4 and ninja.

```bash
bash build/00-env.sh                      # toolchain + arm64 multiarch
bash build/10b-extract-patch-native.sh    # unpack Mesa, apply 179 patches
bash build/20b-host-tools.sh              # host generators (mesa_clc, vtn_bindgen2, ...)
bash build/30b-cross-build.sh             # cross build → dist-glibc-g925/
bash build/40b-verify.sh                  # ELF/glibc/panvk_vXX symbol checks
```

Then apply the local patches on top and rebuild:

```bash
cd mesa-5a07217f*
patch -p1 < /path/to/driver/patches/p2-core-command-aliases.patch
patch -p1 < /path/to/driver/patches/p1-watchdog-rearm.patch
ninja -C ../build-glibc-g925
```

Package the result as:

```
libvulkan_panfrost.so
icd.json          # { "ICD": { "library_path": "./libvulkan_panfrost.so", "api_version": "1.4.363" } }
meta.json         # see driver/meta/main186_p1.json
```

## Test

* **PanProbe** (`dev.zenithblue.panvktest`, Android process, needs a **bionic** build):
  `am start -n dev.zenithblue.panvktest/.MainActivity --es autorun all --es driver imported`,
  results in `files/autorun.txt`.
* **Guest tests** (glibc build, no system loader needed): cross-compile the upstream `tests/**`
  programs and run them through the app's proot, passing the ICD path as `argv[1]`.
* Sanity check before blaming the driver: run a bare kbase probe (queue create, kick, read back a
  value). If that works, the firmware and kbase are healthy and the problem is in the user-space
  command stream.

## Current limitations

* **Performance is low**: the Steam UI runs at roughly 13–16 fps and games are not yet optimised for
  this path; the copy path and frame scheduling still have headroom.
* The subqueue timeout protection patch has no current reproducer; it is kept as a safety net.
* `p8-common-gates` crashes with SIGSEGV on RC3 and later builds; not yet root-caused.
* No bionic build (needs a Linux x86_64 NDK toolchain), so PanProbe cannot load the driver on-device.
* The G925 class is not in upstream's validation matrix; `arch 13` has not been verified by upstream
  on MT8799.
* No dmabuf/GBM export, so presentation copies frames instead of sharing buffers.* No dmabuf/GBM export: this is why the display path uses Xvfb instead of zero-copy presentation.
