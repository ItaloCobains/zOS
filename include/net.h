#ifndef ZOS_NET_H
#define ZOS_NET_H

#include "types.h"

void net_init(void);
int  net_send(const void *data, size_t len);
int  net_recv(void *buf, size_t max);
int  net_ping(uint32_t ip, int count);

/* IP address helpers */
#define IP(a,b,c,d) (((uint32_t)(a)<<24)|((b)<<16)|((c)<<8)|(d))
#define GATEWAY_IP   IP(10,0,2,2)
#define OUR_IP       IP(10,0,2,15)
#define SUBNET_MASK  IP(255,255,255,0)

#endif
