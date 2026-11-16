#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepare an isolated, staged local Git source repository. Never create remotes or publish."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
from export_android_source import ROOT, collect


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'android/artifacts/github-source')
    parser.add_argument('--deny-literal', action='append', default=[])
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() or ROOT.resolve().is_relative_to(output):
        raise SystemExit('Use a new, empty output path; existing trees are never overwritten.')
    files = collect(ROOT, [value.encode() for value in args.deny_literal if value])
    output.mkdir(parents=True)
    for name, raw in files.items():
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(raw)
    env = {key: value for key, value in os.environ.items() if not key.startswith('GIT_')}
    subprocess.run(['git', 'init', '--initial-branch=main', output], check=True, env=env, stdout=subprocess.PIPE)
    subprocess.run(['git', '-C', output, 'add', '--all'], check=True, env=env)
    # Do not rewrite upstream legal texts or mandatory patch-context whitespace.
    subprocess.run(['git', '-C', output, 'diff', '--cached', '--check', '--', '.',
                    ':(exclude)third_party', ':(exclude)LICENSES', ':(exclude)LICENSE',
                    ':(glob,exclude)**/*.patch'], check=True, env=env)
    if subprocess.check_output(['git', '-C', output, 'remote'], env=env).strip():
        raise SystemExit('Unexpected remote; no publication performed.')
    subprocess.run([os.sys.executable, output / 'tools/check_publication.py', '--tree', output, '--staged'], check=True)
    print('GITHUB_PREPARE_RESULT failures=0 scope=source-only staged=1 commit=none remote=none published=0')
    print(output)
    print('manifest_sha256=' + hashlib.sha256(files['SOURCE-MANIFEST.json']).hexdigest())


if __name__ == '__main__':
    main()
