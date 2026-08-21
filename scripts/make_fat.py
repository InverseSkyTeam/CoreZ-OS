"""生成 P1 FAT16 启动分区的扇区数据。

布局 (绝对 LBA, 相对 P1 起始 2048):
  +0(2048)   VBR (vbr.bin)                    1 扇区
  +1(2049)   FAT1                         256 扇区
  +257       FAT2                         256 扇区
  +513       root dir (512 项)              32 扇区   -> ROOT_LBA=2561
  +545(2593) data, 簇从 2 开始, SPC=1                -> DATA_LBA=2593

FAT 项: 0=0xFFF8(介质), 1=0xFFFF(保留), 文件链最后一簇=0xFFFF(EOC)。
根目录项: LOADER.BIN, KERNEL.BIN (8.3 大写, 目录项偏移 26=起始簇, 28=大小低, 14=大小高)。
"""
from pathlib import Path
import struct

SECTOR = 512
PART_START = 2048
RESERVED = 1
FAT_SECTORS = 256
FAT_COUNT = 2
ROOT_ENTRIES = 512
ROOT_SECTORS = ROOT_ENTRIES * 32 // SECTOR
SPC = 1
P1_TOTAL_SECTORS = 16384

ROOT_LBA = PART_START + RESERVED + FAT_COUNT * FAT_SECTORS
DATA_LBA = ROOT_LBA + ROOT_SECTORS


def fat16(build_dir: str, out: str) -> None:
    bd = Path(build_dir)
    vbr = (bd / "vbr.bin").read_bytes()
    loader = (bd / "loader.bin").read_bytes()
    kernel = (bd / "kernel.bin").read_bytes()

    if len(vbr) > SECTOR:
        raise SystemExit(f"vbr.bin too big: {len(vbr)} > {SECTOR}")
    if len(vbr) & 0x1FF:
        vbr += b"\x00" * (SECTOR - (len(vbr) & 0x1FF))
    vbr = vbr[:SECTOR]
    vbr = vbr[:-2] + b"\x55\xAA"

    def clusters(data):
        return (len(data) + SECTOR - 1) // SECTOR

    n_loader = clusters(loader)
    n_kernel = clusters(kernel)
    cloader = 2
    ckernel = cloader + n_loader

    total_clusters = (n_loader + n_kernel)
    fat_entries = [0] * (total_clusters + 2)
    fat_entries[0] = 0xFFF8
    fat_entries[1] = 0xFFFF

    def chain(start, n):
        for i in range(n):
            fat_entries[start + i] = start + i + 1
        fat_entries[start + n - 1] = 0xFFFF

    chain(cloader, n_loader)
    chain(ckernel, n_kernel)

    def put_file(start_cluster, data):
        blobs = []
        pos = 0
        while pos < len(data):
            blobs.append(data[pos:pos + SECTOR].ljust(SECTOR, b'\x00'))
            pos += SECTOR
        return blobs

    loader_blobs = put_file(cloader, loader)
    kernel_blobs = put_file(ckernel, kernel)

    def root_entry(name, start_cluster, size):
        e = bytearray(32)
        base, _, ext = name.partition(".")
        nb = (base + " " * 8)[:8].encode()
        eb = (ext + " " * 3)[:3].encode()
        e[0:8] = nb
        e[8:11] = eb
        e[11] = 0x20
        struct.pack_into("<H", e, 26, start_cluster)
        struct.pack_into("<I", e, 28, size)
        return bytes(e)

    root = bytearray(ROOT_SECTORS * SECTOR)
    root[0:32] = root_entry("LOADER.BIN", cloader, len(loader))
    root[32:64] = root_entry("KERNEL.BIN", ckernel, len(kernel))

    with open(out, "wb") as f:
        f.truncate(P1_TOTAL_SECTORS * SECTOR)
        f.seek(0)
        f.write(vbr)

        for copy in range(FAT_COUNT):
            f.seek((RESERVED + copy * FAT_SECTORS) * SECTOR)
            fat = bytearray(FAT_SECTORS * SECTOR)
            for i, val in enumerate(fat_entries):
                struct.pack_into("<H", fat, i * 2, val)
            f.write(bytes(fat))

        f.seek((ROOT_LBA - PART_START) * SECTOR)
        f.write(bytes(root))

        f.seek((DATA_LBA - PART_START) * SECTOR)
        for blob in (loader_blobs + kernel_blobs):
            f.write(blob)

    print(f"OK: {out} FAT16 builder")
    print(f"  P1 @{PART_START}: VBR(1) FAT({FAT_SECTORS}x{FAT_COUNT}) "
          f"root({ROOT_SECTORS}) data@{DATA_LBA}")
    print(f"  LOADER.BIN {len(loader)}B/{n_loader}cl @cluster {cloader}")
    print(f"  KERNEL.BIN {len(kernel)}B/{n_kernel}cl @cluster {ckernel}")
    if n_loader + n_kernel + 2 > (FAT_SECTORS * SECTOR) // 2:
        raise SystemExit("FAT too small for file clusters")
    if (n_loader + n_kernel) > (P1_TOTAL_SECTORS - (DATA_LBA - PART_START)):
        raise SystemExit("files do not fit in data area")


if __name__ == "__main__":
    import sys
    a1 = sys.argv[1] if len(sys.argv) > 1 else "build"
    a2 = sys.argv[2] if len(sys.argv) > 2 else "fat16.img"
    fat16(a1, a2)
