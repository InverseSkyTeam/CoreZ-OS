// 压轴验收: 单核 fork COW 压力测试.
// 父进程持有若干可写页; 多次 fork, 每个子进程把自己的副本改写为随机值后退出.
// COW 保证父进程页不被破坏——父在回收全部子进程后校验所有页仍为初始值.
#include "stdio.h"
#include "syscall.h"

#define COW_PAGES 16
#define CHILDREN 8

/* static 数组放 .data, 被加载为可写页 -> fork 时被 COW 保护 */
static unsigned char cow_pages[COW_PAGES][4096];

static unsigned int rng_state = 0x9E3779B9u;

static unsigned int next_rand(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

int main(void) {
    /* 初始填满, 每个页头尾打标记 */
    for (int p = 0; p < COW_PAGES; p++) {
        for (int i = 0; i < 4096; i++) {
            cow_pages[p][i] = 0xA5;
        }
    }
    printf("cow_stress: %d pages x %d children\n", COW_PAGES, CHILDREN);

    for (int c = 0; c < CHILDREN; c++) {
        int32_t pid = fork();
        if (pid == 0) {
            /* 子进程: 改写自己的 COW 副本 */
            unsigned int seed = next_rand() ^ (unsigned int)getpid();
            for (int p = 0; p < COW_PAGES; p++) {
                cow_pages[p][0] = (unsigned char)(seed & 0xFF);
                cow_pages[p][4095] = (unsigned char)((seed >> 8) & 0xFF);
            }
            /* 读回验证副本内容一致, 防止写丢 */
            for (int p = 0; p < COW_PAGES; p++) {
                if (cow_pages[p][0] != (unsigned char)(seed & 0xFF) ||
                    cow_pages[p][4095] != (unsigned char)((seed >> 8) & 0xFF)) {
                    printf("child%d: COPY CORRUPT at page %d\n", (int)c, p);
                    exit(1);
                }
            }
            exit(0);
        } else if (pid < 0) {
            printf("cow_stress: fork %d failed\n", c);
            exit(1);
        }
    }

    /* 父进程回收全部子进程 */
    int reaped = 0;
    for (int c = 0; c < CHILDREN; c++) {
        int32_t status = 0;
        int32_t r = wait(&status);
        if (r >= 0) {
            reaped++;
        }
    }

    /* COW 隔离校验: 父进程页必须仍是初始值 */
    int bad = 0;
    for (int p = 0; p < COW_PAGES; p++) {
        for (int i = 0; i < 4096; i++) {
            if (cow_pages[p][i] != 0xA5) {
                bad++;
                if (bad <= 4) {
                    printf("cow_stress: PARENT PAGE %d byte %d corrupted\n", p, i);
                    printf("             (child write leaked past COW)\n");
                }
            }
        }
    }

    printf("cow_stress: reaped=%d children\n", reaped);
    if (reaped == CHILDREN && bad == 0) {
        printf("COW STRESS OK\n");
        exit(0);
    } else {
        printf("COW STRESS FAIL (children=%d bad=%d)\n", reaped, bad);
        exit(1);
    }
    return 0;
}