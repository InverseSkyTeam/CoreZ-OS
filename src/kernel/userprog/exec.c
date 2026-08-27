#include "./exec.h"
#include "../fs/fs.h"
#include "../include/asm/stub.h"
#include "../include/asmFunc.h"
#include "../include/assert.h"
#include "../include/auxv.h"
#include "../initer/gdt/gdt.h"
#include "../initer/io/io.h"
#include "../lib/str/str.h"
#include "../memory/pool/pool.h"
#include "../thread/thread.h"
#include "../userprog/process.h"
#define DIV_ROUND_UP(X, STEP) ((X + STEP - 1) / STEP)
#define PF_X 0x1
#define EFLAGS_MBS (1 << 1)
#define EFLAGS_IF_1 (1 << 9)
#define EFLAGS_IOPL_0 0
#define MAX_ARG_NR 16

struct Elf64_Nhdr {
    uint32_t namesz;
    uint32_t descsz;
    uint32_t type;
};

#define NT_GNU_ABI_TAG 1
#define EM_X86_64 62
#define EM_386 3
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;
struct Elf32_Ehdr {
    unsigned char e_ident[16];
    Elf32_Half e_type;
    Elf32_Half e_machine;
    Elf32_Word e_version;
    Elf32_Addr e_entry;
    Elf32_Off e_phoff;
    Elf32_Off e_shoff;
    Elf32_Word e_flags;
    Elf32_Half e_ehsize;
    Elf32_Half e_phentsize;
    Elf32_Half e_phnum;
    Elf32_Half e_shentsize;
    Elf32_Half e_shnum;
    Elf32_Half e_shstrndx;
};
struct Elf32_Phdr {
    Elf32_Word p_type;
    Elf32_Off p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
};
enum segment_type {
    PT_NULL,
    PT_LOAD,
    PT_DYNAMIC,
    PT_INTERP,
    PT_NOTE,
    PT_SHLIB,
    PT_PHDR
};
typedef uint64_t Elf64_Addr, Elf64_Off, Elf64_Xword;
typedef uint32_t Elf64_Word;
typedef uint16_t Elf64_Half;
struct Elf64_Ehdr {
    unsigned char e_ident[16];
    Elf64_Half e_type;
    Elf64_Half e_machine;
    Elf64_Word e_version;
    Elf64_Addr e_entry;
    Elf64_Off e_phoff;
    Elf64_Off e_shoff;
    Elf64_Word e_flags;
    Elf64_Half e_ehsize;
    Elf64_Half e_phentsize;
    Elf64_Half e_phnum;
    Elf64_Half e_shentsize;
    Elf64_Half e_shnum;
    Elf64_Half e_shstrndx;
};
struct Elf64_Phdr {
    Elf64_Word p_type;
    Elf64_Word p_flags;
    Elf64_Off p_offset;
    Elf64_Addr p_vaddr;
    Elf64_Addr p_paddr;
    Elf64_Xword p_filesz;
    Elf64_Xword p_memsz;
    Elf64_Xword p_align;
};
static int32_t segment_load(int32_t fd, uint32_t offset, uint32_t filesz,
                            uint32_t memsz, uint32_t vaddr, int executable) {
    uint32_t vaddr_first_page = vaddr & 0xfffff000;
    uint32_t size_in_first_page = PAGE_SIZE - (vaddr & 0x00000fff);
    uint32_t occupy_pages =
        (memsz > size_in_first_page)
            ? DIV_ROUND_UP(memsz - size_in_first_page, PAGE_SIZE) + 1
            : 1;
    uint32_t vaddr_page = vaddr_first_page;
    for (uint32_t i = 0; i < occupy_pages; i++) {
        uint64_t *pde = pde_ptr(vaddr_page);
        uint64_t *pte = pte_ptr(vaddr_page);
        if (!(*pde & 1) || (*pde & 0x80) || !(*pte & 1)) {
            if (get_a_page(vaddr_page) == 0) {
                return -1;
            }
        }
        vaddr_page += PAGE_SIZE;
    }
    sys_lseek(fd, offset, SEEK_SET);
    read_file(fd, (void *)vaddr, filesz);
    if (executable) {
        /* W^X: 可执行段 -> RX (清 W 与 NX), 不可再写入 */
        for (uint32_t pg = vaddr_first_page;
             pg < vaddr_first_page + occupy_pages * PAGE_SIZE;
             pg += PAGE_SIZE) {
            uint64_t *pte = pte_ptr(pg);
            if (*pte & PTE_P) {
                *pte = (*pte & 0x000ffffffffff000ull) | PTE_P | PTE_U;
                __asm__ volatile("invlpg (%0)" : : "r"(pg) : "memory");
            }
        }
    }
    return 0;
}
static int32_t load(const char *pathname, int *is64, int *is_linux) {
    int32_t ret = -1;
    unsigned char ident[16];
    int32_t fd = open_file(pathname, O_RDONLY);
    if (fd == -1) {
        return -1;
    }
    if (read_file(fd, ident, sizeof(ident)) != sizeof(ident)) {
        goto done;
    }

    if (ident[0] != 0x7f || ident[1] != 'E' || ident[2] != 'L' ||
        ident[3] != 'F' || (ident[4] != 1 && ident[4] != 2)) {
        goto done;
    }
    *is64 = (ident[4] == 2);
    *is_linux = 0;
    sys_lseek(fd, 0, SEEK_SET);
    if (*is64) {
        struct Elf64_Ehdr elf64_header;
        memset(&elf64_header, 0, sizeof(elf64_header));
        if (read_file(fd, &elf64_header, sizeof(elf64_header)) !=
            sizeof(elf64_header)) {
            goto done;
        }
        if (elf64_header.e_type != 2 || elf64_header.e_machine != EM_X86_64 ||
            elf64_header.e_version != 1 || elf64_header.e_phnum > 1024 ||
            elf64_header.e_phentsize != sizeof(struct Elf64_Phdr)) {
            goto done;
        }

        Elf64_Off prog_header_offset = elf64_header.e_phoff;
        for (uint32_t prog_idx = 0; prog_idx < elf64_header.e_phnum;
             prog_idx++) {
            struct Elf64_Phdr prog64_header;
            memset(&prog64_header, 0, sizeof(prog64_header));
            sys_lseek(fd, prog_header_offset, SEEK_SET);
            if (read_file(fd, &prog64_header, sizeof(prog64_header)) !=
                sizeof(prog64_header)) {
                goto done;
            }

            if (prog64_header.p_type == PT_NOTE) {
                sys_lseek(fd, (uint32_t)prog64_header.p_offset, SEEK_SET);
                uint32_t remaining = (uint32_t)prog64_header.p_filesz;
                while (remaining >= sizeof(struct Elf64_Nhdr)) {
                    struct Elf64_Nhdr nhdr;
                    if (read_file(fd, &nhdr, sizeof(nhdr)) != sizeof(nhdr))
                        break;
                    uint32_t note_size = sizeof(nhdr) +
                        ((nhdr.namesz + 3) & ~3u) +
                        ((nhdr.descsz + 3) & ~3u);
                    if (note_size > remaining)
                        break;
                    if (nhdr.type == NT_GNU_ABI_TAG && nhdr.namesz >= 4) {
                        char name[4];
                        read_file(fd, name, 4);
                        if (memcmp(name, "GNU", 4) == 0 && nhdr.descsz >= 8) {
                            uint8_t desc[8];
                            read_file(fd, desc, 8);
                            if (desc[0] == 0) {
                                *is_linux = 1;
                                kprintf("[exec] Linux ELF detected (GNU ABI-tag)\n");
                            }
                        }
                    }
                    remaining -= note_size;
                    sys_lseek(fd, (uint32_t)prog64_header.p_offset +
                              (prog64_header.p_filesz - remaining), SEEK_SET);
                }
                sys_lseek(fd, prog_header_offset + prog_idx *
                          elf64_header.e_phentsize + sizeof(struct Elf64_Phdr),
                          SEEK_SET);
            }

            if (prog64_header.p_type == PT_LOAD &&
                prog64_header.p_vaddr >= USER_VADDR_START &&
                prog64_header.p_vaddr < USER_STACK3_VADDR) {
                if (segment_load(fd, (uint32_t)prog64_header.p_offset,
                                 (uint32_t)prog64_header.p_filesz,
                                 (uint32_t)prog64_header.p_memsz,
                                 (uint32_t)prog64_header.p_vaddr,
                                 (prog64_header.p_flags & PF_X) != 0) == -1) {
                    goto done;
                }
            }
            prog_header_offset += elf64_header.e_phentsize;
        }
        ret = (int32_t)elf64_header.e_entry;
    } else {
        struct Elf32_Ehdr elf_header;
        struct Elf32_Phdr prog_header;
        memset(&elf_header, 0, sizeof(elf_header));
        if (read_file(fd, &elf_header, sizeof(elf_header)) !=
            sizeof(elf_header)) {
            goto done;
        }
        if (memcmp(elf_header.e_ident, "\177ELF\1\1\1", 7) ||
            elf_header.e_type != 2 || elf_header.e_machine != EM_386 ||
            elf_header.e_version != 1 || elf_header.e_phnum > 1024 ||
            elf_header.e_phentsize != sizeof(struct Elf32_Phdr)) {
            goto done;
        }

        Elf32_Off prog_header_offset = elf_header.e_phoff;
        for (uint32_t prog_idx = 0; prog_idx < elf_header.e_phnum; prog_idx++) {
            memset(&prog_header, 0, sizeof(prog_header));
            sys_lseek(fd, prog_header_offset, SEEK_SET);
            if (read_file(fd, &prog_header, sizeof(prog_header)) !=
                sizeof(prog_header)) {
                goto done;
            }

            if (prog_header.p_type == PT_NOTE) {
                sys_lseek(fd, prog_header.p_offset, SEEK_SET);
                uint32_t remaining = prog_header.p_filesz;
                while (remaining >= 12) {
                    uint32_t namesz, descsz, type;
                    if (read_file(fd, &namesz, 4) != 4) break;
                    if (read_file(fd, &descsz, 4) != 4) break;
                    if (read_file(fd, &type, 4) != 4) break;
                    uint32_t note_size = 12 + ((namesz + 3) & ~3u) +
                        ((descsz + 3) & ~3u);
                    if (note_size > remaining) break;
                    if (type == NT_GNU_ABI_TAG && namesz >= 4) {
                        char name[4];
                        read_file(fd, name, 4);
                        if (memcmp(name, "GNU", 4) == 0 && descsz >= 8) {
                            uint8_t desc[8];
                            read_file(fd, desc, 8);
                            if (desc[0] == 0) {
                                *is_linux = 1;
                                kprintf("[exec] Linux ELF detected (GNU ABI-tag)\n");
                            }
                        }
                    }
                    remaining -= note_size;
                    sys_lseek(fd, prog_header.p_offset +
                              (prog_header.p_filesz - remaining), SEEK_SET);
                }
                sys_lseek(fd, prog_header_offset + prog_idx *
                          elf_header.e_phentsize + sizeof(struct Elf32_Phdr),
                          SEEK_SET);
            }

            if (prog_header.p_type == PT_LOAD &&
                prog_header.p_vaddr >= USER_VADDR_START &&
                prog_header.p_vaddr < USER_STACK3_VADDR) {
                if (segment_load(fd, prog_header.p_offset, prog_header.p_filesz,
                                 prog_header.p_memsz, prog_header.p_vaddr,
                                 (prog_header.p_flags & PF_X) != 0) == -1) {
                    goto done;
                }
            }
            prog_header_offset += elf_header.e_phentsize;
        }
        ret = elf_header.e_entry;
    }
done:
    close_file(fd);
    return ret;
}
int32_t sys_execv(const char *path, const char *argv[],
                  struct Registers *regs) {
    uint32_t argc;
    int32_t entry_point;
    struct task_struct *cur;
    uint32_t old_pgdir;
    uint32_t ustack_ptr;
    uint32_t argv_user_addrs[MAX_ARG_NR];
    uint32_t argv_user_base;
    int32_t i;
    uint32_t slen;
    struct Registers *ps;
    int is64 = 0;
    int is_linux = 0;
    cur = current;
    old_pgdir = cur->pgdir;
    if (cur->pgdir == 0) {
        cur->pgdir = (uint32_t)create_page_dir();
        process_activate(cur);
    }
    if (cur->userprog_v_addr.vaddr_bitmap.bits != NULL) {
        uint32_t bitmap_bytes =
            cur->userprog_v_addr.vaddr_bitmap.btmp_bytes_len;
        uint32_t bitmap_pg_cnt = (bitmap_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
        for (uint32_t i = 0; i < bitmap_pg_cnt; i++) {
            free_kernel_page((uint32_t)cur->userprog_v_addr.vaddr_bitmap.bits +
                             i * PAGE_SIZE);
        }
        cur->userprog_v_addr.vaddr_bitmap.bits = NULL;
    }
    create_user_vaddr_bitmap(cur);
    argc = 0;
    while (argv && argv[argc] && argc < MAX_ARG_NR) {
        ++argc;
    }
    entry_point = load(path, &is64, &is_linux);
    if (entry_point == -1) {
        if (cur->pgdir != old_pgdir) {
            cur->pgdir = old_pgdir;
            page_dir_activate(cur);
        }
        return -1;
    }
    memcpy(cur->name, path, 15);
    cur->name[15] = 0;
    cur->user_brk = 0;
    signal_reset_user(cur);
    for (uint32_t sp = USER_STACK3_VADDR - PAGE_SIZE; sp <= USER_STACK3_VADDR;
         sp += PAGE_SIZE) {
        uint64_t *pde = pde_ptr(sp);
        uint64_t *pte = pte_ptr(sp);
        if (!(*pde & 1) || !(*pte & 1)) {
            if (get_a_page(sp) == 0) {
                kprintf("[exec] get_a_page for user stack failed\n");
                return -1;
            }
        }
    }
    cur->tls_base = 0;
    cur->tls_selector = 0;
    cur->errno = 0;
    cur->compat = is_linux;
    if (is_linux) {
        kprintf("[exec] task %d marked as Linux compat (ABI-tag detected)\n",
                cur->pid);
    }
    ustack_ptr = USER_STACK3_VADDR + PAGE_SIZE;
    for (i = 0; i < MAX_ARG_NR; ++i) {
        argv_user_addrs[i] = 0;
    }
    if (argc > 0) {
        for (i = (int32_t)argc - 1; i >= 0; --i) {
            slen = strlen(argv[i]) + 1;
            ustack_ptr -= slen;
            ustack_ptr &= ~(is64 ? 0x7u : 0x3u);
            if (ustack_ptr < USER_STACK3_VADDR) {
                kprintf("[exec] argv too large for user stack\n");
                return -1;
            }
            memcpy((void *)ustack_ptr, argv[i], slen);
            argv_user_addrs[i] = ustack_ptr;
        }
    }
    {
        static const char *env_defaults[] = {"PATH=/", "HOME=/", 0};
        const char *exefn = path;
        uint32_t envp_addrs[8];
        int envc = 0;
        uint32_t exefn_addr = 0;
        uint32_t aux_dst = 0;
        uint32_t aux_bytes = 0;
        int e;
#define PSTACK32(sp, v)                                                        \
    do {                                                                       \
        (sp) -= 4;                                                             \
        *((uint32_t *)(sp)) = (uint32_t)(v);                                   \
    } while (0)
#define PSTACK64(sp, v)                                                        \
    do {                                                                       \
        (sp) -= 8;                                                             \
        *((uint64_t *)(sp)) = (uint64_t)(v);                                   \
    } while (0)
        exefn_addr = 0;
        while (env_defaults[envc] != 0 && envc < 8) {
            envc++;
        }
#define A64(t, v)                                                              \
    do {                                                                       \
        aux64[naw++] = (uint64_t)(t);                                          \
        aux64[naw++] = (uint64_t)(v);                                          \
    } while (0)
#define A32(t, v)                                                              \
    do {                                                                       \
        aux32[naw++] = (uint32_t)(t);                                          \
        aux32[naw++] = (uint32_t)(v);                                          \
    } while (0)
        if (is64) {
            uint64_t aux64[32];
            int naw = 0;
            A64(AT_EXECFN, 0);
            A64(AT_PAGESZ, PAGE_SIZE);
            A64(AT_CLKTCK, 100);
            A64(AT_ENTRY, (uint64_t)entry_point);
            A64(AT_PHDR, 0);
            A64(AT_PHENT, 0);
            A64(AT_PHNUM, 0);
            A64(AT_FLAGS, 0);
            A64(AT_HWCAP, 0);
            A64(AT_NULL, 0);
            aux_bytes = (uint32_t)(naw * 8);

            slen = strlen(exefn) + 1;
            ustack_ptr -= slen;
            ustack_ptr &= ~0x7u;
            if (ustack_ptr < USER_STACK3_VADDR) {
                return -1;
            }
            memcpy((void *)ustack_ptr, exefn, slen);
            exefn_addr = ustack_ptr;
            for (e = envc - 1; e >= 0; --e) {
                slen = strlen(env_defaults[e]) + 1;
                ustack_ptr -= slen;
                ustack_ptr &= ~0x7u;
                if (ustack_ptr < USER_STACK3_VADDR) {
                    return -1;
                }
                memcpy((void *)ustack_ptr, env_defaults[e], slen);
                envp_addrs[e] = ustack_ptr;
            }
            ustack_ptr &= ~0x7u;
            ustack_ptr -= aux_bytes;
            aux_dst = ustack_ptr;
            aux64[1] = (uint64_t)exefn_addr;
            memcpy((void *)aux_dst, aux64, aux_bytes);
            PSTACK64(ustack_ptr, 0);
            for (e = envc - 1; e >= 0; --e)
                PSTACK64(ustack_ptr, envp_addrs[e]);
            PSTACK64(ustack_ptr, 0);
            for (i = (int32_t)argc - 1; i >= 0; --i)
                PSTACK64(ustack_ptr, argv_user_addrs[i]);
            PSTACK64(ustack_ptr, argc);
            argv_user_base = (uint32_t)((char *)ustack_ptr + 8);
        } else {
            uint32_t aux32[32];
            int naw = 0;
            A32(AT_EXECFN, 0);
            A32(AT_PAGESZ, PAGE_SIZE);
            A32(AT_CLKTCK, 100);
            A32(AT_ENTRY, (uint32_t)entry_point);
            A32(AT_PHDR, 0);
            A32(AT_PHENT, 0);
            A32(AT_PHNUM, 0);
            A32(AT_FLAGS, 0);
            A32(AT_HWCAP, 0);
            A32(AT_NULL, 0);
            aux_bytes = (uint32_t)(naw * 4);
            ustack_ptr &= ~0x3u;
            ustack_ptr -= aux_bytes;
            aux_dst = ustack_ptr;
            slen = strlen(exefn) + 1;
            ustack_ptr -= slen;
            ustack_ptr &= ~0x3u;
            if (ustack_ptr < USER_STACK3_VADDR) {
                return -1;
            }
            memcpy((void *)ustack_ptr, exefn, slen);
            exefn_addr = ustack_ptr;
            aux32[1] = (uint32_t)exefn_addr;
            for (e = envc - 1; e >= 0; --e) {
                slen = strlen(env_defaults[e]) + 1;
                ustack_ptr -= slen;
                ustack_ptr &= ~0x3u;
                if (ustack_ptr < USER_STACK3_VADDR) {
                    return -1;
                }
                memcpy((void *)ustack_ptr, env_defaults[e], slen);
                envp_addrs[e] = ustack_ptr;
            }
            PSTACK32(ustack_ptr, 0);
            for (e = envc - 1; e >= 0; --e)
                PSTACK32(ustack_ptr, envp_addrs[e]);
            PSTACK32(ustack_ptr, 0);
            for (i = (int32_t)argc - 1; i >= 0; --i)
                PSTACK32(ustack_ptr, argv_user_addrs[i]);
            PSTACK32(ustack_ptr, argc);
            argv_user_base = (uint32_t)((char *)ustack_ptr + 4);
            memcpy((void *)aux_dst, aux32, aux_bytes);
        }
#undef A64
#undef A32
#undef PSTACK32
#undef PSTACK64
    }
    if (regs != NULL) {
        memset(regs, 0, sizeof(struct Registers));
        regs->rip = (uint64_t)(uint32_t)entry_point;
        regs->cs = is64 ? SELECTOR_USER64_CODE : SELECTOR_U_CODE;
        regs->rflags = EFLAGS_IOPL_0 | EFLAGS_MBS | EFLAGS_IF_1;
        regs->user_rsp = ustack_ptr;
        regs->ss = SELECTOR_U_DATA;
        if (is64) {
            regs->rdi = argc;
            regs->rsi = argv_user_base;
        } else {
            regs->rbx = argv_user_base;
            regs->rcx = argc;
        }
        return 0;
    }

    ps =
        (struct Registers *)(cur->kernel_stack_top - THREAD_STACK_SIZE + 0x100);
    memset(ps, 0, sizeof(struct Registers));
    ps->rip = (uint64_t)(uint32_t)entry_point;
    ps->cs = is64 ? SELECTOR_USER64_CODE : SELECTOR_U_CODE;
    ps->rflags = EFLAGS_IOPL_0 | EFLAGS_MBS | EFLAGS_IF_1;
    ps->user_rsp = ustack_ptr;
    ps->ss = SELECTOR_U_DATA;
    if (is64) {
        ps->rdi = argc;
        ps->rsi = argv_user_base;
    } else {
        ps->rbx = argv_user_base;
        ps->rcx = argc;
    }

    __asm__ volatile("mov %0, %%ds; mov %0, %%es; mov %0, %%fs;" ::"r"(
                         (uint16_t)SELECTOR_U_DATA)
                     : "memory");
    __asm__ volatile("mov %0, %%rsp; jmp intr_exit" : : "r"(ps) : "memory");
    return 0;
}
