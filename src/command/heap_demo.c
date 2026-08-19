#include "stdio.h"
#include "syscall.h"
#include "stdlib.h"

#define N 256

static int g_fail = 0;

static void check(int cond, const char* msg) {
    if (!cond) {
        g_fail = 1;
        printf("  [FAIL] %s\n", msg);
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("heap_demo: start\n");

    int* a = (int*)malloc(sizeof(int) * 100);
    check(a != NULL, "malloc 100 ints");
    for (int i = 0; i < 100; i++) {
        a[i] = i * 7 + 1;
    }
    for (int i = 0; i < 100; i++) {
        check(a[i] == i * 7 + 1, "malloc payload intact (write/read)");
    }

    int* c = (int*)calloc(50, sizeof(int));
    check(c != NULL, "calloc 50 ints");
    int zeroed = 1;
    for (int i = 0; i < 50; i++) {
        if (c[i] != 0) { zeroed = 0; break; }
    }
    check(zeroed, "calloc zeroes memory");

    int* p[N];
    for (int i = 0; i < N; i++) {
        p[i] = (int*)malloc(sizeof(int) * 4);
        check(p[i] != NULL, "malloc batch");
        if (p[i]) {
            p[i][0] = 0x5151 + i;
            p[i][1] = 0x6161 + i;
            p[i][2] = 0x7171 + i;
            p[i][3] = 0x8181 + i;
        }
    }
    for (int i = 0; i < N; i++) {
        if (!p[i]) continue;
        check(p[i][0] == 0x5151 + i && p[i][1] == 0x6161 + i &&
              p[i][2] == 0x7171 + i && p[i][3] == 0x8181 + i,
              "batch payload intact (no overlap)");
    }

    int* r = (int*)malloc(sizeof(int) * 8);
    check(r != NULL, "malloc for realloc");
    for (int i = 0; i < 8; i++) r[i] = i;
    int* r2 = (int*)realloc(r, sizeof(int) * 64);
    check(r2 != NULL, "realloc grow");
    int kept = 1;
    for (int i = 0; i < 8; i++) {
        if (r2[i] != i) { kept = 0; break; }
    }
    check(kept, "realloc preserves old content");

    free(a);
    free(c);
    for (int i = 0; i < N; i++) {
        if (p[i]) free(p[i]);
    }
    free(r2);

    int* big = (int*)malloc(sizeof(int) * 2000);
    check(big != NULL, "malloc after free (reuse)");
    for (int i = 0; i < 2000; i++) big[i] = -i;
    int ok = 1;
    for (int i = 0; i < 2000; i++) {
        if (big[i] != -i) { ok = 0; break; }
    }
    check(ok, "reused block payload intact");
    free(big);

    if (g_fail == 0) {
        printf("heap_demo: PASS\n");
        exit(0);
    } else {
        printf("heap_demo: FAIL\n");
        exit(1);
    }
    return 0;
}
