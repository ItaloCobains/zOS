/*
 * gui.c -- GUI rendering loop.
 *
 * Polls mouse, manages cursor, delegates clicks to window manager.
 * Called from timer interrupt handler.
 */

#include "gui.h"
#include "fb.h"
#include "mouse.h"
#include "keyboard.h"
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

    /* Windows are created by gfx_console_init and gui later */
    wm_draw_all();

    struct mouse_state ms = mouse_get();
    draw_cursor(ms.x, ms.y);

    uart_puts("[gui] desktop ready\n");
}

void gui_tick(void)
{
    if (!fb_get_buffer()) return;

    keyboard_poll();

    struct mouse_state ms = mouse_get();
    int need_cursor_update = 0;

    /* Detect button press/release */
    int left_pressed  = (ms.buttons & 1) && !(prev_buttons & 1);
    int left_held     = (ms.buttons & 1);
    int left_released = !(ms.buttons & 1) && (prev_buttons & 1);

    if (left_pressed) {
        erase_cursor();
        /* Check taskbar buttons first */
        if (wm_check_taskbar_click(ms.x, ms.y)) {
            static int term_x = 80;
            int win = wm_create_window(term_x, 60, 500, 380, "Terminal");
            if (win >= 0) {
                wm_puts(win, "New terminal window\n");
                term_x += 30;
                if (term_x > 400) term_x = 80;
            }
        } else {
            wm_handle_click(ms.x, ms.y);
        }
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

    /* Redraw windows if dirty -- invalidates cursor save */
    int was_dirty = wm_draw_all();
    if (was_dirty)
        cursor_saved_x = -1;  /* force cursor redraw without stale restore */

    /* Redraw cursor if moved or screen was redrawn */
    if (ms.x != cursor_saved_x || ms.y != cursor_saved_y ||
        need_cursor_update || was_dirty) {
        if (!was_dirty)
            erase_cursor();  /* only erase if screen wasn't fully redrawn */
        draw_cursor(ms.x, ms.y);
    }
}
