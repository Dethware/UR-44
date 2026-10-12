import struct, sys
data = open(sys.argv[1], 'rb').read(); base = 0x10000000
rd = lambda va, n: data[va-base: va-base+n]
u32 = lambda va: struct.unpack('<I', rd(va, 4))[0]
def s(va):
    b = rd(va, 80)
    if b[1:2] == b'\0':  # utf-16
        return b.decode('utf-16le', 'replace').split('\0')[0]
    return b.split(b'\0')[0].decode('latin1')
print("id   name                             grp      type  min   max   def  chans")
for i in range(0xb1):
    p = u32(0x10022118 + 4*i)
    w = struct.unpack('<12i', rd(p, 0x30))
    print(f"0x{i:02x} {s(w[10]):32s} {w[5]&0xffffffff:08x} {w[1]&0xffff:04x} {w[7]:5d} {w[6]:5d} {w[8]:5d}  x{w[2]} {w[3]}")
