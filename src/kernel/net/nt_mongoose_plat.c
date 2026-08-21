// 参考: mongoose/src/util.h, kernel/initer/idt/interrupt.h, kernel/memory/pool/pool.h
#include "mongoose.h"
#include "../initer/idt/interrupt.h"
#include "../initer/pit/pit.h"
#include "../memory/pool/pool.h"
#include "../include/asmFunc.h"
#include "../lib/str/str.h"

uint64_t mg_millis(void) {
    return (uint64_t)g_tick * (1000u / PIT_HZ);
}

bool mg_random(void* buf, size_t len) {
    uint8_t* p = (uint8_t*)buf;
    for (size_t i = 0; i < len; i++) {
        p[i] = (uint8_t)(g_tick ^ (uint32_t)i) ^ (uint8_t)(inb(0x60) + i);
    }
    return true;
}

void* mg_calloc(size_t count, size_t size) {
    size_t total = count * size;
    uint32_t pages = (uint32_t)((total + PAGE_SIZE - 1) / PAGE_SIZE);
    return get_kernel_pages(pages);
}

void mg_free(void* ptr) {
    if (ptr != NULL) {
        free_kernel_page((uint32_t)ptr);
    }
}

int rand(void) {
    static uint32_t seed = 1;
    seed = seed * 1103515245 + 12345;
    return (int)((seed >> 16) & 0x7FFF);
}

void srand(unsigned int s) {
    (void)s;
}

typedef unsigned long long mu64;
typedef long long ms64;

mu64 __udivdi3(mu64 a, mu64 b) {
    if (b == 0) return 0;
    mu64 q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((a >> i) & 1);
        if (r >= b) { r -= b; q |= (1ULL << i); }
    }
    return q;
}

mu64 __umoddi3(mu64 a, mu64 b) {
    if (b == 0) return 0;
    mu64 r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((a >> i) & 1);
        if (r >= b) r -= b;
    }
    return r;
}

ms64 __divdi3(ms64 a, ms64 b) {
    int neg = (a < 0) != (b < 0);
    mu64 ua = (mu64)(a < 0 ? -a : a);
    mu64 ub = (mu64)(b < 0 ? -b : b);
    mu64 q = __udivdi3(ua, ub);
    return neg ? (ms64)(-q) : (ms64)q;
}

ms64 __moddi3(ms64 a, ms64 b) {
    int neg = a < 0;
    mu64 ua = (mu64)(a < 0 ? -a : a);
    mu64 ub = (mu64)(b < 0 ? -b : b);
    mu64 r = __umoddi3(ua, ub);
    return neg ? (ms64)(-r) : (ms64)r;
}

long strtol(const char* s, char** end, int base) {
    const char* p = s;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f' || *p == '\v') p++;
    int neg = 0;
    if (*p == '+') p++;
    else if (*p == '-') { neg = 1; p++; }
    if (base == 0) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
        else if (p[0] == '0') base = 8;
        else base = 10;
    } else if (base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }
    long acc = 0;
    for (;;) {
        char c = *p;
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else break;
        if (d >= base) break;
        acc = acc * base + d;
        p++;
    }
    if (end != NULL) *end = (char*)p;
    return neg ? -acc : acc;
}

int sscanf(const char* s, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const char* p = s;
    int n = 0;
    while (*fmt) {
        if (*fmt == ' ' || *fmt == '\t' || *fmt == '\n') {
            while (*p == ' ' || *p == '\t' || *p == '\n') p++;
            fmt++;
            continue;
        }
        if (*fmt == '%') {
            fmt++;
            if (*fmt == 'n') {
                fmt++;
                *va_arg(ap, int*) = (int)(p - s);
            } else if (*fmt == 'd') {
                fmt++;
                while (*p == ' ' || *p == '\t' || *p == '\n') p++;
                char* end;
                long v = strtol(p, &end, 10);
                if (end == p) break;
                *va_arg(ap, int*) = (int)v;
                p = end;
                n++;
            } else if (*fmt == '[') {
                fmt++;
                int neg = 0;
                if (*fmt == '^') { neg = 1; fmt++; }
                char set[128] = {0};
                while (*fmt && *fmt != ']') { set[(unsigned char)*fmt] = 1; fmt++; }
                if (*fmt == ']') fmt++;
                unsigned cnt = 0;
                while (*p && (set[(unsigned char)*p] ^ neg)) { p++; cnt++; }
                if (cnt == 0) break;
                *va_arg(ap, char*) = 0;
                n++;
            } else {
                fmt++;
            }
        } else {
            if (*p && *p == *fmt) p++;
            fmt++;
        }
    }
    va_end(ap);
    return n;
}

void free(void* ptr) {
    mg_free(ptr);
}

void* memchr(const void* s, int c, size_t n) {
    const unsigned char* p = (const unsigned char*)s;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == (unsigned char)c) return (void*)(p + i);
    }
    return NULL;
}

char* strdup(const char* s) {
    size_t len = 0;
    while (s[len]) len++;
    char* r = (char*)mg_calloc(1, len + 1);
    if (r != NULL) for (size_t i = 0; i <= len; i++) r[i] = s[i];
    return r;
}

int atoi(const char* s) {
    return (int)strtol(s, NULL, 10);
}
