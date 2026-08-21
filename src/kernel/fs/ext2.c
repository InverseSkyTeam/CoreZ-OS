#include "ext2.h"
#include "dir.h"
#include "../device/ide.h"
#include "../memory/pool/pool.h"
#include "../lib/str/str.h"
#include "../initer/io/io.h"

#define EXT2_SUPER_MAGIC 0xEF53u
#define EXT2_S_IFREG 0x8000u
#define EXT2_S_IFDIR 0x4000u
#define EXT2_DT_DIR 2u
#define EXT2_INODE_SIZE 128u

struct ext2_super {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
} __attribute__((packed));

struct ext2_dirent {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[0];
} __attribute__((packed));

static struct disk* g_disk = NULL;
static struct partition* g_part = NULL;
static uint32_t g_start = 0;
static uint32_t g_bs = 1024;
static uint32_t g_sect_per_block = 2;
static uint32_t g_inodes_per_group = 0;
static uint32_t g_inode_table_blk = 0;
static uint32_t g_first_block = 0;

/* 写路径用到的几何布局 */
static uint32_t g_block_bitmap_blk = 0;
static uint32_t g_inode_bitmap_blk = 0;
static uint32_t g_total_blocks = 0;
static uint32_t g_data_start = 0;
static uint32_t g_free_blocks = 0;
static uint32_t g_free_inodes = 0;

static int ext2_read_block(uint32_t blk, void* buf) {
    if (g_disk == NULL) {
        return -1;
    }
    ide_read(g_disk, g_start + blk * g_sect_per_block, buf, g_sect_per_block);
    return 0;
}

static int ext2_write_block(uint32_t blk, const void* buf) {
    if (g_disk == NULL) {
        return -1;
    }
    ide_write(g_disk, g_start + blk * g_sect_per_block, (void*)buf, g_sect_per_block);
    return 0;
}

struct partition* ext2_partition(void) {
    return g_part;
}

int ext2_init(void) {
    struct list_elem* e = partition_list.head.next;
    while (e != &partition_list.tail) {
        struct partition* part = list_entry(e, struct partition, part_tag);
        uint8_t* buf = (uint8_t*)get_kernel_pages(1);
        if (buf == NULL) {
            return -1;
        }
        memset(buf, 0, 4096);
        ide_read(part->my_disk, part->start_lba, buf, 4);
        struct ext2_super* sb = (struct ext2_super*)(buf + 1024);
        if (sb->s_magic == EXT2_SUPER_MAGIC) {
            g_disk = part->my_disk;
            g_part = part;
            g_start = part->start_lba;
            g_bs = 1024u << sb->s_log_block_size;
            g_sect_per_block = g_bs / 512u;
            g_inodes_per_group = sb->s_inodes_per_group;
            g_first_block = sb->s_first_data_block;
            g_total_blocks = sb->s_blocks_count;
            struct ext2_super* sb2 = (struct ext2_super*)buf;
            g_free_blocks = sb2->s_free_blocks_count;
            g_free_inodes = sb2->s_free_inodes_count;
            uint8_t* gb = (uint8_t*)get_kernel_pages(1);
            if (gb == NULL) {
                free_kernel_page((uint32_t)buf);
                return -1;
            }
            ext2_read_block(g_first_block + 1, gb);
            g_block_bitmap_blk = *(uint32_t*)(gb + 0);
            g_inode_bitmap_blk = *(uint32_t*)(gb + 4);
            g_inode_table_blk = *(uint32_t*)(gb + 8);
            uint32_t itable_blocks = (g_inodes_per_group * EXT2_INODE_SIZE + g_bs - 1) / g_bs;
            g_data_start = g_inode_table_blk + itable_blocks;
            free_kernel_page((uint32_t)gb);
            free_kernel_page((uint32_t)buf);
            kprintf("ext2 mounted on %s, block_size=%d, inodes_per_group=%d, data_start=%d\n",
                    part->name, (int)g_bs, (int)g_inodes_per_group, (int)g_data_start);
            return 0;
        }
        free_kernel_page((uint32_t)buf);
        e = e->next;
    }
    kprintf("ext2: no ext2 filesystem found\n");
    return -1;
}

int ext2_read_inode(uint32_t ino, struct inode* out) {
    if (g_disk == NULL || ino == 0) {
        return -1;
    }
    uint8_t* buf = (uint8_t*)get_kernel_pages(1);
    if (buf == NULL) {
        return -1;
    }
    memset(buf, 0, 4096);
    uint32_t per_block = g_bs / EXT2_INODE_SIZE;
    uint32_t blk = g_inode_table_blk + (ino - 1) / per_block;
    uint32_t off = ((ino - 1) % per_block) * EXT2_INODE_SIZE;
    ext2_read_block(blk, buf);
    uint8_t* p = buf + off;
    memset(out, 0, sizeof(struct inode));
    out->i_no = ino;
    out->i_mode = (uint32_t)(*(uint16_t*)(p + 0));
    out->i_size = *(uint32_t*)(p + 4);
    uint32_t bi = 0;
    for (bi = 0; bi < 15; bi++) {
        out->i_block[bi] = *(uint32_t*)(p + 40 + 4 * bi);
    }
    free_kernel_page((uint32_t)buf);
    return 0;
}

static void ext2_super_flush(void) {
    uint8_t* buf = (uint8_t*)get_kernel_pages(1);
    if (buf == NULL) {
        return;
    }
    ext2_read_block(g_first_block, buf);
    *(uint32_t*)(buf + 12) = g_free_blocks;
    *(uint32_t*)(buf + 16) = g_free_inodes;
    ext2_write_block(g_first_block, buf);
    free_kernel_page((uint32_t)buf);
}

static uint32_t ext2_alloc_block(void) {
    uint8_t* buf = (uint8_t*)get_kernel_pages(1);
    if (buf == NULL) {
        return 0;
    }
    for (uint32_t bb = 0; bb * g_bs * 8 < g_total_blocks; bb++) {
        uint32_t bbitblk = g_block_bitmap_blk + bb;
        memset(buf, 0, 4096);
        ext2_read_block(bbitblk, buf);
        uint32_t base = bb * g_bs * 8;
        for (uint32_t i = 0; i < g_bs; i++) {
            for (uint32_t bit = 0; bit < 8; bit++) {
                uint32_t blk = base + i * 8 + bit;
                if (blk < g_data_start || blk >= g_total_blocks) {
                    continue;
                }
                uint8_t mask = (uint8_t)(0x80u >> bit);
                if ((buf[i] & mask) == 0) {
                    buf[i] |= mask;
                    ext2_write_block(bbitblk, buf);
                    g_free_blocks--;
                    free_kernel_page((uint32_t)buf);
                    return blk;
                }
            }
        }
    }
    free_kernel_page((uint32_t)buf);
    return 0;
}

static void ext2_free_block(uint32_t blk) {
    if (blk == 0 || blk >= g_total_blocks) {
        return;
    }
    uint8_t* buf = (uint8_t*)get_kernel_pages(1);
    if (buf == NULL) {
        return;
    }
    uint32_t bbitblk = g_block_bitmap_blk + (blk >> 3) / g_bs;
    uint32_t byte = (blk >> 3) % g_bs;
    uint8_t mask = (uint8_t)(0x80u >> (blk & 7));
    memset(buf, 0, 4096);
    ext2_read_block(bbitblk, buf);
    if (buf[byte] & mask) {
        buf[byte] &= (uint8_t)~mask;
        ext2_write_block(bbitblk, buf);
        g_free_blocks++;
    }
    free_kernel_page((uint32_t)buf);
}

static uint32_t ext2_alloc_inode(void) {
    uint8_t* buf = (uint8_t*)get_kernel_pages(1);
    if (buf == NULL) {
        return 0;
    }
    memset(buf, 0, 4096);
    ext2_read_block(g_inode_bitmap_blk, buf);
    for (uint32_t i = 1; i <= g_inodes_per_group; i++) {
        uint32_t byte = (i - 1) >> 3;
        uint8_t mask = (uint8_t)(0x80u >> ((i - 1) & 7));
        if ((buf[byte] & mask) == 0) {
            buf[byte] |= mask;
            ext2_write_block(g_inode_bitmap_blk, buf);
            g_free_inodes--;
            free_kernel_page((uint32_t)buf);
            return i;
        }
    }
    free_kernel_page((uint32_t)buf);
    return 0;
}

void ext2_free_inode(uint32_t ino) {
    if (ino == 0 || ino > g_inodes_per_group) {
        return;
    }
    uint8_t* buf = (uint8_t*)get_kernel_pages(1);
    if (buf == NULL) {
        return;
    }
    memset(buf, 0, 4096);
    ext2_read_block(g_inode_bitmap_blk, buf);
    uint32_t byte = (ino - 1) >> 3;
    uint8_t mask = (uint8_t)(0x80u >> ((ino - 1) & 7));
    if (buf[byte] & mask) {
        buf[byte] &= (uint8_t)~mask;
        ext2_write_block(g_inode_bitmap_blk, buf);
        g_free_inodes++;
    }
    free_kernel_page((uint32_t)buf);
}

int ext2_write_inode(uint32_t ino, const struct inode* in) {
    if (g_disk == NULL || ino == 0 || ino > g_inodes_per_group) {
        return -1;
    }
    uint8_t* buf = (uint8_t*)get_kernel_pages(1);
    if (buf == NULL) {
        return -1;
    }
    memset(buf, 0, 4096);
    uint32_t per_block = g_bs / EXT2_INODE_SIZE;
    uint32_t blk = g_inode_table_blk + (ino - 1) / per_block;
    uint32_t off = ((ino - 1) % per_block) * EXT2_INODE_SIZE;
    ext2_read_block(blk, buf);
    uint8_t* p = buf + off;
    *(uint16_t*)(p + 0) = (uint16_t)in->i_mode;
    *(uint32_t*)(p + 4) = in->i_size;
    for (uint32_t bi = 0; bi < 15; bi++) {
        *(uint32_t*)(p + 40 + 4 * bi) = in->i_block[bi];
    }
    ext2_write_block(blk, buf);
    free_kernel_page((uint32_t)buf);
    return 0;
}

static uint32_t ext2_ensure_block(struct inode* ino, uint32_t fblk) {
    uint32_t addrs = g_bs / 4u;
    if (fblk < 12) {
        if (ino->i_block[fblk] == 0) {
            ino->i_block[fblk] = ext2_alloc_block();
            if (ino->i_block[fblk] == 0) {
                return 0;
            }
        }
        return ino->i_block[fblk];
    }
    fblk -= 12;
    uint8_t* buf = (uint8_t*)get_kernel_pages(1);
    if (buf == NULL) {
        return 0;
    }
    if (fblk < addrs) {
        if (ino->i_block[12] == 0) {
            ino->i_block[12] = ext2_alloc_block();
            if (ino->i_block[12] == 0) {
                free_kernel_page((uint32_t)buf);
                return 0;
            }
            memset(buf, 0, 4096);
            ext2_write_block(ino->i_block[12], buf);
        }
        memset(buf, 0, 4096);
        ext2_read_block(ino->i_block[12], buf);
        uint32_t b = *(uint32_t*)(buf + 4 * fblk);
        if (b == 0) {
            b = ext2_alloc_block();
            if (b == 0) {
                free_kernel_page((uint32_t)buf);
                return 0;
            }
            *(uint32_t*)(buf + 4 * fblk) = b;
            ext2_write_block(ino->i_block[12], buf);
        }
        free_kernel_page((uint32_t)buf);
        return b;
    }
    fblk -= addrs;
    uint32_t l1 = fblk / addrs;
    uint32_t l2 = fblk % addrs;
    if (ino->i_block[13] == 0) {
        ino->i_block[13] = ext2_alloc_block();
        if (ino->i_block[13] == 0) {
            free_kernel_page((uint32_t)buf);
            return 0;
        }
        memset(buf, 0, 4096);
        ext2_write_block(ino->i_block[13], buf);
    }
    memset(buf, 0, 4096);
    ext2_read_block(ino->i_block[13], buf);
    uint32_t l1blk = *(uint32_t*)(buf + 4 * l1);
    if (l1blk == 0) {
        l1blk = ext2_alloc_block();
        if (l1blk == 0) {
            free_kernel_page((uint32_t)buf);
            return 0;
        }
        *(uint32_t*)(buf + 4 * l1) = l1blk;
        ext2_write_block(ino->i_block[13], buf);
        memset(buf, 0, 4096);
        ext2_write_block(l1blk, buf);
    }
    memset(buf, 0, 4096);
    ext2_read_block(l1blk, buf);
    uint32_t b = *(uint32_t*)(buf + 4 * l2);
    if (b == 0) {
        b = ext2_alloc_block();
        if (b == 0) {
            free_kernel_page((uint32_t)buf);
            return 0;
        }
        *(uint32_t*)(buf + 4 * l2) = b;
        ext2_write_block(l1blk, buf);
    }
    free_kernel_page((uint32_t)buf);
    return b;
}

int ext2_write_to_inode(struct inode* ino, uint32_t off, const void* buf, uint32_t count) {
    if (g_disk == NULL || ino == 0 || ino->i_no == 0) {
        return 0;
    }
    uint8_t* blk = (uint8_t*)get_kernel_pages(1);
    if (blk == NULL) {
        return 0;
    }
    uint32_t done = 0;
    while (done < count) {
        uint32_t fblk = (off + done) / g_bs;
        uint32_t within = (off + done) % g_bs;
        uint32_t addr = ext2_ensure_block((struct inode*)ino, fblk);
        if (addr == 0) {
            break;
        }
        memset(blk, 0, 4096);
        ext2_read_block(addr, blk);
        uint32_t chunk = g_bs - within;
        if (chunk > count - done) {
            chunk = count - done;
        }
        memcpy(blk + within, (const uint8_t*)buf + done, chunk);
        ext2_write_block(addr, blk);
        done += chunk;
    }
    free_kernel_page((uint32_t)blk);
    if (off + done > ino->i_size) {
        ino->i_size = off + done;
    }
    if (done > 0) {
        ext2_write_inode(ino->i_no, (struct inode*)ino);
    }
    return (int)done;
}

void ext2_truncate_inode(struct inode* ino) {
    uint32_t addrs = g_bs / 4u;
    uint8_t* buf = (uint8_t*)get_kernel_pages(1);
    if (buf == NULL) {
        return;
    }
    for (uint32_t i = 0; i < 12; i++) {
        if (ino->i_block[i]) {
            ext2_free_block(ino->i_block[i]);
            ino->i_block[i] = 0;
        }
    }
    if (ino->i_block[12]) {
        memset(buf, 0, 4096);
        ext2_read_block(ino->i_block[12], buf);
        for (uint32_t j = 0; j < addrs; j++) {
            if (*(uint32_t*)(buf + 4 * j)) {
                ext2_free_block(*(uint32_t*)(buf + 4 * j));
            }
        }
        ext2_free_block(ino->i_block[12]);
        ino->i_block[12] = 0;
    }
    if (ino->i_block[13]) {
        memset(buf, 0, 4096);
        ext2_read_block(ino->i_block[13], buf);
        for (uint32_t j = 0; j < addrs; j++) {
            uint32_t l1 = *(uint32_t*)(buf + 4 * j);
            if (!l1) {
                continue;
            }
            uint8_t* l1b = (uint8_t*)get_kernel_pages(1);
            if (l1b == NULL) {
                continue;
            }
            memset(l1b, 0, 4096);
            ext2_read_block(l1, l1b);
            for (uint32_t k = 0; k < addrs; k++) {
                if (*(uint32_t*)(l1b + 4 * k)) {
                    ext2_free_block(*(uint32_t*)(l1b + 4 * k));
                }
            }
            free_kernel_page((uint32_t)l1b);
            ext2_free_block(l1);
        }
        ext2_free_block(ino->i_block[13]);
        ino->i_block[13] = 0;
    }
    if (ino->i_block[14]) {
        ext2_free_block(ino->i_block[14]);
        ino->i_block[14] = 0;
    }
    free_kernel_page((uint32_t)buf);
    memset(ino->i_block, 0, sizeof(ino->i_block));
    ino->i_size = 0;
}

/* 前向声明：目录项函数在 ext2_map_block 定义之前使用它 */
static int ext2_map_block(const struct inode* ino, uint32_t fblk, uint32_t* out);

int ext2_new_inode(uint32_t mode, struct inode* out) {
    uint32_t ino = ext2_alloc_inode();
    if (ino == 0) {
        return 0;
    }
    memset(out, 0, sizeof(struct inode));
    out->i_no = ino;
    out->i_mode = mode;
    out->i_size = 0;
    memset(out->i_block, 0, sizeof(out->i_block));
    if (ext2_write_inode(ino, out)) {
        ext2_free_inode(ino);
        return 0;
    }
    return (int)ino;
}

int ext2_add_entry(struct inode* dino, uint32_t ino, const char* name, int is_dir) {
    uint32_t nl = (uint32_t)strlen(name);
    if (nl == 0 || nl >= 255u) {
        return -1;
    }
    uint32_t need = (8u + nl + 3u) & ~3u;
    uint8_t* blk = (uint8_t*)get_kernel_pages(1);
    if (blk == NULL) {
        return -1;
    }

    /* 1) 优先在已有数据块的空闲目录项中落位 */
    for (uint32_t fblk = 0; fblk * g_bs < dino->i_size; fblk++) {
        uint32_t addr = 0;
        if (ext2_map_block((struct inode*)dino, fblk, &addr)) {
            break;
        }
        memset(blk, 0, 4096);
        ext2_read_block(addr, blk);
        uint32_t off = 0;
        uint32_t target = 0xFFFFFFFFu;
        uint32_t slot_rec = 0;
        while (off < g_bs) {
            struct ext2_dirent* de = (struct ext2_dirent*)(blk + off);
            uint32_t rl = de->rec_len;
            if (rl < 8u) {
                /* 遇到无效槽位：若块尾剩余空间足够，则当作新条目 */
                if (g_bs - off >= need) {
                    target = off;
                    slot_rec = g_bs - off;
                }
                break;
            }
            if (de->inode == 0 && rl >= need) {
                target = off;
                slot_rec = rl;
                break;
            }
            off += rl;
        }
        if (target != 0xFFFFFFFFu) {
            struct ext2_dirent* de = (struct ext2_dirent*)(blk + target);
            de->inode = ino;
            de->rec_len = (uint16_t)slot_rec;
            de->name_len = (uint8_t)nl;
            de->file_type = is_dir ? (uint8_t)EXT2_DT_DIR : 1u;
            memcpy(de->name, name, nl);
            ext2_write_block(addr, blk);
            free_kernel_page((uint32_t)blk);
            return 0;
        }
    }

    /* 2) 无空闲槽位：分配新数据块，条目从块首开始 */
    uint32_t nfblk = dino->i_size / g_bs;
    uint32_t naddr = ext2_ensure_block((struct inode*)dino, nfblk);
    if (naddr == 0) {
        free_kernel_page((uint32_t)blk);
        return -1;
    }
    memset(blk, 0, 4096);
    struct ext2_dirent* de = (struct ext2_dirent*)blk;
    de->inode = ino;
    de->rec_len = (uint16_t)g_bs;
    de->name_len = (uint8_t)nl;
    de->file_type = is_dir ? (uint8_t)EXT2_DT_DIR : 1u;
    memcpy(de->name, name, nl);
    ext2_write_block(naddr, blk);
    dino->i_size += g_bs;
    ext2_write_inode(dino->i_no, (struct inode*)dino);
    free_kernel_page((uint32_t)blk);
    return 0;
}

int ext2_remove_entry(struct inode* dino, const char* name) {
    uint32_t nl = (uint32_t)strlen(name);
    if (nl == 0 || nl >= 255u) {
        return -1;
    }
    uint8_t* blk = (uint8_t*)get_kernel_pages(1);
    if (blk == NULL) {
        return -1;
    }
    for (uint32_t fblk = 0; fblk * g_bs < dino->i_size; fblk++) {
        uint32_t addr = 0;
        if (ext2_map_block((struct inode*)dino, fblk, &addr)) {
            break;
        }
        memset(blk, 0, 4096);
        ext2_read_block(addr, blk);
        uint32_t off = 0;
        while (off < g_bs) {
            struct ext2_dirent* de = (struct ext2_dirent*)(blk + off);
            uint32_t rl = de->rec_len;
            if (rl < 8u) {
                break;
            }
            if (de->inode != 0 && de->name_len == nl && memcmp(de->name, name, nl) == 0) {
                de->inode = 0;
                de->name_len = 0;
                ext2_write_block(addr, blk);
                free_kernel_page((uint32_t)blk);
                return 0;
            }
            off += rl;
        }
        free_kernel_page((uint32_t)blk);
    }
    free_kernel_page((uint32_t)blk);
    return -1;
}

static int ext2_map_block(const struct inode* ino, uint32_t fblk, uint32_t* out) {
    uint32_t addrs = g_bs / 4u;
    if (fblk < 12) {
        if (ino->i_block[fblk] == 0) {
            return -1;
        }
        *out = ino->i_block[fblk];
        return 0;
    }
    fblk -= 12;
    uint8_t* buf = (uint8_t*)get_kernel_pages(1);
    if (buf == NULL) {
        return -1;
    }
    memset(buf, 0, 4096);
    if (ino->i_block[12] != 0) {
        ext2_read_block(ino->i_block[12], buf);
        if (fblk < addrs) {
            uint32_t b = *(uint32_t*)(buf + 4 * fblk);
            free_kernel_page((uint32_t)buf);
            if (b == 0) {
                return -1;
            }
            *out = b;
            return 0;
        }
        fblk -= addrs;
    }
    if (ino->i_block[13] != 0) {
        ext2_read_block(ino->i_block[13], buf);
        uint32_t dblk = *(uint32_t*)(buf + 4 * (fblk / addrs));
        if (dblk != 0) {
            ext2_read_block(dblk, buf);
            uint32_t b = *(uint32_t*)(buf + 4 * (fblk % addrs));
            free_kernel_page((uint32_t)buf);
            if (b == 0) {
                return -1;
            }
            *out = b;
            return 0;
        }
    }
    free_kernel_page((uint32_t)buf);
    return -1;
}

int ext2_read_from_inode(const struct inode* ino, uint32_t off, void* buf, uint32_t count) {
    if (ino->i_no == 0 || off >= ino->i_size) {
        return 0;
    }
    if (off + count > ino->i_size) {
        count = ino->i_size - off;
    }
    uint8_t* blk = (uint8_t*)get_kernel_pages(1);
    if (blk == NULL) {
        return 0;
    }
    uint32_t done = 0;
    while (done < count) {
        uint32_t fblk = (off + done) / g_bs;
        uint32_t within = (off + done) % g_bs;
        uint32_t addr = 0;
        if (ext2_map_block(ino, fblk, &addr)) {
            break;
        }
        memset(blk, 0, 4096);
        ext2_read_block(addr, blk);
        uint32_t chunk = g_bs - within;
        if (chunk > count - done) {
            chunk = count - done;
        }
        memcpy((uint8_t*)buf + done, blk + within, chunk);
        done += chunk;
    }
    free_kernel_page((uint32_t)blk);
    return (int)done;
}

int ext2_dir_next(const struct inode* dino, uint32_t* pos, struct dir_entry* out) {
    while (*pos < dino->i_size) {
        uint32_t fblk = *pos / g_bs;
        uint32_t addr = 0;
        if (ext2_map_block(dino, fblk, &addr)) {
            *pos = (fblk + 1) * g_bs;
            continue;
        }
        uint8_t* blk = (uint8_t*)get_kernel_pages(1);
        if (blk == NULL) {
            return -1;
        }
        memset(blk, 0, 4096);
        ext2_read_block(addr, blk);
        uint32_t off = 0;
        while (off < g_bs) {
            struct ext2_dirent* de = (struct ext2_dirent*)(blk + off);
            uint32_t rec_len = de->rec_len;
            if (rec_len < 8) {
                break;
            }
            uint32_t abs = fblk * g_bs + off;
            if (de->inode != 0 && de->name_len > 0 && de->name_len < 255 && abs >= *pos) {
                uint32_t nl = de->name_len;
                if (nl > 15) {
                    nl = 15;
                }
                memset(out->filename, 0, 16);
                memcpy(out->filename, de->name, nl);
                out->i_no = de->inode;
                out->f_type = (de->file_type == EXT2_DT_DIR) ? FT_DIRECTORY : FT_REGULAR;
                *pos = abs + rec_len;
                free_kernel_page((uint32_t)blk);
                return 0;
            }
            off += rec_len;
        }
        free_kernel_page((uint32_t)blk);
        *pos = (fblk + 1) * g_bs;
    }
    return -1;
}

static int ext2_find_in_dir(const struct inode* dino, const char* name, uint32_t* child, int* is_dir) {
    uint32_t pos = 0;
    struct dir_entry de;
    while (ext2_dir_next(dino, &pos, &de) == 0) {
        if (strcmp(de.filename, name) == 0) {
            *child = de.i_no;
            *is_dir = (de.f_type == FT_DIRECTORY);
            return 0;
        }
    }
    return -1;
}

int ext2_lookup(const char* path, uint32_t* ino, int* is_dir) {
    if (g_disk == NULL || path == NULL || path[0] != '/') {
        return -1;
    }
    *ino = 2;
    *is_dir = 1;
    const char* p = path;
    while (*p == '/') {
        p++;
    }
    if (*p == 0) {
        return 0;
    }
    struct inode cur;
    if (ext2_read_inode(2, &cur)) {
        return -1;
    }
    for (;;) {
        char comp[16];
        uint32_t ci = 0;
        while (*p && *p != '/' && ci < 15) {
            comp[ci++] = *p++;
        }
        comp[ci] = 0;
        if (ci == 0) {
            break;
        }
        uint32_t child = 0;
        int cd = 0;
        if (ext2_find_in_dir(&cur, comp, &child, &cd)) {
            return -1;
        }
        *ino = child;
        *is_dir = cd;
        while (*p == '/') {
            p++;
        }
        if (*p == 0) {
            return 0;
        }
        if (!cd) {
            return -1;
        }
        if (ext2_read_inode(child, &cur)) {
            return -1;
        }
    }
    return -1;
}