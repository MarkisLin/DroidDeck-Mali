// Minimal EGL/GLES2 stub: forwards a clear + swap to the host GPU channel.
// Only the entry points a CEF-style compositor needs first are implemented.
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>

typedef int EGLBoolean;
typedef int EGLint;
typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLContext;
typedef void *EGLSurface;
typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef float GLfloat;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;

#define EGL_TRUE 1
#define EGL_FALSE 0
#define GL_COLOR_BUFFER_BIT 0x4000

#define CHANNEL_NAME "droiddeck-gpu"
#define MAGIC 0x44475055u
#define VERSION 1u
#define PRES 0x50524553u

static int g_fd = -1;
static int g_w = 1280, g_h = 720;
static int g_stride = 0;
static GLfloat g_clear[4] = {0, 0, 0, 1};

static int read_all(int fd, void *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = read(fd, (char *)buf + off, n - off);
        if (r <= 0) return -1;
        off += (size_t)r;
    }
    return 0;
}

static int write_all(int fd, const void *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = write(fd, (const char *)buf + off, n - off);
        if (r <= 0) return -1;
        off += (size_t)r;
    }
    return 0;
}

static int send_u32(int fd, uint32_t v) {
    uint32_t n = htonl(v);
    return write_all(fd, &n, sizeof n);
}

static int recv_u32(int fd, uint32_t *out) {
    uint32_t v = 0;
    if (read_all(fd, &v, sizeof v) != 0) return -1;
    *out = ntohl(v);
    return 0;
}

static int channel_open(void) {
    if (g_fd >= 0) return 0;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    a.sun_path[0] = 0;
    memcpy(a.sun_path + 1, CHANNEL_NAME, strlen(CHANNEL_NAME));
    socklen_t len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + strlen(CHANNEL_NAME));
    if (connect(fd, (struct sockaddr *)&a, len) < 0) {
        close(fd);
        return -1;
    }
    g_fd = fd;
    return 0;
}

void stub_set_size(int w, int h) {
    g_w = w;
    g_h = h;
}

EGLDisplay eglGetDisplay(void *native) {
    (void)native;
    return (EGLDisplay)(intptr_t)1;
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor) {
    (void)dpy;
    if (major) *major = 1;
    if (minor) *minor = 4;
    return channel_open() == 0 ? EGL_TRUE : EGL_FALSE;
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrs, EGLConfig *cfg, EGLint n, EGLint *count) {
    (void)dpy;
    (void)attrs;
    if (cfg && n > 0) cfg[0] = (EGLConfig)(intptr_t)1;
    if (count) *count = 1;
    return EGL_TRUE;
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig cfg, EGLContext share, const EGLint *attrs) {
    (void)dpy;
    (void)cfg;
    (void)share;
    (void)attrs;
    return (EGLContext)(intptr_t)1;
}

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig cfg, void *win, const EGLint *attrs) {
    (void)dpy;
    (void)cfg;
    (void)win;
    (void)attrs;
    if (channel_open() != 0) return (EGLSurface)0;
    if (send_u32(g_fd, MAGIC) != 0 || send_u32(g_fd, VERSION) != 0) return (EGLSurface)0;
    uint32_t status = 0, n = 0;
    if (recv_u32(g_fd, &status) != 0 || status != 0) return (EGLSurface)0;
    if (recv_u32(g_fd, &n) != 0) return (EGLSurface)0;
    char caps[256];
    if (n > 0 && n < sizeof caps && read_all(g_fd, caps, n) != 0) return (EGLSurface)0;
    uint32_t dims[2] = {htonl((uint32_t)g_w), htonl((uint32_t)g_h)};
    if (write_all(g_fd, dims, sizeof dims) != 0) return (EGLSurface)0;
    uint32_t hdr[5] = {0};
    struct iovec iov = {.iov_base = hdr, .iov_len = sizeof hdr};
    char cbuf[CMSG_SPACE(sizeof(int))];
    memset(cbuf, 0, sizeof cbuf);
    struct msghdr msg;
    memset(&msg, 0, sizeof msg);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cbuf;
    msg.msg_controllen = sizeof cbuf;
    if (recvmsg(g_fd, &msg, 0) != (ssize_t)iov.iov_len) return (EGLSurface)0;
    for (struct cmsghdr *cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm)) {
        if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_RIGHTS) {
            int buf_fd = -1;
            memcpy(&buf_fd, CMSG_DATA(cm), sizeof(int));
            if (buf_fd >= 0) close(buf_fd);  // the host owns the pixels; the guest never maps it
        }
    }
    g_stride = (int)ntohl(hdr[3]);
    fprintf(stderr, "gpu stub: surface %ux%u stride=%d format=%u\n", g_w, g_h, g_stride,
            ntohl(hdr[4]));
    return (EGLSurface)(intptr_t)1;
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
    if (g_fd < 0) return EGL_FALSE;
    if (send_u32(g_fd, PRES) != 0) return EGL_FALSE;
    if (write_all(g_fd, g_clear, sizeof(float) * 3) != 0) return EGL_FALSE;
    uint32_t ack = 0;
    if (recv_u32(g_fd, &ack) != 0) return EGL_FALSE;
    return ack == 0xFFFFFFFFu || ack == 0xFFFFFFFEu ? EGL_FALSE : EGL_TRUE;
}

EGLBoolean eglTerminate(EGLDisplay dpy) {
    (void)dpy;
    if (g_fd >= 0) close(g_fd);
    g_fd = -1;
    return EGL_TRUE;
}

void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    g_clear[0] = r;
    g_clear[1] = g;
    g_clear[2] = b;
    g_clear[3] = a;
}

void glClear(GLbitfield mask) {
    (void)mask;
}

void glFinish(void) {}

void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

const GLubyte *glGetString(GLenum name) {
    (void)name;
    return (const GLubyte *)"gpu-channel-stub";
}
