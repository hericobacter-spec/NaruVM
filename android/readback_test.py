#!/usr/bin/env python3
"""Disk-free C API frame readback/resize checks on the installed Android GPU port."""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = 'dev.henge.research'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--software', action='store_true',
                        help='also check the unchanged software-renderer default path')
    args = parser.parse_args()
    base = [str(ROOT / '.tools/android-sdk/platform-tools/adb'), '-s', args.serial]

    def adb(*command):
        return subprocess.check_output(base + list(command), text=True, stderr=subprocess.STDOUT)

    def require_idle():
        listing = adb('shell', 'ps', '-A', '-o', 'NAME')
        if re.search(r'libhenge_(lab|gpu|reims_probe|clone|frames_test)\.so', listing):
            raise SystemExit('Native worker active; normal completion is required before this check.')
        if 'isForeground=true' in adb('shell', 'dumpsys', 'activity', 'services', PACKAGE):
            raise SystemExit('Foreground session active; do not interrupt it.')

    require_idle()
    package = adb('shell', 'dumpsys', 'package', PACKAGE)
    match = re.search(r'legacyNativeLibraryDir=(\S+)', package)
    if not match:
        raise SystemExit('Installed native directory absent; not a pass.')
    native = match[1] + '/arm64'
    args.output.mkdir(parents=True, exist_ok=True)
    boot = adb('shell', 'cat', '/proc/sys/kernel/random/boot_id').strip()
    methods = [('host_image', 'libhenge_panvk_userptr_loader.so', 'texture'),
               ('same_batch_buffer', 'libhenge_panvk_userptr_loader.so', 'buffer')]
    if args.software:
        methods.append(('software_default', 'libhenge_lvp_loader.so', 'texture'))
    results = []
    for name, loader, method in methods:
        require_idle()
        command = ('cd files && LD_LIBRARY_PATH=' + shlex.quote(native) +
                   ' REIMS_VGPU_FRAME_READBACK=' + method + ' HENGE_LVP_POOLED=1 ' + shlex.join([
            native + '/libhenge_reims_probe.so',
            native + '/libreims_vgpu_c.so', '--readback-stress', '--loader=' + native + '/' + loader]))
        result = subprocess.run(base + ['shell', '-T', 'run-as ' + PACKAGE + ' sh -c ' +
                                        shlex.quote(command)], capture_output=True, text=True)
        output = result.stdout + result.stderr
        (args.output / (name + '.txt')).write_text(output + '\nHOST_ADB_EXIT_CODE=' + str(result.returncode) + '\n')
        marker = 'REIMS_FRAME_READBACK method=same_batch_buffer'
        passed = (result.returncode == 0 and 'REIMS_RESULT failures=0' in output and
                  'READBACK_C_API_RESULT failures=0 rounds=30' in output and
                  ((marker in output) == (method == 'buffer')))
        results.append({'method': name, 'exit_code': result.returncode, 'passed': passed})
        print(name, 'PASS' if passed else 'FAIL', flush=True)
        if not passed:
            break
    same_boot = boot == adb('shell', 'cat', '/proc/sys/kernel/random/boot_id').strip()
    require_idle()
    passed = len(results) == len(methods) and all(x['passed'] for x in results) and same_boot
    record = {'failures': int(not passed), 'authored_only': True, 'guest_disk_opened': False,
              'rounds_per_method': 30, 'sizes': [32, 64, 96], 'boot_id': boot,
              'same_boot': same_boot, 'results': results}
    (args.output / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
    print('READBACK_DEVICE_RESULT failures=' + str(record['failures']))
    raise SystemExit(0 if passed else 1)


if __name__ == '__main__':
    main()
