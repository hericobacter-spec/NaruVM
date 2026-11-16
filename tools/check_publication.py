#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline source-publication structure, notice, link, patch and content checks.

This checks observable files, not legal clearance or complete APK source coverage.
On an exported tree it also verifies SOURCE-MANIFEST.json against every source file.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import unquote, urlsplit
from export_android_source import ROOT, collect, manifest_entries, PUBLIC_DOCS, PUBLIC_MEDIA, PUBLIC_GITHUB, PUBLIC_TOOLS, ROOT_FILES


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_index(root, files):
    """Compare the actual Git index objects, not only their worktree names."""
    if not (root / '.git').exists(): raise ValueError('Staged check requires an isolated Git source tree')
    records = subprocess.check_output(['git', '-C', root, 'ls-files', '--stage', '-z']).split(b'\0')
    staged = {}
    for record in filter(None, records):
        info, name = record.split(b'\t', 1)
        mode, oid, stage = info.split()
        name = name.decode('utf-8')
        if mode not in {b'100644', b'100755'} or stage != b'0' or name in staged:
            raise ValueError('Unexpected Git index entry: ' + name)
        staged[name] = oid
    if set(staged) != set(files): raise ValueError('Git index file set differs from reviewed source selection')
    # Local blob hashing does not write objects or consult line-ending filters.
    algorithm = subprocess.check_output(['git', '-C', root, 'rev-parse', '--show-object-format'], text=True).strip()
    if algorithm not in {'sha1', 'sha256'}: raise ValueError('Unsupported Git object format')
    for name, raw in files.items():
        oid = hashlib.new(algorithm, b'blob ' + str(len(raw)).encode() + b'\0' + raw).hexdigest().encode()
        if staged[name] != oid: raise ValueError('Git index bytes differ from reviewed source: ' + name)


def check(root, deny_literals, staged=False):
    files = collect(root, deny_literals)
    required = ROOT_FILES | PUBLIC_DOCS | PUBLIC_MEDIA | PUBLIC_GITHUB | PUBLIC_TOOLS | {
        'android/tests/test_publication.py',
        'android/publication/licenses.json', 'android/publication/dependencies.json',
        'android/publication/termux-sources.json', 'android/publication/license-selection.json',
        'android/publication/ndk-runtime.json', 'android/tests/test_publication_sources.py',
        'android/publication/graphics-sources.json', 'LICENSES/source-info.txt',
        'android/src/dev/henge/research/LicenseViewer.java',
        'android/src/dev/henge/research/MacInputView.java',
        'android/tests/HardwareInputInstrumentation.java', 'android/build_input_test.py',
        'android/audit_source_correspondence.py', 'android/reproduce_source_payloads.py',
        'android/publication/source-correspondence.json', 'android/tests/test_source_correspondence.py',
        'android/tests/test_source_archive.py',
        'LICENSES/LGPL-2.1.txt', 'LICENSES/LGPL-3.0.txt', 'LICENSES/GPL-3.0.txt',
        'LICENSES/curl-COPYING.txt', 'LICENSES/PanVK-MIT.txt'}
    missing = required - set(files)
    if missing: raise ValueError('Missing publication files: ' + ', '.join(sorted(missing)))
    patterns = [re.compile(rb'gh[pousr]_[A-Za-z0-9]{30,}'),
                re.compile(rb'github_pat_[A-Za-z0-9_]{30,}'),
                re.compile(rb'AKIA[0-9A-Z]{16}'),
                re.compile(rb'sk-(?:proj-)?[A-Za-z0-9_-]{32,}')]
    for name, raw in files.items():
        if len(raw) >= 100 * 1024 * 1024: raise ValueError('File exceeds regular Git publication size policy: ' + name)
        if any(pattern.search(raw) for pattern in patterns): raise ValueError('Credential-shaped content requires review: ' + name)
        if name.endswith(('.apk', '.ipa', '.ipsw', '.raw', '.keystore', '.jks', '.p12', '.pfx', '.so', '.metallib', '.spv')):
            raise ValueError('Binary/guest/key must not be a source release: ' + name)
    licenses = json.loads(files['android/publication/licenses.json'])
    for item in licenses['components']:
        for notice in item['notices']:
            if notice['path'] not in files or digest(root / notice['path']) != notice['sha256']:
                raise ValueError('License notice missing/changed: ' + notice['path'])
        if item.get('pin_file') and (root / item['pin_file']).read_text().strip() != item['source_revision']:
            raise ValueError('Vendored source revision changed: ' + item['name'])
    gpu = json.loads(files['android/gpu/dependencies.json'])
    reims = json.loads(files['android/reims/dependencies.json'])
    for path, expected in [('android/gpu/attachment-rog.patch', gpu['android_port']['patch_sha256']),
                           ('android/reims/android.patch', reims['patch_sha256'])]:
        if digest(root / path) != expected: raise ValueError('Pinned patch changed: ' + path)
    dependencies = json.loads(files['android/publication/dependencies.json'])
    if digest(root / 'android/panvk/userptr-driver.patch') != dependencies['panvk_userptr_patch_sha256']:
        raise ValueError('Pinned PanVK patch changed')
    for entry in dependencies['input_manifests']:
        if digest(root / entry['path']) != entry['sha256']: raise ValueError('Dependency input changed: ' + entry['path'])
    graphics = json.loads(files['android/publication/graphics-sources.json'])
    wanted_pins = {item['commit'] for item in dependencies['external_git_sources']}
    if {item['commit'] for item in graphics['git_sources']} != wanted_pins or graphics['binary_release_ready']:
        raise ValueError('Graphics source pins/status changed; review required')
    for item in graphics['inputs']:
        if Path(item['file']).name != item['file'] or not re.fullmatch('[0-9a-f]{64}', item['sha256']):
            raise ValueError('Graphics source filename/hash invalid')
    source_inputs = json.loads(files['android/publication/termux-sources.json'])
    recipes = {(item['name'], item['version']): item for item in source_inputs['packages']}
    for item in dependencies['external_binary_packages']:
        recipe = recipes.get((item['name'], item['version']))
        if not recipe or item.get('version_matching_recipe_commit') != recipe['recipe_commit'] or \
                recipe['recipe_commit'] != source_inputs['repository']['commit'] or \
                item.get('recipe_path') != recipe['recipe_path'] or \
                item.get('source_input_ids') != recipe['source_input_ids']:
            raise ValueError('Binary/source recipe mapping changed: ' + item['name'])
        if item['exact_recipe_commit'] is not None or item['matching_source_bundle']:
            raise ValueError('Binary recipe attestation is not established: ' + item['name'])
    selection = json.loads(files['android/publication/license-selection.json'])
    if selection['combined_option'] != 'GPL-3.0-only' or not selection['source_grants_unchanged']:
        raise ValueError('Combined license selection changed; review required')
    changes = json.loads(files['docs/modifications.json'])
    for item in changes['tracked_upstream_changes']:
        if Path(item['path']).suffix in {'.c', '.h', '.S'} and not files[item['path']].startswith(b'/* Android port modifications:'):
            raise ValueError('Missing prominent change notice: ' + item['path'])
    # Validate local Markdown file targets, not external network availability.
    for name in (required | {'README.md', 'README.ko.md'}):
        if not name.endswith('.md'): continue
        for target in re.findall(r'\]\(([^)]+)\)', files[name].decode()):
            target = target.split(' "', 1)[0].strip('<>')
            if urlsplit(target).scheme or target.startswith('#'): continue
            path = (root / name).parent / unquote(target.split('#', 1)[0])
            if not path.resolve().is_relative_to(root.resolve()) or not path.exists():
                raise ValueError('Broken local Markdown target in ' + name + ': ' + target)
    workflow = files['.github/workflows/android-source.yml'].decode()
    if 'contents: read' not in workflow or 'persist-credentials: false' not in workflow:
        raise ValueError('Workflow must use read-only token and avoid stored checkout credentials')
    if re.search(r'pull_request_target:|contents: write|gh release|git push|upload-artifact@', workflow):
        raise ValueError('Source workflow must not publish binaries or use privileged PR execution')
    actions = re.findall(r'uses:\s*([^\s]+)', workflow)
    if not actions or any(not re.fullmatch(r'actions/checkout@[0-9a-f]{40}', action) for action in actions):
        raise ValueError('Source workflow action must be pinned to a full verified commit')
    stored = root / 'SOURCE-MANIFEST.json'
    if stored.is_file():
        expected = manifest_entries(stored.read_bytes())
        actual = {name: raw for name, raw in files.items() if name != 'SOURCE-MANIFEST.json'}
        if set(expected) != set(actual): raise ValueError('Source manifest file set changed; refresh before release')
        for name, raw in actual.items():
            if hashlib.sha256(raw).hexdigest() != expected[name]['sha256'] or len(raw) != expected[name]['bytes']:
                raise ValueError('Source manifest stale: ' + name)
        # A public Git checkout must not carry additional tracked files outside
        # the inspected source selection, even if those files are ignored here.
        if (root / '.git').exists():
            tracked = set(subprocess.check_output(['git', '-C', root, 'ls-files', '-z'], text=True).strip('\0').split('\0'))
            extra = tracked - set(files)
            if extra: raise ValueError('Unreviewed tracked files: ' + ', '.join(sorted(extra)))
    if staged:
        if not stored.is_file(): raise ValueError('Staged check requires SOURCE-MANIFEST.json')
        check_index(root, files)
    print('PUBLICATION_SOURCE_RESULT failures=0 files=' + str(len(files) - 1) +
          ' components=' + str(len(licenses['components'])) + ' scope=source-only binary_release_ready=0')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tree', type=Path, default=ROOT)
    parser.add_argument('--deny-literal', action='append', default=[])
    parser.add_argument('--staged', action='store_true', help='Require exact Git index bytes as well as source/manifest checks')
    args = parser.parse_args()
    check(args.tree.resolve(), [value.encode() for value in args.deny_literal if value], args.staged)


if __name__ == '__main__':
    try: main()
    except (ValueError, KeyError, OSError) as error: sys.exit('PUBLICATION_SOURCE_ERROR ' + str(error))
