# -*- coding: utf-8 -*-
"""x86-64 decoder -> Intel-syntax text. Subset covering GCC 4.9 -O2 C++ output.
_rm handles only the r/m operand and returns the ModRM reg field separately."""
import struct

R64 = ['rax','rcx','rdx','rbx','rsp','rbp','rsi','rdi','r8','r9','r10','r11','r12','r13','r14','r15']
R32 = ['eax','ecx','edx','ebx','esp','ebp','esi','edi','r8d','r9d','r10d','r11d','r12d','r13d','r14d','r15d']
R16 = ['ax','cx','dx','bx','sp','bp','si','di','r8w','r9w','r10w','r11w','r12w','r13w','r14w','r15w']
R8  = ['al','cl','dl','bl','spl','bpl','sil','dil','r8b','r9b','r10b','r11b','r12b','r13b','r14b','r15b']
R8H = ['al','cl','dl','bl','ah','ch','dh','bh']
XMM = ['xmm%d' % i for i in range(16)]

def rname(i, sz, rex):
    if sz == 8: return R64[i]
    if sz == 4: return R32[i]
    if sz == 2: return R16[i]
    if sz == 1:
        if i < 8: return R8[i] if (rex & 0x40) else R8H[i]
        return R8[i]
    return R64[i]

class Rm:
    """Result of parsing a ModRM byte's r/m operand."""
    __slots__ = ('text', 'p', 'ismem', 'reg', 'target')
    def __init__(self, text, p, ismem, reg, target=None):
        self.text = text; self.p = p; self.ismem = ismem; self.reg = reg; self.target = target

class Dec:
    def __init__(self, code, base):
        self.c = code; self.base = base

    def _rm(self, p, rex, sz):
        c = self.c
        m = c[p]; p += 1
        mod = m >> 6; reg = (m >> 3) & 7; rm = m & 7
        rb = rex & 1; rx = (rex >> 1) & 1; rr = (rex >> 2) & 1
        regf = reg | (rr << 3)
        if mod == 3:
            return Rm(rname(rm | (rb << 3), sz, rex), p, False, regf)
        target = None
        if rm == 4:
            sib = c[p]; p += 1
            scale = 1 << (sib >> 6); index = ((sib >> 3) & 7) | (rx << 3); basen = (sib & 7) | (rb << 3)
            if (sib & 7) == 5 and mod == 0:
                disp = struct.unpack_from('<i', c, p)[0]; p += 4
                base = None
            else:
                disp = 0; base = R64[basen]
            parts = []
            if base: parts.append(base)
            if ((sib >> 3) & 7) != 4:
                parts.append(R64[index] if scale == 1 else '%s*%d' % (R64[index], scale))
            e = '+'.join(parts)
            if disp or not e: e += ('%+d' % disp) if e else str(disp)
            return Rm('[%s]' % e, p, True, regf)
        if rm == 5 and mod == 0:
            disp = struct.unpack_from('<i', c, p)[0]; p += 4
            target = (self.base + p + disp) & 0xffffffffffffffff
            return Rm('[rip%+d]' % disp, p, True, regf, target)
        base = R64[rm | (rb << 3)]
        if mod == 1:
            disp = struct.unpack_from('<b', c, p)[0]; p += 1
            return Rm('[%s%+d]' % (base, disp), p, True, regf)
        if mod == 2:
            disp = struct.unpack_from('<i', c, p)[0]; p += 4
            return Rm('[%s%+d]' % (base, disp), p, True, regf)
        return Rm('[%s]' % base, p, True, regf)

    def one(self, off):
        c = self.c; start = off
        pfx = []; opsz = 0; rex = 0
        while off < len(c):
            x = c[off]
            if x == 0x66: opsz = 1; off += 1
            elif x == 0x67: off += 1
            elif x in (0xf0,0xf2,0xf3,0x2e,0x36,0x3e,0x26,0x64,0x65): pfx.append(x); off += 1
            elif 0x40 <= x <= 0x4f: rex = x; off += 1
            else: break
        sz = 8 if (rex & 8) else (2 if opsz else 4)
        op = c[off]; off += 1
        try:
            if op == 0x0f:
                o2 = c[off]; off += 1
                mn, ops, off, tgt = self.twobyte(o2, off, rex, sz, pfx)
            else:
                mn, ops, off, tgt = self.onebyte(op, off, rex, sz, pfx)
        except Exception:
            mn = 'db'; ops = ['0x%02x' % op]; tgt = None
            off = start + 1
        raw = c[start:off]
        return {'addr': self.base + start, 'raw': raw, 'len': off - start,
                'mn': mn, 'ops': ops, 'text': (mn + ' ' + ', '.join(ops)).strip(),
                'target': tgt}

    def onebyte(self, op, off, rex, sz, pfx):
        c = self.c
        if op in (0x00,0x01,0x02,0x03,0x08,0x09,0x0a,0x0b,0x10,0x11,0x12,0x13,
                  0x18,0x19,0x1a,0x1b,0x20,0x21,0x22,0x23,0x28,0x29,0x2a,0x2b,
                  0x30,0x31,0x32,0x33,0x38,0x39,0x3a,0x3b):
            mn = {0x00:'add',0x08:'or',0x10:'adc',0x18:'sbb',0x20:'and',0x28:'sub',0x30:'xor',0x38:'cmp'}[op & 0xf8]
            s = sz if (op & 1) else 1
            r = self._rm(off, rex, s)
            regop = rname(r.reg, s, rex)
            if op & 2: return mn, [regop, r.text], r.p, None   # reg <- rm
            return mn, [r.text, regop], r.p, None               # rm <- reg
        if op in (0x04,0x0c,0x14,0x1c,0x24,0x2c,0x34,0x3c):
            mn = {0x04:'add',0x0c:'or',0x14:'adc',0x1c:'sbb',0x24:'and',0x2c:'sub',0x34:'xor',0x3c:'cmp'}[op]
            imm = c[off]; off += 1
            return mn, ['al', '0x%x' % imm], off, None
        if op in (0x05,0x0d,0x15,0x1d,0x25,0x2d,0x35,0x3d):
            mn = {0x05:'add',0x0d:'or',0x15:'adc',0x1d:'sbb',0x25:'and',0x2d:'sub',0x35:'xor',0x3d:'cmp'}[op]
            if sz == 2: imm = struct.unpack_from('<h', c, off)[0]; off += 2
            else: imm = struct.unpack_from('<i', c, off)[0]; off += 4
            return mn, [rname(0,sz,rex), '0x%x' % (imm & 0xffffffff)], off, None
        if 0x50 <= op <= 0x57:
            return 'push', [R64[(op-0x50) | ((rex&1)<<3)]], off, None
        if 0x58 <= op <= 0x5f:
            return 'pop', [R64[(op-0x58) | ((rex&1)<<3)]], off, None
        if op == 0x63:
            r = self._rm(off, rex, 4)
            return 'movsxd', [rname(r.reg, 8, rex), r.text], r.p, None
        if op == 0x68:
            imm = struct.unpack_from('<i', c, off)[0]; off += 4
            return 'push', ['0x%x' % (imm & 0xffffffff)], off, None
        if op in (0x69,0x6b):
            r = self._rm(off, rex, sz)
            dst = rname(r.reg, sz, rex)
            if op == 0x69: imm = struct.unpack_from('<i', c, r.p)[0]; p2 = r.p + 4
            else: imm = struct.unpack_from('<b', c, r.p)[0]; p2 = r.p + 1
            return 'imul', [dst, r.text, '0x%x' % (imm & 0xffffffff)], p2, None
        if op == 0x6a:
            imm = struct.unpack_from('<b', c, off)[0]; off += 1
            return 'push', ['0x%x' % (imm & 0xffffffff)], off, None
        if 0x70 <= op <= 0x7f:
            names = ['jo','jno','jb','jae','je','jne','jbe','ja','js','jns','jp','jnp','jl','jge','jle','jg']
            d = struct.unpack_from('<b', c, off)[0]; off += 1
            t = (self.base + off + d) & 0xffffffffffffffff
            return names[op-0x70], ['0x%x' % t], off, t
        if op in (0x80,0x81,0x83):
            s = 1 if op == 0x80 else sz
            r = self._rm(off, rex, s)
            if op == 0x80: imm = c[r.p]; p2 = r.p + 1
            elif op == 0x81:
                if s == 2: imm = struct.unpack_from('<h', c, r.p)[0]; p2 = r.p + 2
                else: imm = struct.unpack_from('<i', c, r.p)[0]; p2 = r.p + 4
            else: imm = struct.unpack_from('<b', c, r.p)[0]; p2 = r.p + 1
            return ['add','or','adc','sbb','and','sub','xor','cmp'][r.reg & 7], [r.text, '0x%x' % (imm & 0xffffffff)], p2, None
        if op in (0x84,0x85):
            s = 1 if op == 0x84 else sz
            r = self._rm(off, rex, s)
            return 'test', [r.text, rname(r.reg, s, rex)], r.p, None
        if op in (0x86,0x87):
            s = 1 if op == 0x86 else sz
            r = self._rm(off, rex, s)
            return 'xchg', [rname(r.reg, s, rex), r.text], r.p, None
        if op in (0x88,0x89,0x8a,0x8b):
            s = 1 if op in (0x88,0x8a) else sz
            r = self._rm(off, rex, s)
            regop = rname(r.reg, s, rex)
            if op in (0x88,0x89): return 'mov', [r.text, regop], r.p, None   # rm <- reg
            return 'mov', [regop, r.text], r.p, None                          # reg <- rm
        if op == 0x8d:
            r = self._rm(off, rex, sz)
            return 'lea', [rname(r.reg, sz, rex), r.text], r.p, None
        if op == 0x8f:
            r = self._rm(off, rex, 8)
            return 'pop', [r.text], r.p, None
        if op == 0x90: return 'nop', [], off, None
        if 0x91 <= op <= 0x97:
            return 'xchg', [rname((op-0x90) | ((rex&1)<<3), sz, rex), rname(0, sz, rex)], off, None
        if op == 0x98: return ('cdqe' if (rex & 8) else 'cwde'), [], off, None
        if op == 0x99: return ('cqo' if (rex & 8) else 'cdq'), [], off, None
        if op == 0x9c: return 'pushfq', [], off, None
        if op == 0x9d: return 'popfq', [], off, None
        if op == 0x9e: return 'sahf', [], off, None
        if op == 0x9f: return 'lahf', [], off, None
        if op in (0xa8,0xa9):
            if op == 0xa8: imm = c[off]; off += 1
            elif sz == 2: imm = struct.unpack_from('<h', c, off)[0]; off += 2
            else: imm = struct.unpack_from('<i', c, off)[0]; off += 4
            return 'test', [rname(0, sz, rex), '0x%x' % (imm & 0xffffffff)], off, None
        if 0xb0 <= op <= 0xb7:
            i = (op-0xb0) | ((rex&1)<<3); imm = c[off]; off += 1
            return 'mov', [rname(i,1,rex), '0x%x' % imm], off, None
        if 0xb8 <= op <= 0xbf:
            i = (op-0xb8) | ((rex&1)<<3)
            if sz == 8: imm = struct.unpack_from('<q', c, off)[0]; off += 8
            elif sz == 2: imm = struct.unpack_from('<H', c, off)[0]; off += 2
            else: imm = struct.unpack_from('<I', c, off)[0]; off += 4
            return 'mov', [rname(i,sz,rex), '0x%x' % (imm & 0xffffffffffffffff)], off, None
        if op in (0xc0,0xc1,0xd0,0xd1,0xd2,0xd3):
            s = 1 if op in (0xc0,0xd0,0xd2) else sz
            r = self._rm(off, rex, s)
            mn = ['rol','ror','rcl','rcr','shl','shr','shl','sar'][r.reg & 7]
            if op in (0xc0,0xc1):
                imm = c[r.p]
                return mn, [r.text, '0x%x' % imm], r.p + 1, None
            if op in (0xd0,0xd1): return mn, [r.text, '1'], r.p, None
            return mn, [r.text, 'cl'], r.p, None
        if op == 0xc2:
            imm = struct.unpack_from('<H', c, off)[0]; off += 2
            return 'ret', ['0x%x' % imm], off, None
        if op == 0xc3: return 'ret', [], off, None
        if op == 0xc6:
            r = self._rm(off, rex, 1)
            imm = c[r.p]
            return 'mov', [r.text, '0x%x' % imm], r.p + 1, None
        if op == 0xc7:
            r = self._rm(off, rex, sz)
            if sz == 2: imm = struct.unpack_from('<h', c, r.p)[0]; p2 = r.p + 2
            else: imm = struct.unpack_from('<i', c, r.p)[0]; p2 = r.p + 4
            return 'mov', [r.text, '0x%x' % (imm & 0xffffffff)], p2, None
        if op == 0xc9: return 'leave', [], off, None
        if op == 0xcc: return 'int3', [], off, None
        if op == 0xcd:
            imm = c[off]; off += 1
            return 'int', ['0x%x' % imm], off, None
        if op == 0xe8:
            d = struct.unpack_from('<i', c, off)[0]; off += 4
            t = (self.base + off + d) & 0xffffffffffffffff
            return 'call', ['0x%x' % t], off, t
        if op == 0xe9:
            d = struct.unpack_from('<i', c, off)[0]; off += 4
            t = (self.base + off + d) & 0xffffffffffffffff
            return 'jmp', ['0x%x' % t], off, t
        if op == 0xeb:
            d = struct.unpack_from('<b', c, off)[0]; off += 1
            t = (self.base + off + d) & 0xffffffffffffffff
            return 'jmp', ['0x%x' % t], off, t
        if op == 0xf4: return 'hlt', [], off, None
        if op in (0xf6,0xf7):
            s = 1 if op == 0xf6 else sz
            r = self._rm(off, rex, s)
            f = r.reg & 7
            if f in (0,1):
                if s == 1: imm = c[r.p]; p2 = r.p + 1
                elif s == 2: imm = struct.unpack_from('<h', c, r.p)[0]; p2 = r.p + 2
                else: imm = struct.unpack_from('<i', c, r.p)[0]; p2 = r.p + 4
                return 'test', [r.text, '0x%x' % (imm & 0xffffffff)], p2, None
            return ['not','neg','mul','imul','div','idiv'][f-2], [r.text], r.p, None
        if op in (0xfe,0xff):
            s = 1 if op == 0xfe else sz
            r = self._rm(off, rex, s)
            f = r.reg & 7
            if op == 0xfe: return ['inc','dec'][f], [r.text], r.p, None
            return ['inc','dec','call','callf','jmp','jmpf','push','?'][f], [r.text], r.p, None
        if op == 0xf8: return 'clc', [], off, None
        if op == 0xf9: return 'stc', [], off, None
        if op == 0xfa: return 'cli', [], off, None
        if op == 0xfb: return 'sti', [], off, None
        if op == 0xfc: return 'cld', [], off, None
        if op == 0xfd: return 'std', [], off, None
        raise ValueError('op 0x%02x' % op)

    def twobyte(self, o2, off, rex, sz, pfx):
        c = self.c
        f2 = 0xf2 in pfx; f3 = 0xf3 in pfx; p66 = 0x66 in pfx
        if 0x10 <= o2 <= 0x17:
            mn = {0x10:'movups',0x11:'movups',0x12:'movlps',0x13:'movlps',
                  0x14:'unpcklps',0x15:'unpckhps',0x16:'movhps',0x17:'movhps'}[o2]
            if f3: mn = {0x10:'movss',0x11:'movss'}.get(o2, mn)
            if f2: mn = {0x10:'movsd',0x11:'movsd'}.get(o2, mn)
            w = 4 if (f3 and o2 in (0x10,0x11)) else (8 if (f2 and o2 in (0x10,0x11)) else 16)
            r = self._rm(off, rex, w)
            x = XMM[r.reg]
            if o2 in (0x10,0x12,0x16): return mn, [x, r.text], r.p, None
            return mn, [r.text, x], r.p, None
        if 0x28 <= o2 <= 0x2f:
            mn = {0x28:'movaps',0x29:'movaps',0x2a:'cvtpi2ps',0x2b:'movntps',
                  0x2c:'cvttps2pi',0x2d:'cvtps2pi',0x2e:'ucomiss',0x2f:'comiss'}[o2]
            if p66: mn = {0x28:'movapd',0x29:'movapd',0x2e:'ucomisd',0x2f:'comisd'}.get(o2, mn)
            if f2:
                mn = {0x2a:'cvtsi2sd',0x2c:'cvttsd2si',0x2d:'cvtsd2si'}.get(o2, mn)
                w = 8 if o2 == 0x2a else 8
                r = self._rm(off, rex, w)
                x = XMM[r.reg]
                if o2 == 0x2a: return mn, [x, r.text], r.p, None
                return mn, [rname(r.reg, sz, rex), r.text], r.p, None
            if f3:
                mn = {0x2a:'cvtsi2ss',0x2c:'cvttss2si',0x2d:'cvtss2si'}.get(o2, mn)
                r = self._rm(off, rex, 4 if o2 == 0x2a else 4)
                x = XMM[r.reg]
                if o2 == 0x2a: return mn, [x, r.text], r.p, None
                return mn, [rname(r.reg, sz, rex), r.text], r.p, None
            r = self._rm(off, rex, 16 if o2 in (0x28,0x29) else 8)
            x = XMM[r.reg]
            if o2 in (0x28,0x2e,0x2f): return mn, [x, r.text], r.p, None
            return mn, [r.text, x], r.p, None
        if 0x40 <= o2 <= 0x4f:
            mn = ['cmovo','cmovno','cmovb','cmovae','cmove','cmovne','cmovbe','cmova',
                  'cmovs','cmovns','cmovp','cmovnp','cmovl','cmovge','cmovle','cmovg'][o2-0x40]
            r = self._rm(off, rex, sz)
            return mn, [rname(r.reg, sz, rex), r.text], r.p, None
        if o2 == 0x6e:
            r = self._rm(off, rex, 4)
            return 'movd', [XMM[r.reg], r.text], r.p, None
        if o2 == 0x7e:
            r = self._rm(off, rex, 8)
            if f3: return 'movq', [XMM[r.reg], r.text], r.p, None
            return 'movd', [XMM[r.reg], r.text], r.p, None
        if o2 in (0x6f,0x7f):
            r = self._rm(off, rex, 16)
            x = XMM[r.reg]
            mn = 'movdqa' if p66 else ('movdqu' if f3 else 'movq')
            return mn, ([x, r.text] if o2 == 0x6f else [r.text, x]), r.p, None
        if 0x50 <= o2 <= 0x5f:
            mn = {0x50:'movmskps',0x51:'sqrtps',0x52:'rsqrtps',0x53:'rcpps',0x54:'andps',
                  0x55:'andnps',0x56:'orps',0x57:'xorps',0x58:'addps',0x59:'mulps',
                  0x5a:'cvtps2pd',0x5b:'cvtdq2ps',0x5c:'subps',0x5d:'minps',0x5e:'divps',
                  0x5f:'maxps'}.get(o2)
            if mn is None: raise ValueError('0f 0x%02x' % o2)
            if o2 == 0x50:
                r = self._rm(off, rex, 16)
                return 'movmskps', [rname(r.reg, 4, rex), XMM[r.reg]], r.p, None
            sd = f2; ss = f3
            if sd: mn = {0x51:'sqrtsd',0x58:'addsd',0x59:'mulsd',0x5c:'subsd',0x5d:'minsd',0x5e:'divsd',0x5f:'maxsd'}.get(o2, mn)
            if ss: mn = {0x51:'sqrtss',0x58:'addss',0x59:'mulss',0x5c:'subss',0x5d:'minss',0x5e:'divss',0x5f:'maxss'}.get(o2, mn)
            w = 8 if (sd or ss) else 16
            r = self._rm(off, rex, w)
            return mn, [XMM[r.reg], r.text], r.p, None
        if 0x80 <= o2 <= 0x8f:
            d = struct.unpack_from('<i', c, off)[0]; off += 4
            t = (self.base + off + d) & 0xffffffffffffffff
            return 'jcc', ['0x%x' % t], off, t
        if 0x90 <= o2 <= 0x9f:
            mn = ['seto','setno','setb','setae','sete','setne','setbe','seta',
                  'sets','setns','setp','setnp','setl','setge','setle','setg'][o2-0x90]
            r = self._rm(off, rex, 1)
            return mn, [r.text], r.p, None
        if o2 == 0xa2: return 'cpuid', [], off, None
        if o2 == 0xaf:
            r = self._rm(off, rex, sz)
            return 'imul', [rname(r.reg, sz, rex), r.text], r.p, None
        if o2 == 0xb1:
            r = self._rm(off, rex, sz)
            return 'cmpxchg', [r.text, rname(r.reg, sz, rex)], r.p, None
        if o2 in (0xb6,0xb7,0xbe,0xbf):
            s = 1 if o2 in (0xb6,0xbe) else 2
            r = self._rm(off, rex, s)
            mn = 'movzx' if o2 in (0xb6,0xb7) else 'movsx'
            return mn, [rname(r.reg, sz, rex), r.text], r.p, None
        if o2 == 0xc1:
            r = self._rm(off, rex, sz)
            return 'xadd', [r.text, rname(r.reg, sz, rex)], r.p, None
        if 0xc8 <= o2 <= 0xcf:
            return 'bswap', [rname(((rex&1)<<3) | (o2-0xc8), sz, rex)], off, None
        if o2 == 0x31: return 'rdtsc', [], off, None
        if o2 == 0x05: return 'syscall', [], off, None
        if o2 == 0x1f:
            r = self._rm(off, rex, sz)
            return 'nop', [r.text], r.p, None
        if o2 == 0x18:
            r = self._rm(off, rex, sz)
            return 'prefetch', [r.text], r.p, None
        if o2 in (0x2a,):
            r = self._rm(off, rex, 8 if f2 else 4)
            return ('cvtsi2sd' if f2 else 'cvtsi2ss'), [XMM[r.reg], r.text], r.p, None
        if o2 in (0x2c,0x2d):
            r = self._rm(off, rex, 8 if f2 else 16)
            return ('cvttsd2si' if o2 == 0x2c else 'cvtsd2si'), [rname(r.reg, sz, rex), r.text], r.p, None
        if o2 in (0x5a,0x5b):
            r = self._rm(off, rex, 8 if f2 else 16)
            return ('cvtsd2ss' if o2 == 0x5a else 'cvtss2sd'), [XMM[r.reg], r.text], r.p, None
        if o2 == 0xd6:
            r = self._rm(off, rex, 8)
            return 'movq', [r.text, XMM[r.reg]], r.p, None
        if o2 == 0xef:
            r = self._rm(off, rex, 16)
            return 'pxor', [XMM[r.reg], r.text], r.p, None
        if o2 == 0x76:
            r = self._rm(off, rex, 16)
            return 'pcmpeqd', [XMM[r.reg], r.text], r.p, None
        if o2 == 0xd7:
            r = self._rm(off, rex, 16)
            return 'pmovmskb', [rname(r.reg, 4, rex), XMM[r.reg]], r.p, None
        raise ValueError('0f 0x%02x' % o2)
