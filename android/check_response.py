#!/usr/bin/env python3
"""Authored input/backpressure and pixel correctness checks; no APK/device/guest changes."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import sys
import shutil

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jdk', type=Path, default=Path(os.environ.get('JAVA_HOME', ROOT/'.tools/jdk-21/Contents/Home')))
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    args = parser.parse_args()
    javac = str(args.jdk/'bin/javac') if (args.jdk/'bin/javac').is_file() else shutil.which('javac')
    java = str(args.jdk/'bin/java') if (args.jdk/'bin/java').is_file() else shutil.which('java')
    if not javac or not java:
        parser.error('JDK required: set JAVA_HOME/--jdk or put javac and java on PATH')
    with tempfile.TemporaryDirectory(prefix='naruvm-response-') as temporary:
        work = Path(temporary)
        def run(*command):
            subprocess.run(list(map(str, command)), cwd=ROOT, check=True)
        run(javac, '--release', '8', '-d', work,
            ROOT/'android/src/dev/henge/research/ControlQueue.java',
            ROOT/'android/src/dev/henge/research/MacInput.java',
            ROOT/'android/tests/ControlQueueTest.java', ROOT/'android/tests/MacInputTest.java')
        for test in ('ControlQueueTest', 'MacInputTest'):
            run(java, '-cp', work, 'dev.henge.research.'+test)
        for test in ('input_queue', 'pixel_convert', 'vm_control'):
            executable = work/test
            run(args.cc, '-O2', '-Wall', '-Wextra', '-Idevices', '-Icore', '-Ihost',
                ROOT/f'android/native/test_{test}.c', '-o', executable)
            run(executable)
        keyboard = work/'keyboard_queue'
        linker = '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections'
        run(args.cc, '-O2', '-D_GNU_SOURCE', '-ffunction-sections', '-fdata-sections',
            '-Idevices', '-Icore', '-Ihost', ROOT/'android/native/test_keyboard_queue.c',
            ROOT/'devices/xhci.c', linker, '-pthread', '-o', keyboard)
        run(keyboard)


if __name__ == '__main__':
    main()
