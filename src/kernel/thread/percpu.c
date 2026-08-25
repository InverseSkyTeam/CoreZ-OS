/* Per-CPU 初始化 */
#include "./percpu.h"
#include "../include/asmFunc.h"
#include "../initer/gdt/gdt.h"
void percpu_init(void) {
    uint16_t sel = SELECTOR_PER_CPU;
    __asm__ volatile("movw %0, %%gs" : : "r"(sel) : "memory");
    set_current((struct task_struct *)0);
}
