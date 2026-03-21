/*
 * gui.c -- GUI rendering loop.
 *
 * Runs as a kernel background task: polls mouse, draws cursor,
 * redraws desktop. This is the foundation for the window manager.
 */

#include "types.h"
#include "gui.h"
#include "fb.h"
#include "mouse.h"
#include "uart.h"

/* Simple 12x16 arrow cursor bitmap */
static const uint8_t cursor_data[16] = {
    0x80, 0xC0, 0xE0, 0xF0,
    0xF8, 0xFC, 0xFE, 0xFC,
    0xF8, 0xD8, 0x8C, 0x0C,
    0x06, 0x06, 0x00, 0x00,
};

/* Save area behind cursor to restore when cursor moves */
static uint32_t cursor_save[16 * 12];
static int cursor_saved_x = -1;
static int cursor_saved_y = -1;

static void draw_cursor(int x, int y)
{
    uint32_t *fb = fb_get_buffer();
    if (!fb) return;

    /* Save pixels behind cursor */
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 12; col++) {
            int px = x + col;
            int py = y + row;
            if (px >= 0 && px < FB_WIDTH && py >= 0 && py < FB_HEIGHT)
                cursor_save[row * 12 + col] = fb[py * FB_WIDTH + px];
        }
    }
    cursor_saved_x = x;
    cursor_saved_y = y;

    /* Draw cursor */
    for (int row = 0; row < 16; row++) {
        uint8_t bits = cursor_data[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                fb_pixel(x + col, y + row, COLOR_WHITE);
                /* Black outline */
                if (col > 0 && !(bits & (0x80 >> (col - 1))))
                    fb_pixel(x + col - 1, y + row, COLOR_BLACK);
            }
        }
    }
}

static void erase_cursor(void)
{
    if (cursor_saved_x < 0) return;

    uint32_t *fb = fb_get_buffer();
    if (!fb) return;

    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 12; col++) {
            int px = cursor_saved_x + col;
            int py = cursor_saved_y + row;
            if (px >= 0 && px < FB_WIDTH && py >= 0 && py < FB_HEIGHT)
                fb[py * FB_WIDTH + px] = cursor_save[row * 12 + col];
        }
    }
}

void gui_init(void)
{
    mouse_init();

    if (!fb_get_buffer()) return;

    /* Draw desktop background */
    fb_fill(COLOR_DARKGRAY);

    /* Taskbar at bottom */
    fb_rect(0, FB_HEIGHT - 32, FB_WIDTH, 32, COLOR_GRAY);
    fb_text(8, FB_HEIGHT - 24, "zOS", COLOR_WHITE, COLOR_GRAY);

    /* Title text */
    fb_text(FB_WIDTH / 2 - 40, 10, "zOS Desktop", COLOR_WHITE, COLOR_DARKGRAY);

    /* Draw initial cursor */
    struct mouse_state ms = mouse_get();
    draw_cursor(ms.x, ms.y);

    uart_puts("[gui] desktop ready\n");
}

void gui_tick(void)
{
    if (!fb_get_buffer()) return;

    struct mouse_state ms = mouse_get();

    if (ms.x != cursor_saved_x || ms.y != cursor_saved_y) {
        erase_cursor();
        draw_cursor(ms.x, ms.y);
    }
}
