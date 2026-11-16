#!/usr/bin/env python3
"""Build the isolated, authored host-copy experiment; never replace the macOS GPU library."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / '.tools/metal2vulkan-android'
LOCK = json.loads((ROOT / 'android/host-copy/dependencies.json').read_text())


def run(*args, **kwargs):
    print('+', *args, flush=True)
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def main():
    original = ROOT / '.tools/metal2vulkan'
    patch = ROOT / 'android/host-copy/runtime.patch'
    if hashlib.sha256(patch.read_bytes()).hexdigest() != LOCK['patch_sha256']:
        raise SystemExit('Host-copy patch checksum mismatch; sources preserved.')
    if not SOURCE.exists():
        run('bash', original / 'scripts/lean-tree', 'worktree', SOURCE,
            'steelbrain/henge-android-host-copy', LOCK['base_commit'], cwd=original)
        run('git', 'apply', '--check', patch, cwd=SOURCE)
        run('git', 'apply', patch, cwd=SOURCE)
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=SOURCE, text=True).strip()
    files = subprocess.check_output(['git', 'diff', '--name-only', 'HEAD'], cwd=SOURCE, text=True).splitlines()
    untracked = subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard'], cwd=SOURCE, text=True).splitlines()
    if head != LOCK['base_commit'] or set(files + untracked) != set(LOCK['files']):
        raise SystemExit('Host-copy source differs from the recorded experiment; sources preserved.')
    for name, expected in LOCK['files'].items():
        if hashlib.sha256((SOURCE / name).read_bytes()).hexdigest() != expected:
            raise SystemExit('Host-copy source checksum mismatch: ' + name)
    sdk = Path(os.environ.get('ANDROID_SDK_ROOT', ROOT / '.tools/android-sdk'))
    linker = sdk / 'ndk/30.0.16248370/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android28-clang'
    env = dict(os.environ, RUSTUP_HOME=str(ROOT / '.tools/rustup'),
               CARGO_HOME=str(ROOT / '.tools/cargo'), RUSTUP_TOOLCHAIN='1.98.1',
               CARGO_INCREMENTAL='0', CARGO_TARGET_DIR=str(ROOT / 'android/gpu/target'),
               CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER=str(linker),
               RUSTFLAGS='-C link-arg=-Wl,-z,max-page-size=16384')
    run(ROOT / '.tools/cargo/bin/cargo', 'build', '--locked', '--offline', '--release',
        '--manifest-path', ROOT / 'android/host-copy/Cargo.toml',
        '--target', 'aarch64-linux-android', cwd=ROOT, env=env)
    output = ROOT / 'android/build/native/bin/libhenge_host_copy_probe.so'
    shutil.copy2(ROOT / 'android/gpu/target/aarch64-linux-android/release/henge-host-copy-probe', output)
    print('Isolated probe:', output)


if __name__ == '__main__':
    main()
