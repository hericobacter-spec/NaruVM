#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Package/verify collected Termux source inputs; not a complete APK source offer."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import sys
import tarfile
from fetch_publication_sources import LOCK_PATH, ROOT, digest, fetch, recipes


def expected(cache, lock, ndk=None, graphics=None):
    snapshots = [recipes(lock['repository'], cache, None, False)]
    selected = [item for item in lock['inputs'] if not item['kind'].startswith('prebuilt-')]
    if ndk: selected += ndk['source_inputs']
    if graphics:
        selected += graphics['inputs']
        snapshots += [recipes(item, cache, None, False) for item in graphics['git_sources']]
    inputs = [fetch(item, cache / 'archives', False) for item in selected]
    entries = {'termux-sources.json': LOCK_PATH}
    if ndk: entries['ndk-runtime.json'] = ROOT / 'android/publication/ndk-runtime.json'
    if graphics: entries['graphics-sources.json'] = ROOT / 'android/publication/graphics-sources.json'
    for item in [*snapshots, *inputs]:
        entries['archives/' + item['file']] = cache / 'archives' / item['file']
    return entries


def verify(path, lock, ndk=None, graphics=None):
    selected = [item for item in lock['inputs'] if not item['kind'].startswith('prebuilt-')]
    if ndk: selected += ndk['source_inputs']
    if graphics: selected += graphics['inputs']
    expected_hashes = {'termux-sources.json': digest(LOCK_PATH),
                       **{'archives/' + item['file']: item['sha256'] for item in selected}}
    if ndk: expected_hashes['ndk-runtime.json'] = digest(ROOT / 'android/publication/ndk-runtime.json')
    if graphics: expected_hashes['graphics-sources.json'] = digest(ROOT / 'android/publication/graphics-sources.json')
    git_records = [lock['repository']] + (graphics['git_sources'] if graphics else [])
    tar_hashes = {
        'archives/' + item['archive_file']: item['archive_tar_sha256'] for item in git_records}
    with tarfile.open(path, 'r:gz') as archive:
        members = archive.getmembers()
        names = [item.name for item in members]
        if len(names) != len(set(names)) or set(names) != set(expected_hashes) | set(tar_hashes):
            raise ValueError('Source-input archive file set mismatch')
        for item in members:
            if not item.isfile(): raise ValueError('Only regular files are allowed')
            with archive.extractfile(item) as source:
                if item.name in tar_hashes:
                    with gzip.GzipFile(fileobj=source) as expanded:
                        actual = hashlib.file_digest(expanded, 'sha256').hexdigest()
                    wanted = tar_hashes[item.name]
                else:
                    actual = hashlib.file_digest(source, 'sha256').hexdigest()
                    wanted = expected_hashes[item.name]
            if actual != wanted: raise ValueError('Source-input checksum mismatch: ' + item.name)
    prefix = 'DEPENDENCY' if graphics else 'TERMUX'
    print(f'{prefix}_SOURCE_VERIFY_RESULT failures=0 source_inputs={len(selected)} git_sources={len(tar_hashes)} complete_apk_source=0')


def package(output, entries):
    if output.exists(): raise ValueError('Output already exists; choose a new filename')
    output.parent.mkdir(parents=True, exist_ok=True)
    partial = output.with_suffix(output.suffix + '.part')
    if partial.exists(): raise ValueError('Partial output already exists; inspect it first')
    try:
        with partial.open('xb') as raw, gzip.GzipFile(fileobj=raw, mode='wb', filename='', mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode='w|') as archive:
                for name, path in sorted(entries.items()):
                    info = tarfile.TarInfo(name)
                    info.size = path.stat().st_size
                    info.mode = 0o644
                    with path.open('rb') as source: archive.addfile(info, source)
        partial.replace(output)
    finally:
        partial.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache', type=Path, default=ROOT / '.tools/publication-sources')
    parser.add_argument('--ndk-source', action='store_true', help='Include pinned LLVM source and Android build scripts')
    parser.add_argument('--graphics', action='store_true', help='Include selected Cargo/Mesa/Rust sources and pinned original Git snapshots')
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--output', type=Path)
    group.add_argument('--verify', type=Path)
    args = parser.parse_args()
    lock = json.loads(LOCK_PATH.read_text())
    ndk = json.loads((ROOT / 'android/publication/ndk-runtime.json').read_text()) if args.ndk_source else None
    graphics = json.loads((ROOT / 'android/publication/graphics-sources.json').read_text()) if args.graphics else None
    if args.verify:
        verify(args.verify, lock, ndk, graphics)
    else:
        package(args.output, expected(args.cache, lock, ndk, graphics))
        verify(args.output, lock, ndk, graphics)
        print(('DEPENDENCY' if graphics else 'TERMUX') + '_SOURCE_SHA256 ' + digest(args.output))


if __name__ == '__main__':
    try: main()
    except (OSError, ValueError, tarfile.TarError) as error: sys.exit('TERMUX_SOURCE_ERROR ' + str(error))
