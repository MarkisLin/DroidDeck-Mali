# GPU channel (host-side UI acceleration)

Host end: native `GpuChannel` (docs/UI-ACCELERATION.md), abstract unix socket `@droiddeck-gpu`.
Guest end: `gpuclient.c`, cross-built for the runtime (aarch64, glibc).

## Protocol v1

All integers are network byte order.

```
guest -> host:  u32 magic "DGPU" (0x44475055), u32 version (1)
host -> guest:  u32 status (0 = ok), u32 caps_len, caps bytes
                ("gles=3.2;ahb=1;transport=abstract;version=1")
guest -> host:  u32 width, u32 height
host -> guest:  20-byte header {status, width, height, stride, format}
                + the buffer fd via SCM_RIGHTS (status 0)
```

The host currently backs the allocation with a memfd (`RGBA8888`, stride = width * 4); the guest
receives the fd and can `fstat`/`mmap` it. AHardwareBuffer replaces the memfd once the host renders
into it (M1.5).

## Build and run

```sh
aarch64-linux-gnu-gcc -O2 -o gpuclient gpuclient.c
# inside the runtime:
/root/gpuclient 1280 720
```

Expected:

```
handshake: gles=3.2;ahb=1;transport=abstract;version=1
alloc: status=0 1280x720 stride=5120 format=1 fd=4
fd fstat: size=3686400
```