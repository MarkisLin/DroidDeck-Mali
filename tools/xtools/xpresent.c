/*
 * xpresent - grab frames from an X display (Xvfb) and present them
 * through wl_shm to a Wayland compositor.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>
#include <X11/keysym.h>

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

static volatile int running = 1;
static void on_signal(int sig) { (void)sig; running = 0; }

struct state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    uint32_t shm_formats[32];
    int shm_format_count;
    uint32_t shm_format_use;
    struct xdg_wm_base *wm_base;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;

    struct wl_buffer *buffers[2];
    void *maps[2];
    size_t map_size[2];
    int buf_index;
    int width, height;
    int configured;
    int configured_w, configured_h;

    Display *x11;
    Window root;
    Pixmap pixmap;
    GC gc;
    XImage *image;
    XShmSegmentInfo shminfo;
    int use_shm;
    unsigned char *fb_map;
    size_t fb_size;
    int fb_stride;
    size_t fb_offset;
    Damage damage;
    int dmg_ev_base;
    XserverRegion region;
    int dmg_active;
    unsigned long long copied_bytes;
    int frames_sent;
    int frames_skipped;
};

static int create_shm_fd(size_t size) {
    int fd = memfd_create("xpresent", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, (off_t)size) < 0) { close(fd); return -1; }
    return fd;
}

static void buffer_release(void *data, struct wl_buffer *buffer) { (void)data; (void)buffer; }

static const struct wl_buffer_listener buffer_listener = { .release = buffer_release };

static int make_buffers(struct state *st) {
    size_t stride = (size_t)st->width * 4;
    size_t size = stride * (size_t)st->height;
    for (int i = 0; i < 2; i++) {
        int fd = create_shm_fd(size);
        if (fd < 0) { fprintf(stderr, "xpresent: shm fd failed: %s\n", strerror(errno)); return -1; }
        void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED) { fprintf(stderr, "xpresent: mmap failed\n"); close(fd); return -1; }
        struct wl_shm_pool *pool = wl_shm_create_pool(st->shm, fd, (int32_t)size);
        if (!pool) { fprintf(stderr, "xpresent: create_pool failed\\n"); close(fd); return -1; }
        uint32_t fmt = st->shm_format_use ? st->shm_format_use : WL_SHM_FORMAT_XRGB8888;
        st->buffers[i] = wl_shm_pool_create_buffer(pool, 0, st->width, st->height,
                                                   (int32_t)stride, fmt);
        wl_shm_pool_destroy(pool);
        close(fd);
        if (!st->buffers[i]) { fprintf(stderr, "xpresent: create_buffer failed (fmt=0x%x)\\n", fmt); return -1; }
        wl_buffer_add_listener(st->buffers[i], &buffer_listener, st);
        st->maps[i] = map;
        st->map_size[i] = size;
    }
    return 0;
}

static void xdg_surface_configure(void *data, struct xdg_surface *surface, uint32_t serial) {
    struct state *st = data;
    xdg_surface_ack_configure(surface, serial);
    st->configured = 1;
}

static const struct xdg_surface_listener xdg_surface_listener = { .configure = xdg_surface_configure };

static void toplevel_configure(void *data, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *states) {
    (void)t; (void)states;
    struct state *st = data;
    if (w > 0 && h > 0) { st->configured_w = w; st->configured_h = h; }
}

static void toplevel_close(void *data, struct xdg_toplevel *t) { (void)data; (void)t; running = 0; }

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
};

static void wm_ping(void *data, struct xdg_wm_base *base, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = { .ping = wm_ping };

static void shm_format(void *data, struct wl_shm *shm, uint32_t format) {
    (void)shm;
    struct state *st = data;
    if (st->shm_format_count < 32) st->shm_formats[st->shm_format_count++] = format;
    fprintf(stderr, "xpresent: shm format 0x%x\\n", format);
}

static const struct wl_shm_listener shm_listener = { .format = shm_format };

static void ptr_enter(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *surf,
                      wl_fixed_t sx, wl_fixed_t sy) {
    (void)p; (void)serial; (void)surf;
    struct state *st = data;
    XTestFakeMotionEvent(st->x11, -1, wl_fixed_to_int(sx), wl_fixed_to_int(sy), CurrentTime);
    XFlush(st->x11);
}
static void ptr_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *surf) {
    (void)data; (void)p; (void)serial; (void)surf;
}
static void ptr_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
    (void)p; (void)time;
    struct state *st = data;
    XTestFakeMotionEvent(st->x11, -1, wl_fixed_to_int(sx), wl_fixed_to_int(sy), CurrentTime);
    XFlush(st->x11);
}
static void ptr_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time,
                       uint32_t button, uint32_t state) {
    (void)p; (void)serial; (void)time;
    struct state *st = data;
    unsigned xb = 1;
    switch (button) {
        case 0x110: xb = 1; break;   /* BTN_LEFT */
        case 0x111: xb = 3; break;   /* BTN_RIGHT */
        case 0x112: xb = 2; break;   /* BTN_MIDDLE */
        default: return;
    }
    XTestFakeButtonEvent(st->x11, xb, state == WL_POINTER_BUTTON_STATE_PRESSED, CurrentTime);
    XFlush(st->x11);
}
static void ptr_axis(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis, wl_fixed_t value) {
    (void)data; (void)p; (void)time; (void)axis; (void)value;
}
static const struct wl_pointer_listener pointer_listener = {
    .enter = ptr_enter, .leave = ptr_leave, .motion = ptr_motion,
    .button = ptr_button, .axis = ptr_axis,
};

static void kb_keymap(void *data, struct wl_keyboard *k, uint32_t fmt, int32_t fd, uint32_t size) {
    (void)data; (void)k; (void)fmt; (void)size;
    if (fd >= 0) close(fd);
}
static void kb_enter(void *data, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s, struct wl_array *keys) {
    (void)data; (void)k; (void)serial; (void)s; (void)keys;
}
static void kb_leave(void *data, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s) {
    (void)data; (void)k; (void)serial; (void)s;
}
static void kb_key(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    (void)k; (void)serial; (void)time;
    struct state *st = data;
    /* wl_keyboard 给的是 evdev keycode，X11 keycode = evdev + 8 */
    XTestFakeKeyEvent(st->x11, key + 8, state == WL_KEYBOARD_KEY_STATE_PRESSED, CurrentTime);
    XFlush(st->x11);
}
static void kb_modifiers(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t dep, uint32_t lat,
                         uint32_t lock, uint32_t grp) {
    (void)data; (void)k; (void)serial; (void)dep; (void)lat; (void)lock; (void)grp;
}
static void kb_repeat(void *data, struct wl_keyboard *k, int32_t rate, int32_t delay) {
    (void)data; (void)k; (void)rate; (void)delay;
}
static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = kb_keymap, .enter = kb_enter, .leave = kb_leave,
    .key = kb_key, .modifiers = kb_modifiers, .repeat_info = kb_repeat,
};

static void seat_caps(void *data, struct wl_seat *seat, uint32_t caps) {
    struct state *st = data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !st->pointer) {
        st->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(st->pointer, &pointer_listener, st);
        fprintf(stderr, "xpresent: pointer ready\n");
    }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !st->keyboard) {
        st->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(st->keyboard, &keyboard_listener, st);
        fprintf(stderr, "xpresent: keyboard ready\n");
    }
}
static const struct wl_seat_listener seat_listener = { .capabilities = seat_caps };

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
                            const char *interface, uint32_t version) {
    struct state *st = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        st->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version < 4 ? version : 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        st->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
        wl_shm_add_listener(st->shm, &shm_listener, st);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        st->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
        wl_seat_add_listener(st->seat, &seat_listener, st);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        st->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, version < 2 ? version : 2);
        xdg_wm_base_add_listener(st->wm_base, &wm_base_listener, st);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static int open_x11(struct state *st) {
    st->x11 = XOpenDisplay(NULL);
    if (!st->x11) {
        const char *fbonly = getenv("XPRESENT_FB");
        if (!fbonly || !*fbonly) { fprintf(stderr, "xpresent: cannot open X display\n"); return -1; }
        st->width = 1280; st->height = 720;
        fprintf(stderr, "xpresent: no X, fb-only mode %dx%d\\n", st->width, st->height);
        return 0;
    }
    st->root = DefaultRootWindow(st->x11);
    st->width = DisplayWidth(st->x11, DefaultScreen(st->x11));
    st->height = DisplayHeight(st->x11, DefaultScreen(st->x11));

    int major = 0, minor = 0;
    st->use_shm = getenv("XPRESENT_SHM") != NULL;
    if (st->use_shm && XShmQueryExtension(st->x11) && XShmQueryVersion(st->x11, &major, &minor, &st->use_shm)) {
        st->image = XShmCreateImage(st->x11, DefaultVisual(st->x11, DefaultScreen(st->x11)),
                                    (unsigned)DefaultDepth(st->x11, DefaultScreen(st->x11)),
                                    ZPixmap, NULL, &st->shminfo, st->width, st->height);
        if (!st->image) { st->use_shm = 0; }
        else {
            st->shminfo.shmid = shmget(IPC_PRIVATE, (size_t)st->image->bytes_per_line * (size_t)st->image->height,
                                       IPC_CREAT | 0600);
            if (st->shminfo.shmid < 0) { XDestroyImage(st->image); st->image = NULL; st->use_shm = 0; }
            else {
                st->shminfo.shmaddr = st->image->data = shmat(st->shminfo.shmid, NULL, 0);
                st->shminfo.readOnly = False;
                XShmAttach(st->x11, &st->shminfo);
                st->use_shm = 1;
            }
        }
    }
    st->pixmap = XCreatePixmap(st->x11, st->root, (unsigned)st->width, (unsigned)st->height,
                               (unsigned)DefaultDepth(st->x11, DefaultScreen(st->x11)));
    st->gc = XCreateGC(st->x11, st->root, 0, NULL);
    if (!st->image) {
        st->image = XGetImage(st->x11, st->pixmap, 0, 0, (unsigned)st->width, (unsigned)st->height, AllPlanes, ZPixmap);
        if (!st->image) { fprintf(stderr, "xpresent: XGetImage failed\n"); return -1; }
        st->use_shm = 0;
    }
    fprintf(stderr, "xpresent: X11 %dx%d shm=%d\n", st->width, st->height, st->use_shm);

    {
        int err_base = 0;
        if (XDamageQueryExtension(st->x11, &st->dmg_ev_base, &err_base)) {
            st->damage = XDamageCreate(st->x11, st->root, XDamageReportNonEmpty);
            st->dmg_active = st->damage != None;
            fprintf(stderr, "xpresent: XDamage %s\n", st->dmg_active ? "enabled" : "create failed");
        } else {
            fprintf(stderr, "xpresent: XDamage unavailable\n");
        }
    }

    const char *fb = getenv("XPRESENT_FB");
    if (fb && *fb) {
        int ffd = open(fb, O_RDONLY);
        if (ffd >= 0) {
            struct stat sb;
            if (fstat(ffd, &sb) == 0 && (size_t)sb.st_size >= (size_t)st->width * (size_t)st->height * 4) {
                void *m = mmap(NULL, (size_t)sb.st_size, PROT_READ, MAP_SHARED, ffd, 0);
                if (m != MAP_FAILED) {
                    st->fb_map = m;
                    st->fb_size = (size_t)sb.st_size;
                    st->fb_stride = st->width * 4;
                    {
                        size_t img = (size_t)st->fb_stride * (size_t)st->height;
                        st->fb_offset = st->fb_size > img ? st->fb_size - img : 0;
                        if (st->fb_offset > (size_t)st->fb_stride) st->fb_offset = 0;
                    }
                    fprintf(stderr, "xpresent: fb stride %d offset %zu (file %zu, w=%d h=%d)\n",
                            st->fb_stride, st->fb_offset, st->fb_size, st->width, st->height);
                    fprintf(stderr, "xpresent: fb mmap %s (%zu bytes)\n", fb, st->fb_size);
                }
            }
            close(ffd);
        }
        if (!st->fb_map) fprintf(stderr, "xpresent: fb mmap failed: %s\n", strerror(errno));
    }
    return 0;
}

static void grab_frame(struct state *st) {
    if (st->fb_map) return;
    if (st->use_shm) {
        XShmGetImage(st->x11, st->root, st->image, 0, 0, AllPlanes);
    } else {
        XCopyArea(st->x11, st->root, st->pixmap, st->gc, 0, 0,
                  (unsigned)st->width, (unsigned)st->height, 0, 0);
        XSync(st->x11, False);
        XImage *fresh = XGetImage(st->x11, st->pixmap, 0, 0, (unsigned)st->width, (unsigned)st->height, AllPlanes, ZPixmap);
        if (fresh) { XDestroyImage(st->image); st->image = fresh; }
    }
}

static void copy_rect(struct state *st, int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > st->width) w = st->width - x;
    if (y + h > st->height) h = st->height - y;
    if (w <= 0 || h <= 0) return;
    uint8_t *dst = st->maps[0];
    const uint8_t *base = st->fb_map ? (st->fb_map + st->fb_offset) : (const uint8_t *)st->image->data;
    size_t src_stride = st->fb_map ? (size_t)st->fb_stride : (size_t)st->image->bytes_per_line;
    size_t dst_stride = (size_t)st->width * 4;
    for (int row = 0; row < h; row++)
        memcpy(dst + (size_t)(y + row) * dst_stride + (size_t)x * 4,
               base + (size_t)(y + row) * src_stride + (size_t)x * 4, (size_t)w * 4);
    st->copied_bytes += (unsigned long long)w * (unsigned long long)h * 4u;
}

static void copy_into_buffer(struct state *st) {
    int idx = st->buf_index;
    uint8_t *dst = st->maps[idx];
    const uint8_t *src = st->fb_map ? (st->fb_map + st->fb_offset) : (const uint8_t *)st->image->data;
    size_t dst_stride = (size_t)st->width * 4;
    size_t src_stride = st->fb_map ? (size_t)st->fb_stride : (size_t)st->image->bytes_per_line;
    if (dst_stride == src_stride) {
        memcpy(dst, src, dst_stride * (size_t)st->height);
    } else {
        for (int y = 0; y < st->height; y++)
            memcpy(dst + (size_t)y * dst_stride, src + (size_t)y * src_stride, dst_stride);
    }
    st->buf_index ^= 1;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    struct state st;
    memset(&st, 0, sizeof(st));

    if (open_x11(&st) != 0) return 1;

    st.display = wl_display_connect(NULL);
    if (!st.display) { fprintf(stderr, "xpresent: cannot connect to Wayland\n"); return 1; }
    st.registry = wl_display_get_registry(st.display);
    wl_registry_add_listener(st.registry, &registry_listener, &st);
    wl_display_roundtrip(st.display);
    wl_display_roundtrip(st.display);
    if (!st.compositor || !st.shm || !st.wm_base) {
        fprintf(stderr, "xpresent: missing globals (compositor=%p shm=%p xdg=%p)\n",
                (void *)st.compositor, (void *)st.shm, (void *)st.wm_base);
        return 1;
    }

    st.shm_format_use = 0;
    for (int i = 0; i < st.shm_format_count; i++) {
        if (st.shm_formats[i] == WL_SHM_FORMAT_XRGB8888) { st.shm_format_use = WL_SHM_FORMAT_XRGB8888; break; }
        if (st.shm_formats[i] == WL_SHM_FORMAT_ARGB8888) st.shm_format_use = WL_SHM_FORMAT_ARGB8888;
    }
    fprintf(stderr, "xpresent: using shm format 0x%x (of %d)\\n", st.shm_format_use, st.shm_format_count);

    st.surface = wl_compositor_create_surface(st.compositor);
    st.xdg_surface = xdg_wm_base_get_xdg_surface(st.wm_base, st.surface);
    xdg_surface_add_listener(st.xdg_surface, &xdg_surface_listener, &st);
    st.toplevel = xdg_surface_get_toplevel(st.xdg_surface);
    xdg_toplevel_add_listener(st.toplevel, &toplevel_listener, &st);
    xdg_toplevel_set_title(st.toplevel, "Xvfb");
    xdg_toplevel_set_app_id(st.toplevel, "xpresent");

    wl_surface_commit(st.surface);
    wl_display_roundtrip(st.display);

    if (!st.configured) {
        for (int i = 0; i < 200 && !st.configured; i++) {
            wl_display_dispatch(st.display);
        }
    }
    fprintf(stderr, "xpresent: surface %dx%d configured=%d (configured %dx%d, capture stays %dx%d)\n",
            st.width, st.height, st.configured, st.configured_w, st.configured_h, st.width, st.height);

    if (make_buffers(&st) != 0) return 1;
    st.region = XFixesCreateRegion(st.x11, NULL, 0);

    long frames = 0;
    while (running) {
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);

        int rects = 0;
        if (st.dmg_active && st.region) {
            while (XPending(st.x11)) { XEvent ev; XNextEvent(st.x11, &ev); }
            XDamageSubtract(st.x11, st.damage, None, st.region);
            int n = 0;
            XRectangle *rr = XFixesFetchRegion(st.x11, st.region, &n);
            if (n == 0 || !rr) {
                st.frames_skipped++;
                if ((frames % 30) == 0)
                    fprintf(stderr, "xpresent: frame %ld skipped (no damage), copied=%llu B\n",
                            frames, st.copied_bytes);
                frames++;
                goto pace;
            }
            grab_frame(&st);
            wl_surface_attach(st.surface, st.buffers[0], 0, 0);
            for (int i = 0; i < n; i++) {
                copy_rect(&st, rr[i].x, rr[i].y, rr[i].width, rr[i].height);
                wl_surface_damage(st.surface, rr[i].x, rr[i].y, rr[i].width, rr[i].height);
            }
            rects = n;
            XFree(rr);
            st.frames_sent++;
        } else {
            grab_frame(&st);
            copy_into_buffer(&st);
            wl_surface_attach(st.surface, st.buffers[st.buf_index ^ 1], 0, 0);
            wl_surface_damage(st.surface, 0, 0, st.width, st.height);
        }
        frames++;
        if ((frames % 30) == 0)
            fprintf(stderr, "xpresent: frame %ld rects=%d copied=%llu B sent=%d skipped=%d\n",
                    frames, rects, st.copied_bytes, st.frames_sent, st.frames_skipped);
        wl_surface_commit(st.surface);
        wl_display_flush(st.display);

        while (wl_display_prepare_read(st.display) != 0) wl_display_dispatch_pending(st.display);
        wl_display_flush(st.display);
        wl_display_read_events(st.display);
        wl_display_dispatch_pending(st.display);

    pace:
        clock_gettime(CLOCK_MONOTONIC, &t1);
        long us = (long)((t1.tv_sec - t0.tv_sec) * 1000000 + (t1.tv_nsec - t0.tv_nsec) / 1000);
        long target = 16666;
        if (us < target) usleep((useconds_t)(target - us));
    }

    fprintf(stderr, "xpresent: done\n");
    return 0;
}
