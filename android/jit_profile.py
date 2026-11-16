#!/usr/bin/env python3
"""Record own-worker CPU samples, then collect generation-valid JIT attribution.

The VM must already be running with boot_cpu_stats enabled. Recording never
starts or stops a VM. The worker resolves the request under its translation
lock after sampling. collect validates and summarizes the saved response.
Only code addresses, block metadata and sample weights are exported.
"""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = 'dev.henge.research'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['record', 'collect'])
    parser.add_argument('--serial', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=10)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 30:
        parser.error('--seconds is the profiling window, in 1..30; not a VM deadline')
    base = [str(ROOT / '.tools/android-sdk/platform-tools/adb'), '-s', args.serial]

    def adb(*command, binary=False):
        return subprocess.check_output(base + list(command), text=not binary,
                                       stderr=subprocess.PIPE)

    out = args.output
    if args.action == 'record':
        out.mkdir(parents=True, exist_ok=True)
        if any(out.iterdir()):
            parser.error('--output must be empty; preserve previous evidence')
        pid = int(adb('shell', 'pidof', 'libhenge_lab.so').strip())
        boot = adb('shell', 'cat', '/proc/sys/kernel/random/boot_id').strip()
        live = adb('exec-out', 'run-as', PACKAGE, 'cat', 'files/vm-live-report.txt')
        states = re.findall(r'JIT_PROFILE_STATE pid=(\d+) generation=(\d+) base=(0x[0-9a-f]+) used=(\d+) blocks=(\d+)', live)
        if not states or int(states[-1][0]) != pid:
            raise SystemExit('No current JIT profile state; enable boot_cpu_stats and wait for a heartbeat.')
        generation = int(states[-1][1])
        nonce = time.time_ns()
        name = f'files/jit-perf-{nonce}.data'
        (out / 'state-before.txt').write_text(live)
        maps = adb('shell', 'run-as', PACKAGE, 'cat', f'/proc/{pid}/maps')
        (out / 'maps-before.txt').write_text(maps)
        ranges = []
        for line in maps.splitlines():
            fields = line.split()
            if len(fields) >= 6 and '/memfd:henge-jit' in line and 'x' in fields[1]:
                ranges.append(tuple(int(x, 16) for x in fields[0].split('-')))
        if not ranges:
            raise SystemExit('Executable own-worker JIT mapping absent; no attribution.')
        record = subprocess.run(base + ['shell', 'run-as', PACKAGE,
                    '/system/bin/simpleperf', 'record', '-e', 'cpu-clock:u',
                    '-f', '99', '-p', str(pid), '--duration', str(args.seconds),
                    '-o', name], text=True, stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT, check=True)
        (out / 'record.txt').write_text(record.stdout)
        (out / 'perf.data').write_bytes(adb('exec-out', 'run-as', PACKAGE, 'cat', name, binary=True))
        report = adb('shell', 'run-as', PACKAGE, '/system/bin/simpleperf', 'report',
                     '-i', name, '--sort', 'symbol', '--raw-period', '-n')
        (out / 'report.txt').write_text(report)
        adb('shell', 'run-as', PACKAGE, 'rm', name)
        total = int(re.search(r'^Event count: (\d+)', report, re.M)[1])
        pcs = []
        for weight, samples, address in re.findall(r'^\s*(\d+)\s+(\d+)\s+unknown\[\+([a-f0-9]+)\]', report, re.M):
            pc = int(address, 16)
            if any(lo <= pc < hi for lo, hi in ranges):
                pcs.append({'pc': pc, 'period': int(weight), 'samples': int(samples)})
        pcs.sort(key=lambda row: row['period'], reverse=True)
        manifest = {'scope': 'one own-worker user-CPU window; no wall-time or global speed claim',
                    'pid': pid, 'generation': generation, 'nonce': nonce, 'boot_id': boot,
                    'seconds': args.seconds, 'total_period': total,
                    'jit_sample_pcs': pcs[:4096], 'dropped_pc_count': max(0, len(pcs)-4096)}
        (out / 'request.json').write_text(json.dumps(manifest, indent=2)+'\n')
        if not pcs:
            raise SystemExit('No own-JIT sampled PCs; no request written.')
        request = f'HENGE_JIT_PROFILE_V1 {pid} {generation} {nonce} {len(pcs[:4096])}\n'
        request += ''.join(f"{row['pc']:x} {row['period']}\n" for row in pcs[:4096])
        (out / 'request.txt').write_text(request)
        # argv stays constant; request bytes go through stdin, never shell interpolation.
        subprocess.run(base + ['shell', 'run-as', PACKAGE, 'sh', '-c',
                               "'cat > files/jit-profile-request.tmp && mv files/jit-profile-request.tmp files/jit-profile-request.txt'"],
                       input=request, text=True, check=True)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            try:
                result = adb('exec-out', 'run-as', PACKAGE, 'cat', 'files/jit-profile-result.txt')
            except subprocess.CalledProcessError:
                result = ''
            if f'nonce={nonce}' in result:
                (out / 'result.txt').write_text(result)
                if 'JIT_PROFILE_ERROR' in result:
                    raise SystemExit('JIT generation changed; raw samples preserved, attribution rejected.')
                print(f'Recorded and resolved {len(pcs[:4096])} JIT PCs. Use collect to summarize; VM remains running.')
                break
            time.sleep(0.25)
        else:
            raise SystemExit('No matching worker response; raw samples preserved.')
    else:
        manifest = json.loads((out / 'request.json').read_text())
        if adb('shell', 'cat', '/proc/sys/kernel/random/boot_id').strip() != manifest['boot_id']:
            raise SystemExit('Tablet boot changed; no valid attribution.')
        result = (out / 'result.txt').read_text()
        expected = (f"JIT_PROFILE_V1 pid={manifest['pid']} generation={manifest['generation']} "
                    f"nonce={manifest['nonce']} count={len(manifest['jit_sample_pcs'])}")
        if not result.startswith(expected+'\n') or 'JIT_PROFILE_ERROR' in result or not result.endswith('JIT_PROFILE_COMPLETE\n'):
            raise SystemExit('Incomplete/stale generation or request; attribution rejected.')
        groups = defaultdict(lambda: {'period': 0, 'sample_pcs': 0})
        hot_cold = defaultdict(int)
        mapped = 0
        rows = result.splitlines()[1:-1]
        pending = {row['pc']: row['period'] for row in manifest['jit_sample_pcs']}
        if len(rows) != len(pending):
            raise SystemExit('Response sample count mismatch; attribution rejected.')
        for line in rows:
            parts = line.split()
            if int(parts[1],16) not in pending or int(parts[-1]) != pending.pop(int(parts[1],16)):
                raise SystemExit('Response sample/weight mismatch; attribution rejected.')
            if parts[0] == 'outside':
                continue
            _, host, va, flags, ninsns, offset, kind, weight = parts
            key = f'{va} el{int(flags)&1} ({ninsns} guest instructions)'
            period = int(weight)
            groups[key]['period'] += period
            groups[key]['sample_pcs'] += 1
            hot_cold[kind] += period
            mapped += period
        summary = {'label': 'observed', 'scope': manifest['scope'], 'generation_valid': True,
                   'attribution_valid': mapped > 0,
                   'total_period': manifest['total_period'], 'mapped_period': mapped,
                   'mapped_percent': 100*mapped/manifest['total_period'],
                   'hot_cold_percent_of_total': {k:100*v/manifest['total_period'] for k,v in hot_cold.items()},
                   'top_blocks': sorted([{'block':k, **v, 'percent_of_total':100*v['period']/manifest['total_period']}
                                         for k,v in groups.items()], key=lambda r:r['period'], reverse=True)[:30],
                   'limits': 'Diagnostic window only. Cold includes slow paths/exit stubs. No guest instruction bytes exported; no crash cause or near-native speed claim.'}
        (out / 'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
        if not mapped:
            raise SystemExit('No samples resolved to a block; not valid for hotspot attribution.')
        print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
