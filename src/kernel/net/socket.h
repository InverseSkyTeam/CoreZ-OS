#ifndef NIITAN_NET_SOCKET_H
#define NIITAN_NET_SOCKET_H

#include <stdint.h>

#define AF_INET 2

#define SOCK_STREAM 1
#define SOCK_DGRAM  2

#define MAX_SOCKET 64

void sock_init(void);
int net_socket(int domain, int type, int proto);
int net_bind(int fd, uint32_t ip, uint16_t port);
int net_listen(int fd, int backlog);
int net_connect(int fd, uint32_t ip, uint16_t port);
int net_send(int fd, const void *buf, uint32_t len);
int net_recv(int fd, void *buf, uint32_t len);
int net_sendto(int fd, const void *buf, uint32_t len, uint32_t daddr,
               uint16_t dport);
int net_recvfrom(int fd, void *buf, uint32_t len, uint32_t *saddr,
                 uint16_t *sport);
int net_accept(int fd);
int net_close(int fd);

void sock_poll(void);

#endif