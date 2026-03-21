/*
 * gui.c -- GUI rendering loop.
 *
 * Polls mouse, manages cursor, delegates clicks to window manager.
 * Called from timer interrupt handler.
 */

#include "gui.h"
#include "fb.h"
#include "mouse.h"
#include "wm.h"
#include "uart.h"

/* Arrow cursor bitmap (8 wide x 16 tall) */
static const uint8_t cursor_data[16] = {
    0x80, 0xC0, 0xE0, 0xF0,
    0xF8, 0xFC, 0xFE, 0xFC,
    0xF8, 0xD8, 0x8C, 0x0C,
    0x06, 0x06, 0x00, 0x00,
};

static uint32_t cursor_save[16 * 12];
static int cursor_saved_x = -1;
static int cursor_saved_y = -1;
static int prev_buttons = 0;

static void draw_cursor(int x, int y)
{
    uint32_t *fb = fb_get_buffer();
    if (!fb) return;

    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 12; col++) {
            int px = x + col, py = y + row;
            if (px >= 0 && px < FB_WIDTH && py >= 0 && py < FB_HEIGHT)
                cursor_save[row * 12 + col] = fb[py * FB_WIDTH + px];
        }
    }
    cursor_saved_x = x;
    cursor_saved_y = y;

    for (int row = 0; row < 16; row++) {
        uint8_t bits = cursor_data[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col))
                fb_pixel(x + col, y + row, COLOR_WHITE);
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
            int px = cursor_saved_x + col, py = cursor_saved_y + row;
            if (px >= 0 && px < FB_WIDTH && py >= 0 && py < FB_HEIGHT)
                fb[py * FB_WIDTH + px] = cursor_save[row * 12 + col];
        }
    }
}

void gui_init(void)
{
    mouse_init();
    wm_init();

    if (!fb_get_buffer()) return;

    /* Create demo windows */
    int w1 = wm_create_window(100, 80, 400, 300, "Welcome");
    if (w1 >= 0) {
        wm_puts(w1, "Welcome to zOS!\n\n");
        wm_puts(w1, "This is a graphical window.\n");
        wm_puts(w1, "You can drag it by the titlebar.\n");
        wm_puts(w1, "Click [X] to close.\n");
    }

    int w2 = wm_create_window(350, 200, 350, 250, "System Info");
    if (w2 >= 0) {
        wm_puts(w2, "zOS v0.2\n");
        wm_puts(w2, "Architecture: aarch64\n");
        wm_puts(w2, "Display: 1024x768x32\n");
        wm_puts(w2, "Filesystem: ext2 + ramfs\n");
    }

    wm_draw_all();

    struct mouse_state ms = mouse_get();
    draw_cursor(ms.x, ms.y);

    uart_puts("[gui] desktop ready\n");
}

void gui_tick(void)
{
    if (!fb_get_buffer()) return;

    struct mouse_state ms = mouse_get();
    int need_cursor_update = 0;

    /* Detect button press/release */
    int left_pressed  = (ms.buttons & 1) && !(prev_buttons & 1);
    int left_held     = (ms.buttons & 1);
    int left_released = !(ms.buttons & 1) && (prev_buttons & 1);

    if (left_pressed) {
        erase_cursor();
        wm_handle_click(ms.x, ms.y);
        need_cursor_update = 1;
    }

    if (left_held) {
        erase_cursor();
        wm_handle_drag(ms.x, ms.y);
        need_cursor_update = 1;
    }

    if (left_released) {
        wm_handle_release();
    }

    prev_buttons = ms.buttons;

    /* Redraw windows if dirty */
    wm_draw_all();

    /* Redraw cursor if moved or screen changed */
    if (ms.x != cursor_saved_x || ms.y != cursor_saved_y || need_cursor_update) {
        erase_cursor();
        draw_cursor(ms.x, ms.y);
    }
}
