// libEGL.so stub: forwards the calls CEF's GPU process needs first to the host channel.
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int EGLBoolean;
typedef int EGLint;
typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLContext;
typedef void *EGLSurface;

#define EGL_TRUE 1
#define EGL_FALSE 0

extern int gpustub_connect(void);
extern int gpustub_alloc(int w, int h);
extern int gpustub_present(const float rgb[3]);

#define LOG(...) do { fprintf(stderr, "gpustub-egl: " __VA_ARGS__); fputc('\n', stderr); } while (0)

EGLDisplay eglGetDisplay(void *native) {
    LOG("eglGetDisplay(%p)", native);
    return (EGLDisplay)(intptr_t)1;
}

EGLDisplay eglGetPlatformDisplay(unsigned int platform, void *native, const intptr_t *attribs) {
    LOG("eglGetPlatformDisplay(platform=%u native=%p)", platform, native);
    return (EGLDisplay)(intptr_t)1;
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor) {
    LOG("eglInitialize");
    if (major) *major = 1;
    if (minor) *minor = 4;
    return gpustub_connect() == 0 ? EGL_TRUE : EGL_FALSE;
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrs, EGLConfig *cfg, EGLint n, EGLint *count) {
    (void)dpy;
    (void)attrs;
    if (cfg && n > 0) cfg[0] = (EGLConfig)(intptr_t)1;
    if (count) *count = 1;
    return EGL_TRUE;
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig cfg, EGLContext share, const EGLint *attrs) {
    LOG("eglCreateContext");
    return (EGLContext)(intptr_t)1;
}

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig cfg, void *win, const EGLint *attrs) {
    (void)dpy;
    (void)cfg;
    (void)attrs;
    LOG("eglCreateWindowSurface(%p)", win);
    return gpustub_alloc(1280, 720) == 0 ? (EGLSurface)(intptr_t)1 : (EGLSurface)0;
}

EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig cfg, const EGLint *attrs) {
    (void)dpy;
    (void)cfg;
    (void)attrs;
    LOG("eglCreatePbufferSurface");
    return gpustub_alloc(1280, 720) == 0 ? (EGLSurface)(intptr_t)1 : (EGLSurface)0;
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
    (void)dpy;
    (void)draw;
    (void)read;
    (void)ctx;
    return EGL_TRUE;
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
    (void)dpy;
    (void)surf;
    extern const float *gpustub_clear_color(void);
    return gpustub_present(gpustub_clear_color()) == 0 ? EGL_TRUE : EGL_FALSE;
}

EGLBoolean eglTerminate(EGLDisplay dpy) {
    (void)dpy;
    return EGL_TRUE;
}

void *eglGetProcAddress(const char *name) {
    LOG("eglGetProcAddress(%s)", name ? name : "(null)");
    return 0;
}

int eglGetError(void) { return 0x3000; }
