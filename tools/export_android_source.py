#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Create a deterministic Android source-only review archive; never publish it.

Only selected source files are copied. Generated binaries, private research
records, tools/caches, guest data, signing material and Git history are excluded.
This is not a complete corresponding-source bundle for a binary APK release.
"""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE_EXTENSIONS = {'.c', '.h', '.S', '.s', '.m', '.mm', '.py', '.java', '.rs', '.toml',
                     '.lock', '.json', '.cmake', '.xml', '.patch', '.comp', '.metal', '.swift', '.Dockerfile'}
DENIED_PARTS = {'.git', '.tools', '.impeccable', 'memory', 'build', 'artifacts', 'target', '__pycache__', 'private-backups'}
ROOT_FILES = {'.gitattributes', 'LICENSE', 'NOTICE', 'README.md', 'README.ko.md', '.gitignore',
              'CONTRIBUTING.md', 'SECURITY.md', 'CHANGELOG.md'}
PUBLIC_DOCS = {'android/DESIGN.md', 'docs/architecture.md', 'docs/licensing.md', 'docs/building.md',
               'docs/publication.md', 'docs/modifications.json', 'docs/source-correspondence.md',
               'docs/installation.md', 'docs/installation.ko.md',
               'docs/binary-release.md', 'docs/binary-release.ko.md', 'docs/images/README.md'}
# Individually inspected historical captures only; never glob research images.
PUBLIC_MEDIA = {'docs/images/macos-setup.png', 'docs/images/macos-safari.png',
                'docs/images/macos-vscode.png'}
PUBLIC_GITHUB = {'.github/workflows/android-source.yml', '.github/ISSUE_TEMPLATE/bug_report.yml',
                 '.github/ISSUE_TEMPLATE/device_report.yml', '.github/ISSUE_TEMPLATE/config.yml',
                 '.github/pull_request_template.md'}
PUBLIC_TOOLS = {'tools/android-toolchain.lock.json', 'tools/bootstrap_android.py',
                'tools/check_android_portability.py', 'tools/check_publication.py',
                'tools/export_android_source.py', 'tools/prepare_github.py', 'tools/verify_github_ready.py'}
PRIVATE_PATTERNS = [re.compile(rb'[/]Users/[^/\s"\x27<>]+/'),
                    re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----\s+[A-Za-z0-9+/]{32}')]


def source_name(name):
    if not isinstance(name, str) or '\\' in name or '\0' in name or any(
            part in {'', '.', '..'} | DENIED_PARTS for part in name.split('/')):
        raise ValueError('Invalid source path: ' + repr(name))
    return name


def source_payload(name, raw, deny_literals=()):
    source_name(name)
    if name in PUBLIC_MEDIA and not raw.startswith(b'\x89PNG\r\n\x1a\n'):
        raise ValueError('Reviewed screenshot is not a PNG: ' + name)
    if raw.startswith((b'\x7fELF', b'PK\x03\x04', b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf',
                       b'\xca\xfe\xba\xbe', b'!<arch>\n', b'MZ', b'\0asm')):
        raise ValueError('Unexpected binary: ' + name)
    if any(pattern.search(raw) for pattern in PRIVATE_PATTERNS) or any(value in raw for value in deny_literals):
        raise ValueError('Private-data screening requires review: ' + name)


def manifest_entries(raw):
    manifest = json.loads(raw)
    if not isinstance(manifest, dict) or manifest.get('format') != 2 or \
            manifest.get('label') != 'observed-source-selection' or \
            manifest.get('release_scope') != 'source-only' or manifest.get('binary_release_ready') is not False:
        raise ValueError('Invalid source manifest scope/format')
    entries = manifest.get('files')
    if not isinstance(entries, list) or not entries:
        raise ValueError('Invalid source manifest file list')
    expected = {}
    for item in entries:
        if not isinstance(item, dict): raise ValueError('Invalid source manifest entry')
        name = source_name(item.get('path'))
        if name == 'SOURCE-MANIFEST.json' or name in expected:
            raise ValueError('Duplicate/reserved source manifest path: ' + name)
        if not isinstance(item.get('sha256'), str) or not re.fullmatch('[0-9a-f]{64}', item['sha256']) or \
                type(item.get('bytes')) is not int or item['bytes'] < 0:
            raise ValueError('Invalid source manifest hash/size: ' + name)
        expected[name] = item
    return expected


def selected(root):
    # Vendored upstream files include extensionless copyright/build inputs.
    # Git's index selects their names only; exported bytes are current source.
    if (root / 'SOURCE-MANIFEST.json').is_file():
        tracked = [name for name in manifest_entries((root / 'SOURCE-MANIFEST.json').read_bytes())
                   if name.startswith('third_party/')]
    else:
        tracked = subprocess.check_output(['git', '-C', root, 'ls-files', '-z', 'third_party'], text=True).split('\0')
    paths = {Path(name) for name in tracked if name}
    # Newly vendored decoder: exact pinned files, never arbitrary untracked files.
    pin = root / 'android/installer/dependencies.json'
    if pin.is_file():
        paths.update(Path('third_party/lzfse') / name for name in json.loads(pin.read_text())['files'])
    for folder in ['core', 'devices', 'host', 'tests', 'android']:
        for path in (root / folder).rglob('*'):
            relative = path.relative_to(root)
            if any(part in DENIED_PARTS for part in relative.parts): continue
            if path.is_file() and (path.suffix in SOURCE_EXTENSIONS or path.name == 'CMakeLists.txt'):
                paths.add(relative)
    paths.update(Path(name) for name in ROOT_FILES if (root / name).is_file())
    # Public docs are intentionally separate from the private research guide.
    for name in PUBLIC_DOCS | PUBLIC_MEDIA | PUBLIC_GITHUB | PUBLIC_TOOLS:
        if (root / name).is_file(): paths.add(Path(name))
    if (root / 'LICENSES').is_dir():
        paths.update(p.relative_to(root) for p in (root / 'LICENSES').rglob('*') if p.is_file())
    return sorted(paths)


def collect(root, deny_literals):
    files = {}
    for path in selected(root):
        if any(part in DENIED_PARTS for part in path.parts): raise ValueError('Excluded path selected: ' + str(path))
        source = root / path
        if source.is_symlink() or not source.resolve().is_relative_to(root.resolve()):
            raise ValueError('Source export refuses symlinks: ' + str(path))
        raw = source.read_bytes()
        source_payload(path.as_posix(), raw, deny_literals)
        files[path.as_posix()] = raw
    manifest = dict(format=2, label='observed-source-selection', release_scope='source-only', binary_release_ready=False,
                    exclusions=sorted(DENIED_PARTS) + ['guest ROM/IPSW/disks', 'keys', 'compiled shaders', 'private Android research guide'],
                    limitations=['No Git history or external dependency source included; source pins/patches/notices are included.',
                                 'Pattern screening is bounded; human review remains required.',
                                 'Full fresh-machine graphics build and binary corresponding-source inventory remain unverified.'],
                    files=[dict(path=path, sha256=hashlib.sha256(raw).hexdigest(), bytes=len(raw)) for path, raw in files.items()])
    files['SOURCE-MANIFEST.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    return files


def verify(path):
    with tarfile.open(path, 'r:gz') as archive:
        members = archive.getmembers()
        for member in members:
            if not member.isfile(): raise ValueError('Unexpected archive entry')
            source_name(member.name)
        if len({member.name for member in members}) != len(members): raise ValueError('Duplicate archive entries')
        contents = {member.name: archive.extractfile(member).read() for member in members}
    expected = manifest_entries(contents.pop('SOURCE-MANIFEST.json'))
    if set(contents) != set(expected): raise ValueError('Manifest file set mismatch')
    for name, raw in contents.items():
        source_payload(name, raw)
        if hashlib.sha256(raw).hexdigest() != expected[name]['sha256'] or len(raw) != expected[name]['bytes']:
            raise ValueError('Manifest content mismatch: ' + name)
    print('SOURCE_VERIFY_RESULT failures=0 files=' + str(len(contents)) + ' binary_release_ready=0')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'android/artifacts/NaruVM-source-review.tar.gz')
    parser.add_argument('--deny-literal', action='append', default=[], help='Additional private identifier to screen; never recorded in archive')
    parser.add_argument('--verify', type=Path, help='Check an existing archive without extracting/executing it')
    parser.add_argument('--refresh-manifest', action='store_true', help='Refresh SOURCE-MANIFEST.json in an exported source checkout after intentional edits')
    args = parser.parse_args()
    if args.verify:
        verify(args.verify)
        return
    files = collect(ROOT, [value.encode() for value in args.deny_literal if value])
    if args.refresh_manifest:
        (ROOT / 'SOURCE-MANIFEST.json').write_bytes(files['SOURCE-MANIFEST.json'])
        print('SOURCE_MANIFEST_REFRESHED files=' + str(len(files) - 1))
        return
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('wb') as output, gzip.GzipFile(fileobj=output, mode='wb', filename='', mtime=0) as compressed:
        with tarfile.open(fileobj=compressed, mode='w') as archive:
            for name, raw in files.items():
                info = tarfile.TarInfo(name)
                info.size = len(raw)
                info.mode = 0o644
                info.mtime = 0
                archive.addfile(info, io.BytesIO(raw))
    verify(args.output)
    print('SOURCE_SHA256 ' + hashlib.sha256(args.output.read_bytes()).hexdigest())
    print(args.output)


if __name__ == '__main__':
    main()
