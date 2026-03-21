/*
 * shell.c -- zOS shell.
 * Uses fork+exec+wait. Supports output redirection with '>'.
 * cd is a builtin (must change shell's own state, not a child's).
 */

#include "../lib/ulib.h"

static char cwd[128] = "/";

static void strcopy(char *dst, const char *src, int max)
{
    int i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* Build a full path from cwd + relative path */
static void resolve_path(const char *path, char *out, int max)
{
    if (path[0] == '/') {
        strcopy(out, path, max);
        return;
    }

    /* Start with cwd */
    int i = 0;
    while (cwd[i] && i < max - 1) { out[i] = cwd[i]; i++; }

    /* Add '/' if needed */
    if (i > 1 && out[i-1] != '/') out[i++] = '/';

    /* Append relative path */
    int j = 0;
    while (path[j] && i < max - 1) out[i++] = path[j++];
    out[i] = 0;
}

static int readline(char *buf, int max)
{
    int i = 0;
    while (i < max - 1) {
        char ch;
        int n = sys_read(0, &ch, 1);
        if (n <= 0) { sys_yield(); continue; }
        int c = (unsigned char)ch;
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

static void parse_line(char *line, char **cmd, char **args, char **redir)
{
    *cmd = line;
    *args = NULL;
    *redir = NULL;

    char *p = line;
    while (*p && *p != ' ' && *p != '>') p++;

    if (*p == ' ') {
        *p = 0;
        p++;
        *args = p;
    }

    char *r = (*args) ? *args : p;
    while (*r) {
        if (*r == '>') {
            *r = 0;
            r++;
            while (*r == ' ') r++;
            if (*r) *redir = r;
            if (*args) {
                char *end = r - 2;
                while (end >= *args && *end == ' ') *end-- = 0;
            }
            return;
        }
        r++;
    }
}

/* Remove trailing component from path (handle "..") */
static void path_go_up(char *path)
{
    int len = 0;
    while (path[len]) len++;

    /* Remove trailing slash */
    if (len > 1 && path[len-1] == '/') path[--len] = 0;

    /* Remove last component */
    while (len > 1 && path[len-1] != '/') len--;

    /* Keep at least "/" */
    if (len <= 1) { path[0] = '/'; path[1] = 0; }
    else { path[len-1] = 0; }  /* remove trailing slash */
}

static void cmd_cd(const char *path)
{
    if (!path || !path[0]) {
        strcopy(cwd, "/", sizeof(cwd));
        return;
    }

    if (strcmp(path, "..") == 0) {
        path_go_up(cwd);
        return;
    }

    char resolved[128];
    resolve_path(path, resolved, sizeof(resolved));

    /* Verify directory exists by trying to open it */
    int fd = sys_open(resolved, 0);
    if (fd < 0) {
        printf("cd: no such directory: %s\n", resolved);
        return;
    }
    sys_close(fd);

    strcopy(cwd, resolved, sizeof(cwd));
}

int main(const char *startup_args)
{
    (void)startup_args;

    printf("\n============================\n");
    printf("  zOS shell (fork+exec)\n");
    printf("============================\n\n");

    char line[128];

    for (;;) {
        printf("%s> ", cwd);
        int len = readline(line, sizeof(line));
        if (len == 0) continue;

        if (strcmp(line, "exit") == 0)
            sys_exit();

        char *cmd, *args, *redir;
        parse_line(line, &cmd, &args, &redir);

        /* Builtin: cd */
        if (strcmp(cmd, "cd") == 0) {
            cmd_cd(args);
            continue;
        }

        /* Builtin: pwd */
        if (strcmp(cmd, "pwd") == 0) {
            printf("%s\n", cwd);
            continue;
        }

        /* Build path: /bin/<command> */
        char path[64] = "/bin/";
        int pi = 5;
        char *c = cmd;
        while (*c && pi < 63)
            path[pi++] = *c++;
        path[pi] = 0;

        /* For ls without args, pass cwd */
        char resolved_args[128];
        if (strcmp(cmd, "ls") == 0 && (!args || !args[0])) {
            args = cwd;
        } else if (args && args[0] && args[0] != '/' && strcmp(cmd, "echo") != 0) {
            resolve_path(args, resolved_args, sizeof(resolved_args));
            args = resolved_args;
        }

        /* Resolve redir path if relative */
        char resolved_redir[128];
        if (redir && redir[0] && redir[0] != '/') {
            resolve_path(redir, resolved_redir, sizeof(resolved_redir));
            redir = resolved_redir;
        }

        int pid = sys_fork();
        if (pid < 0) {
            printf("fork failed\n");
            continue;
        }

        if (pid == 0) {
            if (redir) {
                int fd = sys_open(redir, 4);
                if (fd < 0) {
                    printf("cannot open %s\n", redir);
                    sys_exit();
                }
                sys_close(fd);
                sys_close(1);
                sys_open(redir, 4);
            }

            if (sys_exec(path, args) < 0) {
                printf("command not found: %s\n", cmd);
                sys_exit();
            }
        } else {
            sys_wait(pid);
        }
    }
}
