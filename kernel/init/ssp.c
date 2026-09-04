#include "kernel/ssp.h"

#include "kernel/asmFunc.h"
#include "lib/rand/rand.h"
#include "drivers/char/console/io.h"

uintptr_t __stack_chk_guard;

__attribute__((no_stack_protector))
void stack_canary_init(void) {
    __stack_chk_guard = rand_u64() & ~(uintptr_t)0xff;
}

__attribute__((noreturn, no_stack_protector))
void __stack_chk_fail(void) {
    set_text_color(12);
    kprintf("\n*** STACK SMASHING DETECTED ***\n");
    asm_cli();
    for (;;) {
        asm_hlt();
    }
}
