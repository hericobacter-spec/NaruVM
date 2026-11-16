#!/usr/bin/env python3
"""Install and run Henge Lab on one explicitly selected Android device."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
ADB = ROOT / '.tools/android-sdk/platform-tools/adb'
PACKAGE = 'dev.henge.research'
ROM_HASH = 'fd4973e42e609751830df6f3dd2d2b2ad2309fe38f0ca0f5aba02dcbefe45f91'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True, help='Exact serial shown by adb devices -l')
    parser.add_argument('--rom', type=Path, help='Optional hash-checked AVPBooter for a 15-second firmware experiment')
    parser.add_argument('--output', type=Path, default=ROOT / 'android/artifacts/device-results')
    args = parser.parse_args()
    base = [str(ADB), '-s', args.serial]
    def run(*cmd, **kwargs):
        return subprocess.run(base + list(cmd), check=True, **kwargs)
    def capture(*cmd):
        return run(*cmd, stdout=subprocess.PIPE).stdout

    rom = args.rom.read_bytes() if args.rom else None
    if rom is not None and hashlib.sha256(rom).hexdigest() != ROM_HASH:
        parser.error('ROM SHA-256 does not match the supported AVPBooter; no device changes were made.')
    apk = ROOT / 'android/artifacts/Henge-Lab-arm64-debug.apk'
    if not apk.is_file():
        parser.error('APK missing. Run python3 android/build.py first.')
    state = capture('get-state').decode().strip()
    if state != 'device':
        parser.error('Device is not ready: ' + state)
    abi = capture('shell', 'getprop', 'ro.product.cpu.abilist').decode().strip()
    if 'arm64-v8a' not in abi.split(','):
        parser.error('The selected device does not advertise arm64-v8a: ' + abi)
    args.output.mkdir(parents=True, exist_ok=True)
    properties = capture('shell', 'getprop')
    (args.output / 'device-properties.txt').write_bytes(properties)
    run('install', '--no-incremental', '-r', str(apk))
    run('shell', 'am', 'force-stop', PACKAGE)
    # Remove only this app's previous report so a stale success cannot satisfy this run.
    run('shell', 'run-as', PACKAGE, 'rm', '-f', 'files/last-report.txt')
    if rom is not None:
        run('shell', f"run-as {PACKAGE} sh -c 'cat > files/AVPBooter.vmapple2.bin'", input=rom)
    extra = 'run_rom' if rom is not None else 'run_tests'
    run('shell', 'am', 'start', '-W', '-n', PACKAGE + '/.MainActivity', '--ez', extra, 'true')
    expected = b'=== henge_lab [rom,' if rom is not None else b'SUITES='
    deadline = time.monotonic() + (80 if rom is not None else 330)
    report = b''
    while time.monotonic() < deadline:
        result = subprocess.run(base + ['exec-out', 'run-as', PACKAGE, 'cat', 'files/last-report.txt'],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if result.returncode == 0 and expected in result.stdout:
            report = result.stdout
            break
        time.sleep(1)
    if not report:
        logs = capture('logcat', '-d', '-t', '200', 'AndroidRuntime:E', '*:S')
        (args.output / 'timeout-logcat.txt').write_bytes(logs)
        raise SystemExit('No completed report before deadline; see timeout-logcat.txt. This is not a pass.')
    name = 'rom' if rom is not None else 'cpu'
    (args.output / (name + '-report.txt')).write_bytes(report)
    # The report can finish while Android's launch splash is still visible.
    # Wait for the UI to settle before capturing the result screen.
    try:
        ui = subprocess.run(base + ['shell', 'uiautomator', 'dump', '/data/local/tmp/henge-lab-ui.xml'],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
        if ui.returncode:
            (args.output / (name + '-ui-warning.txt')).write_bytes(ui.stdout)
    except subprocess.TimeoutExpired:
        (args.output / (name + '-ui-warning.txt')).write_text('UI idle wait timed out; screenshot may be transitional.\n')
    (args.output / (name + '-screen.png')).write_bytes(capture('exec-out', 'screencap', '-p'))
    print(report.decode('utf-8', errors='replace'))
    print('Saved: ' + str(args.output.resolve()))
    passed = b'EXIT_CODE=0' in report if rom is not None else b'SUITES=5 FAILED=0' in report
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
