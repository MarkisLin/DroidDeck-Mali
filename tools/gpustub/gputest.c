#include <stdio.h>
#include <unistd.h>

typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLContext;
typedef void *EGLSurface;
typedef int EGLint;
typedef int EGLBoolean;

extern void stub_set_size(int w, int h);
extern EGLDisplay eglGetDisplay(void *native);
extern EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor);
extern EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrs, EGLConfig *cfg, EGLint n, EGLint *count);
extern EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig cfg, EGLContext share, const EGLint *attrs);
extern EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig cfg, void *win, const EGLint *attrs);
extern EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
extern EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surf);
extern void glClearColor(float r, float g, float b, float a);
extern void glClear(unsigned int mask);

int main(void) {
    stub_set_size(1280, 720);
    EGLDisplay d = eglGetDisplay(0);
    if (!eglInitialize(d, 0, 0)) { printf("eglInitialize failed\n"); return 1; }
    EGLConfig cfg; EGLint n = 0;
    if (!eglChooseConfig(d, 0, &cfg, 1, &n)) { printf("eglChooseConfig failed\n"); return 1; }
    EGLContext ctx = eglCreateContext(d, cfg, 0, 0);
    EGLSurface s = eglCreateWindowSurface(d, cfg, 0, 0);
    if (!s) { printf("eglCreateWindowSurface failed\n"); return 1; }
    eglMakeCurrent(d, s, s, ctx);
    const float colors[3][3] = {{1, 0, 0}, {0, 0, 1}, {0, 1, 0}};
    for (int i = 0; i < 3; i++) {
        glClearColor(colors[i][0], colors[i][1], colors[i][2], 1);
        glClear(0x4000);
        printf("swap %d -> %s\n", i + 1, eglSwapBuffers(d, s) ? "ok" : "failed");
        usleep(500000);
    }
    return 0;
}
