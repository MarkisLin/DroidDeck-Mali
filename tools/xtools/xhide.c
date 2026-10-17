#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

/* xhide <map|unmap> : 隐藏/恢复 class 含 steamwebhelper 且尺寸 >= 640x480 的窗口 */
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: xhide map|unmap\n"); return 2; }
    int want_map = strcmp(argv[1], "map") == 0;
    Display *d = XOpenDisplay(NULL);
    if (!d) { fprintf(stderr, "xhide: no display\n"); return 1; }
    Window root = DefaultRootWindow(d);
    Window r, parent, *kids = NULL;
    unsigned n = 0;
    XQueryTree(d, root, &r, &parent, &kids, &n);
    int touched = 0;
    for (int i = 0; i < (int)n; i++) {
        XWindowAttributes a;
        if (!XGetWindowAttributes(d, kids[i], &a)) continue;
        XClassHint cls;
        int is_steam = 0;
        if (XGetClassHint(d, kids[i], &cls)) {
            if ((cls.res_class && strstr(cls.res_class, "steamwebhelper")) ||
                (cls.res_name && strstr(cls.res_name, "steamwebhelper"))) is_steam = 1;
        }
        if (!is_steam) continue;
        if (a.width < 640 || a.height < 480) continue;
        if (want_map) XMapWindow(d, kids[i]);
        else XUnmapWindow(d, kids[i]);
        fprintf(stderr, "xhide: %s 0x%08lx %dx%d\n", want_map ? "map" : "unmap", kids[i], a.width, a.height);
        touched++;
    }
    XSync(d, False);
    if (kids) XFree(kids);
    fprintf(stderr, "xhide: %d window(s)\n", touched);
    XCloseDisplay(d);
    return touched ? 0 : 1;
}
