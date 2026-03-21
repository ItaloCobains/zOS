#ifndef ZOS_WM_H
#define ZOS_WM_H

#include "types.h"

#define MAX_WINDOWS    8
#define TITLEBAR_H    20
#define CLOSE_BTN_W   16
#define WIN_MAX_COLS   80
#define WIN_MAX_ROWS   30

struct window {
    int    id;
    int    active;
    int    x, y, w, h;
    char   title[32];
    uint32_t bg_color;

    int    content_w, content_h;
    int    cursor_col, cursor_row;
    int    cols, rows;

    /* Text buffer: stores characters for redraw */
    char   text[WIN_MAX_ROWS][WIN_MAX_COLS];
    int    text_rows_used;
};

void wm_init(void);
int  wm_create_window(int x, int y, int w, int h, const char *title);
void wm_close_window(int id);
void wm_draw_all(void);
void wm_handle_click(int mx, int my);
void wm_handle_drag(int mx, int my);
void wm_handle_release(void);
struct window *wm_get_window(int id);

/* Write a character to a window's console */
void wm_putc(int win_id, char c);
void wm_puts(int win_id, const char *s);

#endif
