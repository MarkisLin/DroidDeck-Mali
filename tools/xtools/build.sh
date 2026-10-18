#!/bin/sh
# Cross-build the guest-side X tools (aarch64, glibc) for the Linux runtime.
set -eu
CC=${CC:-aarch64-linux-gnu-gcc}
OUT=${OUT:-out}
mkdir -p "$OUT"

$CC -O2 -o "$OUT/xpresent" xpresent.c -lX11 -lXext -lXdamage
$CC -O2 -o "$OUT/xraise"   xraise.c   -lX11
$CC -O2 -o "$OUT/xhide"    xhide.c    -lX11
$CC -O2 -o "$OUT/xwindows" xwindows.c -lX11
$CC -O2 -o "$OUT/xwin"     xwin.c     -lX11
$CC -O2 -o "$OUT/dri3probe" dri3probe.c -lX11
ls -l "$OUT"

