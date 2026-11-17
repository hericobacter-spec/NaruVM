#!/usr/bin/env python3
"""Cross-process shared-frame and Android Bitmap fixtures; no guest disk is opened."""
import argparse
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = 'dev.henge.research'

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--serial', required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    base = [str(ROOT/'.tools/android-sdk/platform-tools/adb'), '-s', a.serial]
    def adb(*args, **kw):
        return subprocess.run(base+list(args), check=True, stdout=subprocess.PIPE, **kw).stdout
    # Do not interrupt a running VM or another native experiment.
    listing = adb('shell', 'ps', '-A', '-o', 'NAME').decode()
    if re.search(r'libhenge_(lab|gpu|reims_probe|clone|frames_test)\.so', listing):
        raise SystemExit('Native worker is active; wait for normal completion before this fixture.')
    package = adb('shell', 'dumpsys', 'package', PACKAGE).decode()
    native = re.search(r'legacyNativeLibraryDir=(\S+)', package)
    if not native: raise SystemExit('Installed native directory missing')
    a.output.mkdir(parents=True, exist_ok=True)
    adb('shell', 'run-as', PACKAGE, 'mkdir', '-p', 'files')
    output = adb('shell', 'run-as', PACKAGE, native[1]+'/arm64/libhenge_frames_test.so',
                 '/data/user/0/'+PACKAGE+'/files')
    (a.output/'cross-process.txt').write_bytes(output)
    if b'FRAME_TRANSPORT_RESULT failures=0' not in output: raise SystemExit('Native fixture failed')
    adb('shell', 'am', 'force-stop', PACKAGE)
    adb('shell', 'run-as', PACKAGE, 'rm', '-f', 'files/last-report.txt')
    adb('shell', 'am', 'start', '-W', '-n', PACKAGE+'/.MainActivity', '--ez', 'run_frame_test', 'true')
    deadline = time.monotonic()+20
    while time.monotonic() < deadline:
        r = subprocess.run(base+['exec-out','run-as',PACKAGE,'cat','files/last-report.txt'], capture_output=True)
        if r.returncode == 0 and b'FRAME_TRANSPORT_BITMAP_RESULT' in r.stdout:
            (a.output/'bitmap.txt').write_bytes(r.stdout)
            print(output.decode()+r.stdout.decode())
            if b'FRAME_TRANSPORT_BITMAP_RESULT failures=0' not in r.stdout: raise SystemExit('Bitmap fixture failed')
            return
        time.sleep(.5)
    raise SystemExit('No Bitmap completion marker; not a pass')

if __name__ == '__main__': main()
