#!/usr/bin/env python3
"""Build authored Metal -> Android Vulkan checks, then stage the isolated ELF worker."""
import json
import os
from pathlib import Path
import shutil
import subprocess
from gpu_source import verify_source

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT/'android/gpu/dependencies.json').read_text())

def run(*args, **kw):
    print('+', *args, flush=True)
    subprocess.run(list(map(str,args)),cwd=ROOT,check=True,**kw)

def main():
    verify_source()
    for name in ['check', 'runtime-sampler']:
        run('xcrun','-sdk','macosx','metal','-std=metal3.1','-o',ROOT/f'android/gpu/{name}.metallib',ROOT/f'android/gpu/{name}.metal')
    run('glslangValidator', '-V', '--target-env', 'vulkan1.2', '-o', ROOT/'android/gpu/sampler-border.spv', ROOT/'android/gpu/sampler-border.comp')
    run('glslangValidator', '-V', '--target-env', 'vulkan1.2', '-o', ROOT/'android/panvk/alias.spv', ROOT/'android/panvk/alias.comp')
    run('glslangValidator', '-V', '--target-env', 'vulkan1.2', '-o', ROOT/'android/panvk/tls.spv', ROOT/'android/panvk/tls.comp')
    sdk = Path(os.environ.get('ANDROID_SDK_ROOT',ROOT/'.tools/android-sdk'))
    linker = sdk/'ndk/30.0.16248370/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android28-clang'
    env = dict(os.environ, RUSTUP_HOME=str(ROOT/'.tools/rustup'), CARGO_HOME=str(ROOT/'.tools/cargo'),
               RUSTUP_TOOLCHAIN=LOCK['rust'], CARGO_INCREMENTAL='0',
               CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER=str(linker),
               RUSTFLAGS='-C link-arg=-Wl,-z,max-page-size=16384')
    run(ROOT/'.tools/cargo/bin/cargo','build','--locked','--release','--manifest-path',ROOT/'android/gpu/Cargo.toml',
        '--target','aarch64-linux-android',env=env)
    destination = ROOT/'android/build/native/bin'
    destination.mkdir(parents=True,exist_ok=True)
    for name in ['henge_gpu', 'henge_sampler_check', 'henge_runtime_sampler_check']:
        output = destination/('lib' + name + '.so')
        shutil.copy2(ROOT/'android/gpu/target/aarch64-linux-android/release'/name, output)
        print('GPU worker:', output)

if __name__ == '__main__': main()
