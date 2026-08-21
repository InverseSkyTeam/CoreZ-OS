#include "e1000.h"
#include "pci.h"
#include "mongoose.h"
#include "../include/asmFunc.h"
#include "../memory/pool/pool.h"

#define E1000_VENDOR  0x8086
#define E1000_DEVICE  0x100E
#define E1000_BAR0    0x10

#define REG_CTRL    0x0000
#define REG_STATUS  0x0008
#define REG_TCTL    0x0400
#define REG_TIPG    0x0410
#define REG_RCTL    0x0100
#define REG_ICR     0x00C0
#define REG_IMC     0x00D8
#define REG_IMS     0x00D0
#define REG_TDBAL   0x3800
#define REG_TDBAH   0x3804
#define REG_TDLEN   0x3808
#define REG_TDH     0x3810
#define REG_TDT     0x3818
#define REG_RDBAL   0x2800
#define REG_RDBAH   0x2804
#define REG_RDLEN   0x2808
#define REG_RDH     0x2810
#define REG_RDT     0x2818
#define REG_RA      0x5400

#define TX_DESC_N 8
#define RX_DESC_N 16
#define RX_BUF_LEN 2048

#define V2P(x) ((*pte_ptr((uint32_t)(x)) & 0xfffff000) | ((uint32_t)(x) & 0xfff))

struct e1000_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed));

struct e1000_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t csum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed));

static volatile uint8_t* regs; 
static struct e1000_tx_desc* tx_desc;
static struct e1000_rx_desc* rx_desc;
static uint8_t* tx_buf;
static uint8_t* rx_buf;
static uint32_t tx_cur, rx_cur;

static inline uint32_t readl(uint32_t off) {
    return *(volatile uint32_t*)(regs + off);
}
static inline void writel(uint32_t off, uint32_t v) {
    *(volatile uint32_t*)(regs + off) = v;
}

static void e1000_reset(void) {
    writel(REG_CTRL, readl(REG_CTRL) | (1 << 26));
    for (uint32_t i = 0; i < 10000; i++) {
        if (!(readl(REG_CTRL) & (1 << 26))) break;
    }
}

static bool e1000_init(struct mg_tcpip_if* ifp) {
    uint8_t bus, dev;
    if (!pci_find_device(E1000_VENDOR, E1000_DEVICE, &bus, &dev)) {
        printf("[NET] e1000 not found\n");
        return false;
    }
    uint32_t bar = pci_read32(bus, dev, E1000_BAR0);
    uint32_t phy = bar & ~0xFu;
    pci_enable_bus_master(bus, dev, 0x6);

    regs = (volatile uint8_t*)ioremap(phy, 0x20000);
    if (!regs) {
        printf("[NET] e1000 ioremap failed\n");
        return false;
    }
    e1000_reset();

    uint32_t ral = readl(REG_RA);
    uint32_t rah = readl(REG_RA + 4);
    ifp->mac[0] = (uint8_t)(ral & 0xFF);
    ifp->mac[1] = (uint8_t)((ral >> 8) & 0xFF);
    ifp->mac[2] = (uint8_t)((ral >> 16) & 0xFF);
    ifp->mac[3] = (uint8_t)((ral >> 24) & 0xFF);
    ifp->mac[4] = (uint8_t)(rah & 0xFF);
    ifp->mac[5] = (uint8_t)((rah >> 8) & 0xFF);

    writel(REG_IMC, 0xFFFFFFFF);

    tx_desc = (struct e1000_tx_desc*)get_kernel_pages(1);
    tx_buf  = (uint8_t*)get_kernel_pages(TX_DESC_N * RX_BUF_LEN / PAGE_SIZE);
    for (uint32_t i = 0; i < TX_DESC_N; i++) {
        tx_desc[i].addr   = V2P(tx_buf) + (uint64_t)i * RX_BUF_LEN;
        tx_desc[i].cmd    = 0;
        tx_desc[i].status = 0x01;  
    }
    writel(REG_TDBAL, V2P(tx_desc));
    writel(REG_TDBAH, 0);
    writel(REG_TDLEN, TX_DESC_N * 16);
    writel(REG_TDH, 0);
    writel(REG_TDT, 0);
    writel(REG_TCTL, 0x10000 | 0x400 | 0x8 | 0x2); 
    writel(REG_TIPG, 0x18);

    rx_desc = (struct e1000_rx_desc*)get_kernel_pages(1);
    rx_buf  = (uint8_t*)get_kernel_pages(RX_DESC_N * RX_BUF_LEN / PAGE_SIZE);
    for (uint32_t i = 0; i < RX_DESC_N; i++) {
        rx_desc[i].addr   = V2P(rx_buf) + (uint64_t)i * RX_BUF_LEN;
        rx_desc[i].status = 0;
    }
    writel(REG_RDBAL, V2P(rx_desc));
    writel(REG_RDBAH, 0);
    writel(REG_RDLEN, RX_DESC_N * 16);
    writel(REG_RDH, 0);
    writel(REG_RDT, RX_DESC_N - 1);
    writel(REG_RCTL, 0x2 | 0x8000 | 0x04000000);

    tx_cur = rx_cur = 0;
    printf("[NET] e1000 up\n");
    return true;
}

static bool e1000_poll(struct mg_tcpip_if* ifp, bool once_per_sec) {
    (void)ifp;
    (void)once_per_sec;
    return readl(REG_STATUS) & 0x2;  
}

static size_t e1000_tx(const void* buf, size_t len, struct mg_tcpip_if* ifp) {
    (void)ifp;
    uint32_t slot = tx_cur % TX_DESC_N;

    for (uint32_t i = 0; i < 100000; i++) {
        if (tx_desc[slot].status & 0x01) break;
    }
    memcpy(tx_buf + (size_t)slot * RX_BUF_LEN, buf, len);
    tx_desc[slot].length = (uint16_t)len;
    tx_desc[slot].cso    = 0;
    tx_desc[slot].css    = 0;
    tx_desc[slot].cmd    = 0x01 | 0x02 | 0x08; 
    tx_desc[slot].status = 0;
    tx_cur++;
    writel(REG_TDT, tx_cur % TX_DESC_N); 

    for (uint32_t i = 0; i < 100000; i++) {
        if (tx_desc[slot].status & 0x01) break;
    }
    return len;
}

static size_t e1000_rx(void* buf, size_t len, struct mg_tcpip_if* ifp) {
    (void)ifp;
    if (!(rx_desc[rx_cur].status & 0x01)) return 0;  
    size_t got = 0;
    if (rx_desc[rx_cur].errors == 0) {
        uint16_t plen = rx_desc[rx_cur].length;
        if (plen > len) plen = (uint16_t)len;
        memcpy(buf, rx_buf + (size_t)rx_cur * RX_BUF_LEN, plen);
        got = plen;
    }
    uint32_t done = rx_cur;
    rx_desc[rx_cur].status = 0;
    rx_cur = (rx_cur + 1) % RX_DESC_N;
    writel(REG_RDT, done);  
    return got;
}

struct mg_tcpip_driver mg_tcpip_driver_e1000 = {
    .init = e1000_init,
    .poll = e1000_poll,
    .tx = e1000_tx,
    .rx = e1000_rx,
};