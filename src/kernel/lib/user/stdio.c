#include "./stdio.h"

#include "./syscall.h"

#include <stddef.h>

static void itoa(uint32_t value, char **buf_ptr_addr, uint8_t base) {
    uint32_t m = value % base;
    uint32_t i = value / base;
    if (i) {
        itoa(i, buf_ptr_addr, base);
    }
    if (m < 10) {
        *((*buf_ptr_addr)++) = (char)(m + '0');
    } else {
        *((*buf_ptr_addr)++) = (char)(m - 10 + 'A');
    }
}

uint32_t vsprintf(char *str, const char *format, va_list ap) {
    char *buf_ptr = str;
    const char *fmt = format;
    char ch;

    while ((ch = *fmt) != 0) {
        if (ch != '%') {
            *buf_ptr++ = ch;
            fmt++;
            continue;
        }

        fmt++;
        ch = *fmt;
        switch (ch) {
        case 'x': {
            uint32_t v = (uint32_t)va_arg(ap, int);
            itoa(v, &buf_ptr, 16);
            break;
        }
        case 'd': {
            int32_t v = va_arg(ap, int);
            if (v < 0) {
                *buf_ptr++ = '-';
                v = (int32_t)(0u - (uint32_t)v);
            }
            itoa((uint32_t)v, &buf_ptr, 10);
            break;
        }
        case 'c':
            *buf_ptr++ = (char)va_arg(ap, int);
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s == 0) {
                s = "(null)";
            }
            while (*s) {
                *buf_ptr++ = *s++;
            }
            break;
        }
        case '%':
            *buf_ptr++ = '%';
            break;
        default:
            *buf_ptr++ = '%';
            *buf_ptr++ = ch;
            break;
        }
        if (ch) {
            fmt++;
        }
    }
    *buf_ptr = 0;
    return (uint32_t)(buf_ptr - str);
}

void printf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    char buf[1024] = {0};
    uint32_t len = vsprintf(buf, format, ap);
    va_end(ap);
    write(1, buf, len);
}

uint32_t sprintf(char *buf, const char *format, ...) {
    va_list ap;
    uint32_t retval;
    va_start(ap, format);
    retval = vsprintf(buf, format, ap);
    va_end(ap);
    return retval;
}

static void itoa_b(uint32_t value, char **buf_ptr_addr, uint64_t *cap,
                   uint64_t *wanted, uint8_t base) {
    uint32_t m = value % base;
    uint32_t i = value / base;
    if (i) {
        itoa_b(i, buf_ptr_addr, cap, wanted, base);
    }
    (*wanted)++;
    if (*cap > 0) {
        **buf_ptr_addr = (char)(m < 10 ? (m + '0') : (m - 10 + 'A'));
        (*buf_ptr_addr)++;
        (*cap)--;
    }
}

int vsnprintf(char *str, size_t n, const char *format, va_list ap) {
    char *buf_ptr = str;
    const char *fmt = format;
    uint64_t cap = (n > 0) ? (uint64_t)n - 1 : 0;
    uint64_t wanted = 0;
    char ch;

    while ((ch = *fmt) != '\0') {
        if (ch != '%') {
            wanted++;
            if (cap > 0) {
                *buf_ptr++ = ch;
                cap--;
            }
            fmt++;
            continue;
        }

        fmt++;
        ch = *fmt;
        switch (ch) {
        case 'x': {
            uint32_t v = (uint32_t)va_arg(ap, int);
            itoa_b(v, &buf_ptr, &cap, &wanted, 16);
            break;
        }
        case 'd': {
            int32_t v = va_arg(ap, int);
            if (v < 0) {
                wanted++;
                if (cap > 0) {
                    *buf_ptr++ = '-';
                    cap--;
                }
                v = (int32_t)(0u - (uint32_t)v);
            }
            itoa_b((uint32_t)v, &buf_ptr, &cap, &wanted, 10);
            break;
        }
        case 'c':
            wanted++;
            if (cap > 0) {
                *buf_ptr++ = (char)va_arg(ap, int);
                cap--;
            }
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s == 0) {
                s = "(null)";
            }
            while (*s) {
                wanted++;
                if (cap > 0) {
                    *buf_ptr++ = *s;
                    cap--;
                }
                s++;
            }
            break;
        }
        case '%':
            wanted++;
            if (cap > 0) {
                *buf_ptr++ = '%';
                cap--;
            }
            break;
        default:
            wanted += 2;
            if (cap > 0) {
                *buf_ptr++ = '%';
                cap--;
            }
            if (cap > 0) {
                *buf_ptr++ = ch;
                cap--;
            }
            break;
        }
        fmt++;
    }
    *buf_ptr = '\0';
    return (int)wanted;
}
