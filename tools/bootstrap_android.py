#!/usr/bin/env python3
"""Install pinned, checksum-verified Android build tools inside .tools/."""
import concurrent.futures
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / '.tools'
SDK = TOOLS / 'android-sdk'



def download(url, path, algorithm, checksum):
    if path.exists() and hashlib.new(algorithm, path.read_bytes()).hexdigest() == checksum:
        return
    print('Downloading ' + path.name, flush=True)
    temporary = path.with_suffix(path.suffix + '.part')
    with urllib.request.urlopen(url, timeout=120) as src, temporary.open('wb') as dst:
        digest = hashlib.new(algorithm)
        while block := src.read(1024 * 1024):
            dst.write(block)
            digest.update(block)
    if digest.hexdigest() != checksum:
        raise RuntimeError('Checksum mismatch: ' + path.name)
    temporary.replace(path)


def install_archive(job):
    url, dest, algorithm, checksum = job
    if dest.exists():
        print('Already installed: ' + str(dest.relative_to(TOOLS)), flush=True)
        return
    archive = TOOLS / 'downloads' / url.rsplit('/', 1)[-1]
    download(url, archive, algorithm, checksum)
    stage = TOOLS / ('unpack-' + dest.name)
    stage.mkdir(exist_ok=True)
    if archive.name.endswith('.zip'):
        subprocess.run(['unzip', '-q', '-o', str(archive), '-d', str(stage)], check=True)
    else:
        subprocess.run(['tar', '-xzf', str(archive), '-C', str(stage)], check=True)
    entries = [p for p in stage.iterdir() if p.name != '__MACOSX']
    if len(entries) != 1 or not entries[0].is_dir():
        raise RuntimeError('Unexpected archive layout: ' + archive.name)
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.move(str(entries[0]), dest)
    stage.rmdir()
    print('Installed ' + str(dest.relative_to(TOOLS)), flush=True)


def main():
    (TOOLS / 'downloads').mkdir(parents=True, exist_ok=True)
    manifest = json.loads((ROOT / 'tools/android-toolchain.lock.json').read_text())
    jobs = [(item['url'], TOOLS / item['destination'], item['algorithm'], item['checksum'])
            for item in manifest]
    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
        list(pool.map(install_archive, jobs))
    print('Android build tools ready: ' + str(SDK), flush=True)


if __name__ == '__main__':
    main()
