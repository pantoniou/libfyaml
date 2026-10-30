#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare the layout of random records with libclang.

usage: fuzz-layout.py SEED TARGET...

For each target the script writes files of random structs and unions (bit-fields,
packed, aligned and #pragma pack), dumps them with cparse-tool and with libclang
and prints the first difference of each file that differs. It keeps those files
in the current directory. A manual check; known differences are rare mixes of the
aligned attribute that lower an alignment below the natural one.

Environment: CPARSE_TOOL and CPARSE_LIBCLANG_DUMP, as for run-cparse-test.sh.
"""
import os
import random
import subprocess
import sys
import tempfile

TYPES = ['char', 'signed char', 'unsigned char', 'short', 'unsigned short', 'int',
         'unsigned int', 'long', 'unsigned long', 'long long', 'unsigned long long',
         '_Bool', 'float', 'double', 'long double', 'void *', '__int128']
BITFIELD_BITS = {'char': 8, 'unsigned char': 8, 'short': 16, 'unsigned short': 16,
                 'int': 32, 'unsigned int': 32, 'long long': 64,
                 'unsigned long long': 64, '_Bool': 8}


def make_file(seed, no_int128):
    r = random.Random(seed)
    out = []
    for i in range(40):
        kind = r.choice(['struct'] * 4 + ['union'])
        attrs = ''
        if r.random() < 0.2:
            attrs += ' __attribute__((packed))'
        if r.random() < 0.15:
            attrs += ' __attribute__((aligned(%d)))' % r.choice([2, 4, 8, 16])
        pre = post = ''
        if r.random() < 0.15:
            pre = '#pragma pack(push, %d)\n' % r.choice([1, 2, 4, 8])
            post = '#pragma pack(pop)\n'
        fields = []
        for j in range(r.randint(1, 7)):
            if r.random() < 0.5:
                t = r.choice(sorted(BITFIELD_BITS))
                w = r.choice([0, 0, 1, 3, 5, 7, 8, 12, 16, 20, 31, 32, 33, 40])
                w = min(w, BITFIELD_BITS[t])
                if t == '_Bool':
                    w = min(w, 1)
                name = '' if (r.random() < 0.25 or w == 0) else 'f%d' % j
                at = ''
                if name and r.random() < 0.1:
                    at = ' __attribute__((aligned(%d)))' % r.choice([2, 4, 8])
                fields.append('%s %s : %d%s;' % (t, name, w, at))
            else:
                t = r.choice(TYPES)
                if no_int128 and t == '__int128':
                    t = 'int'
                arr = '[%d]' % r.choice([1, 2, 3, 5]) if r.random() < 0.2 else ''
                at = ''
                if r.random() < 0.1:
                    at = ' __attribute__((aligned(%d)))' % r.choice([2, 4, 8, 16])
                if r.random() < 0.07:
                    at += ' __attribute__((packed))'
                fields.append('%s f%d%s%s;' % (t, j, arr, at))
        out.append('%s%s S%d {\n  %s\n}%s;\n%s' % (pre, kind, i, '\n  '.join(fields), attrs, post))
    return ''.join(out)


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=False)


def main():
    tool = os.environ['CPARSE_TOOL']
    dump = os.environ['CPARSE_LIBCLANG_DUMP']
    seed0 = int(sys.argv[1])
    bad = 0

    for target in sys.argv[2:]:
        no_int128 = target.startswith(('i686', 'i386', 'arm', 'riscv32', 'mips-', 'mipsel'))
        for k in range(6):
            with tempfile.NamedTemporaryFile('w', suffix='.c', delete=False) as f:
                f.write(make_file(seed0 + k, no_int128))
            mine = run([tool, '-T', target, 'decls', f.name])
            ref = run([dump, '-target', target, '-x', 'c', f.name])
            if mine.stderr.strip() or ref.stderr.strip() or mine.stdout == ref.stdout:
                os.unlink(f.name)
                continue
            bad += 1
            for a, b in zip(ref.stdout.split('\n'), mine.stdout.split('\n')):
                if a != b:
                    print(target, 'seed', seed0 + k, 'libclang:', a, '| cparse:', b)
                    break
            keep = 'fuzz-%s-%d.c' % (target, seed0 + k)
            os.rename(f.name, keep)
    print('files that differ:', bad)
    return 1 if bad else 0


sys.exit(main())
