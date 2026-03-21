/*
 * login.c -- Login screen for zOS.
 * Prompts for username and password, then execs the shell.
 * Hardcoded users: root (no password), user (password: zos)
 */

#include "../lib/ulib.h"

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

static int readline_noecho(char *buf, int max)
{
    int i = 0;
    while (i < max - 1) {
        char ch;
        int n = sys_read(0, &ch, 1);
        if (n <= 0) { sys_yield(); continue; }
        if (ch == '\r' || ch == '\n') { printf("\n"); break; }
        if ((ch == 127 || ch == 8) && i > 0) {
            i--;
            continue;
        }
        if (ch >= 32 && ch < 127)
            buf[i++] = ch;
    }
    buf[i] = 0;
    return i;
}

static int readline_echo(char *buf, int max)
{
    int i = 0;
    while (i < max - 1) {
        char ch;
        int n = sys_read(0, &ch, 1);
        if (n <= 0) { sys_yield(); continue; }
        if (ch == '\r' || ch == '\n') { printf("\n"); break; }
        if ((ch == 127 || ch == 8) && i > 0) {
            i--;
            printf("\b \b");
            continue;
        }
        if (ch >= 32 && ch < 127) {
            buf[i++] = ch;
            sys_write(1, &ch, 1);
        }
    }
    buf[i] = 0;
    return i;
}

int main(const char *args)
{
    (void)args;

    for (;;) {
        printf("\n");
        printf("============================\n");
        printf("  zOS login\n");
        printf("============================\n\n");

        char user[32], pass[32];

        printf("username: ");
        readline_echo(user, sizeof(user));

        printf("password: ");
        readline_noecho(pass, sizeof(pass));

        /* Authenticate */
        int ok = 0;
        if (streq(user, "root") && pass[0] == 0) ok = 1;
        if (streq(user, "user") && streq(pass, "zos")) ok = 1;

        if (ok) {
            printf("Welcome, %s!\n\n", user);
            /* Exec the shell */
            sys_exec("/bin/shell", NULL);
            /* If exec fails */
            printf("Failed to start shell\n");
        } else {
            printf("Login incorrect.\n");
        }
    }
}
