#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <arpa/inet.h>

static int read_all(int fd, void *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = read(fd, (char *)buf + off, n - off);
        if (r <= 0) return -1;
        off += (size_t)r;
    }
    return 0;
}

static int recv_fd(int sock, unsigned int hdr[5], int *out_fd) {
    struct iovec iov;
    iov.iov_base = hdr;
    iov.iov_len = sizeof(unsigned int) * 5;
    char cbuf[CMSG_SPACE(sizeof(int))];
    memset(cbuf, 0, sizeof cbuf);
    struct msghdr msg;
    memset(&msg, 0, sizeof msg);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cbuf;
    msg.msg_controllen = sizeof cbuf;
    if (recvmsg(sock, &msg, 0) != (ssize_t)iov.iov_len) return -1;
    for (struct cmsghdr *cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm)) {
        if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_RIGHTS) {
            memcpy(out_fd, CMSG_DATA(cm), sizeof(int));
            return 0;
        }
    }
    return -1;
}

int main(int argc, char **argv) {
    unsigned int w = (argc > 1) ? (unsigned int)atoi(argv[1]) : 1280;
    unsigned int h = (argc > 2) ? (unsigned int)atoi(argv[2]) : 720;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    const char *name = "droiddeck-gpu";
    a.sun_path[0] = 0;
    memcpy(a.sun_path + 1, name, strlen(name));
    socklen_t len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + strlen(name));
    if (connect(fd, (struct sockaddr *)&a, len) < 0) { perror("connect"); return 1; }
    unsigned int magic = htonl(0x44475055u), ver = htonl(1u);
    unsigned int wh[2] = { htonl(w), htonl(h) };
    if (write(fd, &magic, 4) != 4 || write(fd, &ver, 4) != 4) { perror("write hello"); return 1; }
    unsigned int status = 0, n = 0;
    if (read_all(fd, &status, 4) < 0) { perror("read caps status"); return 1; }
    status = ntohl(status);
    if (status != 0) { printf("handshake rejected: %u\n", status); return 1; }
    if (read_all(fd, &n, 4) < 0) { perror("read caps len"); return 1; }
    n = ntohl(n);
    char buf[256] = {0};
    if (n > 0 && n < sizeof buf && read_all(fd, buf, n) < 0) { perror("read caps"); return 1; }
    printf("handshake: %s\n", buf);
    if (write(fd, wh, sizeof wh) != (ssize_t)sizeof wh) { perror("write alloc"); return 1; }
    unsigned int hdr[5] = {0};
    int buf_fd = -1;
    if (recv_fd(fd, hdr, &buf_fd) != 0) { perror("recv fd"); return 1; }
    printf("alloc: status=%u %ux%u stride=%u format=%u fd=%d\n",
           ntohl(hdr[0]), ntohl(hdr[1]), ntohl(hdr[2]), ntohl(hdr[3]), ntohl(hdr[4]), buf_fd);
    if (buf_fd >= 0) {
        struct stat st;
        if (fstat(buf_fd, &st) == 0) printf("fd fstat: size=%lld\n", (long long)st.st_size);
        close(buf_fd);
    }
    close(fd);
    return 0;
}
