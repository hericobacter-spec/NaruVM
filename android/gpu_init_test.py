#!/usr/bin/env python3
"""Repeat installed Reims device initialization without opening a guest disk.

Stop at the first failed probe or changed tablet boot. Alternating tracing isolates
the diagnostic interposer as a variable; a passing run does not prove VM stability.
"""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = 'dev.henge.research'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rounds', type=int, default=20)
    args = parser.parse_args()
    if not 1 <= args.rounds <= 200:
        parser.error('--rounds must be in 1..200')
    args.output.mkdir(parents=True, exist_ok=True)
    if any(args.output.iterdir()):
        parser.error('--output must be empty; preserve earlier run evidence separately')
    base = [str(ROOT / '.tools/android-sdk/platform-tools/adb'), '-s', args.serial]

    def adb(*command):
        return subprocess.check_output(base + list(command), text=True,
                                       stderr=subprocess.STDOUT)

    def require_idle():
        processes = adb('shell', 'ps', '-A', '-o', 'NAME')
        if re.search(r'libhenge_(lab|gpu|reims_probe|clone|frames_test)\.so', processes):
            raise RuntimeError('Native worker active; wait for normal completion.')
        if 'isForeground=true' in adb('shell', 'dumpsys', 'activity', 'services', PACKAGE):
            raise RuntimeError('Foreground session active; do not interrupt it.')

    def boot_id():
        return adb('shell', 'cat', '/proc/sys/kernel/random/boot_id').strip()

    require_idle()
    package = adb('shell', 'dumpsys', 'package', PACKAGE)
    match = re.search(r'legacyNativeLibraryDir=(\S+)', package)
    if not match:
        raise SystemExit('Installed native directory absent; not a pass.')
    native = match[1] + '/arm64'
    boot = boot_id()
    trace_prefix = 'gpu-init-trace-' + str(time.time_ns())
    trace_name = None
    report = {'scope': 'installed C API device initialization; no VM or guest disk',
              'serial': args.serial, 'boot_id_before': boot, 'requested': args.rounds,
              'native_directory': native, 'rounds': [], 'passed': False}
    (args.output / 'package.txt').write_text(package)

    def save():
        (args.output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')

    save()
    try:
        for index in range(args.rounds):
            require_idle()
            if boot_id() != boot:
                raise RuntimeError('Tablet rebooted; no further probe will start.')
            # ABBA ordering balances warm-up and elapsed time across tracing modes.
            traced = index % 4 in (1, 2)
            prefix = 'LD_LIBRARY_PATH=' + shlex.quote(native)
            if traced:
                if trace_name is not None:
                    adb('shell', 'run-as', PACKAGE, 'rm', 'files/' + trace_name)
                trace_name = f'{trace_prefix}-{index + 1:03d}.bin'
                report['trace_round'] = index + 1
                report['trace_round_passed'] = None
                save()
                prefix += (' HENGE_GPU_MEMORY_TRACE=1 HENGE_GPU_TRACE_FILE=' +
                           shlex.quote('/data/user/0/' + PACKAGE + '/files/' + trace_name))
            command = 'cd files && ' + prefix + ' ' + shlex.join([
                native + '/libhenge_reims_probe.so', native + '/libreims_vgpu_c.so',
                '--loader=' + native + '/libhenge_panvk_userptr_loader.so'])
            start = time.monotonic()
            result = subprocess.run(base + ['shell', 'run-as', PACKAGE, 'sh', '-c',
                                            shlex.quote(command)], text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            name = f'{index + 1:03d}-trace-{int(traced)}.txt'
            (args.output / name).write_text(result.stdout)
            same_boot = boot_id() == boot
            passed = (result.returncode == 0 and same_boot and
                      'REIMS_RESULT failures=0 guest=none disk=none' in result.stdout and
                      'CS_FAULT' not in result.stdout and
                      'PANVK_LOADER source=' in result.stdout and
                      ('PANVK_TRACE active=1' in result.stdout) == traced)
            if traced:
                report['trace_round_passed'] = passed
            report['rounds'].append({'round': index + 1, 'traced': traced,
                                     'elapsed_s': time.monotonic() - start,
                                     'exit_code': result.returncode, 'same_boot': same_boot,
                                     'passed': passed, 'log': name})
            save()
            print(f'GPU_INIT round={index + 1} trace={int(traced)} pass={int(passed)}', flush=True)
            if not passed:
                if same_boot:
                    diagnostic = subprocess.run(base + ['logcat', '-d', '-t', '500'],
                                                text=True, stdout=subprocess.PIPE,
                                                stderr=subprocess.STDOUT)
                    (args.output / 'failure-logcat.txt').write_text(diagnostic.stdout)
                raise RuntimeError('Initialization failed; stopped at the first failure.')
        report['boot_id_after'] = boot_id()
        report['passed'] = report['boot_id_after'] == boot
        require_idle()
    except (RuntimeError, subprocess.CalledProcessError) as error:
        report['error'] = str(error)
        report['passed'] = False
    finally:
        # Preserve the most recent bounded trace without touching a VM's trace file.
        if trace_name is not None:
            trace = subprocess.run(base + ['exec-out', 'run-as', PACKAGE, 'cat',
                                          'files/' + trace_name], stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
            if trace.returncode == 0:
                (args.output / 'last-trace.bin').write_bytes(trace.stdout)
                report['trace_saved'] = True
                subprocess.run(base + ['shell', 'run-as', PACKAGE, 'rm',
                                       'files/' + trace_name], stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
            else:
                report['trace_saved'] = False
        save()
    if not report['passed']:
        raise SystemExit(report.get('error', 'Boot identity changed; not a pass.'))


if __name__ == '__main__':
    main()
