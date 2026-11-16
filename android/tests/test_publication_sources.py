#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Source acquisition/bundle integrity failures on tiny offline fixtures."""
import gzip
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
import subprocess
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import fetch_publication_sources as collect
import package_publication_sources as bundle


class SourceIntegrityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.cache = Path(self.temp.name)
        self.archives = self.cache / 'archives'
        self.archives.mkdir()
        self.raw = b'source fixture\n'
        self.item = {'id': 'fixture', 'file': 'fixture.tar.gz', 'kind': 'source-archive',
                     'sha256': hashlib.sha256(self.raw).hexdigest(), 'url': 'https://example.invalid/source'}

    def test_offline_missing_never_downloads(self):
        with patch('urllib.request.urlopen', side_effect=AssertionError('network used')):
            with self.assertRaisesRegex(ValueError, 'unavailable offline'):
                collect.fetch(self.item, self.archives, False)

    def test_bad_existing_source_is_preserved(self):
        path = self.archives / self.item['file']
        path.write_bytes(b'corrupt')
        with self.assertRaisesRegex(ValueError, 'Existing source checksum'):
            collect.fetch(self.item, self.archives, True)
        self.assertEqual(path.read_bytes(), b'corrupt')

    def test_download_checksum_failure_cleans_partial(self):
        with patch('urllib.request.urlopen', return_value=io.BytesIO(b'wrong bytes')):
            with self.assertRaisesRegex(ValueError, 'Downloaded source checksum'):
                collect.fetch(self.item, self.archives, True)
        self.assertEqual(list(self.archives.iterdir()), [])

    def test_traversal_name_rejected(self):
        self.item['file'] = '../outside'
        with self.assertRaisesRegex(ValueError, 'basename'):
            collect.fetch(self.item, self.archives, False)

    def test_recipe_content_hash_required(self):
        with gzip.open(self.archives / 'recipe.tar.gz', 'wb') as source: source.write(b'wrong recipe')
        with self.assertRaisesRegex(ValueError, 'source content mismatch'):
            collect.recipes({'archive_file': 'recipe.tar.gz', 'archive_tar_sha256': '0' * 64}, self.cache, None, False)

    def test_git_snapshot_uses_pin_and_prefix_not_worktree(self):
        repo = self.cache / 'git-source'; repo.mkdir()
        subprocess.run(['git', 'init', repo], check=True, capture_output=True)
        (repo / 'source.c').write_text('pinned source\n')
        subprocess.run(['git', '-C', repo, 'add', 'source.c'], check=True)
        subprocess.run(['git', '-C', repo, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                        'commit', '--no-gpg-sign', '-m', 'fixture'], check=True, capture_output=True)
        commit = subprocess.check_output(['git', '-C', repo, 'rev-parse', 'HEAD'], text=True).strip()
        raw = subprocess.check_output(['git', '-C', repo, 'archive', '--format=tar', '--prefix=graphics/', commit])
        (repo / 'source.c').write_text('private working-tree change\n')
        record = {'archive_file': 'graphics.tar.gz', 'commit': commit, 'prefix': 'graphics/',
                  'archive_tar_sha256': hashlib.sha256(raw).hexdigest()}
        collect.recipes(record, self.cache, repo, False)
        with tarfile.open(self.archives / record['archive_file']) as archive:
            self.assertEqual(archive.extractfile('graphics/source.c').read(), b'pinned source\n')

    def test_bundle_tamper_and_extra_file_rejected(self):
        lock_path = self.cache / 'lock.json'
        recipe = b'git archive fixture'
        lock = {'repository': {'archive_file': 'recipe.tar.gz', 'archive_tar_sha256': hashlib.sha256(recipe).hexdigest()},
                'inputs': [self.item]}
        lock_path.write_text(json.dumps(lock))
        (self.archives / self.item['file']).write_bytes(self.raw)
        with gzip.open(self.archives / 'recipe.tar.gz', 'wb') as source: source.write(recipe)
        entries = {'termux-sources.json': lock_path, 'archives/fixture.tar.gz': self.archives / self.item['file'],
                   'archives/recipe.tar.gz': self.archives / 'recipe.tar.gz'}
        good = self.cache / 'good.tar.gz'
        with patch.object(bundle, 'LOCK_PATH', lock_path):
            bundle.package(good, entries)
            bundle.verify(good, lock)
            for name, content, error in [('archives/fixture.tar.gz', b'changed', 'checksum mismatch'),
                                         ('../unexpected', b'extra', 'file set mismatch')]:
                bad = self.cache / ('bad-' + str(len(content)) + '.tar.gz')
                with tarfile.open(good) as original, tarfile.open(bad, 'w:gz') as changed:
                    for member in original.getmembers():
                        data = content if member.name == name else original.extractfile(member).read()
                        member.size = len(data)
                        changed.addfile(member, io.BytesIO(data))
                    if name.startswith('../'):
                        member = tarfile.TarInfo(name); member.size = len(content)
                        changed.addfile(member, io.BytesIO(content))
                with self.assertRaisesRegex(ValueError, error): bundle.verify(bad, lock)


if __name__ == '__main__': unittest.main()
