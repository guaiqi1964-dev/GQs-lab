# -*- coding: utf-8 -*-
"""Use the .pdata RUNTIME_FUNCTION table to get exact function boundaries,
then map the recovered names onto them."""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

PATH = r'D:\DSH\DSH workspace\恶魔轮盘赌.exe'
b = open(PATH, 'rb').read()
pe = struct.unpack_from('<I', b, 0x3c)[0]
nsec = struct.unpack_from('<H', b, pe+6)[0]
optsize = struct.unpack_from('<H', b, pe+20)[0]
secstart = pe + 24 + optsize
secs = []
for k in range(nsec):
    o = secstart + k*40
    nm = b[o:o+8].rstrip(b'\0').decode('latin1')
    vs, va, rs, rp = struct.unpack_from('<IIII', b, o+8)
    secs.append({'name': nm, 'vaddr': va, 'vsize': vs, 'raw': rp, 'rawsize': rs})
IMAGE = 0x400000
def sec(name):
    for s in secs:
        if s['name'] == name: return s
    return None

pdata = sec('.pdata')
print('.pdata raw=0x%X size=0x%X vaddr=0x%X' % (pdata['raw'], pdata['rawsize'], pdata['vaddr']))
entries = []
n = pdata['rawsize'] // 12
for i in range(n):
    o = pdata['raw'] + i*12
    start, end, unwind = struct.unpack_from('<III', b, o)
    if start == 0: continue
    entries.append((start, end, unwind))
print('RUNTIME_FUNCTION entries: %d' % len(entries))

# text range
txt = sec('.text')
lo, hi = txt['vaddr'], txt['vaddr'] + txt['vsize']
inside = [e for e in entries if lo <= e[0] < hi]
print('inside .text: %d' % len(inside))
inside.sort()
print('first 20:')
for s, e, u in inside[:20]:
    print('   %08X - %08X  size=%-6d unwind=0x%X' % (IMAGE+s, IMAGE+e, e-s, u))

# which entry contains our recovered function RVAs?
MY = [0x530,0x57E,0x89C,0x91D,0x967,0x1478,0x16E5,0x18C3,0x1EC3,0x1F01,
      0x2135,0x21D0,0x24EE,0x2FFF,0x3B10,0x3B5F]
print('\ncontaining function ranges:')
for r in MY:
    hit = [e for e in inside if e[0] <= r < e[1]]
    if hit:
        s, e, u = hit[0]
        print('   rva 0x%06X -> func 0x%08X..0x%08X (size %d, +%d)' % (r, IMAGE+s, IMAGE+e, e-s, r-s))
    else:
        print('   rva 0x%06X -> NOT in .pdata' % r)

# save the full list of user-code-sized functions in the low range
user_ranges = [e for e in inside if e[0] < 0x4500]
print('\nall .pdata functions below rva 0x4500 (%d):' % len(user_ranges))
for s, e, u in sorted(user_ranges):
    print('   %08X - %08X  size=%d' % (IMAGE+s, IMAGE+e, e-s))
