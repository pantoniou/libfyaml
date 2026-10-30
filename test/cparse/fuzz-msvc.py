#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare the layout of random records with the real MSVC compiler.

usage: fuzz-msvc.py SEED [x64|x86]

The script writes random structs and unions (bit-fields and #pragma pack), asks
cparse-tool for their layout with the target of MSVC, and builds a program with
cl that prints the same numbers: sizeof, _Alignof, offsetof and, for bit-fields,
the bits that the field really takes. A manual check; cl can be the one of
msvc-wine, which runs under wine.

Environment: CPARSE_TOOL, and MSVC_CL, the cl to run (default: cl). WINE is the
program that runs the result (default: wine, empty to run it directly).
"""
import os
import random
import re
import subprocess
import sys
import tempfile

BF_TYPES = {'char': 8, 'unsigned char': 8, 'short': 16, 'unsigned short': 16,
            'int': 32, 'unsigned int': 32, 'long': 32, 'long long': 64,
            'unsigned long long': 64}
TYPES = ['char', 'signed char', 'unsigned char', 'short', 'unsigned short', 'int',
         'unsigned int', 'long', 'unsigned long', 'long long', 'unsigned long long',
         'float', 'double', 'long double', 'void *']


def make_source(seed, count):
    r = random.Random(seed)
    records = []
    for i in range(count):
        kind = r.choice(['struct'] * 4 + ['union'])
        pack = r.choice([0, 0, 0, 1, 2, 4, 8])
        fields = []
        for j in range(r.randint(1, 7)):
            if r.random() < 0.5:
                t = r.choice(sorted(BF_TYPES))
                w = r.choice([0, 1, 3, 5, 7, 8, 12, 16, 20, 31, 32, 33, 40])
                w = min(w, BF_TYPES[t])
                named = w > 0 and r.random() > 0.2
                fields.append(('bit', t, 'f%d' % j if named else '', w))
            else:
                t = r.choice(TYPES)
                arr = r.choice([0, 0, 0, 2, 3])
                fields.append(('plain', t, 'f%d' % j, arr))
        records.append((kind, pack, fields))
    return records


def write_c(records, with_probe):
    out = []
    if with_probe:
        out = ['#include <stdio.h>', '#include <stddef.h>', '#include <string.h>', '']
    for i, (kind, pack, fields) in enumerate(records):
        if pack:
            out.append('#pragma pack(push, %d)' % pack)
        out.append('%s S%d {' % (kind, i))
        for k, t, name, n in fields:
            if k == 'bit':
                out.append('\t%s %s : %d;' % (t, name, n))
            else:
                out.append('\t%s %s%s;' % (t, name, '[%d]' % n if n else ''))
        out.append('};')
        if pack:
            out.append('#pragma pack(pop)')
        out.append('')
    if not with_probe:
        return '\n'.join(out)
    out.append('static void bits(const unsigned char *p, size_t n, int *first, int *count)')
    out.append('{')
    out.append('\tsize_t i;\n\n\t*first = -1;\n\t*count = 0;')
    out.append('\tfor (i = 0; i < n * 8; i++) {')
    out.append('\t\tif (p[i / 8] & (1u << (i % 8))) {')
    out.append('\t\t\tif (*first < 0)\n\t\t\t\t*first = (int)i;\n\t\t\t(*count)++;')
    out.append('\t\t}\n\t}\n}\n')
    out.append('int main(void)\n{\n\tint first, count;\n')
    for i, (kind, pack, fields) in enumerate(records):
        out.append('\t{ %s S%d x; printf("%s S%d size=%%zu align=%%zu\\n", sizeof(x), (size_t)_Alignof(%s S%d));'
                   % (kind, i, kind, i, kind, i))
        for k, t, name, n in fields:
            if not name:
                continue
            if k == 'plain':
                out.append('\t  printf("  field %s off=%%zu\\n", offsetof(%s S%d, %s));'
                           % (name, kind, i, name))
            else:
                out.append('\t  memset(&x, 0, sizeof(x)); x.%s = -1; bits((unsigned char *)&x, sizeof(x), &first, &count);'
                           % name)
                out.append('\t  printf("  field %s bitpos=%%d bits=%%d\\n", first, count);' % name)
        out.append('\t}')
    out.append('\treturn 0;\n}')
    return '\n'.join(out)


def parse_cparse(text):
    """struct S0 size=N align=A / field NAME off=N / field NAME bitpos=P bits=W"""
    res = []
    for line in text.split('\n'):
        m = re.match(r'(struct|union) (S\d+) def size=(-?\d+) align=(-?\d+)', line)
        if m:
            res.append('%s %s size=%s align=%s' % m.groups())
            continue
        m = re.match(r"  field (\w+) '[^']*' bitpos=(\d+) bits=(\d+)", line)
        if m and m.group(1) != '(anon)':
            res.append('  field %s bitpos=%s bits=%s' % m.groups())
            continue
        m = re.match(r"  field (\w+) '[^']*' off=(\d+)", line)
        if m and m.group(1) != '(anon)':
            res.append('  field %s off=%s' % m.groups())
    return res


def main():
    tool = os.environ['CPARSE_TOOL']
    cl = os.environ.get('MSVC_CL', 'cl')
    wine = os.environ.get('WINE', 'wine')
    seed = int(sys.argv[1])
    arch = sys.argv[2] if len(sys.argv) > 2 else 'x64'
    triple = {'x64': 'x86_64-pc-windows-msvc', 'x86': 'i686-pc-windows-msvc'}[arch]
    records = make_source(seed, 30)

    with tempfile.TemporaryDirectory() as d:
        plain = os.path.join(d, 'plain.c')
        probe = os.path.join(d, 'probe.c')
        with open(plain, 'w') as f:
            f.write(write_c(records, False))
        with open(probe, 'w') as f:
            f.write(write_c(records, True))
        mine = subprocess.run([tool, '-T', triple, 'decls', plain], capture_output=True, text=True)
        built = subprocess.run([cl, '/nologo', '/std:c11', '/Fe:probe.exe', 'probe.c'], cwd=d,
                               capture_output=True, text=True)
        exe = os.path.join(d, 'probe.exe')
        if not os.path.exists(exe):
            print(built.stdout[-2000:])
            return 2
        cmd = [wine, exe] if wine else [exe]
        real = subprocess.run(cmd, capture_output=True, text=True, cwd=d)

    want = [l for l in real.stdout.split('\n') if l]
    got = parse_cparse(mine.stdout)
    # compare the lines the probe has: records and their named fields
    diffs = 0
    gi = {}
    cur = None
    for l in got:
        if not l.startswith(' '):
            cur = l.split()[1]
        gi.setdefault(cur, []).append(l)
    cur = None
    for l in want:
        if not l.startswith(' '):
            cur = l.split()[1]
            if l not in gi.get(cur, []):
                print('MSVC:', l, '| cparse:', [x for x in gi.get(cur, []) if not x.startswith(' ')])
                diffs += 1
        elif l not in gi.get(cur, []):
            print('MSVC:', cur, l.strip(), '| cparse:',
                  [x.strip() for x in gi.get(cur, []) if l.split()[1] in x])
            diffs += 1
    print('records %d, differences %d' % (len(records), diffs))
    return 1 if diffs else 0


sys.exit(main())
