#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Disk-free JIT metadata scheduling crossover on an owned, ADB-authorized ARM64 device."""
import argparse
import hashlib
import json
import re
import statistics
import subprocess
import uuid
from pathlib import Path
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--rounds', type=int, default=8, help='paired processes per MMU mode, 4..32')
    parser.add_argument('--output', type=Path, default=ROOT / 'android/artifacts/jit-memory')
    args = parser.parse_args()
    if not 4 <= args.rounds <= 32:
        parser.error('--rounds must be 4..32')
    adb = ROOT / '.tools/android-sdk/platform-tools/adb'
    binaries = [ROOT / 'android/build/native/bin' / name for name in
                ['libhenge_cpu_native_bench.so', 'libtest_mempath.so']]
    if not adb.is_file() or not all(p.is_file() for p in binaries):
        parser.error('Prepare the Android native build, then build targets henge_cpu_native_bench and test_mempath.')
    base = [str(adb), '-s', args.serial]

    def call(*values, check=True):
        result = subprocess.run(base + list(map(str, values)), check=False,
                                capture_output=True, text=True, timeout=60)
        if check and result.returncode:
            detail = result.stderr.strip() or result.stdout.strip() or 'no diagnostic output'
            raise SystemExit(f'ADB command failed (exit {result.returncode}): {detail}')
        return result

    state = call('get-state')
    if state.stdout.strip() != 'device':
        raise SystemExit('ADB device not ready; unlock it and authorize this computer.')
    if call('shell', 'pidof', 'libhenge_lab.so', check=False).stdout.strip():
        raise SystemExit('VM is running. Stop it normally in Henge before this disk-free test; nothing was changed.')
    boot = call('shell', 'cat', '/proc/sys/kernel/random/boot_id').stdout.strip()
    args.output.mkdir(parents=True, exist_ok=True)
    token = uuid.uuid4().hex
    remote = [f'/data/local/tmp/henge-jit-memory-{token}-{i}' for i in range(2)]
    samples, checks = [], []
    try:
        for local, target in zip(binaries, remote):
            call('push', local, target)
            call('shell', 'chmod', '755', target)
        for flag in [0, 1]:
            for layout in [0, 1]:
                for recheck in [0, 1]:
                    q = call('shell', 'env', f'HV_EARLY_ADDEND={flag}', f'HV_TLBT={layout}',
                             f'HV_RECHECK={recheck}', remote[1])
                    name = f'guards-{flag}-{layout}-{recheck}.txt'
                    (args.output / name).write_text(q.stdout + q.stderr)
                    if 'MEMPATH_RESULT checks=202 failures=0' not in q.stdout:
                        raise RuntimeError('Guard test did not report success: ' + name)
                    checks.append({'early_addend': flag, 'layout': layout, 'recheck': recheck,
                                   'checks': 202, 'failures': 0, 'raw': name})
        for mmu in ['on', 'off']:
            for pair in range(args.rounds):
                for flag in ([0, 1] if pair % 2 == 0 else [1, 0]):
                    q = call('shell', 'env', f'HV_EARLY_ADDEND={flag}', remote[0],
                             '--superblocks=0', f'--mmu={mmu}')
                    name = f'bench-{mmu}-{pair}-{flag}.txt'
                    (args.output / name).write_text(q.stdout + q.stderr)
                    if 'NATIVE_JIT_RESULT failures=0' not in q.stdout:
                        raise RuntimeError('Benchmark did not report success: ' + name)
                    rows = []
                    for line in q.stdout.splitlines():
                        if line.startswith('NATIVE_JIT_BENCH '):
                            v = dict(re.findall(r'(\w+)=([^ ]+)', line))
                            if v.get('exact') != '1':
                                raise RuntimeError('Native/JIT result mismatch: ' + name)
                            v.update(pair_round=pair, early_addend=flag, raw=name)
                            v['ratio'] = int(v['jit_us']) / int(v['native_us'])
                            rows.append(v)
                    if len(rows) != 9:
                        raise RuntimeError('Incomplete benchmark output: ' + name)
                    samples.extend(rows)
        summary = []
        for mmu in ['on', 'off']:
            for workload in ['pointer_chase', 'memory', 'alu']:
                groups = [[v for v in samples if v['mmu'] == mmu and
                           v['workload'] == workload and v['early_addend'] == flag] for flag in [0, 1]]
                med = [statistics.median(v['ratio'] for v in group) for group in groups]
                summary.append({'mmu': mmu, 'workload': workload, 'processes_per_variant': args.rounds,
                                'samples_per_process': 3, 'baseline_jit_native_ratio': med[0],
                                'early_jit_native_ratio': med[1],
                                'normalized_reduction_percent': 100 * (1 - med[1] / med[0])})
        after = call('shell', 'cat', '/proc/sys/kernel/random/boot_id').stdout.strip()
        if after != boot:
            raise RuntimeError('Device boot changed; reject this comparison.')
        report = {'label': 'observed', 'date': datetime.now(timezone.utc).isoformat(),
                  'binaries': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in binaries},
                  'scope': 'authored loops under ADB shell UID, not macOS/app latency',
                  'limits': 'Native benchmark compares first output word; guard tests check guest effects/faults. No GPU/endurance conclusion.',
                  'boot_unchanged': True, 'guards': checks, 'samples': samples, 'summary': summary}
        (args.output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(summary, indent=2))
        print('JIT_MEMORY_RESULT failures=0 report=' + str(args.output / 'result.json'))
    finally:
        # Only this invocation's two own executables; no app or guest files.
        call('shell', 'rm', '-f', *remote, check=False)


if __name__ == '__main__':
    main()
