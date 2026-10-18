# Guest EGL/GLES2 stub

Minimal stand-in for `libEGL`/`libGLESv2` inside the Linux runtime. Instead of touching the GPU it
serialises the handful of calls a compositor needs first to the host channel (`@droiddeck-gpu`):

* `eglInitialize` → handshake;
* `eglCreateWindowSurface` → `ALLOC`, the host returns an AHardwareBuffer fd;
* `glClearColor`/`glClear` → stored colour;
* `eglSwapBuffers` → `PRES` with the RGB floats, the host clears the buffer on its GPU and presents.

Build and run inside the runtime:

```sh
aarch64-linux-gnu-gcc -O2 -o gputest gputest.c guest_egl.c
/root/gputest
# gpu stub: surface 1280x720 stride=1280 format=1
# swap 1 -> ok
# swap 2 -> ok
# swap 3 -> ok
```

The screen shows the last requested colour. This is the M2 data path; the remaining work is the
shader/texture/FBO subset CEF needs and loading the stub into `steamwebhelper`.