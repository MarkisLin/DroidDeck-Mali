#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

static void dump(Display *d, Window w, int depth) {
    XWindowAttributes a;
    if (!XGetWindowAttributes(d, w, &a)) return;
    char *name = NULL;
    XFetchName(d, w, &name);
    XClassHint cls;
    const char *res_name = "", *res_class = "";
    if (XGetClassHint(d, w, &cls)) { res_name = cls.res_name ? cls.res_name : ""; res_class = cls.res_class ? cls.res_class : ""; }
    printf("%*s0x%08lx %dx%d map=%d name='%s' class='%s/%s'\n", depth * 2, "", w,
           a.width, a.height, a.map_state, name ? name : "", res_name, res_class);
    if (name) XFree(name);
    if (a.map_state == IsViewable && depth < 2) {
        Window r, parent, *kids = NULL;
        unsigned n = 0;
        if (XQueryTree(d, w, &r, &parent, &kids, &n)) {
            for (int i = 0; i < (int)n; i++) dump(d, kids[i], depth + 1);
            if (kids) XFree(kids);
        }
    }
}

int main(void) {
    Display *d = XOpenDisplay(NULL);
    if (!d) { fprintf(stderr, "xwindows: no display\n"); return 1; }
    Window root = DefaultRootWindow(d);
    printf("root 0x%08lx\n", root);
    Window r, parent, *kids = NULL;
    unsigned n = 0;
    XQueryTree(d, root, &r, &parent, &kids, &n);
    for (int i = 0; i < (int)n; i++) dump(d, kids[i], 1);
    if (kids) XFree(kids);
    XCloseDisplay(d);
    return 0;
}
