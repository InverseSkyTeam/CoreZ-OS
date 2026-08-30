#ifndef NIITAN_NET_H
#define NIITAN_NET_H

#include <stdint.h>
#include "kernel/nt_ping_reply.h"
#include "drivers/net/netif.h"

extern NETIF g_netif;

extern int net_enable;
void net_init(void);
uint32_t net_ms(void);
int nt_icmp_send(uint32_t dst, uint16_t id, uint16_t seq);
int nt_icmp_recv(struct nt_ping_reply *out, int max);

#endif