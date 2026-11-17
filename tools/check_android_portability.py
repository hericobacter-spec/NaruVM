#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Disk-free host policy checks. Requires C compiler, Python 3.11+ and JDK 21.

No ADB, downloads, guest files or driver execution. Not a device-support test.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    cc = shutil.which(os.environ.get('CC', 'cc'))
    jdk = Path(os.environ.get('JAVA_HOME', ROOT / '.tools/jdk-21/Contents/Home'))
    javac = str(jdk / 'bin/javac') if (jdk / 'bin/javac').is_file() else shutil.which('javac')
    java = str(jdk / 'bin/java') if (jdk / 'bin/java').is_file() else shutil.which('java')
    if not cc or not javac or not java:
        raise SystemExit('Install a C compiler and JDK 21, then set CC/JAVA_HOME as needed.')
    subprocess.run([sys.executable, ROOT / "android/installer_sources.py"], check=True)
    with tempfile.TemporaryDirectory(prefix='henge-policy-') as temporary:
        work = Path(temporary)
        subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                        ROOT / 'android/native/test_platform_compat.c', ROOT / 'android/native/platform_compat.c',
                        '-o', work / 'platform-test'], check=True)
        subprocess.run([work / 'platform-test'], check=True)
        subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                        ROOT / 'android/native/test_cpu_caps.c', '-o', work / 'cpu-caps-test'], check=True)
        subprocess.run([work / 'cpu-caps-test'], check=True)
        subprocess.run([cc, '-std=c11', '-D_GNU_SOURCE', '-Wall', '-Wextra', '-Werror',
                        ROOT / 'android/native/test_installed_guard.c', '-o', work / 'installed-guard'], check=True)
        subprocess.run([work / 'installed-guard'], check=True)
        sources = [ROOT / 'android/src/dev/henge/research' / (name + '.java') for name in ['VmConfig', 'GpuConfig', 'GpuEnvironment', 'MacInput', 'MacRelease', 'VerifiedDownload', 'ManagedMachine']]
        sources += [ROOT / 'android/tests' / (name + '.java') for name in ['VmConfigTest', 'GpuConfigTest', 'GpuEnvironmentTest', 'MacInputTest', 'InstallerTest']]
        subprocess.run([javac, '--release', '8', '-encoding', 'UTF-8', '-d', work, *sources], check=True)
        for test in ['VmConfigTest', 'GpuConfigTest', 'GpuEnvironmentTest', 'MacInputTest', 'InstallerTest']:
            subprocess.run([java, '-cp', work, 'dev.henge.research.' + test], check=True)
        subprocess.run([sys.executable, ROOT / 'android/tests/test_turnip_bundle.py'], check=True)
    print('PORTABILITY_HOST_RESULT failures=0 scope=policy-and-packaging device_tested=0')


if __name__ == '__main__':
    main()
