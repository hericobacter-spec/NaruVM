#!/usr/bin/env python3
"""Build experimental PanVK source using Linux host tools and the Mac Android NDK.

Use --bootstrap to prepare pinned source and a dedicated Linux host-tools
container. Without it, the default remains colima-henge-gpu / henge-panvk-build.
This does not install or select a guest backend.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / '.tools/mali-panvk-android'
VENV = ROOT / 'android/build/panvk-build-venv/bin'
BUILD = SOURCE / 'build-android'
PIN = '58f2c55854bd48b18df756edb11f187af9cd2fde'

def write_changed(path, contents):
    if not path.exists() or path.read_text() != contents:
        path.write_text(contents)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--asan', action='store_true', help='Build an isolated AddressSanitizer diagnostic ICD')
    parser.add_argument('--bootstrap', action='store_true', help='Prepare pinned source, venv and Linux host compilers first')
    parser.add_argument('--docker-context', default='colima-henge-gpu')
    parser.add_argument('--host-container')
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 8: raise SystemExit('Use 1–8 build jobs.')
    container = args.host_container or 'henge-panvk-build'
    if args.bootstrap:
        from bootstrap_panvk import container_name
        container = args.host_container or container_name()
        subprocess.run([os.sys.executable, ROOT / 'android/bootstrap_panvk.py',
            '--docker-context', args.docker_context, '--container', container, '--jobs', str(args.jobs)], check=True)
    docker = shutil.which('docker')
    if not docker: raise SystemExit('Docker command unavailable.')
    lock = json.loads((ROOT / 'android/panvk/host-tools.lock.json').read_text())
    build = SOURCE / ('build-android-asan' if args.asan else 'build-android')
    head = subprocess.check_output(['git', '-C', SOURCE, 'rev-parse', 'HEAD'], text=True).strip()
    if head != PIN:
        raise SystemExit('PanVK source HEAD differs from the research pin.')
    patch = subprocess.check_output(['git', '-C', SOURCE, 'diff', '--binary', 'HEAD'])
    if hashlib.sha256(patch).hexdigest() != lock['source']['patch_sha256']:
        raise SystemExit('PanVK source differs from the recorded patch; source preserved.')
    wrappers = ROOT / 'android/build/panvk-host-tools'
    wrappers.mkdir(exist_ok=True)
    tools = {'mesa_clc': 'src/compiler/clc/mesa_clc',
             'vtn_bindgen2': 'src/compiler/spirv/vtn_bindgen2',
             'panfrost_compile': 'src/panfrost/clc/panfrost_compile'}
    for name, relative in tools.items():
        tool = SOURCE / 'build-host' / relative
        if not tool.is_file():
            raise SystemExit('Build the pinned Linux host tools first: ' + str(tool))
        path = wrappers / name
        write_changed(path, '#!/bin/sh\nexec ' + shlex.quote(docker) + ' --context ' + shlex.quote(args.docker_context) +
                        ' exec -w "$PWD" ' + shlex.quote(container) + ' ' + shlex.quote(str(tool)) + ' "$@"\n')
        path.chmod(0o755)
    build.mkdir(exist_ok=True)
    empty = build / 'pkgconfig-empty'
    empty.mkdir(exist_ok=True)
    llvm = ROOT / '.tools/android-sdk/ndk/30.0.16248370/toolchains/llvm/prebuilt/darwin-x86_64/bin'
    cross = build / 'cross.ini'
    write_changed(cross, '[binaries]\n' + ''.join(f"{k} = '{llvm / v}'\n" for k, v in {
        'c': 'aarch64-linux-android31-clang', 'cpp': 'aarch64-linux-android31-clang++',
        'ar': 'llvm-ar', 'strip': 'llvm-strip'}.items()) +
        "pkg-config = 'pkg-config'\n[properties]\n" +
        f"pkg_config_libdir = ['{empty}']\ncpp_link_args = ['-static-libstdc++']\n" +
        "[host_machine]\nsystem = 'android'\ncpu_family = 'aarch64'\ncpu = 'aarch64'\nendian = 'little'\n")
    env = dict(os.environ, PATH=str(wrappers) + ':' + str(VENV) + ':' + os.environ['PATH'])
    env.pop('PKG_CONFIG_PATH', None)
    if not (build / 'build.ninja').exists():
        subprocess.run([str(VENV / 'meson'), 'setup', str(build), '--cross-file', str(cross),
            '-Dbuildtype=debugoptimized', '-Db_ndebug=true', '-Dplatforms=android',
            '-Db_sanitize=' + ('address' if args.asan else 'none'),
            '-Dplatform-sdk-version=31', '-Dandroid-stub=true', '-Dandroid-strict=false',
            '-Dandroid-libbacktrace=disabled', '-Dvulkan-drivers=panfrost', '-Dgallium-drivers=',
            '-Dvulkan-layers=', '-Dtools=', '-Dvideo-codecs=', '-Dxclipse-gralloc-deps=',
            '-Dmesa-clc=system', '-Dprecomp-compiler=system', '-Dllvm=disabled',
            '-Dzstd=disabled', '-Dlmsensors=disabled', '-Dperfetto=false', '-Dglx=disabled',
            '-Dgbm=disabled', '-Degl=disabled', '-Dgles1=disabled', '-Dgles2=disabled',
            '-Dallow-fallback-for=libdrm,perfetto', '--force-fallback-for=expat,libdrm,zlib',
            '-Dlibdrm:default_library=static', '-Dexpat:default_library=static',
            '-Dzlib:default_library=static'], cwd=SOURCE, env=env, check=True)
    subprocess.run([str(VENV / 'ninja'), '-j' + str(args.jobs), '-C', str(build),
                    'src/panfrost/vulkan/libvulkan_panfrost.so'], cwd=SOURCE, env=env, check=True)
    output = build / 'src/panfrost/vulkan/libvulkan_panfrost.so'
    patch = subprocess.check_output(['git', '-C', SOURCE, 'diff', '--binary', 'HEAD'])
    print('Built experimental source ICD:', output)
    record = 'panvk-source-asan-build.json' if args.asan else 'panvk-source-build.json'
    (ROOT / 'android/build' / record).write_text(json.dumps({
        'source_commit': head, 'output': str(output),
        'output_sha256': hashlib.sha256(output.read_bytes()).hexdigest(),
        'patch_sha256': hashlib.sha256(patch).hexdigest(),
        'host_container': container,
        'host_context': args.docker_context, 'gralloc_backend': False,
        'guest_backend_selected': False, 'address_sanitizer': args.asan}, indent=2) + '\n')

if __name__ == '__main__':
    main()
