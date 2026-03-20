/*
 * shell.c -- Interactive shell for zOS.
 * Reads commands from UART and executes them.
 */

#include "printf.h"

extern long sys_write(const char *buf, unsigned long len);
extern void sys_yield(void);
extern void sys_sleep(unsigned long ticks);
extern int  sys_getc(void);
extern void sys_exit(void);
extern unsigned long sys_uptime(void);

static int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}

/* Read a line from UART into buf, Returns when Enter is pressed.*/
static int readline(char *buf, int max) {
    int i = 0;

    while (i < max - 1) {
        int c = sys_getc();
        if (c < 0) {
            sys_yield();  /* No input, yield to other tasks. */
            continue;
        }

        if (c == '\r' || c == '\n') {
            printf("\n");
            break;
        }

        if (c == 127 || c == 8) {
            if (i > 0) {
                i--;
                printf("\b \b");
            }
            continue;
        }

        if (c >= 32 && c < 127) {
            buf[i++] = (char)c;
            char ch[2] = {(char)c, 0};
            sys_write(ch, 1);  /* Echo the character. */
        }
    }

    buf[i] = 0;
    return i;
}

/* --- Commands --- */

static void cmd_help(void)
{
    printf("zOS shell commands:\n");
    printf("  help   - show this message\n");
    printf("  echo   - print arguments\n");
    printf("  clear  - clear screen\n");
    printf("  uptime - show tick count\n");
    printf("  hello  - greeting\n");
}

static void cmd_clear(void)
{
    printf("\033[2J\033[H");  /* ANSI escape: clear screen, cursor home */
}

static void cmd_hello(void)
{
    printf("Hello from zOS shell! Running in EL0.\n");
}

static void cmd_uptime(void)
{
    unsigned long ticks = sys_uptime();
    unsigned long seconds = ticks / 100;
    unsigned long ms = (ticks % 100) * 10;
    printf("uptime: %d ticks (%d.%d seconds)\n", ticks, seconds, ms);
}

/* --- Main shell loop --- */

void user_main2(void)
{
    printf("\n");
    printf("============================\n");
    printf("  zOS shell\n");
    printf("============================\n");
    printf("Type 'help' for commands.\n\n");

    char line[128];

    for (;;) {
        printf("zOS> ");
        int len = readline(line, sizeof(line));

        if (len == 0)
            continue;

        if (strcmp(line, "help") == 0) {
            cmd_help();
        } else if (strcmp(line, "clear") == 0) {
            cmd_clear();
        } else if (strcmp(line, "hello") == 0) {
            cmd_hello();
        } else if (strcmp(line, "uptime") == 0) {
            cmd_uptime();
        } else if (line[0] == 'e' && line[1] == 'c' &&
                    line[2] == 'h' && line[3] == 'o' &&
                    line[4] == ' ') {
            printf("%s\n", line + 5);
        } else {
            printf("unknown command: %s\n", line);
        }
    }
}