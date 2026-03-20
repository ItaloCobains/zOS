/*
 * shell.c -- Interactive shell for zOS.
 * Reads commands from UART and executes them.
 */

#include "printf.h"
#include "taskinfo.h"

extern long sys_write(int fd, const char *buf, unsigned long len);
extern void sys_yield(void);
extern void sys_sleep(unsigned long ticks);
extern int sys_getc(void);
extern void sys_exit(void);
extern unsigned long sys_uptime(void);
extern int sys_ps(void *buf, int max);
extern int sys_open(const char *path, int flags);
extern int sys_read(int fd, void *buf, unsigned long len);
extern int sys_close(int fd);
extern int sys_stat(const char *path, void *st);
extern int sys_mkdir(const char *path);
extern int sys_readdir(int fd, void *entries, int max);
extern int sys_unlink(const char *path);

/* Returns 1 if s starts with prefix, 0 otherwise */
static int startswith(const char *s, const char *prefix) {
  while (*prefix) {
    if (*s++ != *prefix++) return 0;
  }
  return 1;
}

static int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a - *b;
}

/* Read a line from UART into buf, Returns when Enter is pressed.*/
static int readline(char *buf, int max) {
  int i = 0;

  while (i < max - 1) {
    int c = sys_getc();
    if (c < 0) {
      sys_yield(); /* No input, yield to other tasks. */
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
      sys_write(1, ch, 1); /* Echo the character. */
    }
  }

  buf[i] = 0;
  return i;
}

/* --- Commands --- */

static void cmd_help(void) {
  printf("zOS shell commands:\n");
  printf("  help   - show this message\n");
  printf("  echo   - print arguments\n");
  printf("  clear  - clear screen\n");
  printf("  uptime - show tick count\n");
  printf("  hello  - greeting\n");
  printf("  ps     - list running tasks\n");
  printf("  ls     - list files in directory\n");
  printf("  cat    - print file contents\n");
  printf("  touch  - create empty file\n");
  printf("  mkdir  - create directory\n");
  printf("  rm     - remove file\n");
  printf("  write  - write text to file\n");
}

static void cmd_clear(void) {
  printf("\033[2J\033[H"); /* ANSI escape: clear screen, cursor home */
}

static void cmd_hello(void) {
  printf("Hello from zOS shell! Running in EL0.\n");
}

static void cmd_uptime(void) {
  unsigned long ticks = sys_uptime();
  unsigned long seconds = ticks / 100;
  unsigned long ms = (ticks % 100) * 10;
  printf("uptime: %d ticks (%d.%d seconds)\n", ticks, seconds, ms);
}

static void cmd_ps(void) {
  struct {
    int id;
    int state;
    unsigned long sleep_ticks;
  } info[8];

  int count = sys_ps(info, 8);

  printf("  ID   STATE\n");
  for (int i = 0; i < count; i++) {
    printf("  %d    ", info[i].id);
    switch (info[i].state) {
    case 1:
      printf("READY");
      break;
    case 2:
      printf("RUNNING");
      break;
    case 3:
      printf("SLEEPING (%d ticks left)", info[i].sleep_ticks);
      break;
    case 4:
      printf("DEAD");
      break;
    default:
      printf("???");
      break;
    }
    printf("\n");
  }
}

static void cmd_ls(const char *path) {
  if (!path || path[0] == 0) path = "/";
  int fd = sys_open(path, 0);
  if (fd < 0) { printf("ls: cannot open %s\n", path); return; }

  struct { int inode; int type; char name[32]; } entries[16];
  int count = sys_readdir(fd, entries, 16);
  sys_close(fd);

  if (count < 0) { printf("ls: not a directory\n"); return; }

  for (int i = 0; i < count; i++) {
    if (entries[i].type == 2)       /* INODE_DIR */
      printf("  %s/\n", entries[i].name);
    else if (entries[i].type == 3)  /* INODE_DEVICE */
      printf("  %s  (device)\n", entries[i].name);
    else
      printf("  %s\n", entries[i].name);
  }
}

static void cmd_cat(const char *path) {
  if (!path || path[0] == 0) { printf("cat: missing path\n"); return; }
  int fd = sys_open(path, 0);
  if (fd < 0) { printf("cat: cannot open %s\n", path); return; }

  char buf[256];
  int n;
  while ((n = sys_read(fd, buf, sizeof(buf))) > 0)
    sys_write(1, buf, n);

  sys_close(fd);
  printf("\n");
}

static void cmd_touch(const char *path) {
  if (!path || path[0] == 0) { printf("touch: missing path\n"); return; }
  int fd = sys_open(path, 4); /* O_CREATE */
  if (fd < 0) printf("touch: cannot create %s\n", path);
  else sys_close(fd);
}

static void cmd_mkdir_shell(const char *path) {
  if (!path || path[0] == 0) { printf("mkdir: missing path\n"); return; }
  if (sys_mkdir(path) < 0)
    printf("mkdir: cannot create %s\n", path);
}

static void cmd_rm(const char *path) {
  if (!path || path[0] == 0) { printf("rm: missing path\n"); return; }
  if (sys_unlink(path) < 0)
    printf("rm: cannot remove %s\n", path);
}

static void cmd_write_file(const char *args) {
  if (!args || args[0] == 0) { printf("write: usage: write <path> <text>\n"); return; }
  /* Parse: first word is path, rest is text */
  char path[64];
  int i = 0;
  while (args[i] && args[i] != ' ' && i < 63) {
    path[i] = args[i];
    i++;
  }
  path[i] = 0;

  if (args[i] != ' ') { printf("write: missing text\n"); return; }
  const char *text = args + i + 1;

  int fd = sys_open(path, 4); /* O_CREATE */
  if (fd < 0) { printf("write: cannot open %s\n", path); return; }

  unsigned long len = 0;
  while (text[len]) len++;
  sys_write(fd, text, len);
  sys_close(fd);
}

/* --- Main shell loop --- */

void user_main2(void) {
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
      } else if (strcmp(line, "ps") == 0) {
        cmd_ps();
      } else if (startswith(line, "echo ")) {
        printf("%s\n", line + 5);
      } else if (strcmp(line, "ls") == 0) {
        cmd_ls("/");
      } else if (startswith(line, "ls ")) {
        cmd_ls(line + 3);
      } else if (startswith(line, "cat ")) {
        cmd_cat(line + 4);
      } else if (startswith(line, "touch ")) {
        cmd_touch(line + 6);
      } else if (startswith(line, "mkdir ")) {
        cmd_mkdir_shell(line + 6);
      } else if (startswith(line, "rm ")) {
        cmd_rm(line + 3);
      } else if (startswith(line, "write ")) {
        cmd_write_file(line + 6);
      } else {
        printf("unknown command: %s\n", line);
      }
    }
}
