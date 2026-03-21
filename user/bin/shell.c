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
        if (c == 3) {
            /* Ctrl+C: cancel current line */
            printf("^C\n");
            i = 0;
            buf[0] = 0;
            return 0;
        }
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

        /* Check for pipe: cmd1 | cmd2 */
        char *pipe_pos = NULL;
        for (char *p = line; *p; p++) {
            if (*p == '|') { pipe_pos = p; break; }
        }

        if (pipe_pos) {
            /* Split into two commands */
            *pipe_pos = 0;
            char *cmd2_line = pipe_pos + 1;
            while (*cmd2_line == ' ') cmd2_line++;

            /* Re-parse cmd1 */
            parse_line(line, &cmd, &args, &redir);

            char path1[64] = "/bin/";
            { int pi = 5; char *c = cmd; while (*c && pi < 63) path1[pi++] = *c++; path1[pi] = 0; }

            char resolved_args[128];
            if (strcmp(cmd, "ls") == 0 && (!args || !args[0])) args = cwd;
            else if (args && args[0] && args[0] != '/' && strcmp(cmd, "echo") != 0) {
                resolve_path(args, resolved_args, sizeof(resolved_args));
                args = resolved_args;
            }

            /* Parse cmd2 */
            char *cmd2, *args2, *redir2;
            parse_line(cmd2_line, &cmd2, &args2, &redir2);

            char path2[64] = "/bin/";
            { int pi = 5; char *c = cmd2; while (*c && pi < 63) path2[pi++] = *c++; path2[pi] = 0; }

            /* Create pipe */
            int pipe_fds[2];
            if (sys_pipe(pipe_fds) < 0) { printf("pipe failed\n"); continue; }

            /* Fork cmd1: stdout -> pipe write end */
            int pid1 = sys_fork();
            if (pid1 == 0) {
                sys_close(pipe_fds[0]); /* close read end */
                sys_close(1);
                /* pipe_fds[1] becomes fd 1 (lowest free) */
                sys_exec(path1, args);
                sys_exit();
            }

            /* Fork cmd2: stdin -> pipe read end */
            int pid2 = sys_fork();
            if (pid2 == 0) {
                sys_close(pipe_fds[1]); /* close write end */
                sys_close(0);
                /* pipe_fds[0] becomes fd 0 (lowest free) */
                sys_exec(path2, args2);
                sys_exit();
            }

            /* Parent: close both pipe ends and wait */
            sys_close(pipe_fds[0]);
            sys_close(pipe_fds[1]);
            sys_wait(pid1);
            sys_wait(pid2);
            continue;
        }

        /* Check for background: trailing & */
        int background = 0;
        {
            int ll = 0;
            while (line[ll]) ll++;
            while (ll > 0 && line[ll-1] == ' ') ll--;
            if (ll > 0 && line[ll-1] == '&') {
                background = 1;
                line[ll-1] = 0;
                /* Re-parse without & */
                parse_line(line, &cmd, &args, &redir);
            }
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
            if (background) {
                printf("[bg] pid %d\n", pid);
            } else {
                sys_wait(pid);
            }
        }
    }
}
