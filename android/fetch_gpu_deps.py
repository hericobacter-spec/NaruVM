#!/usr/bin/env python3
"""Prepare the pinned, optional GPU diagnostic dependencies in .tools (arm64 Mac)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT/'android/gpu/dependencies.json').read_text())

def run(*args, **kw):
    print('+', *args, flush=True)
    return subprocess.run(list(map(str,args)), check=True, **kw)

def fetch_pinned(folder, url, commit):
    # New checkouts need one pinned revision, not unrelated moving branch history.
    run('git', 'init', folder)
    run('git', '-C', folder, 'fetch', '--depth=1', url, commit)
    run('git', '-C', folder, 'checkout', '--detach', commit)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reims', action='store_true', help='Also prepare the pinned Android virtual-GPU port')
    args = parser.parse_args()
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        raise SystemExit('This bootstrap is pinned for an arm64 Mac.')
    folder = ROOT/'.tools/metal2vulkan'
    pin = LOCK['metal2vulkan']
    if not folder.exists():
        fetch_pinned(folder, pin['url'], pin['commit'])
    head = subprocess.check_output(['git','-C',str(folder),'rev-parse','HEAD'],text=True).strip()
    dirty = subprocess.check_output(['git','-C',str(folder),'status','--porcelain'],text=True)
    if head != pin['commit'] or dirty:
        raise SystemExit('GPU source differs from the pin; existing checkout preserved. Resolve it explicitly before building.')
    from gpu_source import SOURCE as gpu_port, verify_source as verify_gpu_source
    if not gpu_port.exists():
        run('bash', folder/'scripts/lean-tree', 'worktree', gpu_port,
            'henge/android-attachment-rog', pin['commit'], cwd=folder)
        run('git', '-C', gpu_port, 'apply', '--check', ROOT/'android/gpu/attachment-rog.patch')
        run('git', '-C', gpu_port, 'apply', ROOT/'android/gpu/attachment-rog.patch')
    verify_gpu_source()
    installer = ROOT/'.tools/rustup-init'
    if not installer.is_file():
        partial = installer.with_suffix('.part')
        with urllib.request.urlopen(LOCK['rustup']['url']) as response, partial.open('wb') as output:
            while chunk := response.read(1024*1024): output.write(chunk)
        partial.rename(installer)
    if hashlib.file_digest(installer.open('rb'),'sha256').hexdigest() != LOCK['rustup']['sha256']:
        raise SystemExit('rustup-init SHA-256 mismatch; not executing it.')
    installer.chmod(0o755)
    env = dict(os.environ, RUSTUP_HOME=str(ROOT/'.tools/rustup'), CARGO_HOME=str(ROOT/'.tools/cargo'))
    if not (ROOT/'.tools/cargo/bin/rustup').is_file():
        run(installer,'-y','--no-modify-path','--profile','minimal','--default-toolchain',LOCK['rust'],
            '--target','aarch64-linux-android',env=env)
    else:
        run(ROOT/'.tools/cargo/bin/rustup','toolchain','install',LOCK['rust'],'--profile','minimal',env=env)
        run(ROOT/'.tools/cargo/bin/rustup','target','add','--toolchain',LOCK['rust'],'aarch64-linux-android',env=env)
    if args.reims:
        reims = json.loads((ROOT/'android/reims/dependencies.json').read_text())
        original = ROOT/'.tools/reims-vgpu'
        if not original.exists():
            fetch_pinned(original, reims['url'], reims['commit'])
        reims_head = subprocess.check_output(['git','-C',str(original),'rev-parse','HEAD'],text=True).strip()
        reims_dirty = subprocess.check_output(['git','-C',str(original),'status','--porcelain'],text=True)
        if reims_head != reims['commit'] or reims_dirty:
            raise SystemExit('Reims original checkout differs from its pin; existing source preserved.')
        port = Path(os.environ.get('HENGE_REIMS_SOURCE', Path.home()/'.cache/henge-reims-android'))
        port.parent.mkdir(parents=True, exist_ok=True)
        if not port.exists():
            run('bash',original/'scripts/internal/new-worktree.sh',port,'steelbrain/henge-android',reims['commit'],cwd=original)
            run('git','-C',port,'apply','--check',ROOT/'android/reims/android.patch')
            run('git','-C',port,'apply',ROOT/'android/reims/android.patch')
        from build_reims import verify_source
        verify_source()
    print('Pinned GPU sources and local Rust/Android toolchain ready.')

if __name__ == '__main__': main()
