/*
 * 参考: nr_micro_shell 官方 examples/linux_with_mini_config/nr_shell.c
 * 说明: Nitian-OS 用户态 shell 主程序。
 *       shell_init() 初始化后, 循环 read(0,...) 逐字符喂给 shell(c);
 *       内核 sys_read(fd==0) 每读一次返回一个键盘字符,
 * 与行编辑/补全实时交互兼容。
 */
#include "nr_micro_shell.h"
#include <unistd.h>

int main(void) {
    unsigned char c;

    shell_init();
    for (;;) {
        if (read(0, &c, 1) == 1) {
            shell(c);
        }
    }
    return 0;
}