#!/usr/bin/env python3
"""Stage the separately named source-built USER_BUFFER diagnostic ICD."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--asan', action='store_true')
    args = parser.parse_args()
    source = ROOT / '.tools/mali-panvk-android'
    record_name = 'panvk-source-asan-build.json' if args.asan else 'panvk-source-build.json'
    record = json.loads((ROOT / 'android/build' / record_name).read_text())
    if bool(record.get('address_sanitizer', False)) != args.asan:
        raise SystemExit('Driver diagnostic profile does not match staging request.')
    head = subprocess.check_output(['git', '-C', source, 'rev-parse', 'HEAD'], text=True).strip()
    patch = subprocess.check_output(['git', '-C', source, 'diff', '--binary', 'HEAD'])
    driver = Path(record['output'])
    if (head != record['source_commit'] or digest(driver) != record['output_sha256'] or
        hashlib.sha256(patch).hexdigest() != record['patch_sha256']):
        raise SystemExit('Source or binary changed since the recorded build; rebuild before staging.')
    output = ROOT / 'android/build/native/bin'
    driver_name = 'libhenge_panvk_userptr_driver.so'
    loader_name = 'libhenge_panvk_userptr_loader.so'
    shutil.copy2(driver, output / driver_name)
    sdk = Path(os.environ.get('ANDROID_SDK_ROOT', ROOT / '.tools/android-sdk'))
    clang = sdk / 'ndk/30.0.16248370/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android31-clang'
    asan_name = 'libclang_rt.asan-aarch64-android.so'
    if args.asan:
        runtimes = list((clang.parent.parent / 'lib/clang').glob('*/lib/linux/' + asan_name))
        if len(runtimes) != 1:
            raise SystemExit('Expected exactly one NDK AddressSanitizer runtime.')
        shutil.copy2(runtimes[0], output / asan_name)
    else:
        (output / asan_name).unlink(missing_ok=True)
    subprocess.run([str(clang.parent / 'llvm-strip'), '--strip-debug', str(output / driver_name)], check=True)
    subprocess.run([str(clang), '-O2', '-Wall', '-Wextra', '-shared', '-fPIC', '-ffixed-x18',
        '-Wl,-z,max-page-size=16384', f'-DHENGE_PANVK_LIBRARY="{driver_name}"',
        '-DHENGE_PANVK_SOURCE="58f2c558-local-userptr"',
        str(ROOT / 'android/native/panvk_loader.c'), '-ldl', '-o', str(output / loader_name)], check=True)
    record.update(libraries=[driver_name, loader_name], published_binary=False,
                  staged_driver_sha256=digest(output / driver_name), stripped_debug=True,
                  guest_backend=False, scope='Owned Vulkan fixture only; no guest memory.')
    (ROOT / 'android/build/panvk-userptr-libraries.json').write_text(json.dumps(record, indent=2) + '\n')

if __name__ == '__main__':
    main()
