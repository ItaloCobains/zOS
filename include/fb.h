#ifndef ZOS_FB_H
#define ZOS_FB_H

#include "types.h"

#define FB_WIDTH  1024
#define FB_HEIGHT 768
#define FB_BPP    4    /* bytes per pixel (32-bit XRGB) */
#define FB_STRIDE (FB_WIDTH * FB_BPP)
#define FB_SIZE   (FB_STRIDE * FB_HEIGHT)

/* Colors (XRGB8888) */
#define COLOR_BLACK   0x00000000
#define COLOR_WHITE   0x00FFFFFF
#define COLOR_RED     0x00FF0000
#define COLOR_GREEN   0x0000FF00
#define COLOR_BLUE    0x000000FF
#define COLOR_GRAY    0x00808080
#define COLOR_DARKGRAY 0x00404040
#define COLOR_LIGHTGRAY 0x00C0C0C0
#define COLOR_CYAN    0x0000AAAA
#define COLOR_YELLOW  0x00FFFF00

void fb_init(void);
void fb_pixel(int x, int y, uint32_t color);
void fb_rect(int x, int y, int w, int h, uint32_t color);
void fb_fill(uint32_t color);
void fb_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void fb_text(int x, int y, const char *s, uint32_t fg, uint32_t bg);

/* Get raw framebuffer pointer */
uint32_t *fb_get_buffer(void);

#endif
