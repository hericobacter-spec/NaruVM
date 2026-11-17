#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline per-ELF origin audit. Matching source candidates are not attestations.

Reads an APK, prepared build tree, selected source checkout and cached pinned
debs. Executes no package scripts, drivers or guest code. No network or install.
"""
import argparse
from datetime import date
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import sys
import tarfile
import zipfile
import tempfile
import subprocess
import shutil

ROOT = Path(__file__).resolve().parents[1]


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def file_digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def ar_members(raw):
    if not raw.startswith(b'!<arch>\n'):
        raise ValueError('Not a Debian ar archive')
    at = 8
    while at < len(raw):
        header = raw[at:at + 60]
        if len(header) != 60 or header[58:60] != b'`\n':
            raise ValueError('Invalid Debian member header')
        size = int(header[48:58])
        end = at + 60 + size
        if size < 0 or end > len(raw):
            raise ValueError('Truncated Debian member')
        yield header[:16].decode().strip().rstrip('/'), raw[at + 60:end]
        at = end + size % 2


def package_index(root, cache, source_lock):
    by_hash, packages = {}, []
    recipes = {(p['name'], p['version']): p for p in source_lock['packages']}
    for group, folder in [('network', 'android-net'), ('software', 'android-lavapipe')]:
        lock = json.loads((root / ('android/' + group + '/dependencies.json')).read_text())
        for package in lock['packages']:
            path = cache / folder / 'debs' / Path(package['Filename']).name
            raw = path.read_bytes()
            if digest(raw) != package['SHA256']:
                raise ValueError('Package checksum mismatch: ' + package['Package'])
            recipe = recipes.get((package['Package'], package['Version']))
            if recipe is None:
                raise ValueError('No version-matched source recipe: ' + package['Package'])
            record = {'group': group, 'package': package['Package'], 'version': package['Version'],
                      'deb_sha256': package['SHA256'], 'recipe_commit': recipe['recipe_commit'],
                      'recipe_path': recipe['recipe_path'], 'source_input_ids': recipe['source_input_ids'],
                      'binary_producing_recipe_attested': False, 'control_fields': {}}
            for name, body in ar_members(raw):
                if not name.startswith(('data.tar', 'control.tar')):
                    continue
                with tarfile.open(fileobj=io.BytesIO(body), mode='r:*') as archive:
                    for member in archive:
                        if not member.isfile():
                            continue
                        if name.startswith('control.tar') and PurePosixPath(member.name).name == 'control':
                            text = archive.extractfile(member).read().decode()
                            fields = dict(re.findall(r'^([^\s:]+):[ \t]*(.*)$', text, re.M))
                            record['control_fields'] = {k: v for k, v in fields.items()
                                if k in {'Package', 'Version', 'Architecture', 'Source', 'Built-Using',
                                         'Build-Id', 'Build-Commit', 'Vcs-Git', 'Vcs-Commit'}}
                        elif name.startswith('data.tar') and '.so' in member.name:
                            data = archive.extractfile(member).read()
                            if data.startswith(b'\x7fELF'):
                                match = {**record, 'member': member.name}
                                by_hash.setdefault(digest(data), []).append(match)
            if record['control_fields'].get('Package') != package['Package'] or \
                    record['control_fields'].get('Version') != package['Version']:
                raise ValueError('Package control version mismatch: ' + package['Package'])
            # The copied member records must retain the final control metadata too.
            for matches in by_hash.values():
                for match in matches:
                    if match['group'] == group and match['package'] == package['Package']:
                        match['control_fields'] = record['control_fields']
            packages.append(record)
    return by_hash, packages


def verify_recipe_files(cache, lock):
    archive = cache / 'archives' / lock['repository']['archive_file']
    import gzip
    with gzip.open(archive, 'rb') as raw:
        if hashlib.file_digest(raw, 'sha256').hexdigest() != lock['repository']['archive_tar_sha256']:
            raise ValueError('Recipe Git archive mismatch')
    count = 0
    with tarfile.open(archive) as source:
        for package in lock['packages']:
            for entry in package['recipe_files']:
                if digest(source.extractfile('termux-packages/' + entry['path']).read()) != entry['sha256']:
                    raise ValueError('Recipe file mismatch: ' + entry['path'])
                count += 1
    return count


def verify_project_inputs(source, build):
    manifest = json.loads((source / 'SOURCE-MANIFEST.json').read_text())
    prefixes = ('core/', 'devices/', 'host/', 'third_party/', 'tests/',
                'android/native/', 'android/src/', 'android/res/', 'android/installer/', 'android/gpu/', 'android/reims/', 'android/panvk/')
    extensions = {'.c', '.h', '.S', '.s', '.java', '.rs', '.toml', '.lock', '.patch', '.metal', '.comp', '.py', '.xml', '.json'}
    count = 0
    for item in manifest['files']:
        name = item['path']
        selected = name.startswith(prefixes) and Path(name).suffix in extensions
        selected |= name in {'android/CMakeLists.txt', 'android/restore.cmake', 'android/AndroidManifest.xml', 'android/installer_sources.py', 'android/release_signing.py'}
        selected |= name.startswith('android/build') and name.endswith('.py')
        if not selected:
            continue
        if file_digest(source / name) != item['sha256'] or file_digest(build / name) != item['sha256']:
            raise ValueError('Project build input differs from selected source: ' + name)
        count += 1
    return count


def native_rows(apk, native, index, build):
    rows = []
    helpers = {
        'libhenge_gpu.so': 'android/build_gpu.py',
        'libhenge_sampler_check.so': 'android/build_gpu.py',
        'libhenge_runtime_sampler_check.so': 'android/build_gpu.py',
        'libreims_vgpu_c.so': 'android/build_reims.py',
        'libhenge_reims_probe.so': 'android/build_reims.py',
        'libhenge_lvp_loader.so': 'android/build_software.py',
        'libhenge_panvk_userptr_loader.so': 'android/build_panvk_userptr_probe.py',
        'libhenge_panvk_userptr_driver.so': 'android/build_panvk_source.py + android/build_panvk_userptr_probe.py',
    }
    with zipfile.ZipFile(apk) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise ValueError('Duplicate APK member')
        for name in sorted(n for n in names if n.startswith('lib/')):
            if len(PurePosixPath(name).parts) != 3 or not name.startswith('lib/arm64-v8a/'):
                raise ValueError('Unexpected APK native member: ' + name)
            raw = archive.read(name)
            if not raw.startswith(b'\x7fELF'):
                raise ValueError('Non-ELF native member: ' + name)
            h = digest(raw)
            row = {'apk_member': name, 'sha256': h, 'bytes': len(raw)}
            matches = index.get(h, [])
            if matches:
                row.update(origin='pinned-deb-byte-identical', package_matches=matches,
                           source_correspondence='version-matched-candidate-not-build-attested')
            else:
                filename = PurePosixPath(name).name
                if file_digest(native / filename) != h:
                    raise ValueError('APK does not match local build output: ' + name)
                target = filename.removeprefix('lib').removesuffix('.so')
                link = build / 'android/build/native/CMakeFiles' / (target + '.dir') / 'link.txt'
                recipe = 'android/CMakeLists.txt:' + target if link.is_file() else helpers.get(filename)
                if not recipe:
                    raise ValueError('No build recipe for native member: ' + name)
                row.update(origin='local-build-output-byte-identical', build_recipe=recipe,
                           source_correspondence='observed-build-output-and-selected-inputs')
            rows.append(row)
    if not rows:
        raise ValueError('APK contains no native ELF')
    return rows


def release_packaging(build):
    records = json.loads((build / 'android/build/release-native-map.json').read_text())
    seen = set()
    strip = build / '.tools/android-sdk/ndk/30.0.16248370/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-strip'
    with tempfile.TemporaryDirectory(prefix='naruvm-strip-proof-') as folder:
        for record in records:
            name = record['name']
            if Path(name).name != name or name in seen:
                raise ValueError('Invalid release-native map name')
            seen.add(name)
            original = build / 'android/build/native/bin' / name
            staged = build / 'android/build/release-native/bin' / name
            if file_digest(original) != record['source_sha256'] or file_digest(staged) != record['packaged_sha256']:
                raise ValueError('Release native packaging hash mismatch: ' + name)
            copy = Path(folder) / name
            shutil.copy2(original, copy)
            if record['strip_unneeded']:
                subprocess.run([strip, '--strip-unneeded', copy], check=True)
            if file_digest(copy) != record['packaged_sha256']:
                raise ValueError('Release strip transformation differs: ' + name)
    return records


def audit(apk, build, source, cache, inputs_cache, release=False):
    lock = json.loads((ROOT / 'android/publication/termux-sources.json').read_text())
    recipe_files = verify_recipe_files(inputs_cache, lock)
    project_files = verify_project_inputs(source, build)
    index, packages = package_index(ROOT, cache, lock)
    packaging = release_packaging(build) if release else []
    rows = native_rows(apk, build / ('android/build/release-native/bin' if release else 'android/build/native/bin'), index, build)
    if release and {row['apk_member'].split('/')[-1] for row in rows} != {item['name'] for item in packaging}:
        raise ValueError('Release native packaging inventory mismatch')
    used = sorted({(p['package'], p['version']) for row in rows for p in row.get('package_matches', [])})
    external = sum(row['origin'] == 'pinned-deb-byte-identical' for row in rows)
    return {'format': 1, 'label': 'observed-offline-origin-audit', 'date': date.today().isoformat(),
        'apk_sha256': file_digest(apk), 'native_members': len(rows), 'native_members_mapped': len(rows),
        'exact_package_members': external, 'exact_local_build_outputs': len(rows) - external,
        'used_package_versions': [{'name': n, 'version': v} for n, v in used],
        'input_package_entries': len(packages), 'recipe_files_verified': recipe_files,
        'project_input_files_verified': project_files,
        'source_manifest_sha256': file_digest(source / 'SOURCE-MANIFEST.json'),
        'rows': rows, 'input_packages': packages,
        'release_packaging_verified': packaging,
        'complete_source_correspondence': False, 'binary_release_ready': False,
        'limits': ['Byte-identical package origin is narrower than proof of its binary-producing recipe commit.',
                   'Local output/input matches do not by themselves attest a historical source-to-binary build.',
                   'Source completeness, runtime/toolchain sources and release installation/relink acceptance remain separate gates.',
                   'No network, package scripts, driver, guest or device execution occurs in this audit.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apk', type=Path, required=True)
    parser.add_argument('--build-tree', type=Path, required=True)
    parser.add_argument('--source-tree', type=Path, required=True)
    parser.add_argument('--cache', type=Path, default=ROOT / '.tools')
    parser.add_argument('--inputs-cache', type=Path, default=ROOT / '.tools/publication-sources')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--require-complete-source', action='store_true')
    parser.add_argument('--release', action='store_true', help='Verify release strip transformations against exact original native outputs')
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError('Audit output exists; choose a new path')
    report = audit(args.apk, args.build_tree, args.source_tree, args.cache, args.inputs_cache, args.release)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print('APK_ORIGIN_AUDIT_RESULT failures=0 native=' + str(report['native_members']) +
          ' exact_deb=' + str(report['exact_package_members']) +
          ' local_outputs=' + str(report['exact_local_build_outputs']) + ' complete_source_correspondence=0')
    if args.require_complete_source:
        sys.exit(2)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, tarfile.TarError, zipfile.BadZipFile) as error:
        sys.exit('APK_ORIGIN_AUDIT_ERROR ' + str(error))
