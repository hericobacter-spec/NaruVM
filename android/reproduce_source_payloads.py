#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Recompile selected Java/manifest payloads offline and compare exact APK bytes.

With --libcxx, also reproduce NDK runtime packaging using checksum-pinned
termux-elf-cleaner source and llvm-strip. Only temporary copies are modified.
This is not a libc++ source rebuild or a complete native build attestation.
"""
import argparse
from datetime import date
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def compare(generated, original):
    actual = generated.read_bytes()
    return {'equal': actual == original, 'bytes': len(actual),
            'rebuilt_sha256': hashlib.sha256(actual).hexdigest(),
            'apk_sha256': hashlib.sha256(original).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apk', type=Path, required=True)
    parser.add_argument('--source-tree', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, default=ROOT / '.tools/android-sdk')
    parser.add_argument('--jdk', type=Path, default=ROOT / '.tools/jdk-21/Contents/Home')
    parser.add_argument('--libcxx', action='store_true')
    parser.add_argument('--release', action='store_true', help='Reproduce the explicit 0.1.1-preview release manifest transformation')
    parser.add_argument('--inputs-cache', type=Path, default=ROOT / '.tools/publication-sources')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists(): raise ValueError('Output exists; choose a new path')
    env = dict(os.environ, JAVA_HOME=str(args.jdk.resolve()),
               PATH=str(args.jdk.resolve() / 'bin') + os.pathsep + os.environ['PATH'])
    def run(*command): subprocess.run(list(map(str, command)), check=True, env=env)
    report = {'label': 'observed-payload-reproduction', 'date': date.today().isoformat(),
              'apk_sha256': digest(args.apk), 'complete_source_correspondence': False,
              'libcxx_source_rebuilt': False, 'limits': [
                  'Prepared toolchains are reused; this is not a wholly fresh-machine build.',
                  'Java/manifest equality and runtime packaging equality do not attest other native source builds.']}
    bt = args.sdk.resolve() / 'build-tools/36.0.0'
    android = args.sdk.resolve() / 'platforms/android-36/android.jar'
    with tempfile.TemporaryDirectory(prefix='henge-payload-proof-') as folder, zipfile.ZipFile(args.apk) as apk:
        work = Path(folder); classes = work / 'classes'; classes.mkdir()
        dex = work / 'dex'; dex.mkdir()
        resource_args = []
        if (args.source_tree / 'android/res').is_dir():
            run(bt / 'aapt2', 'compile', '--dir', args.source_tree / 'android/res', '-o', work / 'resources.zip')
            resource_args = [work / 'resources.zip']
        manifest_source = args.source_tree / 'android/AndroidManifest.xml'
        if args.release:
            from release_signing import manifest as release_manifest
            manifest_source = release_manifest(manifest_source, work / 'release/AndroidManifest.xml', '0.1.1-preview', 2)
        run(bt / 'aapt2', 'link', '-I', android, '--manifest',
            manifest_source, '--java', work / 'generated', '-o', work / 'manifest.apk', *resource_args)
        java = sorted((args.source_tree / 'android/src').rglob('*.java')) + sorted((work / 'generated').rglob('*.java'))
        run(args.jdk.resolve() / 'bin/javac', '--release', '8', '-encoding', 'UTF-8',
            '-classpath', android, '-d', classes, *java)
        run(bt / 'd8', '--min-api', '28', '--lib', android, '--output', dex, *sorted(classes.rglob('*.class')))
        report['classes.dex'] = compare(dex / 'classes.dex', apk.read('classes.dex'))
        with zipfile.ZipFile(work / 'manifest.apk') as manifest:
            raw = manifest.read('AndroidManifest.xml'); target = work / 'AndroidManifest.xml'; target.write_bytes(raw)
            if resource_args:
                resource_members = [name for name in manifest.namelist() if name == 'resources.arsc' or name.startswith('res/')]
                report['resources'] = dict(equal=all(manifest.read(name) == apk.read(name) for name in resource_members), members=resource_members)
        report['AndroidManifest.xml'] = compare(target, apk.read('AndroidManifest.xml'))
        if args.libcxx:
            inventory = json.loads((ROOT / 'android/publication/graphics-sources.json').read_text())
            item = next(i for i in inventory['inputs'] if i['id'] == 'termux-elf-cleaner-source')
            archive = args.inputs_cache / 'archives' / item['file']
            if digest(archive) != item['sha256']: raise ValueError('ELF cleaner source checksum mismatch')
            with tarfile.open(archive) as source: source.extractall(work, filter='data')
            source = work / ('termux-elf-cleaner-' + item['version'])
            run('cmake', '-S', source, '-B', work / 'cleaner', '-DCMAKE_BUILD_TYPE=Release')
            run('cmake', '--build', work / 'cleaner', '-j', '2')
            llvm = args.sdk.resolve() / 'ndk/30.0.16248370/toolchains/llvm/prebuilt/darwin-x86_64'
            runtime = llvm / 'sysroot/usr/lib/aarch64-linux-android/libc++_shared.so'
            record = json.loads((ROOT / 'android/publication/ndk-runtime.json').read_text())
            if digest(runtime) != record['runtime_sha256']: raise ValueError('NDK runtime checksum mismatch')
            output = work / 'libc++_shared.so'; shutil.copy2(runtime, output)
            run(work / 'cleaner/termux-elf-cleaner', '--api-level', '24', output)
            run(llvm / 'bin/llvm-strip', '--strip-unneeded', '--preserve-dates', output)
            report['libcxx_packaging'] = {**compare(output, apk.read('lib/arm64-v8a/libhenge_lvp_libc___shared_so.so')),
                'source_runtime_sha256': record['runtime_sha256'], 'cleaner_source_sha256': item['sha256'],
                'api_level': 24, 'strip': '--strip-unneeded --preserve-dates', 'source_rebuilt': False}
    report['failures'] = sum(not report[k]['equal'] for k in ['classes.dex', 'AndroidManifest.xml'] +
                             (['libcxx_packaging'] if args.libcxx else []))
    if 'resources' in report: report['failures'] += not report['resources']['equal']
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print('SOURCE_PAYLOAD_REPRODUCE_RESULT failures=' + str(report['failures']) +
          ' dex=' + str(int(report['classes.dex']['equal'])) +
          ' manifest=' + str(int(report['AndroidManifest.xml']['equal'])) +
          ' libcxx_packaging=' + str(int(report.get('libcxx_packaging', {}).get('equal', False))) +
          ' libcxx_source_rebuilt=0')
    return bool(report['failures'])


if __name__ == '__main__':
    try: sys.exit(main())
    except (OSError, ValueError, tarfile.TarError, subprocess.CalledProcessError) as error:
        sys.exit('SOURCE_PAYLOAD_REPRODUCE_ERROR ' + str(error))
