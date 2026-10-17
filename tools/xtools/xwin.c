/* xwin.c — 极简 X11 客户端：开一个窗口，用 put_image 画彩色渐变并循环刷新
 * 目的：判断 gamescope 的 Xwayland 到底会不会合成 X 客户端画面。
 */
#include <xcb/xcb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
   xcb_connection_t *c = xcb_connect(NULL, NULL);
   if (xcb_connection_has_error(c)) { fprintf(stderr, "xwin: no display\n"); return 1; }
   xcb_screen_t *s = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
   int W = s->width_in_pixels, H = s->height_in_pixels;
   printf("xwin: screen %dx%d\n", W, H);
   fflush(stdout);

   xcb_window_t w = xcb_generate_id(c);
   xcb_create_window(c, XCB_COPY_FROM_PARENT, w, s->root, 0, 0, W, H, 0,
                     XCB_WINDOW_CLASS_INPUT_OUTPUT, s->root_visual,
                     XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
                     (uint32_t[]){ s->black_pixel, XCB_EVENT_MASK_EXPOSURE });
   xcb_map_window(c, w);
   xcb_flush(c);

   size_t n = (size_t)W * H;
   uint32_t *px = calloc(n, 4);
   if (!px) return 1;
   xcb_gcontext_t gc = xcb_generate_id(c);
   xcb_create_gc(c, gc, w, 0, NULL);

   printf("xwin: window 0x%08x mapped, drawing...\n", w);
   fflush(stdout);
   for (int f = 0; f < 3600; f++) {
      for (int y = 0; y < H; y++) {
         uint32_t color = ((uint32_t)((y + f * 4) & 0xff) << 16) |
                          ((uint32_t)((y * 2 + f) & 0xff) << 8) | 0x40;
         for (int x = 0; x < W; x++) px[(size_t)y * W + x] = color;
      }
      xcb_put_image(c, XCB_IMAGE_FORMAT_Z_PIXMAP, w, gc, W, H, 0, 0, 0,
                    s->root_depth, (uint32_t)(n * 4), (const uint8_t *)px);
      xcb_flush(c);
      if ((f % 60) == 0) { printf("xwin: frame %d\n", f); fflush(stdout); }
      usleep(33000);
   }
   return 0;
}
