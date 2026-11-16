#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the offline GitHub source-preview gate from an exported/staged source tree.

Requires Python 3.12+, Git, a C compiler and JDK21. No network, Android SDK,
device, guest, commit, remote mutation, signing or upload is used. An exit-zero
result covers local source checks only; it never approves APK distribution.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from export_android_source import ROOT, manifest_entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, help='Optional new JSON result path; existing results are preserved')
    args = parser.parse_args()
    if args.output and args.output.exists(): raise SystemExit('Use a new result path; existing results are preserved.')
    manifest_path = ROOT / 'SOURCE-MANIFEST.json'
    if not manifest_path.is_file(): raise SystemExit('First export/stage the source with tools/prepare_github.py; run this gate there.')
    manifest_raw = manifest_path.read_bytes(); entries = manifest_entries(manifest_raw)
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
    checks = []

    def run(arguments, name):
        process = subprocess.run(list(map(str, arguments)), cwd=ROOT, env=env,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        checks.append(dict(check=name, result='pass' if process.returncode == 0 else 'fail',
                           exit_code=process.returncode, output=process.stdout))
        print(process.stdout, end='', flush=True)
        return process.returncode == 0

    steps = [([sys.executable, 'tools/check_publication.py', '--staged'], 'source-and-git-index'),
             ([sys.executable, 'tools/check_android_portability.py'], 'host-policy'),
             *[([sys.executable, 'android/tests/' + name + '.py'], name) for name in
               ['test_publication', 'test_publication_sources', 'test_source_correspondence', 'test_source_archive']],
             (['git', 'fsck', '--strict', '--no-reflogs'], 'git-object-integrity')]
    passed = all([run(command, name) for command, name in steps])
    with tempfile.TemporaryDirectory(prefix='henge-final-source-') as temporary:
        paths = [Path(temporary) / (name + '.tar.gz') for name in ['first', 'second']]
        for i, path in enumerate(paths):
            passed = run([sys.executable, 'tools/export_android_source.py', '--output', path],
                         'archive-build-' + str(i + 1)) and passed
            if path.is_file():
                passed = run([sys.executable, 'tools/export_android_source.py', '--verify', path],
                             'archive-verify-' + str(i + 1)) and passed
        hashes = [hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None for path in paths]
        deterministic = hashes[0] is not None and hashes[0] == hashes[1]
        checks.append(dict(check='deterministic-archive', result='pass' if deterministic else 'fail', sha256=hashes))
        passed = deterministic and passed
    passed = run([sys.executable, 'tools/check_publication.py', '--staged'], 'source-and-index-after-checks') and passed
    unchanged = manifest_path.read_bytes() == manifest_raw
    checks.append(dict(check='manifest-preserved', result='pass' if unchanged else 'fail'))
    passed = unchanged and passed
    report = dict(format=1, label='observed-local-source-checks', checked_at=datetime.now(timezone.utc).isoformat(),
                  result='pass' if passed else 'fail', scope='source-only', binary_release_ready=False,
                  published=False, source_files=len(entries), source_bytes=sum(item['bytes'] for item in entries.values()),
                  largest_file_bytes=max(item['bytes'] for item in entries.values()),
                  manifest_sha256=hashlib.sha256(manifest_raw).hexdigest(), checks=checks,
                  limits=['Local host checks do not execute GitHub-hosted CI or an Android guest.',
                          'Bounded privacy/integrity checks are not legal clearance or a complete APK source offer.'])
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open('x') as destination: json.dump(report, destination, indent=2); destination.write('\n')
    print('GITHUB_SOURCE_GATE_RESULT failures=' + ('0' if passed else '1') +
          ' scope=source-only files=' + str(len(entries)) + ' binary_release_ready=0 published=0')
    return 0 if passed else 1


if __name__ == '__main__': sys.exit(main())
