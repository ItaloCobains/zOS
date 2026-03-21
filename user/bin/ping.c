/*
 * ping.c -- Send ICMP echo requests.
 * Usage: ping <ip>  (e.g., ping 10.0.2.2)
 */

#include "../lib/ulib.h"

/* sys_ping: kernel-level ping (syscall 19) */
extern int sys_ping(unsigned long ip, int count);

static unsigned long parse_ip(const char *s)
{
    unsigned long parts[4] = {0,0,0,0};
    int p = 0;
    while (*s && p < 4) {
        if (*s >= '0' && *s <= '9')
            parts[p] = parts[p] * 10 + (*s - '0');
        else if (*s == '.')
            p++;
        s++;
    }
    return (parts[0]<<24)|(parts[1]<<16)|(parts[2]<<8)|parts[3];
}

int main(const char *args)
{
    if (!args || !args[0]) {
        printf("usage: ping <ip>\n");
        printf("  e.g., ping 10.0.2.2\n");
        return 1;
    }

    unsigned long ip = parse_ip(args);
    printf("PING %d.%d.%d.%d\n",
           (int)(ip>>24)&0xFF, (int)(ip>>16)&0xFF,
           (int)(ip>>8)&0xFF, (int)ip&0xFF);

    int ok = sys_ping(ip, 4);
    printf("%d/4 packets received\n", ok);
    return ok > 0 ? 0 : 1;
}
