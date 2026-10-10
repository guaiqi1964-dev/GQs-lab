# -*- coding: utf-8 -*-
"""Linear-sweep disassembly of .text with exact .pdata function boundaries."""
import struct, sys, os, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
from dec2 import Dec

PATH = r'D:\DSH\DSH workspace\恶魔轮盘赌.exe'
OUT = r'D:\DSH\DSH workspace\_reanalyze\disasm'
os.makedirs(OUT, exist_ok=True)
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
def sec(n):
    for s in secs:
        if s['name'] == n: return s
def off_of(rva):
    for s in secs:
        if s['vaddr'] <= rva < s['vaddr'] + max(s['vsize'], s['rawsize']):
            return s['raw'] + (rva - s['vaddr'])

# --- symbols ---
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
symmap = {}; data_symmap = {}
p = psym
while p < symend:
    nm = name_of(p); val = struct.unpack_from('<I', b, p+8)[0]; s = struct.unpack_from('<h', b, p+12)[0]
    if nm and not nm.startswith('.'):
        d = symmap if s == 1 else data_symmap
        d.setdefault(val, [])
        if nm not in d[val]: d[val].append(nm)
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

# --- .pdata boundaries ---
PD = sec('.pdata')
ents = []
for i in range(PD['rawsize']//12):
    o = PD['raw'] + i*12
    s, e, u = struct.unpack_from('<III', b, o)
    if s: ents.append((s, e))
ents.sort()
ents = [(s, e) for s, e in ents if sec('.text')['vaddr'] <= s < sec('.text')['vaddr']+sec('.text')['vsize']]

USER = [e for e in ents if e[0] < 0x4500]

NAMES = {
 0x1000:'stub_build_gun_duo',  # symbol only; section discarded by linker
 0x1010:'build_gun',           # single-player: live/blank + 4 items each side
 0x1160:'IsOver',              # single-player win/lose
 0x11B0:'Timeyou',             # single-player: player-turn pacing
 0x14D0:'Timeother_duo',       # duo: forwards to Timeyou
 0x1500:'wait',                # wait
 0x1530:'IsOver_duo',          # duo win/lose
 0x157E:'Timeother',           # demon-turn pacing
 0x189C:'settle',              # prints lose / win+money
 0x191D:'timing_helper',
 0x1967:'danrenplay',          # SINGLE-PLAYER MAIN LOOP
 0x2478:'demon_shoot_you',     # demon turn: shoot the player
 0x26E5:'demon_shoot_self',    # demon turn: shoot itself
 0x28C3:'demon_turn',          # demon-turn driver (magnifier / knife / 有趣)
 0x2EC3:'boot_stub',           # forwards to main
 0x2F01:'main',                # ENTRY: menu / rules / mode select
 0x3135:'duo_settle',          # duo winner + money
 0x31D0:'Timeyou_duo',         # duo A pacing
 0x34EE:'duorenplay',          # TWO-PLAYER MAIN LOOP
 0x3FFF:'single_integration',
}
RANGES = {e: NAMES.get(e[0], 'sub_%X' % e[0]) for e in USER}

def label(va):
    rva = va - IMAGE
    if rva in RANGES: return RANGES[rva]
    if rva in symmap: return dem(max(symmap[rva], key=len))
    return None
def data_label(va):
    return max(data_symmap[va-IMAGE], key=len) if (va-IMAGE) in data_symmap else None
def cstr(va, maxlen=180):
    o = off_of(va-IMAGE)
    if o is None: return None
    e = b.find(b'\0', o, o+maxlen)
    if e <= o: return None
    try: t = b[o:e].decode('gbk')
    except UnicodeDecodeError: return None
    if all(32 <= ord(ch) or ch in '\n\r\t' for ch in t): return t
    return None

# --- linear sweep of the whole .text ---
T = sec('.text')
code = b[T['raw']:T['raw']+T['rawsize']]
d = Dec(code, IMAGE + T['vaddr'])
insns = []
pos = 0
bad = 0
while pos < len(code):
    try:
        ins = d.one(pos)
    except Exception:
        ins = None
    if ins is None or ins['mn'] == 'db':
        bad += 1
        pos += 1
        continue
    insns.append({'rva': T['vaddr']+pos, 'addr': ins['addr'], 'text': ins['text'],
                  'mn': ins['mn'], 'ops': ins['ops'], 'len': ins['len'], 'target': ins['target']})
    pos += ins['len']
print('decoded %d instructions; %d undecoded bytes' % (len(insns), bad))

by_func = {e: [] for e in USER}
for ins in insns:
    for s, e in USER:
        if s <= ins['rva'] < e:
            by_func[(s, e)].append(ins)
            break

summary = []
for s, e in USER:
    nm = RANGES[(s, e)]
    lines = []
    for ins in by_func[(s, e)]:
        extra = ''
        if ins['mn'] == 'call' and ins['target'] is not None:
            lb = label(ins['target'])
            if lb: extra += '   ; -> %s' % lb
        if ins['mn'] in ('jmp','jcc') or ins['mn'].startswith('j'):
            if ins['target'] is not None:
                lb = RANGES.get(ins['target']-IMAGE)
                if lb: extra += '   ; -> %s' % lb
        for op in ins['ops']:
            if op.startswith('[rip'):
                try:
                    disp = int(op[4:].rstrip(']'))
                    tgt = ins['addr'] + ins['len'] + disp
                    lb = data_label(tgt); st = cstr(tgt)
                    if lb: extra += '   ; &%s' % lb
                    if st: extra += '   ; "%s"' % st.replace('\n','\\n')
                except Exception: pass
        lines.append('  %08X  %-46s%s' % (ins['addr'], ins['text'], extra))
    fn = os.path.join(OUT, '%05X_%s.asm' % (s, nm))
    with open(fn, 'w', encoding='utf-8') as f:
        f.write('; %s @ 0x%08X..0x%08X (%d bytes, %d insns)\n\n' % (nm, IMAGE+s, IMAGE+e, e-s, len(lines)))
        f.write('\n'.join(lines) + '\n')
    summary.append((s, e, nm, len(lines)))

for s, e, nm, n in summary:
    print('  %08X  %-18s %5d bytes  %4d insns' % (IMAGE+s, nm, e-s, n))
json.dump([{'rva': s, 'end': e, 'name': RANGES[(s,e)]} for s, e in USER],
          open(os.path.join(OUT, '_functions.json'), 'w'), indent=1)
print('\nwrote', len(summary), 'files to', OUT)
