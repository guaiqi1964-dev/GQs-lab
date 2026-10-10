# -*- coding: utf-8 -*-
"""Map global variables and build a call/string cross-reference for the game code."""
import struct, sys, os, json, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
from dec2 import Dec

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
def off_of(rva):
    for s in secs:
        if s['vaddr'] <= rva < s['vaddr'] + max(s['vsize'], s['rawsize']):
            return s['raw'] + (rva - s['vaddr'])

psym = struct.unpack_from('<I', b, pe+12)[0]
nsym = struct.unpack_from('<I', b, pe+16)[0]
symend = psym + nsym*18
STR = b[symend+4:symend+struct.unpack_from('<I', b, symend)[0]]
def name_of(p):
    raw = b[p:p+8]
    if raw[:4] == b'\0\0\0\0':
        off = struct.unpack_from('<I', raw, 4)[0]
        return STR[off:STR.find(b'\0', off)].decode('utf-8','replace')
    return raw.rstrip(b'\0').decode('latin1')

allrec = []
p = psym
while p < symend:
    nm = name_of(p)
    val = struct.unpack_from('<I', b, p+8)[0]
    s = struct.unpack_from('<h', b, p+12)[0]
    allrec.append((nm, val, s, b[p+16]))
    p += 18*(1 + b[p+17])

def dem(n):
    if not n.startswith('_Z'): return n
    s = n[2:]; out = []; i = 0
    while i < len(s):
        if s[i].isdigit():
            j = i
            while j < len(s) and s[j].isdigit(): j += 1
            ln = int(s[i:j]); out.append(s[j:j+ln]); i = j + ln
        else: i += 1
    if not out: return n
    return out[0] + ('<' + '_'.join(out[1:]) + '>' if len(out) > 1 else '') + '()'

print('=== data symbols (.data / .bss) ===')
data = {}
for nm, val, s, cls in allrec:
    if s in (2, 6) and nm and not nm.startswith('.') and len(nm) > 2:
        data.setdefault(val, set()).add(nm)
for val in sorted(data):
    print('   rva 0x%06X  %s' % (val, ', '.join(sorted(data[val]))))

# ---- collect rip-relative data accesses and call targets per function ----
PD = [s for s in secs if s['name'] == '.pdata'][0]
ents = []
for i in range(PD['rawsize']//12):
    o = PD['raw'] + i*12
    s, e, u = struct.unpack_from('<III', b, o)
    if s: ents.append((s, e))
ents.sort()
USER = [e for e in ents if e[0] < 0x4500]
NAMES = {
 0x1000:'build_gun_duo', 0x1010:'build_gun', 0x1160:'IsOver', 0x11B0:'Timeyou',
 0x14D0:'Timeother_duo', 0x1500:'wait', 0x1530:'IsOver_duo', 0x157E:'Timeother',
 0x189C:'helper_189C', 0x191D:'Timeyou_duo', 0x1967:'danrenplay', 0x2478:'Play',
 0x26E5:'helper_26E5', 0x28C3:'helper_28C3', 0x2EC3:'thunk_2EC3',
 0x2F01:'build_gun_duo2', 0x3135:'IsOver_duo2', 0x31D0:'Timeyou_duo2',
 0x34EE:'duorenplay', 0x3FFF:'main',
}
RNG = {e: NAMES.get(e[0], 'sub_%X' % e[0]) for e in USER}

def cstr(va, maxlen=200):
    o = off_of(va-IMAGE)
    if o is None: return None
    e = b.find(b'\0', o, o+maxlen)
    if e <= o or e-o > maxlen: return None
    try: t = b[o:e].decode('gbk')
    except UnicodeDecodeError: return None
    if all(32 <= ord(ch) or ch in '\n\r\t' for ch in t): return t
    return None

T = [s for s in secs if s['name'] == '.text'][0]
code = b[T['raw']:T['raw']+T['rawsize']]
d = Dec(code, IMAGE + T['vaddr'])
insns = []
pos = 0
while pos < len(code):
    try: ins = d.one(pos)
    except Exception: ins = None
    if ins is None or ins['mn'] == 'db':
        pos += 1; continue
    insns.append({'rva': T['vaddr']+pos, 'addr': ins['addr'], 'mn': ins['mn'],
                  'ops': ins['ops'], 'len': ins['len'], 'target': ins['target'],
                  'text': ins['text']})
    pos += ins['len']

report = {}
for s, e in USER:
    nm = RNG[(s, e)]
    calls = collections.Counter(); strings = []; globals_ = collections.Counter()
    for ins in insns:
        if not (s <= ins['rva'] < e): continue
        if ins['mn'] == 'call' and ins['target'] is not None:
            t = ins['target'] - IMAGE
            calls[RNG.get(t) or dem(max([x[0] for x in allrec if x[1] == t and x[2] == 1] or ['sub_%X' % t], key=len))] += 1
        for op in ins['ops']:
            if op.startswith('[rip'):
                disp = int(op[4:].rstrip(']'))
                tgt = ins['addr'] + ins['len'] + disp
                rva = tgt - IMAGE
                st = cstr(tgt)
                if st and len(st) > 1: strings.append((tgt, st))
                if rva in data:
                    globals_[', '.join(sorted(data[rva]))] += 1
    report[nm] = {'range': '0x%08X..0x%08X' % (IMAGE+s, IMAGE+e),
                  'calls': dict(calls), 'strings': strings, 'globals': dict(globals_)}

for nm, r in report.items():
    print('\n=== %s  %s ===' % (nm, r['range']))
    if r['globals']:
        print('  globals touched:')
        for g, c in sorted(r['globals'].items(), key=lambda x: -x[1]):
            print('     %-46s x%d' % (g[:46], c))
    if r['strings']:
        print('  strings (%d):' % len(r['strings']))
        for va, st in r['strings'][:40]:
            print('     %08X  %s' % (va, st.replace('\n','\\n')))
    if r['calls']:
        print('  calls:')
        for c, n in sorted(r['calls'].items(), key=lambda x: -x[1])[:20]:
            print('     %-40s x%d' % (c[:40], n))

json.dump(report, open(r'D:\DSH\DSH workspace\_reanalyze\report.json','w'), ensure_ascii=False, indent=1)
print('\nwrote report.json')
