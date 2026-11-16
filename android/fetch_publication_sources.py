#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Collect checksum-pinned dependency source; never execute downloaded recipes.

Default is offline validation. --download permits public source downloads.
The NDK binary archive is deliberately not called libc++ source.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
LOCK_PATH = ROOT / 'android/publication/termux-sources.json'


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def fetch(item, archives, download):
    name = item['file']
    if Path(name).name != name:
        raise ValueError('Source archive filename must be a basename')
    path = archives / name
    if path.exists() and digest(path) != item['sha256']:
        raise ValueError('Existing source checksum mismatch: ' + name)
    if not path.exists():
        if not download:
            raise ValueError('Source archive unavailable offline: ' + name)
        partial = path.with_suffix(path.suffix + '.part')
        request = urllib.request.Request(item['url'], headers={'User-Agent': 'Henge-source-preparation/1'})
        try:
            with urllib.request.urlopen(request, timeout=60) as source, partial.open('wb') as output:
                while chunk := source.read(1024 * 1024):
                    output.write(chunk)
            if digest(partial) != item['sha256']:
                raise ValueError('Downloaded source checksum mismatch: ' + name)
            partial.replace(path)
        finally:
            partial.unlink(missing_ok=True)
    print('SOURCE_INPUT_OK ' + item['id'], flush=True)
    return {'id': item['id'], 'file': name, 'bytes': path.stat().st_size, 'sha256': item['sha256']}


def recipes(lock, cache, recipe_tree, download):
    path = cache / 'archives' / lock['archive_file']
    if not path.exists():
        if recipe_tree is None:
            if not download:
                raise ValueError('Pinned recipe archive unavailable offline')
            recipe_tree = cache / lock.get('cache_repository', 'termux-repository')
            if not recipe_tree.exists():
                subprocess.run(['git', 'init', recipe_tree], check=True, stdout=subprocess.DEVNULL)
            present = subprocess.run(['git', '-C', recipe_tree, 'cat-file', '-e', lock['commit'] + '^{commit}'], capture_output=True)
            if present.returncode:
                subprocess.run(['git', '-C', recipe_tree, 'fetch', '--depth=1', lock['url'], lock['commit']], check=True)
        # git archive reads the pinned commit, never arbitrary working-tree edits.
        with path.open('wb') as output, gzip.GzipFile(fileobj=output, filename='', mode='wb', mtime=0) as compressed:
            command = ['git', '-C', recipe_tree, 'archive', '--format=tar', '--prefix=' + lock.get('prefix', 'termux-packages/'), lock['commit']]
            process = subprocess.Popen(command, stdout=subprocess.PIPE)
            with process.stdout:
                while chunk := process.stdout.read(1024 * 1024):
                    compressed.write(chunk)
            if process.wait():
                path.unlink(missing_ok=True)
                raise ValueError('Could not archive pinned recipes')
    with gzip.open(path, 'rb') as source:
        if hashlib.file_digest(source, 'sha256').hexdigest() != lock['archive_tar_sha256']:
            raise ValueError('Recipe archive source content mismatch')
    return {'file': path.name, 'sha256': digest(path), 'commit': lock['commit']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--download', action='store_true')
    parser.add_argument('--ndk-source', action='store_true', help='Also collect pinned LLVM source and Android build/patch scripts, never NDK prebuilts')
    parser.add_argument('--graphics', action='store_true', help='Also collect selected Cargo, Mesa wrap, Rust library and pinned graphics Git sources')
    parser.add_argument('--cache', type=Path, default=ROOT / '.tools/publication-sources')
    parser.add_argument('--recipe-tree', type=Path, help='Existing Git object store containing the pinned recipe commit')
    args = parser.parse_args()
    lock = json.loads(LOCK_PATH.read_text())
    cache = args.cache.resolve()
    archives = cache / 'archives'
    archives.mkdir(parents=True, exist_ok=True)
    recipe = recipes(lock['repository'], cache, args.recipe_tree, args.download)
    # All actual source inputs, including auxiliary terminfo; prebuilt NDK is not source.
    selected = [item for item in lock['inputs'] if not item['kind'].startswith('prebuilt-')]
    if args.ndk_source:
        selected += json.loads((ROOT / 'android/publication/ndk-runtime.json').read_text())['source_inputs']
    graphics_git = []
    if args.graphics:
        graphics = json.loads((ROOT / 'android/publication/graphics-sources.json').read_text())
        selected += graphics['inputs']
        graphics_git = [recipes(item, cache, None, args.download) for item in graphics['git_sources']]
    successes, failures = [], []
    def collect_one(item):
        try:
            return fetch(item, archives, args.download), None
        except Exception as error:
            return None, {'id': item['id'], 'error': str(error)}
    with ThreadPoolExecutor(max_workers=4) as pool:
        for success, failure in pool.map(collect_one, selected):
            if success: successes.append(success)
            if failure: failures.append(failure)
    report = {'label': 'observed-source-collection', 'lock_sha256': digest(LOCK_PATH),
              'recipe': recipe, 'source_inputs': successes, 'failures': failures,
              'graphics_git_sources': graphics_git,
              'prebuilt_ndk_is_not_source': True, 'binary_release_ready': False}
    (cache / 'status.json').write_text(json.dumps(report, indent=2) + '\n')
    for failure in failures:
        print('SOURCE_INPUT_ERROR ' + failure['id'] + ': ' + failure['error'], file=sys.stderr)
    print(f'PUBLICATION_DEP_SOURCE_RESULT inputs={len(successes)} failures={len(failures)} binary_release_ready=0')
    return bool(failures)


if __name__ == '__main__':
    sys.exit(main())
