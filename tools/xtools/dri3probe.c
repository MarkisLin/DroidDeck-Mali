#include <stdio.h>
#include <X11/Xlib.h>

int main(void) {
    Display *d = XOpenDisplay(NULL);
    if (!d) { printf("cannot open display\n"); return 2; }
    int opcode, first_event, first_error;
    const char *names[] = {"DRI3", "DRI2", "PRESENT", "RANDR", "MIT-SHM", "X-Resource", "XFIXES"};
    for (unsigned i = 0; i < sizeof(names)/sizeof(names[0]); i++) {
        Bool present = XQueryExtension(d, names[i], &opcode, &first_event, &first_error);
        printf("%-10s %s\n", names[i], present ? "present" : "ABSENT");
    }
    printf("vendor: %s\n", ServerVendor(d));
    printf("version: %d\n", VendorRelease(d));
    XCloseDisplay(d);
    return 0;
}
