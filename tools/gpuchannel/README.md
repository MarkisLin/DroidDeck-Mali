# GPU channel (host-side UI acceleration)

Host end: `GpuChannel` in the app (abstract unix socket `@droiddeck-gpu`).
Guest end: `gpuclient.c` here, cross-built for the runtime (aarch64, glibc).

Protocol v1 is a capability handshake only:

```
guest -> host:   u32 magic "DGPU" (0x44475055), u32 version (1)      network byte order
host -> guest:   u32 status (0 = ok), u32 caps_len, caps bytes
```

Build the guest client:

```sh
aarch64-linux-gnu-gcc -O2 -o gpuclient gpuclient.c
```

Run it inside the runtime (any session):

```sh
/root/gpuclient
# status=0 caps=gles=3.2;ahb=1;transport=abstract;version=1
```

Next milestones (GL command stream, AHardwareBuffer handoff) are described in
`docs/UI-ACCELERATION.md`.