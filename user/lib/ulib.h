#ifndef ZOS_ULIB_H
#define ZOS_ULIB_H

#define NULL ((void *)0)

/* Syscalls */
long sys_write(int fd, const char *buf, unsigned long len);
void sys_exit(void);
void sys_yield(void);
void sys_sleep(unsigned long ticks);
int  sys_getc(void);
unsigned long sys_uptime(void);
int  sys_ps(void *buf, int max);
int  sys_open(const char *path, int flags);
int  sys_read(int fd, void *buf, unsigned long len);
int  sys_close(int fd);
int  sys_stat(const char *path, void *st);
int  sys_mkdir(const char *path);
int  sys_readdir(int fd, void *entries, int max);
int  sys_unlink(const char *path);
int  sys_fork(void);
int  sys_exec(const char *path, const char *args);
int  sys_wait(int pid);
int  sys_getpid(void);
int  sys_pipe(int fds[2]);

/* printf */
void printf(const char *fmt, ...);

/* Utilities */
unsigned long strlen(const char *s);
int strcmp(const char *a, const char *b);
int startswith(const char *s, const char *prefix);

#endif
