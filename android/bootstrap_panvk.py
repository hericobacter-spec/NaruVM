#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepare pinned PanVK source, Mac generators and isolated Linux host compilers.

Requires Apple Silicon macOS, Python 3.12+, Docker and a running Docker context.
Creates only a named project container; never changes device/system drivers.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / 'android/panvk/host-tools.lock.json').read_text())
SOURCE = ROOT / '.tools/mali-panvk-android'


def run(*command, **kwargs):
    print('+', *map(str, command), flush=True)
    return subprocess.run(list(map(str, command)), check=True, **kwargs)


def container_name(root=ROOT):
    return 'henge-pub-panvk-' + hashlib.sha256(str(root).encode()).hexdigest()[:12]


def prepare_source():
    pin = LOCK['source']
    patch = ROOT / pin['patch']
    if hashlib.sha256(patch.read_bytes()).hexdigest() != pin['patch_sha256']:
        raise ValueError('Pinned PanVK patch changed; source preserved')
    if not SOURCE.exists():
        SOURCE.parent.mkdir(parents=True, exist_ok=True)
        run('git', 'init', SOURCE)
        run('git', '-C', SOURCE, 'fetch', '--depth=1', pin['url'], pin['commit'])
        run('git', '-C', SOURCE, 'checkout', '--detach', pin['commit'])
        run('git', '-C', SOURCE, 'apply', '--check', patch)
        run('git', '-C', SOURCE, 'apply', patch)
    head = subprocess.check_output(['git', '-C', SOURCE, 'rev-parse', 'HEAD'], text=True).strip()
    actual = subprocess.check_output(['git', '-C', SOURCE, 'diff', '--binary', 'HEAD'])
    if head != pin['commit'] or hashlib.sha256(actual).hexdigest() != pin['patch_sha256']:
        raise ValueError('Existing PanVK source differs from pin/patch; no overwrite')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--docker-context', default='colima-henge-gpu')
    parser.add_argument('--container')
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        raise SystemExit('This helper currently requires an Apple Silicon Mac.')
    if not 1 <= args.jobs <= 8:
        raise SystemExit('Use 1–8 build jobs.')
    docker = shutil.which('docker')
    if not docker:
        raise SystemExit('Install Docker and start the chosen context first.')
    prepare_source()
    build = ROOT / 'android/build'
    build.mkdir(parents=True, exist_ok=True)
    venv = build / 'panvk-build-venv'
    if not venv.exists(): run(sys.executable, '-m', 'venv', venv)
    run(venv / 'bin/python', '-m', 'pip', 'install', '--require-hashes', '-r', ROOT / 'android/panvk/macos-build-requirements.lock')
    files = [ROOT / ('android/panvk/' + name) for name in ['linux-build.Dockerfile', 'linux-build-requirements.lock']]
    key = hashlib.sha256(b''.join(path.read_bytes() for path in files)).hexdigest()[:16]
    image = 'henge-panvk-pub:' + key
    dc = [docker, '--context', args.docker_context]
    run(*dc, 'build', '--platform', LOCK['platform'], '--file', files[0], '--tag', image, ROOT / 'android/panvk')
    name = args.container or container_name()
    ownership = hashlib.sha256(str(ROOT).encode()).hexdigest()
    existing = subprocess.run(dc + ['inspect', name], capture_output=True, text=True)
    image_id = subprocess.check_output(dc + ['image', 'inspect', image, '--format', '{{.Id}}'], text=True).strip()
    if existing.returncode == 0:
        data = json.loads(existing.stdout)[0]
        if data['Config'].get('Labels', {}).get('dev.henge.source-root') != ownership or data['Image'] != image_id:
            raise ValueError('Container name belongs to another build; choose a new --container')
        if not data['State']['Running']: run(*dc, 'start', name)
    else:
        run(*dc, 'run', '--detach', '--name', name, '--label', 'dev.henge.source-root=' + ownership,
            '--mount', f'type=bind,source={ROOT},target={ROOT}', image, 'sleep', 'infinity')
    host = SOURCE / 'build-host'
    if not (host / 'build.ninja').exists():
        run(*dc, 'exec', '-w', SOURCE, name, 'meson', 'setup', host,
            '-Dbuildtype=release', '-Dmesa-clc=enabled', '-Dprecomp-compiler=enabled',
            '-Dvulkan-drivers=panfrost', '-Dgallium-drivers=', '-Dplatforms=', '-Dllvm=enabled',
            '-Dgles1=disabled', '-Dgles2=disabled', '-Degl=disabled', '-Dglx=disabled', '-Dgbm=disabled',
            '-Dtools=', '-Dvideo-codecs=', '-Dallow-fallback-for=libdrm',
            '--force-fallback-for=libdrm,expat,zlib', '-Dlibdrm:default_library=static')
    run(*dc, 'exec', '-w', SOURCE, name, 'ninja', '-j' + str(args.jobs), '-C', host,
        'src/compiler/clc/mesa_clc', 'src/compiler/spirv/vtn_bindgen2', 'src/panfrost/clc/panfrost_compile')
    packages = subprocess.check_output(dc + ['exec', name, 'dpkg-query', '-W'], text=True)
    (build / 'panvk-host-environment.json').write_text(json.dumps({
        'label': 'observed', 'image_id': image_id, 'container': name, 'context': args.docker_context,
        'lock_sha256': hashlib.sha256((ROOT / 'android/panvk/host-tools.lock.json').read_bytes()).hexdigest(),
        'installed_packages': packages.splitlines(), 'fresh_machine': False}, indent=2) + '\n')
    print('PANVK_HOST_TOOLS_RESULT failures=0 container=' + name)


if __name__ == '__main__':
    main()
