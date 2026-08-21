import struct
import sys
from pathlib import Path

SECTOR = 512
TOTAL_SECTORS = 80 * 1024 * 1024 // SECTOR
MBR_SIG = 0x55AA

EXT2_SUPER_MAGIC = 0xEF53
BLOCK = 1024
SECT_PER_BLOCK = BLOCK // SECTOR
INODES_PER_GROUP = 1024
INODE_SIZE = 128
FIRST_DATA_BLOCK = 1

SUPER_BLK = 1
GDT_BLK = FIRST_DATA_BLOCK + 1     
BLOCK_BITMAP_BLK = 3
INODE_BITMAP_BLK = 4
ITABLE_BLK = 5
ITABLE_BLOCKS = (INODES_PER_GROUP * INODE_SIZE + BLOCK - 1) // BLOCK
DATA_START = ITABLE_BLK + ITABLE_BLOCKS                    

FILES = [
    "prog_no_arg.elf", "prog_arg.elf", "cat.elf", "fork_demo.elf",
    "prog_pipe.elf", "font_demo.elf", "heap_demo.elf", "signal_demo.elf",
    "font_subset.ttf", "libc_testsuite.elf"
]
ALIASES = {"forktest.elf": "fork_demo.elf"}


def part_entry(bootable, fs_type, start_lba, sec_cnt):
    return struct.pack("<BBBBBBBBII", bootable, 0, 0, 0, fs_type, 0, 0, 0,
                       start_lba & 0xFFFFFFFF, sec_cnt & 0xFFFFFFFF)


def mbr_sector(entries):
    buf = bytearray(SECTOR)
    for i, e in enumerate(entries[:4]):
        buf[446 + i * 16:446 + (i + 1) * 16] = e
    buf[510] = MBR_SIG & 0xFF
    buf[511] = (MBR_SIG >> 8) & 0xFF
    return buf


def build_dirent_block(entries, block=BLOCK):
    out = bytearray(block)
    pos = 0
    n = len(entries)
    for i, (ino, ftype, name) in enumerate(entries):
        nl = len(name)
        base = 8 + nl
        if i == n - 1:
            reclen = block - pos
        else:
            reclen = (base + 3) & ~3
        struct.pack_into("<IHBB", out, pos, ino, reclen, nl, ftype)
        out[pos + 8:pos + 8 + nl] = name.encode()
        pos += reclen
    return bytes(out)


def put_inode(table, ino, payload_len, blocks, is_dir):
    off = (ino - 1) * INODE_SIZE
    mode = 0x41ED if is_dir else 0x81A4
    struct.pack_into("<H", table, off + 0, mode)
    struct.pack_into("<I", table, off + 4, payload_len)
    struct.pack_into("<H", table, off + 26, 2) 
    for i in range(15):
        b = blocks[i] if i < len(blocks) else 0
        struct.pack_into("<I", table, off + 40 + 4 * i, b)


def build(build_dir, out):
    bd = Path(build_dir)
    names = list(FILES)
    names += [a for a in ALIASES if (bd / a).exists()]

    pre = {}
    for name in names:
        src = bd / (ALIASES.get(name, name))
        if src.exists():
            pre[name] = src.read_bytes()

    next_ino = 3
    dir_entries = [(2, 2, "."), (2, 2, "..")]
    var_blocks = []          
    var_indirect = []        
    root_block = DATA_START
    cur_block = DATA_START + 1
    file_ptrs = {}          
    for name in names:
        payload = pre[name]
        nblk = (len(payload) + BLOCK - 1) // BLOCK
        ptrs = [0] * 15
        if nblk <= 12:
            blocks = list(range(cur_block, cur_block + nblk))
            cur_block += nblk
            ptrs[0:nblk] = blocks
        else:
            indirect_blk = cur_block
            cur_block += 1
            blocks = list(range(cur_block, cur_block + nblk))
            cur_block += nblk
            ptrs[0:12] = blocks[0:12]
            ptrs[12] = indirect_blk
            var_indirect.append((indirect_blk, blocks[12:]))
        file_ptrs[name] = ptrs
        var_blocks.append((blocks, payload))
        dir_entries.append((next_ino, 1, name))
        next_ino += 1

    root_dir = build_dirent_block(dir_entries)
    used_inodes = next_ino - 1

    ino_map = {}
    di = 3
    for name in names:
        ino_map[name] = di
        di += 1

    itable = bytearray(ITABLE_BLOCKS * BLOCK)
    put_inode(itable, 2, len(root_dir), [root_block], True)
    for name in names:
        put_inode(itable, ino_map[name], len(pre[name]),
                  file_ptrs[name], False)

    used_blocks = set(range(0, DATA_START))
    used_blocks.add(root_block)
    for blocks, _ in var_blocks:
        for b in blocks:
            used_blocks.add(b)
    for iblk, _ in var_indirect:
        used_blocks.add(iblk)
    total_blocks = (TOTAL_SECTORS - 2048) // SECT_PER_BLOCK
    free_blocks = total_blocks - len(used_blocks)

    bm_len = (total_blocks + 7) // 8
    block_bitmap = bytearray(bm_len)
    for b in used_blocks:
        block_bitmap[b >> 3] |= 0x80 >> (b & 7)

    im_len = (used_inodes + 7) // 8
    inode_bitmap = bytearray(im_len)
    for i in range(1, used_inodes + 1):
        inode_bitmap[(i - 1) >> 3] |= 0x80 >> ((i - 1) & 7)

    gdt = bytearray(BLOCK)
    struct.pack_into("<III", gdt, 0,
                     BLOCK_BITMAP_BLK, INODE_BITMAP_BLK, ITABLE_BLK)

    sb = bytearray(BLOCK)
    struct.pack_into("<IIIIIIIIIII", sb, 0,
                     INODES_PER_GROUP, total_blocks, 0, free_blocks,
                     INODES_PER_GROUP - used_inodes, FIRST_DATA_BLOCK,
                     0, 0, total_blocks, total_blocks, INODES_PER_GROUP)
    struct.pack_into("<IIHHH", sb, 44, 0, 0, 0, 0, 0) 
    struct.pack_into("<H", sb, 56, EXT2_SUPER_MAGIC)

    start_lba = 2048
    base = start_lba 
    with open(out, "wb") as f:
        f.truncate(TOTAL_SECTORS * SECTOR)
        f.seek(0)
        f.write(mbr_sector([part_entry(0, 0x83, start_lba,
                                       TOTAL_SECTORS - start_lba)]))
        f.seek(base * SECTOR + SUPER_BLK * BLOCK)
        f.write(bytes(sb))
        f.seek(base * SECTOR + GDT_BLK * BLOCK)
        f.write(bytes(gdt))
        f.seek(base * SECTOR + BLOCK_BITMAP_BLK * BLOCK)
        f.write(bytes(block_bitmap))
        f.seek(base * SECTOR + INODE_BITMAP_BLK * BLOCK)
        f.write(bytes(inode_bitmap))
        f.seek(base * SECTOR + ITABLE_BLK * BLOCK)
        f.write(bytes(itable))
        f.seek(base * SECTOR + root_block * BLOCK)
        f.write(root_dir)
        for iblk, data in var_indirect:
            idx = bytearray(BLOCK)
            for j, b in enumerate(data):
                struct.pack_into("<I", idx, 4 * j, b)
            f.seek(base * SECTOR + iblk * BLOCK)
            f.write(bytes(idx))
        for blocks, payload in var_blocks:
            f.seek(base * SECTOR + blocks[0] * BLOCK)
            f.write(payload)

    print(f"OK: {out} ({TOTAL_SECTORS * SECTOR // 1024 // 1024}MB)")
    print(f"  sda1 @{start_lba} ext2: {len(names)} files, {free_blocks} free blocks")


if __name__ == "__main__":
    arg1 = sys.argv[1] if len(sys.argv) > 1 else "build"
    arg2 = sys.argv[2] if len(sys.argv) > 2 else "test_hd.img"
    build(arg1, arg2)