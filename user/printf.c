/*
 * printf.c - Minimal printf implementation for userspace.
 */

extern long sys_write(int fd, const char *buf, unsigned long len);

static void write_str(const char *s)
{
    unsigned long len = 0;
    while (s[len]) len++;
    sys_write(1, s, len);  /* fd 1 = stdout */
}

static void write_char(char c)
{
    sys_write(1, &c, 1);
}

static void write_int(long value)
{
    if (value < 0) {
        write_char('-');
        value = -value;
    }

    char buf[20];  /* Enough for 64-bit integer */
    int i = 0;

    if (value == 0) {
        write_char('0');
        return;
    }

    while (value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }

    while(i > 0)
        write_char(buf[--i]);
}

static void write_hex(unsigned long value)
{
    const char *hex = "0123456789abcdef";
    write_str("0x");

    /* Skip leading zeros */
    int started = 0;
    for (int i = 60; i >= 0; i -= 4) {
        int digit = (value >> i) & 0xF;
        if (digit || started || i == 0) {
            write_char(hex[digit]);
            started = 1;
        }
    }
}

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)

void printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
            case 's': write_str(va_arg(ap, const char *)); break;
            case 'd': write_int(va_arg(ap, long)); break;
            case 'x': write_hex(va_arg(ap, unsigned long)); break;
            case 'c': write_char((char)va_arg(ap, int)); break;
            case '%': write_char('%'); break;
            default:  write_char('%'); write_char(*fmt); break;
            }
        } else {
            if (*fmt == '\n') write_char('\r');
            write_char(*fmt);
        }
        fmt++;
    }

    va_end(ap);
}