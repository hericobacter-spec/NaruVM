#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build authored Android input-dispatch instrumentation; no install or guest run.

Build android/build.py first. The target app and this test must use the same local
debug key. Testing restarts the target app process: use only while its VM is off.
"""
import os
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get('ANDROID_SDK_ROOT', ROOT / '.tools/android-sdk'))
JDK = Path(os.environ.get('JAVA_HOME', ROOT / '.tools/jdk-21/Contents/Home'))


def main():
    work = ROOT / 'android/build/input-instrumentation'
    classes, dex = work / 'classes', work / 'dex'
    classes.mkdir(parents=True, exist_ok=True); dex.mkdir(exist_ok=True)
    android = SDK / 'platforms/android-36/android.jar'
    tools = SDK / 'build-tools/36.0.0'
    env = dict(os.environ, JAVA_HOME=str(JDK), PATH=str(JDK / 'bin') + os.pathsep + os.environ['PATH'])
    def run(*cmd): subprocess.run(list(map(str, cmd)), check=True, env=env)
    run(JDK / 'bin/javac', '--release', '8', '-classpath', str(android) + os.pathsep + str(ROOT / 'android/build/classes'),
        '-d', classes, ROOT / 'android/tests/HardwareInputInstrumentation.java', ROOT / 'android/tests/InstallerUiInstrumentation.java')
    run(tools / 'd8', '--min-api', '28', '--lib', android, '--output', dex, *classes.rglob('*.class'))
    unsigned = work / 'unsigned.apk'
    run(tools / 'aapt2', 'link', '-I', android, '--manifest', ROOT / 'android/tests/AndroidManifest-input.xml', '-o', unsigned)
    with zipfile.ZipFile(unsigned, 'a', compression=zipfile.ZIP_DEFLATED) as archive:
        archive.write(dex / 'classes.dex', 'classes.dex')
    aligned = work / 'aligned.apk'; run(tools / 'zipalign', '-f', '4', unsigned, aligned)
    output = ROOT / 'android/artifacts/NaruVM-input-test.apk'
    run(tools / 'apksigner', 'sign', '--ks', ROOT / '.tools/research-debug.keystore', '--ks-pass', 'pass:android',
        '--key-pass', 'pass:android', '--out', output, aligned)
    run(tools / 'apksigner', 'verify', output)
    print('INPUT_TEST_APK ' + str(output))


if __name__ == '__main__': main()
