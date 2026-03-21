/*
 * wm.c -- Window manager.
 *
 * Windows have a text buffer that persists across redraws.
 * Only redraws when a dirty flag is set (move, focus change, close).
 */

#include "wm.h"
#include "fb.h"
#include "uart.h"

static struct window windows[MAX_WINDOWS];
static int focus_id = -1;
static int drag_id = -1;
static int drag_off_x, drag_off_y;
static int dirty = 1;

#define TITLE_ACTIVE   0x00336699
#define TITLE_INACTIVE 0x00555555
#define TITLE_TEXT     0x00FFFFFF
#define CLOSE_BG       0x00CC3333
#define CLOSE_TEXT     0x00FFFFFF
#define BORDER_COLOR   0x00222222
#define CONTENT_BG     0x00111111

static void strcopy(char *dst, const char *src, int max)
{
    int i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

void wm_init(void)
{
    for (int i = 0; i < MAX_WINDOWS; i++)
        windows[i].active = 0;
    dirty = 1;
}

int wm_create_window(int x, int y, int w, int h, const char *title)
{
    int id = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].active) { id = i; break; }
    }
    if (id < 0) return -1;

    struct window *win = &windows[id];
    win->id = id;
    win->active = 1;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    strcopy(win->title, title, 32);
    win->bg_color = CONTENT_BG;

    win->content_w = w - 2;
    win->content_h = h - TITLEBAR_H - 1;
    win->cols = win->content_w / 8;
    win->rows = win->content_h / 16;
    if (win->cols > WIN_MAX_COLS) win->cols = WIN_MAX_COLS;
    if (win->rows > WIN_MAX_ROWS) win->rows = WIN_MAX_ROWS;
    win->cursor_col = 0;
    win->cursor_row = 0;
    win->text_rows_used = 0;

    /* Clear text buffer */
    for (int r = 0; r < WIN_MAX_ROWS; r++)
        for (int c = 0; c < WIN_MAX_COLS; c++)
            win->text[r][c] = 0;

    focus_id = id;
    dirty = 1;
    return id;
}

void wm_close_window(int id)
{
    if (id < 0 || id >= MAX_WINDOWS) return;
    windows[id].active = 0;
    if (focus_id == id) {
        focus_id = -1;
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (windows[i].active) { focus_id = i; break; }
        }
    }
    dirty = 1;
}

static void draw_window(struct window *win)
{
    int x = win->x, y = win->y, w = win->w, h = win->h;
    int is_focused = (win->id == focus_id);

    /* Border */
    fb_rect(x, y, w, h, BORDER_COLOR);

    /* Titlebar */
    uint32_t tc = is_focused ? TITLE_ACTIVE : TITLE_INACTIVE;
    fb_rect(x + 1, y + 1, w - 2, TITLEBAR_H - 1, tc);
    fb_text(x + 4, y + 3, win->title, TITLE_TEXT, tc);

    /* Close button */
    int cx = x + w - CLOSE_BTN_W - 2;
    fb_rect(cx, y + 2, CLOSE_BTN_W, TITLEBAR_H - 4, CLOSE_BG);
    fb_char(cx + 4, y + 3, 'X', CLOSE_TEXT, CLOSE_BG);

    /* Content area */
    int content_x = x + 1;
    int content_y = y + TITLEBAR_H;
    fb_rect(content_x, content_y, win->content_w, win->content_h, win->bg_color);

    /* Redraw text from buffer */
    for (int r = 0; r < win->rows; r++) {
        for (int c = 0; c < win->cols; c++) {
            char ch = win->text[r][c];
            if (ch && ch >= 32)
                fb_char(content_x + c * 8, content_y + r * 16,
                        ch, COLOR_WHITE, win->bg_color);
        }
    }
}

int wm_draw_all(void)
{
    if (!fb_get_buffer() || !dirty) return 0;
    dirty = 0;

    /* Desktop background */
    fb_fill(COLOR_DARKGRAY);
    fb_text(FB_WIDTH / 2 - 40, 10, "zOS Desktop", COLOR_WHITE, COLOR_DARKGRAY);

    /* Taskbar */
    fb_rect(0, FB_HEIGHT - 32, FB_WIDTH, 32, COLOR_GRAY);
    fb_text(8, FB_HEIGHT - 24, "zOS", COLOR_WHITE, COLOR_GRAY);

    /* Draw unfocused windows first, focused last (on top) */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].active && i != focus_id)
            draw_window(&windows[i]);
    }
    if (focus_id >= 0 && windows[focus_id].active)
        draw_window(&windows[focus_id]);

    return 1;
}

void wm_handle_click(int mx, int my)
{
    /* Check focused window first, then others */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < MAX_WINDOWS; i++) {
            struct window *win = &windows[i];
            if (!win->active) continue;
            if (pass == 0 && i != focus_id) continue;
            if (pass == 1 && i == focus_id) continue;

            if (mx >= win->x && mx < win->x + win->w &&
                my >= win->y && my < win->y + win->h) {

                /* Close button */
                int cx = win->x + win->w - CLOSE_BTN_W - 2;
                if (mx >= cx && mx < cx + CLOSE_BTN_W &&
                    my >= win->y + 2 && my < win->y + TITLEBAR_H - 2) {
                    wm_close_window(i);
                    return;
                }

                /* Titlebar drag */
                if (my >= win->y && my < win->y + TITLEBAR_H) {
                    drag_id = i;
                    drag_off_x = mx - win->x;
                    drag_off_y = my - win->y;
                }

                /* Focus */
                if (focus_id != i) {
                    focus_id = i;
                    dirty = 1;
                }
                return;
            }
        }
    }
}

void wm_handle_drag(int mx, int my)
{
    if (drag_id < 0 || !windows[drag_id].active) return;

    int new_x = mx - drag_off_x;
    int new_y = my - drag_off_y;

    if (new_x != windows[drag_id].x || new_y != windows[drag_id].y) {
        windows[drag_id].x = new_x;
        windows[drag_id].y = new_y;
        dirty = 1;
    }
}

void wm_handle_release(void)
{
    drag_id = -1;
}

struct window *wm_get_window(int id)
{
    if (id < 0 || id >= MAX_WINDOWS || !windows[id].active) return NULL;
    return &windows[id];
}

void wm_putc(int win_id, char c)
{
    struct window *win = wm_get_window(win_id);
    if (!win) return;

    if (c == '\b' || c == 127) {
        /* Backspace: move cursor back, clear character */
        if (win->cursor_col > 0) {
            win->cursor_col--;
            if (win->cursor_row < WIN_MAX_ROWS && win->cursor_col < WIN_MAX_COLS)
                win->text[win->cursor_row][win->cursor_col] = ' ';
        }
    } else if (c == '\n') {
        win->cursor_col = 0;
        win->cursor_row++;
    } else if (c == '\r') {
        win->cursor_col = 0;
    } else if (c >= 32) {
        if (win->cursor_row < WIN_MAX_ROWS && win->cursor_col < WIN_MAX_COLS)
            win->text[win->cursor_row][win->cursor_col] = c;
        win->cursor_col++;
        if (win->cursor_col >= win->cols) {
            win->cursor_col = 0;
            win->cursor_row++;
        }
    }

    if (win->cursor_row > win->text_rows_used)
        win->text_rows_used = win->cursor_row;

    if (win->cursor_row >= win->rows) {
        /* Scroll: shift text buffer up by one row */
        for (int r = 0; r < win->rows - 1; r++)
            for (int c = 0; c < WIN_MAX_COLS; c++)
                win->text[r][c] = win->text[r + 1][c];
        for (int c = 0; c < WIN_MAX_COLS; c++)
            win->text[win->rows - 1][c] = 0;
        win->cursor_row = win->rows - 1;
        win->cursor_col = 0;
    }

    dirty = 1;
}

void wm_puts(int win_id, const char *s)
{
    while (*s)
        wm_putc(win_id, *s++);
}
