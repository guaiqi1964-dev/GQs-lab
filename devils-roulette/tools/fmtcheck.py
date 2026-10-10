# -*- coding: utf-8 -*-
"""Check every printf/scanf in the source: format specifier count vs argument count."""
import re, sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

def split_args(s):
    """Split a C argument list on top-level commas."""
    out, depth, cur, i = [], 0, '', 0
    instr = None
    while i < len(s):
        c = s[i]
        if instr:
            cur += c
            if c == '\\': cur += s[i+1]; i += 2; continue
            if c == instr: instr = None
            i += 1; continue
        if c in '"\'':
            instr = c; cur += c; i += 1; continue
        if c in '([': depth += 1
        elif c in ')]': depth -= 1
        if c == ',' and depth == 0:
            out.append(cur.strip()); cur = ''
        else:
            cur += c
        i += 1
    if cur.strip(): out.append(cur.strip())
    return out

SPEC = re.compile(r'%[-+ #0]*[\d*]*(?:\.\d+|\.\*)?(?:hh|h|ll|l|L|z|j|t)?[diouxXeEfgGaAcspn%]')

def count_specs(fmt):
    n = 0
    for m in SPEC.finditer(fmt):
        if m.group() != '%%': n += 1
    return n

def process(path, label):
    src = open(path, encoding='utf-8').read()
    # strip comments to avoid false hits
    src_nc = re.sub(r'//[^\n]*', '', src)
    src_nc = re.sub(r'/\*.*?\*/', '', src_nc, flags=re.S)

    problems = []
    checked = 0
    for m in re.finditer(r'\b(printf|fprintf|scanf|fscanf|sprintf|snprintf)\s*\(', src_nc):
        fn = m.group(1)
        start = m.end()
        # find matching close paren
        depth = 1; i = start; instr = None
        while i < len(src_nc) and depth:
            c = src_nc[i]
            if instr:
                if c == '\\': i += 2; continue
                if c == instr: instr = None
            elif c in '"\'':
                instr = c
            elif c == '(': depth += 1
            elif c == ')': depth -= 1
            i += 1
        inner = src_nc[start:i-1]
        args = split_args(inner)
        if not args: continue
        # first arg may be a concatenation of string literals
        fmtparts = re.findall(r'"((?:[^"\\]|\\.)*)"', args[0])
        if not fmtparts:
            continue
        fmt = ''.join(fmtparts)
        nspec = count_specs(fmt)
        nargs = len(args) - 1
        checked += 1
        if nspec != nargs:
            problems.append((fn, fmt, nspec, nargs, args[1:]))

    print('=== %s ===' % label)
    print('  calls with literal formats checked: %d' % checked)
    if problems:
        print('  MISMATCHES: %d' % len(problems))
        for fn, fmt, nspec, nargs, rest in problems:
            print('    %s("%s")  specs=%d args=%d   args=%s' % (fn, fmt, nspec, nargs, rest))
    else:
        print('  all format strings match their argument counts OK')
    return len(problems)

bad = 0
for p, l in [(r'D:\DSH\DSH workspace\_recovered\恶魔轮盘赌.cpp', 'faithful reconstruction')]:
    bad += process(p, l)
sys.exit(1 if bad else 0)
