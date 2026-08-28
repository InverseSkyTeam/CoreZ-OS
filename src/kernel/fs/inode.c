#include "inode.h"

#include "../device/ide.h"
#include "../include/asmFunc.h"
#include "../lib/str/str.h"
#include "../memory/pool/pool.h"
#include "ext2.h"
#include "fs.h"

struct inode *inode_open(struct partition *part, uint32_t inode_no) {
    struct RB_NODE *found = rb_find(&part->open_inodes_rb, inode_no);
    if (found != NULL) {
        struct inode *inode = rb_entry(found, struct inode, inode_rb_node);
        inode->i_open_cnt++;
        return inode;
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
    inode->inode_rb_node.key = inode_no;
    rb_insert(&part->open_inodes_rb, &inode->inode_rb_node);
    return inode;
}

void inode_close(struct inode *inode) {
    if (inode == NULL || inode->i_open_cnt == 0) {
        return;
    }
    uint32_t old = asm_save_eflags();
    asm_cli();
    if (--inode->i_open_cnt == 0) {
        rb_erase(&cur_part->open_inodes_rb, &inode->inode_rb_node);
        inode->i_open_cnt = 0;
        free_kernel_page((uint32_t)inode);
    }
    asm_restore_eflags(old);
}