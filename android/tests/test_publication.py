#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Negative publication-boundary tests on an isolated source fixture; no network."""
from contextlib import contextmanager, redirect_stdout
import io
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from check_publication import check
from export_android_source import collect


class PublicationBoundaryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='henge-publication-')
        cls.tree = Path(cls.temporary.name) / 'source'
        for name, raw in collect(ROOT, []).items():
            target = cls.tree / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(raw)
        subprocess.run(['git', 'init', '--initial-branch=main', cls.tree], check=True, stdout=subprocess.DEVNULL)
        subprocess.run(['git', '-C', cls.tree, 'add', '--all'], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    @contextmanager
    def changed(self, name, raw):
        path = self.tree / name
        old = path.read_bytes() if path.exists() else None
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(raw)
        try:
            yield path
        finally:
            if old is None: path.unlink()
            else: path.write_bytes(old)

    def run_check(self):
        with redirect_stdout(io.StringIO()):
            check(self.tree, [])

    def test_clean_fixture(self):
        self.run_check()

    def test_ignored_private_and_build_state_stays_out(self):
        with self.changed('memory/private.c', b'private fixture'), \
             self.changed('android/artifacts/fixture.c', b'not source'), \
             self.changed('.github/workflows/build-henge.yml', b'old release workflow'):
            self.run_check()
            selected = collect(self.tree, [])
            self.assertFalse(any(name.startswith('memory/') for name in selected))
            self.assertNotIn('android/artifacts/fixture.c', selected)
            self.assertNotIn('.github/workflows/build-henge.yml', selected)

    def test_private_host_path_in_source_rejected(self):
        private = ('/' + 'Users/' + 'fixture/' + 'private.c').encode()
        with self.changed('android/native/publication-leak.c', private):
            with self.assertRaisesRegex(ValueError, 'Private-data'):
                self.run_check()

    def test_credential_shaped_fixture_rejected(self):
        token = b'gh' + b'p_' + b'x' * 36
        with self.changed('android/native/publication-leak.c', token):
            with self.assertRaisesRegex(ValueError, 'Credential-shaped'):
                self.run_check()

    def test_symlink_source_rejected(self):
        path = self.tree / 'android/native/publication-link.c'
        path.symlink_to(self.tree / 'README.md')
        try:
            with self.assertRaisesRegex(ValueError, 'symlinks'):
                self.run_check()
        finally:
            path.unlink()

    def test_changed_license_rejected(self):
        with self.changed('LICENSES/PanVK-MIT.txt', b'missing original notice'):
            with self.assertRaisesRegex(ValueError, 'License notice'):
                self.run_check()

    def test_changed_patch_rejected(self):
        with self.changed('android/reims/android.patch', b'wrong revision patch'):
            with self.assertRaisesRegex(ValueError, 'Pinned patch'):
                self.run_check()

    def test_unpinned_workflow_rejected(self):
        name = '.github/workflows/android-source.yml'
        raw = (self.tree / name).read_bytes()
        import re
        raw = re.sub(rb'actions/checkout@[0-9a-f]{40}', b'actions/checkout@main', raw)
        with self.changed(name, raw):
            with self.assertRaisesRegex(ValueError, 'full verified commit'):
                self.run_check()

    def test_false_binary_recipe_attestation_rejected(self):
        import json
        name = 'android/publication/dependencies.json'
        data = json.loads((self.tree / name).read_text())
        data['external_binary_packages'][0]['exact_recipe_commit'] = '0' * 40
        with self.changed(name, json.dumps(data).encode()):
            with self.assertRaisesRegex(ValueError, 'attestation is not established'):
                self.run_check()

    def test_stale_manifest_rejected(self):
        with self.changed('README.md', (self.tree / 'README.md').read_bytes() + b'\nEdited fixture\n'):
            with self.assertRaisesRegex(ValueError, 'manifest stale'):
                self.run_check()

    def test_tracked_private_file_outside_export_rejected(self):
        name = 'memory/accidentally-staged.txt'
        with self.changed(name, b'private fixture; must not be committed'):
            subprocess.run(['git', '-C', self.tree, 'add', '-f', name], check=True)
            try:
                with self.assertRaisesRegex(ValueError, 'Unreviewed tracked files'):
                    self.run_check()
            finally:
                subprocess.run(['git', '-C', self.tree, 'update-index', '--force-remove', name], check=True)

    def test_staged_bytes_differ_from_restored_worktree_rejected(self):
        name = 'README.md'
        with self.changed(name, (self.tree / name).read_bytes() + b'\nUnreviewed staged fixture\n'):
            subprocess.run(['git', '-C', self.tree, 'add', name], check=True)
        try:
            with self.assertRaisesRegex(ValueError, 'Git index bytes differ'):
                with redirect_stdout(io.StringIO()): check(self.tree, [], staged=True)
        finally:
            subprocess.run(['git', '-C', self.tree, 'add', name], check=True)

    def test_wrong_manifest_size_rejected(self):
        import json
        name = 'SOURCE-MANIFEST.json'
        manifest = json.loads((self.tree / name).read_bytes())
        manifest['files'][0]['bytes'] += 1
        with self.changed(name, json.dumps(manifest).encode()):
            with self.assertRaisesRegex(ValueError, 'manifest stale'):
                self.run_check()

    def test_clean_git_index(self):
        with redirect_stdout(io.StringIO()): check(self.tree, [], staged=True)


if __name__ == '__main__':
    unittest.main()
