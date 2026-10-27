#!/usr/bin/env python3
"""Run durable tests against already-built FIL-C and native libfyaml libraries.
Usage: run-durable.py FILC_ROOT FILC_BUILD NATIVE_BUILD OUTPUT_DIRECTORY
Build both libraries with generics enabled, portable target enabled, and
libclang/reflection disabled. Assertions must remain enabled in this probe.
"""
import json
import os
from pathlib import Path
import resource
import signal
import subprocess
import sys
import tempfile
import time

if len(sys.argv) != 5:
    raise SystemExit(__doc__)
filc, fb, nb, out = map(lambda x: Path(x).resolve(), sys.argv[1:])
repo = Path(__file__).resolve().parents[2]
out.mkdir(parents=True, exist_ok=True)
work = Path(tempfile.mkdtemp(prefix='run-', dir=out))
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
envs = {
    'filc': dict(os.environ, TMPDIR=str(work), LD_LIBRARY_PATH=f'{fb}:{filc}/pizfix/lib'),
    'native': dict(os.environ, TMPDIR=str(work), LD_LIBRARY_PATH=str(nb)),
}
rows = []

def command(argv, env, timeout=60):
    return subprocess.run(list(map(str, argv)), env=env, capture_output=True,
                          text=True, timeout=timeout)

for kind, cc, build in [('filc', filc/'build/bin/clang', fb), ('native', 'gcc', nb)]:
    includes = [repo/'include', build] + [repo/'src'/x for x in
        ['lib', 'util', 'xxhash', 'thread', 'allocator', 'blake3', 'generic', 'reflection']]
    args = [cc, '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-DHAVE_CONFIG_H']
    args += ['-I'+str(x) for x in includes]
    args += [repo/'test/filc/durable-probe.c', '-L'+str(build), '-lfyaml', '-lpthread',
             '-o', work/kind]
    p = command(args, envs[kind])
    (out/f'build-probe-{kind}.log').write_text(p.stdout+p.stderr)
    if p.returncode: raise SystemExit(p.stderr)


def record(label, kind, mode, p, expected=0, marker='OK'):
    ok = p.returncode == expected and marker in p.stdout+p.stderr
    rows.append(dict(test=label, compiler=kind, mode=mode, passed=ok,
                     returncode=p.returncode, expected=expected))
    (out/(label+'.log')).write_text(f'exit={p.returncode}\nstdout:\n{p.stdout}\nstderr:\n{p.stderr}')
    print(('PASS ' if ok else 'FAIL ')+label, flush=True)
    return ok


def run(label, mode, folder, *args, kind='filc', expected=0, marker='OK'):
    p = command([work/kind, mode, work/folder, *args], envs[kind])
    return record(label, kind, mode, p, expected, marker)

run('seed', 'seed', 'basic')
run('reopen', 'read', 'basic', 1, 42)
run('grow', 'append', 'basic', 24, 100)
run('reopen-grown', 'read', 'basic', 25, 2718)
run('readonly', 'readonly', 'basic', 25, 2718)
run('stale-cas', 'cas', 'basic')
run('inspect', 'inspect', 'basic')
run('retired-pointer', 'uaf', 'basic', expected=-signal.SIGTRAP, marker='pointer to free object')
run('retired-pointer-after-reopen', 'reopen', 'basic', expected=-signal.SIGTRAP,
    marker='pointer to free object')
run('native-reads-filc', 'read', 'basic', 25, 2718, kind='native')
run('native-seed', 'seed', 'interop', kind='native')
run('filc-reads-native', 'read', 'interop', 1, 42)
run('filc-appends-native', 'append', 'interop', 8, 100)
run('native-reads-appended', 'read', 'interop', 9, 870, kind='native')
run('abrupt-writer-exit', 'crash', 'crash', marker='RECORDS=1')
run('reopen-after-abrupt-exit', 'read', 'crash', 1, 42)
run('thread-writers', 'threads', 'threads')
run('reopen-thread-writes', 'read', 'threads', 32, 8112)
run('checkpoint', 'checkpoint', 'checkpoint')
run('verify-reopened-checkpoint', 'verify', 'checkpoint')
run('native-verifies-filc-checkpoint', 'verify', 'checkpoint', kind='native')
run('native-checkpoint', 'checkpoint', 'native-checkpoint', kind='native')
run('filc-verifies-native-checkpoint', 'verify', 'native-checkpoint')
run('checkpoint-rollback', 'recover', 'rollback')
run('reopen-rolled-back', 'read', 'rollback', 1, 42)
# persistent dedup: every run, of either runtime, finds the entries the
# earlier runs stored, at the same logical addresses
dedup_sums = []
for label, kind in [('dedup-filc', 'filc'), ('dedup-native', 'native'), ('dedup-filc-again', 'filc')]:
    p = command([work/kind, 'dedup', work/'dedup'], envs[kind])
    words = (p.stdout.split() + ['', ''])[:2]
    if words[0] == 'DEDUP_OK':
        dedup_sums.append(words[1])
    ok = p.returncode == 0 and words[0] == 'DEDUP_OK' and len(set(dedup_sums)) == 1
    p = subprocess.CompletedProcess(p.args, 0 if ok else 1, p.stdout, p.stderr)
    record(label, kind, 'dedup', p, marker='DEDUP_OK')

# compaction keeps a generic root intact, whichever runtime collects or reads
run('gc-seed', 'gc-seed', 'gc', marker='GC_SEED_OK')
run('gc-filc', 'gc', 'gc', marker='GC_OK')
run('gc-filc-read', 'gc-read', 'gc', marker='GC_READ_OK')
run('gc-native-read', 'gc-read', 'gc', kind='native', marker='GC_READ_OK')
run('gc-native', 'gc', 'gc', kind='native', marker='GC_OK')
run('gc-native-filc-read', 'gc-read', 'gc', marker='GC_READ_OK')

# Concurrent executables, not forked snapshots: each owns a different runtime mapping.
for group, kinds in [('process', ['filc']*4), ('mixed', ['filc', 'native'])]:
    run(group+'-seed', 'seed', group)
    jobs = []
    gate = work/(group+'-gate')
    count = 1 + len(kinds)*8
    for i, kind in enumerate(kinds):
        p = subprocess.Popen([str(work/kind), 'append', str(work/group), '8', str(1000*(i+1)), str(gate)],
                             env=envs[kind], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        jobs.append((kind, p))
    observer = subprocess.Popen([str(work/'filc'), 'watch', str(work/group), str(count), str(gate)],
                                env=envs['filc'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    deadline = time.monotonic() + 10
    while len(list(work.glob(group+'-gate.ready.*'))) != len(jobs)+1:
        if time.monotonic() >= deadline:
            for _, p in jobs: p.kill()
            observer.kill()
            raise SystemExit('concurrency gate timed out')
        time.sleep(0.01)
    gate.touch()
    for i, (kind, p) in enumerate(jobs):
        stdout, stderr = p.communicate(timeout=60)
        result = subprocess.CompletedProcess(p.args, p.returncode, stdout, stderr)
        record(f'{group}-writer-{i}', kind, 'append', result)
    stdout, stderr = observer.communicate(timeout=60)
    record(group+'-live-reader', 'filc', 'watch',
           subprocess.CompletedProcess(observer.args, observer.returncode, stdout, stderr))
    total = 42 + sum(8000*(i+1)+28 for i in range(len(kinds)))
    run(group+'-final-read', 'read', group, count, total)
    run(group+'-native-final-read', 'read', group, count, total, kind='native')

summary = dict(passed=sum(x['passed'] for x in rows), total=len(rows), tests=rows,
               filc=command([filc/'build/bin/clang', '--version'], envs['filc']).stdout,
               base_commit=command(['git','-C',repo,'rev-parse','HEAD'],envs['native']).stdout.strip())
(out/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
print(f"{summary['passed']}/{summary['total']} passed")
raise SystemExit(0 if summary['passed']==summary['total'] else 1)
