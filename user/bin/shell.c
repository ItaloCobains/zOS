/*
 * shell.c -- zOS shell.
 * Uses fork+exec+wait. Supports output redirection with '>'.
 */

#include "../lib/ulib.h"

static int readline(char *buf, int max)
{
    int i = 0;
    while (i < max - 1) {
        int c = sys_getc();
        if (c < 0) { sys_yield(); continue; }
        if (c == '\r' || c == '\n') { printf("\n"); break; }
        if ((c == 127 || c == 8) && i > 0) {
            i--;
            printf("\b \b");
            continue;
        }
        if (c >= 32 && c < 127) {
            buf[i++] = (char)c;
            sys_write(1, (char[]){(char)c}, 1);
        }
    }
    buf[i] = 0;
    return i;
}

/*
 * Parse a command line. Extracts:
 *   - command name (first word)
 *   - arguments (rest, before any '>')
 *   - redirect file (after '>', if present)
 */
static void parse_line(char *line, char **cmd, char **args, char **redir)
{
    *cmd = line;
    *args = NULL;
    *redir = NULL;

    /* Find first space -> start of args */
    char *p = line;
    while (*p && *p != ' ' && *p != '>') p++;

    if (*p == ' ') {
        *p = 0;  /* terminate command */
        p++;
        *args = p;
    }

    /* Find '>' in remaining string */
    char *r = (*args) ? *args : p;
    while (*r) {
        if (*r == '>') {
            *r = 0;  /* terminate args before '>' */
            r++;
            while (*r == ' ') r++;  /* skip spaces after '>' */
            if (*r) *redir = r;
            /* Trim trailing spaces from args */
            if (*args) {
                char *end = r - 2;  /* before the null we placed */
                while (end >= *args && *end == ' ') *end-- = 0;
            }
            return;
        }
        r++;
    }
}

int main(const char *startup_args)
{
    (void)startup_args;

    printf("\n============================\n");
    printf("  zOS shell (fork+exec)\n");
    printf("============================\n\n");

    char line[128];

    for (;;) {
        printf("zOS> ");
        int len = readline(line, sizeof(line));
        if (len == 0) continue;

        if (strcmp(line, "exit") == 0)
            sys_exit();

        char *cmd, *args, *redir;
        parse_line(line, &cmd, &args, &redir);

        /* Build path: /bin/<command> */
        char path[64] = "/bin/";
        int pi = 5;
        char *c = cmd;
        while (*c && pi < 63)
            path[pi++] = *c++;
        path[pi] = 0;

        int pid = sys_fork();
        if (pid < 0) {
            printf("fork failed\n");
            continue;
        }

        if (pid == 0) {
            /* Child process */

            /* Handle output redirection: > file */
            if (redir) {
                int fd = sys_open(redir, 4); /* O_CREATE */
                if (fd < 0) {
                    printf("cannot open %s\n", redir);
                    sys_exit();
                }
                /* Close stdout, replace with file */
                sys_close(1);
                /* fd should now be 1 since we just closed it...
                 * but our allocator gives the lowest free fd.
                 * For simplicity, write directly to the file fd. */
                /* Actually: dup2 would be ideal but we don't have it.
                 * Workaround: close fd 1, then open the file which
                 * gets fd 1 (lowest free). But we already opened it.
                 * Let's just close fd 1 and re-open. */
                sys_close(fd);
                sys_close(1);
                sys_open(redir, 4); /* This gets fd 1 (stdout) */
            }

            if (sys_exec(path, args) < 0) {
                printf("command not found: %s\n", cmd);
                sys_exit();
            }
        } else {
            /* Parent: wait for child */
            sys_wait(pid);
        }
    }
}
