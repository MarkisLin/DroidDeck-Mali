// Shared state and channel transport for the guest GPU stubs (libEGL.so / libGLESv2.so).
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

#define CHANNEL_NAME "droiddeck-gpu"
#define MAGIC 0x44475055u
#define VERSION 1u
#define PRES 0x50524553u

static int g_fd = -1;
static int g_w = 1280, g_h = 720;
static int g_stride = 0;
static float g_clear[3] = {0, 0, 0};
static int g_presents = 0;

#define LOG(...) do { fprintf(stderr, "gpustub: " __VA_ARGS__); fputc('\n', stderr); } while (0)

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
    LOG("channel connected");
    return 0;
}

int gpustub_connect(void) { return channel_open(); }

int gpustub_alloc(int w, int h) {
    g_w = w;
    g_h = h;
    if (channel_open() != 0) return -1;
    if (send_u32(g_fd, MAGIC) != 0 || send_u32(g_fd, VERSION) != 0) return -1;
    uint32_t status = 0, n = 0;
    if (recv_u32(g_fd, &status) != 0 || status != 0) return -1;
    if (recv_u32(g_fd, &n) != 0) return -1;
    char caps[256];
    if (n > 0 && n < sizeof caps && read_all(g_fd, caps, n) != 0) return -1;
    LOG("handshake caps=%s", caps);
    uint32_t dims[2] = {htonl((uint32_t)w), htonl((uint32_t)h)};
    if (write_all(g_fd, dims, sizeof dims) != 0) return -1;
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
    if (recvmsg(g_fd, &msg, 0) != (ssize_t)iov.iov_len) return -1;
    for (struct cmsghdr *cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm)) {
        if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_RIGHTS) {
            int buf_fd = -1;
            memcpy(&buf_fd, CMSG_DATA(cm), sizeof(int));
            if (buf_fd >= 0) close(buf_fd);
        }
    }
    g_stride = (int)ntohl(hdr[3]);
    LOG("alloc %dx%d stride=%d format=%u", w, h, g_stride, ntohl(hdr[4]));
    return 0;
}

int gpustub_present(const float rgb[3]) {
    if (g_fd < 0) return -1;
    if (send_u32(g_fd, PRES) != 0) return -1;
    if (write_all(g_fd, rgb, sizeof(float) * 3) != 0) return -1;
    uint32_t ack = 0;
    if (recv_u32(g_fd, &ack) != 0) return -1;
    g_presents++;
    if (g_presents <= 5 || (g_presents % 60) == 0)
        LOG("present #%d ack=%u rgb=%.2f,%.2f,%.2f", g_presents, ack, rgb[0], rgb[1], rgb[2]);
    return ack == 0xFFFFFFFFu || ack == 0xFFFFFFFEu ? -1 : 0;
}

void gpustub_set_clear(float r, float g, float b) {
    g_clear[0] = r;
    g_clear[1] = g;
    g_clear[2] = b;
}

const float *gpustub_clear_color(void) { return g_clear; }
int gpustub_width(void) { return g_w; }
int gpustub_height(void) { return g_h; }
