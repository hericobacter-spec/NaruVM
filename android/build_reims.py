#!/usr/bin/env python3
"""Build the pinned Android C API research port and its isolated public-API probe."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from gpu_source import SOURCE as METAL_SOURCE, verify_source as verify_metal_source

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT/'android/reims/dependencies.json').read_text())
SOURCE = Path(os.environ.get('HENGE_REIMS_SOURCE', Path.home()/'.cache/henge-reims-android'))
TARGET = Path(os.environ.get('HENGE_REIMS_TARGET', Path.home()/'.cache/henge-reims-android-target'))

def run(*args, **kw):
    print('+', *args, flush=True)
    subprocess.run(list(map(str, args)), check=True, **kw)

def verify_source():
    verify_metal_source()
    patch = ROOT/'android/reims/android.patch'
    if hashlib.sha256(patch.read_bytes()).hexdigest() != LOCK['patch_sha256']:
        raise SystemExit('Reims port patch checksum mismatch.')
    if not SOURCE.is_dir():
        raise SystemExit('Run python3 android/fetch_gpu_deps.py --reims first.')
    head = subprocess.check_output(['git','rev-parse','HEAD'],cwd=SOURCE,text=True).strip()
    if head != LOCK['commit']: raise SystemExit('Reims base differs from the recorded pin; source preserved.')
    changed = subprocess.check_output(['git','diff','--name-only','HEAD'],cwd=SOURCE,text=True).splitlines()
    untracked = subprocess.check_output(['git','ls-files','--others','--exclude-standard'],cwd=SOURCE,text=True).splitlines()
    if set(changed+untracked) != set(LOCK['port_files']):
        raise SystemExit('Reims working tree differs from the recorded research patch; source preserved.')
    for name, digest in LOCK['port_files'].items():
        if hashlib.sha256((SOURCE/name).read_bytes()).hexdigest() != digest:
            raise SystemExit('Reims source checksum mismatch: '+name)

def cargo_patch_args():
    # Override only the recorded upstream git source, with the separately verified local port.
    args = []
    for crate in ['metal2vulkan', 'metal2vulkan-air', 'metal2vulkan-runtime']:
        args += ['--config', 'patch."https://github.com/steelbrain/metal2vulkan".' + crate +
                 '.path=' + json.dumps(str(METAL_SOURCE/'crates'/crate))]
    return args

def main():
    verify_source()
    ndk = Path(os.environ.get('ANDROID_SDK_ROOT', ROOT/'.tools/android-sdk'))/'ndk/30.0.16248370'
    llvm = ndk/'toolchains/llvm/prebuilt/darwin-x86_64/bin'
    env = dict(os.environ, RUSTUP_HOME=str(ROOT/'.tools/rustup'), CARGO_HOME=str(ROOT/'.tools/cargo'),
        RUSTUP_TOOLCHAIN='1.98.1', RUSTUP_AUTO_INSTALL='0', CARGO_INCREMENTAL='0',
        CARGO_TARGET_DIR=str(TARGET), CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER=str(llvm/'aarch64-linux-android28-clang'),
        RUSTFLAGS='-C target-feature=+lse,+rcpc -C link-arg=-Wl,-z,max-page-size=16384',
        CARGO_PROFILE_RELEASE_OPT_LEVEL='3', CARGO_PROFILE_RELEASE_CODEGEN_UNITS='16')
    env['CARGO_ENCODED_RUSTFLAGS'] = '\x1f'.join(['-C', 'target-feature=+lse,+rcpc',
        '-C', 'link-arg=-Wl,-z,max-page-size=16384',
        '--remap-path-prefix=' + str(Path.home()) + '=/src/build'])
    env.pop('RUSTFLAGS', None)
    env['PATH'] = str(ROOT/'.tools/cargo/bin')+os.pathsep+env['PATH']
    # Follow the dependency's worktree environment; arguments remain positional shell data.
    run('/bin/bash','-c','. scripts/internal/dev-env.sh; exec "$@"','reims-build',
        ROOT/'.tools/cargo/bin/cargo','build','--locked','--release','-p','reims-vgpu-c',
        '--no-default-features','--features','vulkan','--target','aarch64-linux-android',
        *cargo_patch_args(),cwd=SOURCE,env=env)
    output = ROOT/'android/build/native/bin'; output.mkdir(parents=True,exist_ok=True)
    library = output/'libreims_vgpu_c.so'
    shutil.copy2(TARGET/'aarch64-linux-android/release/libreims_vgpu_c.so',library)
    run(llvm/'llvm-strip','--strip-debug',library)
    run(llvm/'aarch64-linux-android28-clang','-O2','-ffixed-x18','-Wl,-z,max-page-size=16384',
        '-I',SOURCE/'crates/reims-vgpu-c/include',ROOT/'android/native/reims_probe.c','-ldl',
        '-o',output/'libhenge_reims_probe.so')

if __name__ == '__main__': main()
