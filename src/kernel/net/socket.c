#include "socket.h"

#include "../include/asmFunc.h"
#include "../lib/str/str.h"
#include "../thread/thread.h"
#include "net.h"
#include "tcp.h"
#include "udp.h"

enum SOCK_WAIT {
    SWAIT_NONE,
    SWAIT_CONNECT,
    SWAIT_RECV,
    SWAIT_SEND,
    SWAIT_ACCEPT,
};

struct SOCKET {
    uint8_t active;
    uint8_t type;
    uint8_t wtype;
    struct task_struct *waiter;
    struct TCP_PCB *pcb;
    struct UDP_PCB *upcb;
};

static struct SOCKET s_sock[MAX_SOCKET];

#define SOCK_CONNECT_TMO 10000

void sock_init(void) { memset(s_sock, 0, sizeof s_sock); }

static struct SOCKET *sock_alloc(void) {
    for (int i = 0; i < MAX_SOCKET; i++) {
        if (!s_sock[i].active) {
            s_sock[i].active = 1;
            return &s_sock[i];
        }
    }
    return 0;
}

static int sock_ready(struct SOCKET *s) {
    if (s->type == SOCK_DGRAM)
        return udp_rx_ready(s->upcb);
    struct TCP_PCB *p = s->pcb;
    if (s->wtype == SWAIT_CONNECT)
        return p->state == TCP_ESTABLISHED || !p->active;
    if (s->wtype == SWAIT_RECV)
        return (p->rx_tail - p->rx_head) > 0 || p->fin_rcvd || !p->active;
    if (s->wtype == SWAIT_SEND)
        return TCP_SND_BUF - p->tx_len > 0 || !p->active;
    if (s->wtype == SWAIT_ACCEPT)
        return tcp_accept_ready(p);
    return 0;
}

static int sock_block(struct SOCKET *s, uint8_t wtype, uint32_t timeout_ms) {
    s->wtype = wtype;
    uint32_t deadline = timeout_ms ? net_ms() + timeout_ms : 0;
    for (;;) {
        if (sock_ready(s))
            return 0;
        if (timeout_ms && (int32_t)(net_ms() - deadline) >= 0)
            return -1;
        s->waiter = current;
        thread_block_with_status(TASK_BLOCKED);
        s->waiter = 0;
    }
}

void sock_poll(void) {
    asm_cli();
    for (int i = 0; i < MAX_SOCKET; i++) {
        struct SOCKET *s = &s_sock[i];
        if (!s->active || !s->waiter)
            continue;
        if (!sock_ready(s))
            continue;
        struct task_struct *w = s->waiter;
        s->waiter = 0;
        thread_unblock(w);
    }
    asm_sti();
}

int net_socket(int domain, int type, int proto) {
    if (domain != AF_INET ||
        (type != SOCK_STREAM && type != SOCK_DGRAM))
        return -1;
    (void)proto;
    struct SOCKET *s = sock_alloc();
    if (!s)
        return -1;
    s->type = (uint8_t)type;
    if (type == SOCK_DGRAM) {
        s->upcb = udp_pcb_alloc();
        if (!s->upcb) {
            s->active = 0;
            return -1;
        }
        return (int)(s - s_sock);
    }
    s->pcb = tcp_pcb_alloc();
    if (!s->pcb) {
        s->active = 0;
        return -1;
    }
    s->pcb->local_port = 0;
    return (int)(s - s_sock);
}

int net_bind(int fd, uint32_t ip, uint16_t port) {
    if (fd < 0 || fd >= MAX_SOCKET || !s_sock[fd].active)
        return -1;
    struct SOCKET *s = &s_sock[fd];
    if (s->type == SOCK_DGRAM)
        return udp_bind(s->upcb, ip, port);
    return tcp_bind(s->pcb, ip, port);
}

int net_listen(int fd, int backlog) {
    if (fd < 0 || fd >= MAX_SOCKET || !s_sock[fd].active)
        return -1;
    (void)backlog;
    return tcp_listen(s_sock[fd].pcb);
}

int net_connect(int fd, uint32_t ip, uint16_t port) {
    if (fd < 0 || fd >= MAX_SOCKET || !s_sock[fd].active)
        return -1;
    struct SOCKET *s = &s_sock[fd];
    if (s->type == SOCK_DGRAM) {
        udp_connect(s->upcb, ip, port);
        return 0;
    }
    if (tcp_connect(s->pcb, ip, port) < 0)
        return -1;
    if (sock_block(s, SWAIT_CONNECT, SOCK_CONNECT_TMO))
        return -1;
    return (s->pcb->active && s->pcb->state == TCP_ESTABLISHED) ? 0 : -1;
}

int net_send(int fd, const void *buf, uint32_t len) {
    if (fd < 0 || fd >= MAX_SOCKET || !s_sock[fd].active)
        return -1;
    struct SOCKET *s = &s_sock[fd];
    if (s->type == SOCK_DGRAM) {
        extern NETIF g_netif;
        if (!s->upcb->remote_ip)
            return -1;
        return udp_sendto(&g_netif, s->upcb, buf, len, s->upcb->remote_ip,
                          s->upcb->remote_port);
    }
    if (sock_block(s, SWAIT_SEND, SOCK_CONNECT_TMO))
        return -1;
    return tcp_send(s->pcb, buf, len);
}

int net_recv(int fd, void *buf, uint32_t len) {
    if (fd < 0 || fd >= MAX_SOCKET || !s_sock[fd].active)
        return -1;
    struct SOCKET *s = &s_sock[fd];
    if (s->type == SOCK_DGRAM) {
        if (sock_block(s, SWAIT_RECV, 0))
            return -1;
        return udp_recv(s->upcb, buf, len, 0, 0);
    }
    if (sock_block(s, SWAIT_RECV, 0))
        return -1;
    int n = tcp_recv(s->pcb, buf, len);
    if (n < 0 && (s->pcb->fin_rcvd || !s->pcb->active))
        return 0;
    return n;
}

int net_sendto(int fd, const void *buf, uint32_t len, uint32_t daddr,
               uint16_t dport) {
    if (fd < 0 || fd >= MAX_SOCKET || !s_sock[fd].active ||
        s_sock[fd].type != SOCK_DGRAM)
        return -1;
    struct SOCKET *s = &s_sock[fd];
    extern NETIF g_netif;
    return udp_sendto(&g_netif, s->upcb, buf, len, daddr, dport);
}

int net_recvfrom(int fd, void *buf, uint32_t len, uint32_t *saddr,
                 uint16_t *sport) {
    if (fd < 0 || fd >= MAX_SOCKET || !s_sock[fd].active ||
        s_sock[fd].type != SOCK_DGRAM)
        return -1;
    struct SOCKET *s = &s_sock[fd];
    if (sock_block(s, SWAIT_RECV, 0))
        return -1;
    return udp_recv(s->upcb, buf, len, saddr, sport);
}

int net_accept(int fd) {
    if (fd < 0 || fd >= MAX_SOCKET || !s_sock[fd].active ||
        s_sock[fd].type != SOCK_STREAM || s_sock[fd].pcb->state != TCP_LISTEN)
        return -1;
    struct SOCKET *s = &s_sock[fd];
    if (sock_block(s, SWAIT_ACCEPT, 0))
        return -1;
    struct TCP_PCB *np = tcp_accept(s->pcb);
    if (!np)
        return -1;
    struct SOCKET *n = sock_alloc();
    if (!n)
        return -1;
    n->type = SOCK_STREAM;
    n->pcb = np;
    return (int)(n - s_sock);
}

int net_close(int fd) {
    if (fd < 0 || fd >= MAX_SOCKET || !s_sock[fd].active)
        return -1;
    struct SOCKET *s = &s_sock[fd];
    if (s->type == SOCK_STREAM) {
        tcp_close(s->pcb);
        tcp_pcb_free(s->pcb);
    } else {
        udp_pcb_free(s->upcb);
    }
    s->active = 0;
    s->waiter = 0;
    s->pcb = 0;
    s->upcb = 0;
    return 0;
}