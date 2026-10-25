#!/usr/bin/env python3
"""FDict One: dong goi du lieu giong Pico en-US -> voice.bin cho phan vung flash 'voice' (fd_tts mmap, khong sao chep RAM).
Dung: python tools/mk_voice.py <ta.bin> <sg.bin> <out voice.bin>
Header 32 B (little-endian): magic "FDVOICE1", u32 version=1, u32 ta_off, u32 ta_len, u32 sg_off, u32 sg_len,
u32 crc32 (zlib, tu byte 32 den het sg). ta_off = 36 (= 4 mod 16: du lieu sau header SVOX 124 B thang hang 8 B),
sg_off can 16 B (header SVOX 128 B -> thang hang 16 B). Phan vung bat dau o bien 64 KB nen can le giu nguyen khi mmap."""
import struct, sys, zlib

def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    ta = open(sys.argv[1], 'rb').read()
    sg = open(sys.argv[2], 'rb').read()
    ta_off = 36
    sg_off = (ta_off + len(ta) + 15) & ~15
    body = bytearray(sg_off + len(sg) - 32)
    body[ta_off - 32:ta_off - 32 + len(ta)] = ta
    body[sg_off - 32:] = sg
    crc = zlib.crc32(body) & 0xFFFFFFFF
    hdr = b'FDVOICE1' + struct.pack('<6I', 1, ta_off, len(ta), sg_off, len(sg), crc)
    with open(sys.argv[3], 'wb') as f:
        f.write(hdr + body)
    print('voice.bin: %d B (ta %d B @%d, sg %d B @%d, crc32 %08x)' % (32 + len(body), len(ta), ta_off, len(sg), sg_off, crc))

if __name__ == '__main__':
    main()
