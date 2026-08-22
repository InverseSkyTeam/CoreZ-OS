#include "inode.h"

#include "../device/ide.h"
#include "../include/asmFunc.h"
#include "../lib/str/str.h"
#include "../memory/pool/pool.h"
#include "ext2.h"
#include "fs.h"

struct inode *inode_open(struct partition *part, uint32_t inode_no) {
    struct list_elem *elem = part->open_inodes.head.next;
    while (elem != &part->open_inodes.tail) {
        struct inode *inode = list_entry(elem, struct inode, inode_tag);
        if (inode->i_no == inode_no) {
            inode->i_open_cnt++;
            return inode;
        }
        elem = elem->next;
    }
    struct inode *inode = (struct inode *)get_kernel_pages(1);
    if (inode == NULL) {
        return NULL;
    }
    memset(inode, 0, PAGE_SIZE);
    if (ext2_read_inode(inode_no, inode)) {
        free_kernel_page((uint32_t)inode);
        return NULL;
    }
    inode->i_open_cnt = 1;
    inode->write_deny = 0;
    list_append(&part->open_inodes, &inode->inode_tag);
    return inode;
}

void inode_close(struct inode *inode) {
    if (inode == NULL || inode->i_open_cnt == 0) {
        return;
    }
    uint32_t old = asm_save_eflags();
    asm_cli();
    if (--inode->i_open_cnt == 0) {
        list_remove(&inode->inode_tag);
        inode->i_open_cnt = 0;
        free_kernel_page((uint32_t)inode);
    }
    asm_restore_eflags(old);
}