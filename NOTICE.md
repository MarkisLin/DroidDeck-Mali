# Third-party notices

DroidDeck-Mali is a fork of DroidDeck and is distributed under **GPL-3.0** (see `LICENSE`).
The upstream project's copyright and licence notices are kept in this repository.

Nothing from Valve is bundled: the Steam client, Proton and the games are downloaded or supplied by
the user. Steam and Proton belong to Valve Corporation.

Components used or built upon, with their own licences:

| Component | Used for | Licence |
|---|---|---|
| [DroidDeck](https://github.com/Droid-Deck/DroidDeck) | fork base: app, compositor, runtime scripts | GPL-3.0 |
| [Mesa](https://gitlab.freedesktop.org/mesa/mesa) (PanVK, kbase path) | the Vulkan driver this fork builds | MIT (see Mesa's `docs/license.rst`) |
| [PRoot](https://github.com/termux/proot) (Termux fork) | the fake-root/user-space runtime | GPL-2.0-or-later |
| [gamescope](https://github.com/ValveSoftware/gamescope) | optional session compositor (the Xvfb path does not use it) | see its `LICENSE` |
| [uruntime](https://github.com/VHSgunzo/uruntime) | AppImage unpacking, shipped unmodified with its licence | MIT |
| WinNative, Bannerlator | shim/input/controller work inherited from upstream | see upstream credits |

`xpresent`, `xraise`, `xhide` and the `XvfbHost`/`WindowTools` integration are part of this fork and
are covered by the repository's GPL-3.0 licence.

If you redistribute a built APK, keep the upstream and third-party notices, and make the
corresponding source (this repository plus your patches) available as GPL-3.0 requires.
