---
name: Bug report
about: Something does not work on your device
title: "[bug] "
labels: bug
---

**Device**
- Model / SoC:
- GPU (`ro.hardware` or GPU name):
- Android version:
- Display path (Xvfb or gamescope):
- Runtime driver (name + sha256 of `libvulkan_panfrost.so`):

**What you did**
<!-- session mode, game, steps -->

**What happened**
<!-- black screen / crash / wrong window / input goes to the shell ... -->

**Evidence**
- `droiddeck-env`:
- `session.log` excerpt:
- `wayland.log` excerpt (`frames on screen`, `windows open`):
- `xpresent.out` excerpt:
- Screenshot (optional):

**Did you check the basics?**
- [ ] `xwin`/`xwindows` shows the game window on the session display
- [ ] `kbase: timeout` count in the session log
- [ ] the driver is the one you think it is (Components → GPU drivers)
