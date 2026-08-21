BUILD_DIR   = build
SRC_DIR     = src
LINKER_DIR  = linker
SCRIPTS_DIR = scripts

.DEFAULT_GOAL := all

ASM     = nasm
CC      = zig cc
LD      = ld.lld        
OBJCOPY = objcopy
PYTHON  = python3       

CFLAGS = -target x86-freestanding -ffreestanding -fno-builtin -fno-sanitize=all

UP_CFLAGS = $(CFLAGS) -I $(SRC_DIR)/kernel/lib/user -I $(SRC_DIR)/kernel/lib/str -I $(SRC_DIR)/kernel/lib
UP_LDFLAGS = -s -m elf_i386 -Ttext 0x8048000 -e main

MUSL_SRC = $(SRC_DIR)/app/musl
MUSL_CFLAGS = $(CFLAGS) -I $(MUSL_SRC)/arch/i386 -I $(MUSL_SRC)/arch/generic \
              -I $(MUSL_SRC)/src/internal -I $(MUSL_SRC)/include -I $(MUSL_SRC)/nitian \
              -include $(MUSL_SRC)/nitian/nt_libc_macros.h

KERNEL_HDRS := $(wildcard $(SRC_DIR)/kernel/include/*.h \
                         $(SRC_DIR)/kernel/include/asm/*.h \
                         $(SRC_DIR)/kernel/lib/*/*.h \
                         $(SRC_DIR)/kernel/lib/user/*.h \
                         $(SRC_DIR)/kernel/memory/*/*.h \
                         $(SRC_DIR)/kernel/thread/*.h \
                         $(SRC_DIR)/kernel/device/*.h \
                         $(SRC_DIR)/kernel/fs/*.h \
                         $(SRC_DIR)/kernel/userprog/*.h \
                         $(SRC_DIR)/kernel/syscall/*.h \
                         $(SRC_DIR)/kernel/shell/*.h \
                         $(SRC_DIR)/kernel/initer/*/*.h)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/boot.bin: $(SRC_DIR)/boot/boot.asm | $(BUILD_DIR)
	$(ASM) -f bin $< -o $@

$(BUILD_DIR)/loader.bin: $(SRC_DIR)/boot/loader.asm | $(BUILD_DIR)
	$(ASM) -f bin $< -o $@

$(BUILD_DIR)/func.o: $(SRC_DIR)/kernel/asmCall/func.asm | $(BUILD_DIR)
	$(ASM) -f elf32 $< -o $@

$(BUILD_DIR)/io.o: $(SRC_DIR)/kernel/asmCall/io.asm | $(BUILD_DIR)
	$(ASM) -f elf32 $< -o $@

$(BUILD_DIR)/stub.o: $(SRC_DIR)/kernel/asmCall/stub.asm | $(BUILD_DIR)	
	$(ASM) -f elf32 $< -o $@

$(BUILD_DIR)/entry.o: $(SRC_DIR)/kernel/asmCall/entry.asm | $(BUILD_DIR)
	$(ASM) -f elf32 $< -o $@

$(BUILD_DIR)/switch.o: $(SRC_DIR)/kernel/asmCall/switch.asm | $(BUILD_DIR)
	$(ASM) -f elf32 $< -o $@

$(BUILD_DIR)/ioc.o: $(SRC_DIR)/kernel/initer/io/io.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pic.o: $(SRC_DIR)/kernel/initer/pic/pic.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pit.o: $(SRC_DIR)/kernel/initer/pit/pit.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/idt.o: $(SRC_DIR)/kernel/initer/idt/idt.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/interrupt.o: $(SRC_DIR)/kernel/initer/idt/interrupt.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel.o: $(SRC_DIR)/kernel/main.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/assert.o: $(SRC_DIR)/kernel/assert.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/str.o: $(SRC_DIR)/kernel/lib/str/str.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/bitmap.o: $(SRC_DIR)/kernel/memory/bitmap/bitmap.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pool.o: $(SRC_DIR)/kernel/memory/pool/pool.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/list.o: $(SRC_DIR)/kernel/lib/list/list.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/thread.o: $(SRC_DIR)/kernel/thread/thread.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sync.o: $(SRC_DIR)/kernel/thread/sync.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ioqueue.o: $(SRC_DIR)/kernel/device/ioqueue.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/keyboard.o: $(SRC_DIR)/kernel/device/keyboard.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ide.o: $(SRC_DIR)/kernel/device/ide.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ext2.o: $(SRC_DIR)/kernel/fs/ext2.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/fs.o: $(SRC_DIR)/kernel/fs/fs.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/inode.o: $(SRC_DIR)/kernel/fs/inode.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/dir.o: $(SRC_DIR)/kernel/fs/dir.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/file.o: $(SRC_DIR)/kernel/fs/file.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gdt.o: $(SRC_DIR)/kernel/initer/gdt/gdt.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/tss.o: $(SRC_DIR)/kernel/initer/tss/tss.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/process.o: $(SRC_DIR)/kernel/userprog/process.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/shell.o: $(SRC_DIR)/kernel/shell/shell.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/buildin_cmd.o: $(SRC_DIR)/kernel/shell/buildin_cmd.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pipe.o: $(SRC_DIR)/kernel/shell/pipe.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ksyscall.o: $(SRC_DIR)/kernel/syscall/syscall.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/mmap.o: $(SRC_DIR)/kernel/syscall/mmap.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/futex.o: $(SRC_DIR)/kernel/syscall/futex.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/file_syscall.o: $(SRC_DIR)/kernel/syscall/file_syscall.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/signal.o: $(SRC_DIR)/kernel/syscall/signal.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/linux_compat.o: $(SRC_DIR)/kernel/syscall/linux_compat.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/usyscall.o: $(SRC_DIR)/kernel/lib/user/syscall.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ustdio.o: $(SRC_DIR)/kernel/lib/user/stdio.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/exec.o: $(SRC_DIR)/kernel/userprog/exec.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/wait_exit.o: $(SRC_DIR)/kernel/userprog/wait_exit.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/fork.o: $(SRC_DIR)/kernel/userprog/fork.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/clone.o: $(SRC_DIR)/kernel/userprog/clone.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mouse.o: $(SRC_DIR)/kernel/device/mouse.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gfx.o: $(SRC_DIR)/kernel/gui/gfx.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/shm.o: $(SRC_DIR)/kernel/gui/shm.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/guiserver.o: $(SRC_DIR)/kernel/gui/server.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/layout.o: $(SRC_DIR)/kernel/gui/layout.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/wm.o: $(SRC_DIR)/kernel/gui/wm.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/guiclients.o: $(SRC_DIR)/kernel/gui/clients.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gui.o: $(SRC_DIR)/kernel/gui/gui.c $(KERNEL_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/prog_no_arg.elf: $(SRC_DIR)/command/prog_no_arg.c \
                               $(SRC_DIR)/kernel/lib/user/stdio.c \
                               $(SRC_DIR)/kernel/lib/user/syscall.c \
                               $(SRC_DIR)/kernel/lib/str/str.c | $(BUILD_DIR)
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/command/prog_no_arg.c -o $(BUILD_DIR)/up_no_arg.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/stdio.c -o $(BUILD_DIR)/up_stdio.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/syscall.c -o $(BUILD_DIR)/up_syscall.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/str/str.c -o $(BUILD_DIR)/up_str.o
	$(LD) $(UP_LDFLAGS) -o $@ \
	      $(BUILD_DIR)/up_no_arg.o $(BUILD_DIR)/up_stdio.o $(BUILD_DIR)/up_syscall.o $(BUILD_DIR)/up_str.o

$(BUILD_DIR)/prog_no_arg_data.o: $(BUILD_DIR)/prog_no_arg.elf | $(BUILD_DIR)
	cd $(BUILD_DIR) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 prog_no_arg.elf prog_no_arg_data.o

$(BUILD_DIR)/up_start.o: $(SRC_DIR)/command/start.asm | $(BUILD_DIR)
	$(ASM) -f elf32 $< -o $@

$(BUILD_DIR)/prog_arg.elf: $(BUILD_DIR)/up_start.o \
                           $(SRC_DIR)/command/prog_arg.c \
                           $(SRC_DIR)/command/start.asm \
                           $(SRC_DIR)/kernel/lib/user/stdio.c \
                           $(SRC_DIR)/kernel/lib/user/syscall.c \
                           $(SRC_DIR)/kernel/lib/str/str.c | $(BUILD_DIR)
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/command/prog_arg.c -o $(BUILD_DIR)/up_arg.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/stdio.c -o $(BUILD_DIR)/up_stdio.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/syscall.c -o $(BUILD_DIR)/up_syscall.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/str/str.c -o $(BUILD_DIR)/up_str.o
	$(LD) -s -m elf_i386 -Ttext 0x8048000 -e _start -o $@ \
	      $(BUILD_DIR)/up_start.o $(BUILD_DIR)/up_arg.o $(BUILD_DIR)/up_stdio.o $(BUILD_DIR)/up_syscall.o $(BUILD_DIR)/up_str.o

$(BUILD_DIR)/prog_arg_data.o: $(BUILD_DIR)/prog_arg.elf | $(BUILD_DIR)
	cd $(BUILD_DIR) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 prog_arg.elf prog_arg_data.o

$(BUILD_DIR)/cat.elf: $(BUILD_DIR)/up_start.o \
                      $(SRC_DIR)/command/cat.c \
                      $(SRC_DIR)/command/start.asm \
                      $(SRC_DIR)/kernel/lib/user/stdio.c \
                      $(SRC_DIR)/kernel/lib/user/syscall.c \
                      $(SRC_DIR)/kernel/lib/str/str.c | $(BUILD_DIR)
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/command/cat.c -o $(BUILD_DIR)/up_cat.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/stdio.c -o $(BUILD_DIR)/up_stdio.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/syscall.c -o $(BUILD_DIR)/up_syscall.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/str/str.c -o $(BUILD_DIR)/up_str.o
	$(LD) -s -m elf_i386 -Ttext 0x8048000 -e _start -o $@ \
	      $(BUILD_DIR)/up_start.o $(BUILD_DIR)/up_cat.o $(BUILD_DIR)/up_stdio.o $(BUILD_DIR)/up_syscall.o $(BUILD_DIR)/up_str.o

$(BUILD_DIR)/cat_data.o: $(BUILD_DIR)/cat.elf | $(BUILD_DIR)
	cd $(BUILD_DIR) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 cat.elf cat_data.o

$(BUILD_DIR)/fork_demo.elf: $(BUILD_DIR)/up_start.o \
                            $(SRC_DIR)/command/fork_demo.c \
                            $(SRC_DIR)/command/start.asm \
                            $(SRC_DIR)/kernel/lib/user/stdio.c \
                            $(SRC_DIR)/kernel/lib/user/syscall.c \
                            $(SRC_DIR)/kernel/lib/str/str.c | $(BUILD_DIR)
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/command/fork_demo.c -o $(BUILD_DIR)/up_fork.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/stdio.c -o $(BUILD_DIR)/up_stdio.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/syscall.c -o $(BUILD_DIR)/up_syscall.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/str/str.c -o $(BUILD_DIR)/up_str.o
	$(LD) -s -m elf_i386 -Ttext 0x8048000 -e _start -o $@ \
	      $(BUILD_DIR)/up_start.o $(BUILD_DIR)/up_fork.o $(BUILD_DIR)/up_stdio.o $(BUILD_DIR)/up_syscall.o $(BUILD_DIR)/up_str.o

$(BUILD_DIR)/fork_demo_data.o: $(BUILD_DIR)/fork_demo.elf | $(BUILD_DIR)
	cd $(BUILD_DIR) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 fork_demo.elf fork_demo_data.o

$(BUILD_DIR)/prog_pipe.elf: $(BUILD_DIR)/up_start.o \
                            $(SRC_DIR)/command/prog_pipe.c \
                            $(SRC_DIR)/command/start.asm \
                            $(SRC_DIR)/kernel/lib/user/stdio.c \
                            $(SRC_DIR)/kernel/lib/user/syscall.c \
                            $(SRC_DIR)/kernel/lib/str/str.c | $(BUILD_DIR)
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/command/prog_pipe.c -o $(BUILD_DIR)/up_pipe.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/stdio.c -o $(BUILD_DIR)/up_stdio.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/syscall.c -o $(BUILD_DIR)/up_syscall.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/str/str.c -o $(BUILD_DIR)/up_str.o
	$(LD) -s -m elf_i386 -Ttext 0x8048000 -e _start -o $@ \
	      $(BUILD_DIR)/up_start.o $(BUILD_DIR)/up_pipe.o $(BUILD_DIR)/up_stdio.o $(BUILD_DIR)/up_syscall.o $(BUILD_DIR)/up_str.o

$(BUILD_DIR)/prog_pipe_data.o: $(BUILD_DIR)/prog_pipe.elf | $(BUILD_DIR)
	cd $(BUILD_DIR) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 prog_pipe.elf prog_pipe_data.o

$(BUILD_DIR)/heap_demo.elf: $(BUILD_DIR)/up_start.o \
                            $(SRC_DIR)/command/heap_demo.c \
                            $(SRC_DIR)/kernel/lib/user/stdlib.c \
                            $(SRC_DIR)/command/start.asm \
                            $(SRC_DIR)/kernel/lib/user/stdio.c \
                            $(SRC_DIR)/kernel/lib/user/syscall.c \
                            $(SRC_DIR)/kernel/lib/str/str.c | $(BUILD_DIR)
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/command/heap_demo.c -o $(BUILD_DIR)/up_heap_demo.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/stdlib.c -o $(BUILD_DIR)/up_heap.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/stdio.c -o $(BUILD_DIR)/up_stdio.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/syscall.c -o $(BUILD_DIR)/up_syscall.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/str/str.c -o $(BUILD_DIR)/up_str.o
	$(LD) -s -m elf_i386 -Ttext 0x8048000 -e _start -o $@ \
	      $(BUILD_DIR)/up_start.o $(BUILD_DIR)/up_heap_demo.o $(BUILD_DIR)/up_heap.o $(BUILD_DIR)/up_stdio.o $(BUILD_DIR)/up_syscall.o $(BUILD_DIR)/up_str.o

$(BUILD_DIR)/heap_demo_data.o: $(BUILD_DIR)/heap_demo.elf | $(BUILD_DIR)
	cd $(BUILD_DIR) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 heap_demo.elf heap_demo_data.o

$(BUILD_DIR)/signal_demo.elf: $(BUILD_DIR)/up_start.o \
                            $(SRC_DIR)/command/signal_demo.c \
                            $(SRC_DIR)/kernel/lib/user/stdlib.c \
                            $(SRC_DIR)/command/start.asm \
                            $(SRC_DIR)/kernel/lib/user/stdio.c \
                            $(SRC_DIR)/kernel/lib/user/syscall.c \
                            $(SRC_DIR)/kernel/lib/str/str.c | $(BUILD_DIR)
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/command/signal_demo.c -o $(BUILD_DIR)/up_signal_demo.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/stdlib.c -o $(BUILD_DIR)/up_stdlib.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/stdio.c -o $(BUILD_DIR)/up_stdio.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/syscall.c -o $(BUILD_DIR)/up_syscall.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/str/str.c -o $(BUILD_DIR)/up_str.o
	$(LD) -s -m elf_i386 -Ttext 0x8048000 -e _start -o $@ \
	      $(BUILD_DIR)/up_start.o $(BUILD_DIR)/up_signal_demo.o $(BUILD_DIR)/up_stdlib.o $(BUILD_DIR)/up_stdio.o $(BUILD_DIR)/up_syscall.o $(BUILD_DIR)/up_str.o

$(BUILD_DIR)/signal_demo_data.o: $(BUILD_DIR)/signal_demo.elf | $(BUILD_DIR)
	cd $(BUILD_DIR) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 signal_demo.elf signal_demo_data.o

$(BUILD_DIR)/mmap_demo.elf: $(BUILD_DIR)/up_start.o \
                            $(SRC_DIR)/command/mmap_demo.c \
                            $(SRC_DIR)/command/start.asm \
                            $(SRC_DIR)/kernel/lib/user/stdio.c \
                            $(SRC_DIR)/kernel/lib/user/syscall.c \
                            $(SRC_DIR)/kernel/lib/str/str.c | $(BUILD_DIR)
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/command/mmap_demo.c -o $(BUILD_DIR)/up_mmap_demo.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/stdio.c -o $(BUILD_DIR)/up_stdio.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/syscall.c -o $(BUILD_DIR)/up_syscall.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/str/str.c -o $(BUILD_DIR)/up_str.o
	$(LD) -s -m elf_i386 -Ttext 0x8048000 -e _start -o $@ \
	      $(BUILD_DIR)/up_start.o $(BUILD_DIR)/up_mmap_demo.o $(BUILD_DIR)/up_stdio.o $(BUILD_DIR)/up_syscall.o $(BUILD_DIR)/up_str.o

$(BUILD_DIR)/mmap_demo_data.o: $(BUILD_DIR)/mmap_demo.elf | $(BUILD_DIR)
	cd $(BUILD_DIR) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 mmap_demo.elf mmap_demo_data.o

$(BUILD_DIR)/mmap2_demo.elf: $(BUILD_DIR)/up_start.o \
                            $(SRC_DIR)/command/mmap2_demo.c \
                            $(SRC_DIR)/command/start.asm \
                            $(SRC_DIR)/kernel/lib/user/stdio.c \
                            $(SRC_DIR)/kernel/lib/user/syscall.c \
                            $(SRC_DIR)/kernel/lib/str/str.c | $(BUILD_DIR)
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/command/mmap2_demo.c -o $(BUILD_DIR)/up_mmap2_demo.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/stdio.c -o $(BUILD_DIR)/up_stdio.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/syscall.c -o $(BUILD_DIR)/up_syscall.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/str/str.c -o $(BUILD_DIR)/up_str.o
	$(LD) -s -m elf_i386 -Ttext 0x8048000 -e _start -o $@ \
	      $(BUILD_DIR)/up_start.o $(BUILD_DIR)/up_mmap2_demo.o $(BUILD_DIR)/up_stdio.o $(BUILD_DIR)/up_syscall.o $(BUILD_DIR)/up_str.o

$(BUILD_DIR)/mmap2_demo_data.o: $(BUILD_DIR)/mmap2_demo.elf | $(BUILD_DIR)
	cd $(BUILD_DIR) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 mmap2_demo.elf mmap2_demo_data.o

$(BUILD_DIR)/futex_demo.elf: $(BUILD_DIR)/up_start.o \
                            $(SRC_DIR)/command/futex_demo.c \
                            $(SRC_DIR)/command/start.asm \
                            $(SRC_DIR)/kernel/lib/user/stdio.c \
                            $(SRC_DIR)/kernel/lib/user/syscall.c \
                            $(SRC_DIR)/kernel/lib/str/str.c | $(BUILD_DIR)
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/command/futex_demo.c -o $(BUILD_DIR)/up_futex_demo.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/stdio.c -o $(BUILD_DIR)/up_stdio.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/syscall.c -o $(BUILD_DIR)/up_syscall.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/str/str.c -o $(BUILD_DIR)/up_str.o
	$(LD) -s -m elf_i386 -Ttext 0x8048000 -e _start -o $@ \
	      $(BUILD_DIR)/up_start.o $(BUILD_DIR)/up_futex_demo.o $(BUILD_DIR)/up_stdio.o $(BUILD_DIR)/up_syscall.o $(BUILD_DIR)/up_str.o

$(BUILD_DIR)/futex_demo_data.o: $(BUILD_DIR)/futex_demo.elf | $(BUILD_DIR)
	cd $(BUILD_DIR) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 futex_demo.elf futex_demo_data.o

$(BUILD_DIR)/fsyscall_demo.elf: $(BUILD_DIR)/up_start.o \
                            $(SRC_DIR)/command/fsyscall_demo.c \
                            $(SRC_DIR)/command/start.asm \
                            $(SRC_DIR)/kernel/lib/user/stdio.c \
                            $(SRC_DIR)/kernel/lib/user/syscall.c \
                            $(SRC_DIR)/kernel/lib/str/str.c | $(BUILD_DIR)
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/command/fsyscall_demo.c -o $(BUILD_DIR)/up_fsyscall_demo.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/stdio.c -o $(BUILD_DIR)/up_stdio.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/syscall.c -o $(BUILD_DIR)/up_syscall.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/str/str.c -o $(BUILD_DIR)/up_str.o
	$(LD) -s -m elf_i386 -Ttext 0x8048000 -e _start -o $@ \
	      $(BUILD_DIR)/up_start.o $(BUILD_DIR)/up_fsyscall_demo.o $(BUILD_DIR)/up_stdio.o $(BUILD_DIR)/up_syscall.o $(BUILD_DIR)/up_str.o

$(BUILD_DIR)/fsyscall_demo_data.o: $(BUILD_DIR)/fsyscall_demo.elf | $(BUILD_DIR)
	cd $(BUILD_DIR) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 fsyscall_demo.elf fsyscall_demo_data.o

$(BUILD_DIR)/clone_demo.elf: $(BUILD_DIR)/up_start.o \
                            $(SRC_DIR)/command/clone_demo.c \
                            $(SRC_DIR)/kernel/lib/user/stdlib.c \
                            $(SRC_DIR)/command/start.asm \
                            $(SRC_DIR)/kernel/lib/user/stdio.c \
                            $(SRC_DIR)/kernel/lib/user/syscall.c \
                            $(SRC_DIR)/kernel/lib/str/str.c | $(BUILD_DIR)
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/command/clone_demo.c -o $(BUILD_DIR)/up_clone_demo.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/stdlib.c -o $(BUILD_DIR)/up_stdlib.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/stdio.c -o $(BUILD_DIR)/up_stdio.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/syscall.c -o $(BUILD_DIR)/up_syscall.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/str/str.c -o $(BUILD_DIR)/up_str.o
	$(LD) -s -m elf_i386 -Ttext 0x8048000 -e _start -o $@ \
	      $(BUILD_DIR)/up_start.o $(BUILD_DIR)/up_clone_demo.o $(BUILD_DIR)/up_stdlib.o $(BUILD_DIR)/up_stdio.o $(BUILD_DIR)/up_syscall.o $(BUILD_DIR)/up_str.o

$(BUILD_DIR)/clone_demo_data.o: $(BUILD_DIR)/clone_demo.elf | $(BUILD_DIR)
	cd $(BUILD_DIR) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 clone_demo.elf clone_demo_data.o

LC_CFLAGS = $(CFLAGS) -I $(SRC_DIR)/kernel/lib/compat

$(BUILD_DIR)/lc_start.o: $(SRC_DIR)/command/lc_crt0.asm | $(BUILD_DIR)
	$(ASM) -f elf32 $< -o $@

$(BUILD_DIR)/lc_libc.o: $(SRC_DIR)/kernel/lib/compat/lc_libc.c \
                        $(SRC_DIR)/kernel/lib/compat/lc.h | $(BUILD_DIR)
	$(CC) $(LC_CFLAGS) -c $< -o $@

$(BUILD_DIR)/lc_demo.o: $(SRC_DIR)/command/lc_demo.c \
                        $(SRC_DIR)/kernel/lib/compat/lc.h | $(BUILD_DIR)
	$(CC) $(LC_CFLAGS) -c $< -o $@

$(BUILD_DIR)/lc_demo.elf: $(BUILD_DIR)/lc_start.o \
                          $(BUILD_DIR)/lc_demo.o \
                          $(BUILD_DIR)/lc_libc.o | $(BUILD_DIR)
	$(LD) -s -m elf_i386 -Ttext 0x8048000 -e _lc_start -o $@ \
	      $(BUILD_DIR)/lc_start.o $(BUILD_DIR)/lc_demo.o $(BUILD_DIR)/lc_libc.o

$(BUILD_DIR)/lc_demo_data.o: $(BUILD_DIR)/lc_demo.elf | $(BUILD_DIR)
	cd $(BUILD_DIR) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 lc_demo.elf lc_demo_data.o

# --- musl 引导核心子集 (Bootstrap) ---
$(BUILD_DIR)/musl_start.o: $(MUSL_SRC)/nitian/musl_crt0.asm | $(BUILD_DIR)
	$(ASM) -f elf32 $< -o $@

$(BUILD_DIR)/musl_syscall.o: $(MUSL_SRC)/nitian/musl_syscall.asm | $(BUILD_DIR)
	$(ASM) -f elf32 $< -o $@

$(BUILD_DIR)/nt_errno.o: $(MUSL_SRC)/nitian/nt_errno.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/musl_ret.o: $(MUSL_SRC)/src/internal/syscall_ret.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/musl_getpid.o: $(MUSL_SRC)/src/unistd/getpid.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/musl_write.o: $(MUSL_SRC)/src/unistd/write.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/musl_exit.o: $(MUSL_SRC)/src/unistd/_exit.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/musl_exit_cap.o: $(MUSL_SRC)/src/exit/_Exit.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/musl_strlen.o: $(MUSL_SRC)/src/string/strlen.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/musl_strcpy.o: $(MUSL_SRC)/src/string/strcpy.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/musl_strcmp.o: $(MUSL_SRC)/src/string/strcmp.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/musl_strchr.o: $(MUSL_SRC)/src/string/strchr.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/musl_stpcpy.o: $(MUSL_SRC)/src/string/stpcpy.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/musl_strchrnul.o: $(MUSL_SRC)/src/string/strchrnul.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/musl_demo.o: $(SRC_DIR)/command/musl_demo.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/musl_demo.elf: $(BUILD_DIR)/musl_start.o \
                            $(BUILD_DIR)/musl_demo.o \
                            $(BUILD_DIR)/musl_getpid.o \
                            $(BUILD_DIR)/musl_write.o \
                            $(BUILD_DIR)/musl_exit.o \
                            $(BUILD_DIR)/musl_exit_cap.o \
                            $(BUILD_DIR)/musl_strlen.o \
                            $(BUILD_DIR)/musl_strcpy.o \
                            $(BUILD_DIR)/musl_strcmp.o \
                            $(BUILD_DIR)/musl_strchr.o \
                            $(BUILD_DIR)/musl_stpcpy.o \
                            $(BUILD_DIR)/musl_strchrnul.o \
                            $(BUILD_DIR)/musl_ret.o \
                            $(BUILD_DIR)/nt_errno.o \
                            $(BUILD_DIR)/musl_syscall.o | $(BUILD_DIR)
	$(LD) -s -m elf_i386 -Ttext 0x8048000 -e _musl_start -o $@ \
	      $(BUILD_DIR)/musl_start.o $(BUILD_DIR)/musl_demo.o \
	      $(BUILD_DIR)/musl_getpid.o $(BUILD_DIR)/musl_write.o \
	      $(BUILD_DIR)/musl_exit.o $(BUILD_DIR)/musl_exit_cap.o \
	      $(BUILD_DIR)/musl_strlen.o $(BUILD_DIR)/musl_strcpy.o \
	      $(BUILD_DIR)/musl_strcmp.o $(BUILD_DIR)/musl_strchr.o \
	      $(BUILD_DIR)/musl_stpcpy.o $(BUILD_DIR)/musl_strchrnul.o \
	      $(BUILD_DIR)/musl_ret.o $(BUILD_DIR)/nt_errno.o $(BUILD_DIR)/musl_syscall.o

$(BUILD_DIR)/musl_demo_data.o: $(BUILD_DIR)/musl_demo.elf | $(BUILD_DIR)
	cd $(BUILD_DIR) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 musl_demo.elf musl_demo_data.o

# --- musl libc-testsuite 程序 (纯算法测试子集) ---
$(BUILD_DIR)/libc_tests_main.o: $(SRC_DIR)/command/libc_tests_main.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/test_string.o: $(SRC_DIR)/app/libc-testsuite/string.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/test_qsort.o: $(SRC_DIR)/app/libc-testsuite/qsort.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/test_strtol.o: $(SRC_DIR)/app/libc-testsuite/strtol.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/test_strtod.o: $(SRC_DIR)/app/libc-testsuite/strtod.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/test_basename.o: $(SRC_DIR)/app/libc-testsuite/basename.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/test_dirname.o: $(SRC_DIR)/app/libc-testsuite/dirname.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/test_fnmatch.o: $(SRC_DIR)/app/libc-testsuite/fnmatch.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/musl_vfprintf.o: $(MUSL_SRC)/src/stdio/vfprintf.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_vsnprintf.o: $(MUSL_SRC)/src/stdio/vsnprintf.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_snprintf.o: $(MUSL_SRC)/src/stdio/snprintf.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_sprintf.o: $(MUSL_SRC)/src/stdio/sprintf.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_printf.o: $(MUSL_SRC)/src/stdio/printf.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_fprintf.o: $(MUSL_SRC)/src/stdio/fprintf.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_stdout.o: $(MUSL_SRC)/src/stdio/stdout.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_towrite.o: $(MUSL_SRC)/src/stdio/__towrite.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_stdiowrite.o: $(MUSL_SRC)/src/stdio/__stdio_write.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_fwrite.o: $(MUSL_SRC)/src/stdio/fwrite.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_overflow.o: $(MUSL_SRC)/src/stdio/__overflow.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_uflow.o: $(MUSL_SRC)/src/stdio/__uflow.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_toread.o: $(MUSL_SRC)/src/stdio/__toread.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_stdioclose.o: $(MUSL_SRC)/src/stdio/__stdio_close.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/musl_strncpy.o: $(MUSL_SRC)/src/string/strncpy.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_strncmp.o: $(MUSL_SRC)/src/string/strncmp.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_strncat.o: $(MUSL_SRC)/src/string/strncat.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_strrchr.o: $(MUSL_SRC)/src/string/strrchr.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_strspn.o: $(MUSL_SRC)/src/string/strspn.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_strcspn.o: $(MUSL_SRC)/src/string/strcspn.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_strpbrk.o: $(MUSL_SRC)/src/string/strpbrk.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_strtok.o: $(MUSL_SRC)/src/string/strtok.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_strlcpy.o: $(MUSL_SRC)/src/string/strlcpy.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_strlcat.o: $(MUSL_SRC)/src/string/strlcat.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_strdup.o: $(MUSL_SRC)/src/string/strdup.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_strnlen.o: $(MUSL_SRC)/src/string/strnlen.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_memcpy.o: $(MUSL_SRC)/src/string/memcpy.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_memset.o: $(MUSL_SRC)/src/string/memset.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_memcmp.o: $(MUSL_SRC)/src/string/memcmp.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/musl_qsort.o: $(MUSL_SRC)/src/stdlib/qsort.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_qsortnr.o: $(MUSL_SRC)/src/stdlib/qsort_nr.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_atol.o: $(MUSL_SRC)/src/stdlib/atol.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_atoi.o: $(MUSL_SRC)/src/stdlib/atoi.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_strtol.o: $(MUSL_SRC)/src/stdlib/strtol.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/musl_intscan.o: $(MUSL_SRC)/src/internal/intscan.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_shgetc.o: $(MUSL_SRC)/src/internal/shgetc.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/musl_basename.o: $(MUSL_SRC)/src/misc/basename.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/musl_dirname.o: $(MUSL_SRC)/src/misc/dirname.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/nt_libc_stubs.o: $(MUSL_SRC)/nitian/nt_libc_stubs.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/nt_fnmatch.o: $(MUSL_SRC)/nitian/nt_fnmatch.c | $(BUILD_DIR)
	$(CC) $(MUSL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/libc_testsuite.elf: $(BUILD_DIR)/musl_start.o \
                               $(BUILD_DIR)/musl_syscall.o \
                               $(BUILD_DIR)/musl_ret.o \
                               $(BUILD_DIR)/nt_errno.o \
                               $(BUILD_DIR)/libc_tests_main.o \
                               $(BUILD_DIR)/test_string.o \
                               $(BUILD_DIR)/test_qsort.o \
                               $(BUILD_DIR)/test_strtol.o \
                               $(BUILD_DIR)/test_strtod.o \
                               $(BUILD_DIR)/test_basename.o \
                               $(BUILD_DIR)/test_dirname.o \
                               $(BUILD_DIR)/test_fnmatch.o \
                               $(BUILD_DIR)/musl_vfprintf.o \
                               $(BUILD_DIR)/musl_vsnprintf.o \
                               $(BUILD_DIR)/musl_snprintf.o \
                               $(BUILD_DIR)/musl_sprintf.o \
                               $(BUILD_DIR)/musl_printf.o \
                               $(BUILD_DIR)/musl_fprintf.o \
                               $(BUILD_DIR)/musl_stdout.o \
                               $(BUILD_DIR)/musl_towrite.o \
                               $(BUILD_DIR)/musl_stdiowrite.o \
                               $(BUILD_DIR)/musl_fwrite.o \
                               $(BUILD_DIR)/musl_overflow.o \
                               $(BUILD_DIR)/musl_uflow.o \
                               $(BUILD_DIR)/musl_toread.o \
                               $(BUILD_DIR)/musl_stdioclose.o \
                               $(BUILD_DIR)/musl_strlen.o \
                               $(BUILD_DIR)/musl_strcpy.o \
                               $(BUILD_DIR)/musl_strcmp.o \
                               $(BUILD_DIR)/musl_strchr.o \
                               $(BUILD_DIR)/musl_strncpy.o \
                               $(BUILD_DIR)/musl_strncmp.o \
                               $(BUILD_DIR)/musl_strncat.o \
                               $(BUILD_DIR)/musl_strrchr.o \
                               $(BUILD_DIR)/musl_strspn.o \
                               $(BUILD_DIR)/musl_strcspn.o \
                               $(BUILD_DIR)/musl_strpbrk.o \
                               $(BUILD_DIR)/musl_strtok.o \
                               $(BUILD_DIR)/musl_strlcpy.o \
                               $(BUILD_DIR)/musl_strlcat.o \
                               $(BUILD_DIR)/musl_strdup.o \
                               $(BUILD_DIR)/musl_strnlen.o \
                               $(BUILD_DIR)/musl_memcpy.o \
                               $(BUILD_DIR)/musl_memset.o \
                               $(BUILD_DIR)/musl_memcmp.o \
                               $(BUILD_DIR)/musl_qsort.o \
                               $(BUILD_DIR)/musl_qsortnr.o \
                               $(BUILD_DIR)/musl_atol.o \
                               $(BUILD_DIR)/musl_atoi.o \
                               $(BUILD_DIR)/musl_strtol.o \
                               $(BUILD_DIR)/musl_intscan.o \
                               $(BUILD_DIR)/musl_shgetc.o \
                               $(BUILD_DIR)/musl_basename.o \
                               $(BUILD_DIR)/musl_dirname.o \
                               $(BUILD_DIR)/nt_libc_stubs.o \
                               $(BUILD_DIR)/nt_fnmatch.o | $(BUILD_DIR)
	$(LD) -s -m elf_i386 -Ttext 0x8048000 -e _musl_start -o $@ \
	      $(BUILD_DIR)/musl_start.o $(BUILD_DIR)/musl_syscall.o \
	      $(BUILD_DIR)/musl_ret.o $(BUILD_DIR)/nt_errno.o \
	      $(BUILD_DIR)/libc_tests_main.o \
	      $(BUILD_DIR)/test_string.o $(BUILD_DIR)/test_qsort.o \
	      $(BUILD_DIR)/test_strtol.o $(BUILD_DIR)/test_strtod.o \
	      $(BUILD_DIR)/test_basename.o $(BUILD_DIR)/test_dirname.o \
	      $(BUILD_DIR)/test_fnmatch.o \
	      $(BUILD_DIR)/musl_vfprintf.o $(BUILD_DIR)/musl_vsnprintf.o \
	      $(BUILD_DIR)/musl_snprintf.o $(BUILD_DIR)/musl_sprintf.o \
	      $(BUILD_DIR)/musl_printf.o $(BUILD_DIR)/musl_fprintf.o \
	      $(BUILD_DIR)/musl_stdout.o $(BUILD_DIR)/musl_towrite.o \
	      $(BUILD_DIR)/musl_stdiowrite.o $(BUILD_DIR)/musl_fwrite.o \
	      $(BUILD_DIR)/musl_overflow.o $(BUILD_DIR)/musl_uflow.o \
	      $(BUILD_DIR)/musl_toread.o $(BUILD_DIR)/musl_stdioclose.o \
	      $(BUILD_DIR)/musl_strlen.o $(BUILD_DIR)/musl_strcpy.o \
	      $(BUILD_DIR)/musl_strcmp.o $(BUILD_DIR)/musl_strchr.o \
	      $(BUILD_DIR)/musl_strncpy.o $(BUILD_DIR)/musl_strncmp.o \
	      $(BUILD_DIR)/musl_strncat.o $(BUILD_DIR)/musl_strrchr.o \
	      $(BUILD_DIR)/musl_strspn.o $(BUILD_DIR)/musl_strcspn.o \
	      $(BUILD_DIR)/musl_strpbrk.o $(BUILD_DIR)/musl_strtok.o \
	      $(BUILD_DIR)/musl_strlcpy.o $(BUILD_DIR)/musl_strlcat.o \
	      $(BUILD_DIR)/musl_strdup.o $(BUILD_DIR)/musl_strnlen.o \
	      $(BUILD_DIR)/musl_memcpy.o $(BUILD_DIR)/musl_memset.o \
	      $(BUILD_DIR)/musl_memcmp.o $(BUILD_DIR)/musl_qsort.o \
	      $(BUILD_DIR)/musl_qsortnr.o $(BUILD_DIR)/musl_atol.o \
	      $(BUILD_DIR)/musl_atoi.o $(BUILD_DIR)/musl_strtol.o \
	      $(BUILD_DIR)/musl_intscan.o $(BUILD_DIR)/musl_shgetc.o \
	      $(BUILD_DIR)/musl_basename.o $(BUILD_DIR)/musl_dirname.o \
	      $(BUILD_DIR)/nt_libc_stubs.o $(BUILD_DIR)/nt_fnmatch.o

$(BUILD_DIR)/libc_testsuite_data.o: $(BUILD_DIR)/libc_testsuite.elf | $(BUILD_DIR)
	cd $(BUILD_DIR) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 libc_testsuite.elf libc_testsuite_data.o

$(BUILD_DIR)/font_subset.ttf: $(SCRIPTS_DIR)/make_font_subset.py \
                               $(SRC_DIR)/kernel/lib/assets/font.ttf | $(BUILD_DIR)
	$(PYTHON) $(SCRIPTS_DIR)/make_font_subset.py \
	          $(SRC_DIR)/kernel/lib/assets/font.ttf $@

$(BUILD_DIR)/font.ttf: $(SCRIPTS_DIR)/make_font_subset.py \
                               $(SRC_DIR)/kernel/lib/assets/font.ttf | $(BUILD_DIR)
	$(PYTHON) $(SCRIPTS_DIR)/make_font_subset.py \
	          $(SRC_DIR)/kernel/lib/assets/font.ttf $@

$(BUILD_DIR)/font_subset_ttf_data.o: $(BUILD_DIR)/font_subset.ttf | $(BUILD_DIR)
	cd $(BUILD_DIR) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 font_subset.ttf font_subset_ttf_data.o

$(BUILD_DIR)/font_demo.elf: $(BUILD_DIR)/up_start.o \
                            $(SRC_DIR)/command/font_demo.c \
                            $(SRC_DIR)/command/start.asm \
                            $(SRC_DIR)/kernel/lib/stb_truetype.h \
                            $(SRC_DIR)/kernel/lib/user/stdio.c \
                            $(SRC_DIR)/kernel/lib/user/syscall.c \
                            $(SRC_DIR)/kernel/lib/str/str.c | $(BUILD_DIR)
	$(CC) $(UP_CFLAGS) -Os -c $(SRC_DIR)/command/font_demo.c -o $(BUILD_DIR)/up_font.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/stdio.c -o $(BUILD_DIR)/up_stdio.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/user/syscall.c -o $(BUILD_DIR)/up_syscall.o
	$(CC) $(UP_CFLAGS) -c $(SRC_DIR)/kernel/lib/str/str.c -o $(BUILD_DIR)/up_str.o
	$(LD) -s -m elf_i386 -Ttext 0x8048000 -e _start -o $@ \
	      $(BUILD_DIR)/up_start.o $(BUILD_DIR)/up_font.o $(BUILD_DIR)/up_stdio.o $(BUILD_DIR)/up_syscall.o $(BUILD_DIR)/up_str.o

$(BUILD_DIR)/font_demo_data.o: $(BUILD_DIR)/font_demo.elf | $(BUILD_DIR)
	cd $(BUILD_DIR) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 font_demo.elf font_demo_data.o

$(BUILD_DIR)/kernel.elf: $(BUILD_DIR)/entry.o \
                         $(BUILD_DIR)/kernel.o \
                         $(BUILD_DIR)/func.o \
                         $(BUILD_DIR)/ioc.o \
						 $(BUILD_DIR)/io.o \
                         $(BUILD_DIR)/pic.o \
						 $(BUILD_DIR)/pit.o \
						 $(BUILD_DIR)/stub.o \
						 $(BUILD_DIR)/idt.o \
						 $(BUILD_DIR)/interrupt.o \
						 $(BUILD_DIR)/assert.o \
						 $(BUILD_DIR)/str.o \
						 $(BUILD_DIR)/bitmap.o \
						 $(BUILD_DIR)/pool.o \
						 $(BUILD_DIR)/list.o \
						 $(BUILD_DIR)/switch.o \
						 $(BUILD_DIR)/thread.o \
						 $(BUILD_DIR)/sync.o \
						 $(BUILD_DIR)/ioqueue.o \
						 $(BUILD_DIR)/keyboard.o \
						 $(BUILD_DIR)/ide.o \
						 $(BUILD_DIR)/ext2.o \
						 $(BUILD_DIR)/fs.o \
						 $(BUILD_DIR)/inode.o \
						 $(BUILD_DIR)/dir.o \
						 $(BUILD_DIR)/file.o \
						 $(BUILD_DIR)/gdt.o \
						 $(BUILD_DIR)/tss.o \
						 $(BUILD_DIR)/process.o \
						 $(BUILD_DIR)/exec.o \
						 $(BUILD_DIR)/shell.o \
						 $(BUILD_DIR)/buildin_cmd.o \
						 $(BUILD_DIR)/ksyscall.o \
						 $(BUILD_DIR)/mmap.o \
						 $(BUILD_DIR)/futex.o \
						 $(BUILD_DIR)/linux_compat.o \
						 $(BUILD_DIR)/signal.o \
						 $(BUILD_DIR)/file_syscall.o \
						 $(BUILD_DIR)/usyscall.o \
						 $(BUILD_DIR)/ustdio.o \
						 $(BUILD_DIR)/prog_no_arg_data.o \
						 $(BUILD_DIR)/prog_arg_data.o \
						 $(BUILD_DIR)/cat_data.o \
						 $(BUILD_DIR)/fork_demo_data.o \
						 $(BUILD_DIR)/prog_pipe_data.o \
						 $(BUILD_DIR)/font_demo_data.o \
						 $(BUILD_DIR)/font_subset_ttf_data.o \
						 $(BUILD_DIR)/heap_demo_data.o \
						 $(BUILD_DIR)/signal_demo_data.o \
						 $(BUILD_DIR)/mmap_demo_data.o \
						 $(BUILD_DIR)/mmap2_demo_data.o \
						 $(BUILD_DIR)/futex_demo_data.o \
						 $(BUILD_DIR)/lc_demo_data.o \
						 $(BUILD_DIR)/musl_demo_data.o \
						 $(BUILD_DIR)/libc_testsuite_data.o \
						 $(BUILD_DIR)/fsyscall_demo_data.o \
						 $(BUILD_DIR)/clone_demo_data.o \
						 $(BUILD_DIR)/wait_exit.o \
						 $(BUILD_DIR)/fork.o \
						 $(BUILD_DIR)/clone.o \
						 $(BUILD_DIR)/pipe.o \
						 $(BUILD_DIR)/mouse.o \
						 $(BUILD_DIR)/gfx.o \
						 $(BUILD_DIR)/shm.o \
						 $(BUILD_DIR)/guiserver.o \
						 $(BUILD_DIR)/layout.o \
						 $(BUILD_DIR)/wm.o \
						 $(BUILD_DIR)/guiclients.o \
						 $(BUILD_DIR)/gui.o \
                         $(LINKER_DIR)/kernel.ld | $(BUILD_DIR)
	$(LD) -T $(LINKER_DIR)/kernel.ld -o $@ \
	      $(BUILD_DIR)/entry.o \
	      $(BUILD_DIR)/kernel.o \
	      $(BUILD_DIR)/func.o \
	      $(BUILD_DIR)/ioc.o \
		  $(BUILD_DIR)/io.o \
	      $(BUILD_DIR)/pic.o \
		  $(BUILD_DIR)/pit.o \
		  $(BUILD_DIR)/stub.o \
		  $(BUILD_DIR)/idt.o \
	      $(BUILD_DIR)/interrupt.o \
		  $(BUILD_DIR)/assert.o \
		  $(BUILD_DIR)/str.o \
		  $(BUILD_DIR)/bitmap.o \
		  $(BUILD_DIR)/pool.o \
		  $(BUILD_DIR)/list.o \
		  $(BUILD_DIR)/switch.o \
		  $(BUILD_DIR)/thread.o \
		  $(BUILD_DIR)/sync.o \
		  $(BUILD_DIR)/ioqueue.o \
		  $(BUILD_DIR)/keyboard.o \
		  $(BUILD_DIR)/ide.o \
		  $(BUILD_DIR)/ext2.o \
		  $(BUILD_DIR)/fs.o \
		  $(BUILD_DIR)/inode.o \
		  $(BUILD_DIR)/dir.o \
		  $(BUILD_DIR)/file.o \
		  $(BUILD_DIR)/gdt.o \
		  $(BUILD_DIR)/tss.o \
		  $(BUILD_DIR)/process.o \
		  $(BUILD_DIR)/exec.o \
		  $(BUILD_DIR)/shell.o \
		  $(BUILD_DIR)/buildin_cmd.o \
		  $(BUILD_DIR)/ksyscall.o \
		  $(BUILD_DIR)/mmap.o \
		  $(BUILD_DIR)/futex.o \
	      $(BUILD_DIR)/linux_compat.o \
	      $(BUILD_DIR)/signal.o \
	      $(BUILD_DIR)/file_syscall.o \
	      $(BUILD_DIR)/usyscall.o \
	      $(BUILD_DIR)/ustdio.o \
	      $(BUILD_DIR)/prog_no_arg_data.o \
	      $(BUILD_DIR)/prog_arg_data.o \
	      $(BUILD_DIR)/cat_data.o \
	      $(BUILD_DIR)/fork_demo_data.o \
	      $(BUILD_DIR)/prog_pipe_data.o \
	      $(BUILD_DIR)/font_demo_data.o \
	      $(BUILD_DIR)/font_subset_ttf_data.o \
	      $(BUILD_DIR)/heap_demo_data.o \
	      $(BUILD_DIR)/signal_demo_data.o \
	      $(BUILD_DIR)/mmap_demo_data.o \
	      $(BUILD_DIR)/mmap2_demo_data.o \
	      $(BUILD_DIR)/futex_demo_data.o \
	      $(BUILD_DIR)/lc_demo_data.o \
	      $(BUILD_DIR)/musl_demo_data.o \
	      $(BUILD_DIR)/libc_testsuite_data.o \
	      $(BUILD_DIR)/fsyscall_demo_data.o \
	      $(BUILD_DIR)/clone_demo_data.o \
		  $(BUILD_DIR)/wait_exit.o \
		  $(BUILD_DIR)/fork.o \
		  $(BUILD_DIR)/clone.o \
		  $(BUILD_DIR)/pipe.o \
		  $(BUILD_DIR)/mouse.o \
		  $(BUILD_DIR)/gfx.o \
		  $(BUILD_DIR)/shm.o \
		  $(BUILD_DIR)/guiserver.o \
		  $(BUILD_DIR)/layout.o \
		  $(BUILD_DIR)/wm.o \
		  $(BUILD_DIR)/guiclients.o \
		  $(BUILD_DIR)/gui.o

$(BUILD_DIR)/kernel.bin: $(BUILD_DIR)/kernel.elf
	$(OBJCOPY) -O binary $< $@

FLOPPY_DEPS = $(BUILD_DIR)/boot.bin \
              $(BUILD_DIR)/loader.bin \
              $(BUILD_DIR)/kernel.bin \
              $(SCRIPTS_DIR)/mkfloppy.py

$(BUILD_DIR)/floppy.img: $(FLOPPY_DEPS) | $(BUILD_DIR)
	$(PYTHON) $(SCRIPTS_DIR)/mkfloppy.py \
	          $(BUILD_DIR)/boot.bin \
	          $(BUILD_DIR)/loader.bin \
	          $(BUILD_DIR)/kernel.bin \
	          $@

$(BUILD_DIR)/test_hd.img: $(SCRIPTS_DIR)/make_ext2.py | $(BUILD_DIR)
	$(PYTHON) $(SCRIPTS_DIR)/make_ext2.py $(BUILD_DIR) $@

.PHONY: all floppy run clean

all: floppy

floppy: $(BUILD_DIR)/floppy.img

run: floppy $(BUILD_DIR)/test_hd.img
	qemu-system-i386 -accel tcg,tb-size=256 -m 1G -fda $(BUILD_DIR)/floppy.img \
	                 -hda $(BUILD_DIR)/test_hd.img -debugcon stdio

clean:
	rm -rf $(BUILD_DIR)

-include $(BUILD_DIR)/*.d