#!/usr/bin/env python3
"""Stage a checksum-pinned private ICD and build its explicit diagnostic loader."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / 'android/panvk/dependencies.json').read_text())

def main():
    package = ROOT / '.tools/downloads' / LOCK['filename']
    package.parent.mkdir(parents=True, exist_ok=True)
    if not package.exists():
        partial = package.with_suffix('.part')
        with urllib.request.urlopen(LOCK['url'], timeout=30) as response, partial.open('wb') as out:
            shutil.copyfileobj(response, out)
        partial.rename(package)
    if hashlib.sha256(package.read_bytes()).hexdigest() != LOCK['sha256']:
        raise SystemExit('Pinned PanVK release checksum differs; refusing to stage it.')
    folder = ROOT / 'android/build/panvk-release-v1.6.0'
    folder.mkdir(exist_ok=True)
    with zipfile.ZipFile(package) as archive:
        for name in ['libvulkan_panfrost.so', 'NOTICE.txt', 'meta.json']:
            (folder / name).write_bytes(archive.read(name))
    output = ROOT / 'android/build/native/bin'
    output.mkdir(parents=True, exist_ok=True)
    shutil.copy2(folder / 'libvulkan_panfrost.so', output / 'libhenge_panvk_driver.so')
    sdk = Path(os.environ.get('ANDROID_SDK_ROOT', ROOT / '.tools/android-sdk'))
    clang = sdk / 'ndk/30.0.16248370/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android31-clang'
    subprocess.run([str(clang), '-O2', '-Wall', '-Wextra', '-shared', '-fPIC', '-ffixed-x18',
                    '-Wl,-z,max-page-size=16384', str(ROOT / 'android/native/panvk_loader.c'),
                    '-ldl', '-o', str(output / 'libhenge_panvk_loader.so')], check=True)
    (ROOT / 'android/build/panvk-libraries.json').write_text(json.dumps({
        'libraries': ['libhenge_panvk_driver.so', 'libhenge_panvk_loader.so'],
        'archive_sha256': LOCK['sha256'], 'driver_sha256': hashlib.sha256(
            (output / 'libhenge_panvk_driver.so').read_bytes()).hexdigest(),
        'notice': str(folder / 'NOTICE.txt'), 'published_binary': True,
        'guest_backend': False}, indent=2) + '\n')
    print('Staged private PanVK authored-shader diagnostic; no guest backend change.')

if __name__ == '__main__':
    main()
