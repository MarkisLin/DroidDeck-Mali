// Loaded into Steam and its children. Two jobs:
//  * append the GL flags Steam's whitelist drops when steamwebhelper is spawned (execve/posix_spawn),
//  * redirect Steam's bundled ANGLE libraries to the GPU stubs (dlopen).
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *EXTRA[] = {"--use-gl=egl", "--enable-gpu-rasterization", "--ignore-gpu-blocklist",
                             "--ozone-platform=wayland"};
#define NEXTRA ((int)(sizeof EXTRA / sizeof EXTRA[0]))

static void *(*real_dlopen)(const char *, int);
static int (*real_execve)(const char *, char *const[], char *const[]);
static int (*real_spawn)(pid_t *, const char *, const posix_spawn_file_actions_t *,
                         const posix_spawnattr_t *, char *const[], char *const[]);

static void logline(const char *fmt, const char *a, const char *b) {
    FILE *f = fopen("/root/gpustub/hook.log", "a");
    if (!f) return;
    fprintf(f, "pid=%d ", (int)getpid());
    fprintf(f, fmt, a ? a : "-", b ? b : "-");
    fputc('\n', f);
    fclose(f);
}

static void cmdline(char *out, size_t n) {
    out[0] = 0;
    int fd = open("/proc/self/cmdline", O_RDONLY);
    if (fd < 0) return;
    ssize_t r = read(fd, out, n - 1);
    close(fd);
    if (r > 0) out[r] = 0;
}

static const char *type_of(const char *cmd) {
    const char *t = strstr(cmd, "type=");
    return t ? t : "type=?";
}

static int is_helper_path(const char *p) { return p && strstr(p, "steamwebhelper") && !strstr(p, ".bin"); }

static char **augment(char *const argv[]) {
    int n = 0;
    while (argv && argv[n]) n++;
    char **out = calloc((size_t)n + NEXTRA + 1, sizeof(char *));
    if (!out) return NULL;
    for (int i = 0; i < n; i++) out[i] = argv[i];
    for (int i = 0; i < NEXTRA; i++) out[n + i] = (char *)EXTRA[i];
    out[n + NEXTRA] = NULL;
    return out;
}

static void load_core(void) {
    static int done;
    if (done) return;
    done = 1;
    if (!dlopen("/root/gpustub/libgpustubcore.so", RTLD_NOW | RTLD_GLOBAL))
        logline("core failed: %s", dlerror(), NULL);
}

static int g_sym_log;

void *dlopen(const char *path, int flags) {
    if (!real_dlopen) real_dlopen = (void *(*)(const char *, int))dlsym(RTLD_NEXT, "dlopen");
    char cmd[512];
    cmdline(cmd, sizeof cmd);
    if (getenv("BL_GL_REDIRECT") && path && strstr(cmd, "steamwebhelper") &&
        (strstr(path, "libEGL") || strstr(path, "libGLESv2") || strstr(path, "libGL"))) {
        if (strstr(path, "libEGL.so")) {
            load_core();
            logline("redirect EGL from %s", type_of(cmd), path);
            return real_dlopen("/root/gpustub/libEGL.so", flags);
        }
        if (strstr(path, "libGLESv2.so")) {
            load_core();
            logline("redirect GLES from %s", type_of(cmd), path);
            return real_dlopen("/root/gpustub/libGLESv2.so", flags);
        }
    }
    return real_dlopen(path, flags);
}

static const char *cpu_mask(void) {
    static char mask[32];
    static int got;
    if (!got) {
        got = 1;
        const char *e = getenv("BL_CPU_MASK");
        snprintf(mask, sizeof mask, "%s", (e && *e) ? e : "0x7c");
    }
    return mask;
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    if (!real_execve) real_execve = (int (*)(const char *, char *const[], char *const[]))dlsym(RTLD_NEXT, "execve");
    if (path && strstr(path, "taskset") && argv && argv[1] && strncmp(argv[1], "0x", 2) == 0) {
        const char *mask = cpu_mask();
        if (strcmp(argv[1], mask) != 0) {
            int n = 0;
            while (argv[n]) n++;
            char **copy = calloc((size_t)n + 1, sizeof(char *));
            if (copy) {
                for (int i = 0; i < n; i++) copy[i] = argv[i];
                copy[1] = (char *)mask;
                logline("taskset mask %s -> %s", argv[1], mask);
                real_execve(path, copy, envp);
                free(copy);
                return real_execve(path, argv, envp);
            }
        }
    }
    if (getenv("BL_GL_REDIRECT") && is_helper_path(path)) {
        char **argv2 = augment(argv);
        logline("execve helper with gl flags: %s", path, NULL);
        if (argv2) {
            real_execve(path, argv2, envp);
            free(argv2);
        }
    }
    return real_execve(path, argv, envp);
}

int posix_spawn(pid_t *pid, const char *path, const posix_spawn_file_actions_t *fa,
                const posix_spawnattr_t *attr, char *const argv[], char *const envp[]) {
    if (!real_spawn)
        real_spawn = (int (*)(pid_t *, const char *, const posix_spawn_file_actions_t *,
                              const posix_spawnattr_t *, char *const[], char *const[]))dlsym(RTLD_NEXT, "posix_spawn");
    if (is_helper_path(path)) {
        char **argv2 = augment(argv);
        logline("posix_spawn helper with gl flags: %s", path, NULL);
        if (argv2) {
            int rc = real_spawn(pid, path, fa, attr, argv2, envp);
            free(argv2);
            return rc;
        }
    }
    return real_spawn(pid, path, fa, attr, argv, envp);
}

__attribute__((constructor)) static void gpuhook_init(void) {
    if (!real_dlopen) real_dlopen = (void *(*)(const char *, int))dlsym(RTLD_NEXT, "dlopen");
    char cmd[512];
    cmdline(cmd, sizeof cmd);
    if (strstr(cmd, "steam")) logline("hook loaded in %s", cmd, NULL);
}
