#!/usr/bin/env python3
"""Fetch the pinned, official curl release used by the Android restore build."""
import hashlib
from pathlib import Path
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
VERSION = '8.23.0'
# Official GitHub release asset digest, read on 2026-11-14.
SHA256 = '8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5'
URL = f'https://curl.se/download/curl-{VERSION}.tar.xz'

def main():
    dest = ROOT / '.tools'
    dest.mkdir(exist_ok=True)
    archive = dest / f'curl-{VERSION}.tar.xz'
    if not archive.exists():
        with urllib.request.urlopen(URL, timeout=60) as src, archive.open('wb') as out:
            while block := src.read(1024 * 1024): out.write(block)
    with archive.open('rb') as source:
        digest = hashlib.file_digest(source, 'sha256').hexdigest()
    if digest != SHA256:
        raise SystemExit('curl SHA-256 mismatch; archive was not extracted')
    if not (dest / f'curl-{VERSION}' / 'CMakeLists.txt').exists():
        with tarfile.open(archive) as src: src.extractall(dest, filter='data')
    print(f'curl {VERSION}: checksum verified, sources ready')

if __name__ == '__main__': main()
