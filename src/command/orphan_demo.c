#include "stdio.h"
#include "syscall.h"

int main(void) {
    int32_t pid = fork();
    if (pid > 0) {
        printf("parent: child pid=%d, parent exits now\n", (int)pid);
        exit(0);
    } else if (pid == 0) {
        for (volatile int i = 0; i < 20000000; i++) {
        }
        printf("orphan child exiting\n");
        exit(7);
    }
    return 0;
}
