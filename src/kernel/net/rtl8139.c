
#include "rtl8139.h"

#include "../include/asmFunc.h"
#include "../memory/pool/pool.h"
#include "mongoose.h"

#define RTL8139_VENDOR 0x10EC
#define RTL8139_DEVICE 0x8139
#define RX_BUF_SIZE 8192 + 16

#define REG_CMD 0x37
#define REG_CAPR 0x38
#define REG_RBSTART 0x30
#define REG_IMR 0x3C
#define REG_RCR 0x44
#define REG_TSD0 0x10
#define REG_TSAD0 0x20

#define V2P(x)                                                                 \
    ((uint32_t)(x) >= 0xC0000000u                                              \
         ? ((uint32_t)(x) - 0xC0000000u)                                       \
         : ((*pte_ptr((uint32_t)(x)) & 0xfffff000) | ((uint32_t)(x) & 0xfff)))

static uint16_t ioaddr;
static uint8_t *rx_buffer;
static uint32_t rx_cur;
static uint8_t tx_slot;

static inline void outw(uint16_t port, uint16_t v) {
    __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(port));
}
static inline void outl(uint16_t port, uint32_t v) {
    __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline uint32_t inl(uint16_t port) {
    uint32_t v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t reg) {
    uint32_t addr = 0x80000000u | (bus << 16) | (dev << 11) | (reg & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}
static void pci_write32(uint8_t bus, uint8_t dev, uint8_t reg, uint32_t val) {
    uint32_t addr = 0x80000000u | (bus << 16) | (dev << 11) | (reg & 0xFC);
    outl(0xCF8, addr);
    outl(0xCFC, val);
}

static int rtl8139_find_dev(uint8_t *bus, uint8_t *dev) {
    for (uint16_t d = 0; d < 32; d++) {
        uint32_t id = pci_read32(0, (uint8_t)d, 0);
        if ((id & 0xFFFF) == RTL8139_VENDOR &&
            ((id >> 16) & 0xFFFF) == RTL8139_DEVICE) {
            *bus = 0;
            *dev = (uint8_t)d;
            return 1;
        }
    }
    return 0;
}

static bool rtl8139_init(struct mg_tcpip_if *ifp) {
    uint8_t bus, dev;
    if (!rtl8139_find_dev(&bus, &dev)) {
        printf("[NET] rtl8139 not found\n");
        return false;
    }
    uint32_t bar = pci_read32(bus, dev, 0x10);
    ioaddr = (uint16_t)(bar & 0xFFFFFFFC);

    uint32_t cmd = pci_read32(bus, dev, 0x04);
    pci_write32(bus, dev, 0x04, cmd | 0x7);

    outb(ioaddr + 0x52, 0x00);
    outb(ioaddr + REG_CMD, 0x10);
    while (inb(ioaddr + REG_CMD) & 0x10) {
    }

    for (int i = 0; i < 6; i++) {
        ifp->mac[i] = inb(ioaddr + i);
    }

    rx_buffer = (uint8_t *)get_kernel_pages(3);
    outl(ioaddr + REG_RBSTART, V2P(rx_buffer));

    outw(ioaddr + REG_IMR, 0x0000);
    outl(ioaddr + REG_RCR, 0xF | (1 << 7));
    outb(ioaddr + REG_CMD, 0x0C);
    rx_cur = 0;
    tx_slot = 0;
    printf("[NET] rtl8139 up\n");
    return true;
}

static bool rtl8139_poll(struct mg_tcpip_if *ifp, bool once_per_sec) {
    (void)ifp;
    (void)once_per_sec;
    return true;
}

static size_t rtl8139_tx(const void *buf, size_t len, struct mg_tcpip_if *ifp) {
    (void)ifp;
    outl(ioaddr + REG_TSAD0 + tx_slot * 4, V2P(buf));
    outl(ioaddr + REG_TSD0 + tx_slot * 4, (uint32_t)len);

    for (uint32_t i = 0; i < 100000; i++) {
        if (inl(ioaddr + REG_TSD0 + tx_slot * 4) & (1 << 13))
            break;
    }
    tx_slot = (tx_slot + 1) & 3;
    return len;
}

static size_t rtl8139_rx(void *buf, size_t len, struct mg_tcpip_if *ifp) {
    (void)ifp;
    uint16_t st = *(uint16_t *)(rx_buffer + rx_cur);
    if (!(st & 0x0001)) {
        return 0;
    }
    uint16_t pkt_len = *(uint16_t *)(rx_buffer + rx_cur + 2);
    if (pkt_len > len)
        pkt_len = (uint16_t)len;
    printf("[N] rx\n");
    memcpy(buf, rx_buffer + rx_cur + 4, pkt_len);
    rx_cur = (rx_cur + 4 + pkt_len + 3) & ~3u;
    if (rx_cur >= 8192)
        rx_cur = 0;
    outw(ioaddr + REG_CAPR, rx_cur >= 16 ? rx_cur - 16 : rx_cur + 8192 - 16);
    return pkt_len;
}

struct mg_tcpip_driver mg_tcpip_driver_rtl8139 = {
    .init = rtl8139_init,
    .poll = rtl8139_poll,
    .tx = rtl8139_tx,
    .rx = rtl8139_rx,
};