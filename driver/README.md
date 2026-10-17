# PanVK kbase driver for Mali (MT8799 / G925 class)

The runtime driver this fork imports is a **self-built PanVK (kbase) ICD** for devices whose vendor
DDK is not usable and which have no DRM render node. This directory holds the patches, build
scripts, profile and metadata needed to reproduce it.

## What is built

* Mesa `5a07217f034b3e50d8c7c7794f97a2df1742613b`
* upstream PanVK kbase series, profile `g615-v11-csf` (179 patches applied, zero drift)
* two local patches:
  * `patches/p1-watchdog-rearm.patch` — before the 10 s kbase software watchdog declares a
    subqueue lost, re-arm it while `extract`/`seqno`/`stream_progress` still advance (cap 12).
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

## Status and known gaps

* Wedge (`kbase: timeout on subqueue`) is gone with the upstream RC3 series; the P1 patch is a
  safety net that has **not** been proven to change behaviour (no reproducer with the current build).
* `p8-common-gates` crashes with SIGSEGV on RC3 and later builds; beta.4 passes. Not yet root-caused.
* The bionic build (needed for PanProbe) is not produced here: it requires a Linux x86_64 NDK
  toolchain, which the original build host did not have.
* The G925 class is not in upstream's validation matrix; the `g615-v11-csf` profile is what upstream
  documents as covering v10–v14, and `arch 13` was never re-validated by upstream on this GPU.
* No dmabuf/GBM export: this is why the display path uses Xvfb instead of zero-copy presentation.
