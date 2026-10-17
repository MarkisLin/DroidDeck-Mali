#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

/* xraise <hex-id> | xraise --newest : 把指定窗口（或最新映射的全屏窗口）提到最前并聚焦 */
int main(int argc, char **argv) {
    Display *d = XOpenDisplay(NULL);
    if (!d) { fprintf(stderr, "xraise: no display\n"); return 1; }
    Window root = DefaultRootWindow(d);

    if (argc >= 2 && strcmp(argv[1], "--game") == 0) {
        Window r2, parent2, *kids2 = NULL;
        unsigned n2 = 0;
        if (!XQueryTree(d, root, &r2, &parent2, &kids2, &n2)) return 1;
        Window best = 0;
        for (int i = (int)n2 - 1; i >= 0; i--) {
            XWindowAttributes a;
            if (!XGetWindowAttributes(d, kids2[i], &a)) continue;
            if (a.map_state != IsViewable) continue;
            if (a.width < 640 || a.height < 480) continue;
            XClassHint cls;
            int steam = 0;
            if (XGetClassHint(d, kids2[i], &cls)) {
                if ((cls.res_class && strstr(cls.res_class, "steamwebhelper")) ||
                    (cls.res_name && strstr(cls.res_name, "steamwebhelper"))) steam = 1;
            }
            if (steam) continue;
            best = kids2[i];
            break;
        }
        if (kids2) XFree(kids2);
        if (!best) { fprintf(stderr, "xraise: no game window\n"); return 1; }
        XRaiseWindow(d, best);
        XSetInputFocus(d, best, RevertToParent, CurrentTime);
        XSync(d, False);
        fprintf(stderr, "xraise: raised game 0x%08lx\n", best);
    } else if (argc >= 2 && strcmp(argv[1], "--newest") == 0) {
        Window r, parent, *kids = NULL;
        unsigned n = 0;
        if (!XQueryTree(d, root, &r, &parent, &kids, &n)) return 1;
        Window best = 0;
        for (int i = (int)n - 1; i >= 0; i--) {
            XWindowAttributes a;
            if (!XGetWindowAttributes(d, kids[i], &a)) continue;
            if (a.map_state != IsViewable) continue;
            if (a.width < 320 || a.height < 240) continue;
            best = kids[i];
            break;
        }
        if (kids) XFree(kids);
        if (!best) { fprintf(stderr, "xraise: no candidate window\n"); return 1; }
        XRaiseWindow(d, best);
        XSetInputFocus(d, best, RevertToParent, CurrentTime);
        XSync(d, False);
        fprintf(stderr, "xraise: raised 0x%08lx\n", best);
    } else if (argc >= 2) {
        Window w = (Window)strtoul(argv[1], NULL, 0);
        XRaiseWindow(d, w);
        XSetInputFocus(d, w, RevertToParent, CurrentTime);
        XSync(d, False);
        fprintf(stderr, "xraise: raised 0x%08lx\n", w);
    } else {
        fprintf(stderr, "usage: xraise <winid> | xraise --newest\n");
        return 2;
    }
    XCloseDisplay(d);
    return 0;
}
