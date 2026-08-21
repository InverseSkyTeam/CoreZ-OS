/*
 * 参考: nr_micro_shell 官方 examples/linux_with_mini_config/nr_micro_shell_port.h
 * 说明: Nitian-OS 用户态移植(不变更 vendored 端 mr_micro_shell 任何源码)
 *       shell_putc 经 write(1,...) 输出到 console;
 *       不启用依赖 sscanf 的 rd/wr/hex2dec/time 命令, 避免引入 stdio 扫描依赖。
 */
#ifndef __NR_MICRO_SHELL_PORT_H__
#define __NR_MICRO_SHELL_PORT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <unistd.h>

/* 必需配置宏: 每个字符经 fd1 写往 console */
#define shell_putc(x) write(1, &(x), 1)

/* 可选配置宏 */
#define NR_SHELL_MAX_LINE_SZ  100
#define NR_SHELL_PROMPT       "nitian"
#define NR_SHELL_MAX_PARAM_NUM 16

#define NR_SHELL_SHOW_LOGO

#define NR_SHELL_AUTO_COMPLETE_SUPPORT
#define NR_SHELL_HISTORY_CMD_SUPPORT
#define NR_SHELL_HISTORY_CMD_NUM  5
#define NR_SHELL_HISTORY_CMD_SZ  100

#ifdef __cplusplus
}
#endif
#endif /* __NR_MICRO_SHELL_PORT_H__ */