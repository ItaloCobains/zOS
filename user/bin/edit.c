/*
 * edit.c -- Simple text editor for zOS.
 * Usage: edit <filename>
 * Ctrl+S to save, Ctrl+Q to quit.
 */

#include "../lib/ulib.h"

#define MAX_LINES 100
#define MAX_COLS  80

static char lines[MAX_LINES][MAX_COLS];
static int num_lines = 1;
static int cur_row = 0, cur_col = 0;
static char filename[64];
static int modified = 0;

static void strcopy(char *dst, const char *src, int max)
{
    int i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static void redraw(void)
{
    /* Clear screen */
    printf("\033[2J\033[H");

    /* Header */
    printf("=== edit: %s %s===\n", filename, modified ? "[modified] " : "");

    /* Content */
    int display_rows = 20;
    int start = 0;
    if (cur_row >= display_rows) start = cur_row - display_rows + 1;

    for (int r = start; r < start + display_rows && r < MAX_LINES; r++) {
        if (r < num_lines)
            printf("%s", lines[r]);
        printf("\n");
    }

    /* Status bar */
    printf("--- Ln %d Col %d | Ctrl+S save | Ctrl+Q quit ---\n", cur_row + 1, cur_col + 1);
}

static void load_file(const char *path)
{
    int fd = sys_open(path, 0);
    if (fd < 0) return;

    char buf[4096];
    int n = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);
    if (n <= 0) return;
    buf[n] = 0;

    num_lines = 0;
    int col = 0;
    for (int i = 0; i < n && num_lines < MAX_LINES; i++) {
        if (buf[i] == '\n') {
            lines[num_lines][col] = 0;
            num_lines++;
            col = 0;
        } else if (col < MAX_COLS - 1) {
            lines[num_lines][col++] = buf[i];
        }
    }
    if (col > 0) {
        lines[num_lines][col] = 0;
        num_lines++;
    }
    if (num_lines == 0) num_lines = 1;
}

static void save_file(const char *path)
{
    int fd = sys_open(path, 4); /* O_CREATE */
    if (fd < 0) { printf("Cannot save!\n"); return; }

    for (int r = 0; r < num_lines; r++) {
        int len = 0;
        while (lines[r][len]) len++;
        if (len > 0)
            sys_write(fd, lines[r], len);
        sys_write(fd, "\n", 1);
    }
    sys_close(fd);
    modified = 0;
}

int main(const char *args)
{
    if (!args || !args[0]) {
        printf("usage: edit <file>\n");
        return 1;
    }

    strcopy(filename, args, sizeof(filename));

    /* Init empty buffer */
    for (int r = 0; r < MAX_LINES; r++)
        for (int c = 0; c < MAX_COLS; c++)
            lines[r][c] = 0;

    load_file(filename);
    redraw();

    for (;;) {
        char ch;
        int n = sys_read(0, &ch, 1);
        if (n <= 0) { sys_yield(); continue; }
        int c = (unsigned char)ch;

        if (c == 17) { /* Ctrl+Q */
            printf("\033[2J\033[H");
            return 0;
        }

        if (c == 19) { /* Ctrl+S */
            save_file(filename);
            redraw();
            continue;
        }

        if (c == '\r' || c == '\n') {
            /* Insert new line */
            if (num_lines < MAX_LINES - 1) {
                /* Shift lines down */
                for (int r = num_lines; r > cur_row + 1; r--)
                    for (int cc = 0; cc < MAX_COLS; cc++)
                        lines[r][cc] = lines[r-1][cc];
                /* Split current line */
                for (int cc = 0; cc < MAX_COLS; cc++)
                    lines[cur_row + 1][cc] = 0;
                int col = cur_col;
                int dc = 0;
                while (lines[cur_row][col]) {
                    lines[cur_row + 1][dc++] = lines[cur_row][col];
                    lines[cur_row][col] = 0;
                    col++;
                }
                num_lines++;
                cur_row++;
                cur_col = 0;
                modified = 1;
            }
            redraw();
            continue;
        }

        if (c == '\b' || c == 127) {
            if (cur_col > 0) {
                cur_col--;
                /* Shift chars left */
                for (int cc = cur_col; cc < MAX_COLS - 1; cc++)
                    lines[cur_row][cc] = lines[cur_row][cc + 1];
                modified = 1;
            } else if (cur_row > 0) {
                /* Join with previous line */
                int prev_len = 0;
                while (lines[cur_row - 1][prev_len]) prev_len++;
                int cur_len = 0;
                while (lines[cur_row][cur_len]) cur_len++;
                if (prev_len + cur_len < MAX_COLS) {
                    for (int cc = 0; cc < cur_len; cc++)
                        lines[cur_row - 1][prev_len + cc] = lines[cur_row][cc];
                    /* Shift lines up */
                    for (int r = cur_row; r < num_lines - 1; r++)
                        for (int cc = 0; cc < MAX_COLS; cc++)
                            lines[r][cc] = lines[r+1][cc];
                    for (int cc = 0; cc < MAX_COLS; cc++)
                        lines[num_lines - 1][cc] = 0;
                    num_lines--;
                    cur_row--;
                    cur_col = prev_len;
                    modified = 1;
                }
            }
            redraw();
            continue;
        }

        if (c >= 32 && c < 127) {
            int len = 0;
            while (lines[cur_row][len]) len++;
            if (len < MAX_COLS - 2) {
                /* Shift chars right */
                for (int cc = len + 1; cc > cur_col; cc--)
                    lines[cur_row][cc] = lines[cur_row][cc - 1];
                lines[cur_row][cur_col] = (char)c;
                cur_col++;
                modified = 1;
            }
            redraw();
        }
    }
}
