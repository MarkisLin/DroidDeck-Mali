# Security policy

## Supported versions

Only the latest release is supported. Experimental builds (`Pre-release` on the Releases page) may
change without notice.

## Reporting a vulnerability

Do **not** open a public issue for a vulnerability. Report it privately:

* use GitHub's **Security → Report a vulnerability** (private advisory) on this repository, or
* email the maintainer listed in `CONTRIBUTORS.md`.

Please include the affected version, the device, and a minimal reproduction. You should receive an
answer within a few days.

## Scope

This project runs a Linux user-space runtime inside an Android app (proot, Xvfb, a Wayland
compositor and a Vulkan driver). Relevant areas include:

* privilege or sandbox escapes from the guest to the Android app or system;
* path traversal or arbitrary file write through the import flows (driver zip, component zips);
* unsafe handling of downloaded archives or game executables;
* secrets or credentials exposed in logs.

Out of scope: vulnerabilities in Steam, Proton, Mesa, proot or other third-party components that
are not patched by this project — report those upstream.

## What the app stores

* the Linux runtime and games under the app's private storage;
* session logs under `files/logs/` (app-private);
* the Steam account credentials are entered inside the Steam client running in the guest; the app
  itself does not store them.
