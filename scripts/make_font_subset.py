#!/usr/bin/env python3
"""从大字体中提取渲染所需字符子集, 生成小 TTF 供内核嵌入。
用法: python3 scripts/make_font_subset.py <大字体> <输出字体>
"""
import sys
from fontTools import subset
from fontTools.ttLib import TTFont

RENDER_TEXT = (r""" !"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_`abcdefghijklmnopqrstuvwxyz{|}~""")

def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "src/kernel/lib/assets/font.ttf"
    dst = sys.argv[2] if len(sys.argv) > 2 else "build/font_subset.ttf"

    chars = set(RENDER_TEXT)
    for c in range(0x20, 0x7F):
        chars.add(chr(c))

    opts = subset.Options()
    opts.flavor = None         
    opts.desubroutinize = True
    opts.hinting = False        
    opts.drop_tables += ["GSUB", "GPOS", "meta", "name", "post", "gasp"]

    font = subset.load_font(src, opts)
    ss = subset.Subsetter()
    ss.populate(text="".join(sorted(chars)))
    ss.subset(font)
    font.save(dst)
    print(f"subset {len(chars)} chars -> {dst}")

if __name__ == "__main__":
    main()
