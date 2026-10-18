#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
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

int main(void) {
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
    if (write(fd, &magic, 4) != 4 || write(fd, &ver, 4) != 4) { perror("write"); return 1; }
    unsigned int status = 0, n = 0;
    if (read_all(fd, &status, 4) < 0 || read_all(fd, &n, 4) < 0) { perror("read"); return 1; }
    status = ntohl(status); n = ntohl(n);
    char buf[256] = {0};
    if (n > 0 && n < sizeof buf && read_all(fd, buf, n) < 0) { perror("read caps"); return 1; }
    printf("status=%u caps=%s\n", status, buf);
    close(fd);
    return 0;
}
