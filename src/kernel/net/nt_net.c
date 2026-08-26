#include "nt_net.h"

#include "../include/asmFunc.h"
#include "../lib/str/str.h"
#include "../thread/thread.h"
#include "e1000.h"
#include "mongoose.h"
#include "pci.h"
#include "rtl8139.h"

static struct mg_mgr mgr;
static struct mg_tcpip_if mip;

static struct mg_tcpip_driver *net_driver_probe(void) {
    uint8_t bus, dev;
    if (pci_find_device(0x8086, 0x100E, &bus, &dev)) {
        return &mg_tcpip_driver_e1000;
    }
    if (pci_find_device(0x10EC, 0x8139, &bus, &dev)) {
        return &mg_tcpip_driver_rtl8139;
    }
    printf("[NET] no supported NIC found, fallback rtl8139\n");
    return &mg_tcpip_driver_rtl8139;
}

void mg_tcpip_set_icmp_echo_cb(void (*cb)(struct mg_tcpip_if *, uint32_t,
                                          uint16_t, uint16_t));
void mg_tcpip_icmp_echo(struct mg_tcpip_if *ifp, uint32_t dst, uint16_t id,
                        uint16_t seq);
static uint64_t s_ping_send_ms;

#define PING_QUEUE_MAX 32
static struct nt_ping_reply s_ping_q[PING_QUEUE_MAX];
static int s_ping_head, s_ping_tail, s_ping_cnt;

static void ping_cb(struct mg_tcpip_if *ifp, uint32_t src, uint16_t id,
                    uint16_t seq) {
    (void)ifp;
    struct nt_ping_reply rep;
    rep.src = src;
    rep.id = id;
    rep.seq = seq;
    rep.rtt_ms = (uint32_t)(mg_millis() - s_ping_send_ms);
    asm_cli();
    if (s_ping_cnt < PING_QUEUE_MAX) {
        s_ping_q[s_ping_tail] = rep;
        s_ping_tail = (s_ping_tail + 1) % PING_QUEUE_MAX;
        s_ping_cnt++;
    }
    asm_sti();
}

int nt_icmp_send(uint32_t dst, uint16_t id, uint16_t seq) {
    if (dst == 0 || dst == mip.ip)
        return -1;
    s_ping_send_ms = mg_millis();
    mg_tcpip_icmp_echo(&mip, dst, id, seq);
    return 0;
}

int nt_icmp_recv(struct nt_ping_reply *out, int max) {
    int n = 0;
    asm_cli();
    while (n < max && s_ping_cnt > 0) {
        out[n] = s_ping_q[s_ping_head];
        s_ping_head = (s_ping_head + 1) % PING_QUEUE_MAX;
        s_ping_cnt--;
        n++;
    }
    asm_sti();
    return n;
}

static void http_handler(struct mg_connection *c, int ev, void *ev_data) {
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;

    if (ev == MG_EV_HTTP_MSG) {
        printf("[N] http-msg\n");
        if (mg_match(hm->uri, mg_str("/"), NULL)) {
            mg_http_reply(c, 200, "content-type: text/plain\r\n",
                          "hello from nitian-os\n");
        } else {
            mg_http_reply(c, 404, "content-type: text/plain\r\n",
                          "not found\n");
        }
    }
}

static void net_thread(void *arg) {
    (void)arg;
    memset(&mip, 0, sizeof(mip));
    mip.driver = net_driver_probe();
    mip.ip = MG_IPV4(10, 0, 2, 15);
    mip.mask = MG_IPV4(255, 255, 255, 0);
    mip.gw = MG_IPV4(10, 0, 2, 2);
    mip.enable_dhcp_client = false;

    mg_mgr_init(&mgr);
    mg_tcpip_init(&mgr, &mip);
    mg_tcpip_set_icmp_echo_cb(ping_cb);
    if (mg_http_listen(&mgr, "http://0.0.0.0:8765", http_handler, NULL) ==
        NULL) {
        printf("[NET] listen failed\n");
        for (;;)
            thread_yield();
    }

    for (;;) {
        mg_mgr_poll(&mgr, 10);
        thread_yield();
    }
}

void net_init(void) {
    kernel_thread("net", 8, net_thread, NULL);
}
