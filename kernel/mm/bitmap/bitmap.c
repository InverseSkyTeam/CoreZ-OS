#include "kernel/mm/bitmap/bitmap.h"

#include "lib/str/str.h"

void bitmap_init(struct bitmap *btmp) {
    memset(btmp->bits, 0, btmp->btmp_bytes_len);
}

int bitmap_scan_test(const struct bitmap *btmp, uint32_t bit_idx) {
    uint32_t byte = bit_idx >> 3;
    if (byte >= btmp->btmp_bytes_len) {
        return -1;
    }
    return (btmp->bits[byte] & (BITMAP_MASK >> (bit_idx & 7))) ? 1 : 0;
}

void bitmap_set(struct bitmap *btmp, uint32_t bit_idx, int8_t value) {
    uint32_t byte = bit_idx >> 3;
    if (byte >= btmp->btmp_bytes_len) {
        return;
    }
    if (value) {
        btmp->bits[byte] |= (BITMAP_MASK >> (bit_idx & 7));
    } else {
        btmp->bits[byte] &= ~(BITMAP_MASK >> (bit_idx & 7));
    }
}

int bitmap_scan(const struct bitmap *btmp, uint32_t cnt) {
    if (cnt == 0 || btmp->btmp_bytes_len == 0) {
        return -1;
    }
    if (cnt == 1) {
        const uint64_t *words = (const uint64_t *)btmp->bits;
        uint32_t nwords = btmp->btmp_bytes_len >> 3;
        for (uint32_t w = 0; w < nwords; w++) {
            uint64_t inv = ~__builtin_bswap64(words[w]);
            if (inv == 0) {
                continue;
            }
            return (int)((w << 6) + __builtin_clzll(inv));
        }
        for (uint32_t byte = nwords << 3; byte < btmp->btmp_bytes_len; byte++) {
            uint32_t inv = (uint32_t)(uint8_t)~btmp->bits[byte];
            if (inv == 0) {
                continue;
            }
            return (int)((byte << 3) + __builtin_clz(inv) - 24);
        }
        return -1;
    }
    uint32_t run = 0;
    for (uint32_t byte = 0; byte < btmp->btmp_bytes_len; byte++) {
        uint8_t v = btmp->bits[byte];
        if (v == 0xff) {
            run = 0;
            continue;
        }
        if (v == 0) {
            run += 8;
            if (run >= cnt) {
                return (int)((byte << 3) + 8 - cnt);
            }
            continue;
        }
        for (uint32_t j = 0; j < 8; j++) {
            if (v & (BITMAP_MASK >> j)) {
                run = 0;
            } else if (++run == cnt) {
                return (int)((byte << 3) + j - cnt + 1);
            }
        }
    }
    return -1;
}
