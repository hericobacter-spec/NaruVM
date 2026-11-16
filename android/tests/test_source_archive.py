#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Reject self-consistent but invalid source-preview archives; offline fixtures."""
from contextlib import redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from export_android_source import manifest_entries, verify


class SourceArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'fixture.tar.gz'
        self.raw = b'# Source fixture\n'
        self.manifest = dict(format=2, label='observed-source-selection', release_scope='source-only',
                             binary_release_ready=False, files=[dict(path='README.md',
                             sha256=hashlib.sha256(self.raw).hexdigest(), bytes=len(self.raw))])

    def archive(self, name='README.md', raw=None, duplicate=False):
        if raw is None: raw = self.raw
        with tarfile.open(self.path, 'w:gz') as archive:
            members = [(name, raw), ('SOURCE-MANIFEST.json', json.dumps(self.manifest).encode())]
            if duplicate: members.append((name, raw))
            for path, content in members:
                item = tarfile.TarInfo(path); item.size = len(content)
                archive.addfile(item, io.BytesIO(content))

    def test_valid_source_integrity(self):
        self.archive()
        with redirect_stdout(io.StringIO()): verify(self.path)

    def test_false_binary_release_scope_rejected(self):
        self.manifest['binary_release_ready'] = True; self.archive()
        with self.assertRaisesRegex(ValueError, 'scope/format'): verify(self.path)

    def test_duplicate_manifest_path_rejected(self):
        self.manifest['files'] *= 2; self.archive()
        with self.assertRaisesRegex(ValueError, 'Duplicate/reserved'): verify(self.path)

    def test_traversal_and_noncanonical_paths_rejected(self):
        for name in ['../outside', '/outside', 'third_party/../README.md', 'a//b', 'a\\b']:
            with self.subTest(name=name):
                self.manifest['files'][0]['path'] = name; self.archive(name)
                with self.assertRaisesRegex(ValueError, 'Invalid source path'): verify(self.path)

    def test_binary_even_with_matching_hash_rejected(self):
        raw = b'\x7fELF authored fixture'
        self.manifest['files'][0].update(sha256=hashlib.sha256(raw).hexdigest(), bytes=len(raw))
        self.archive(raw=raw)
        with self.assertRaisesRegex(ValueError, 'Unexpected binary'): verify(self.path)

    def test_wrong_size_rejected(self):
        self.manifest['files'][0]['bytes'] += 1; self.archive()
        with self.assertRaisesRegex(ValueError, 'content mismatch'): verify(self.path)

    def test_duplicate_archive_member_rejected(self):
        self.archive(duplicate=True)
        with self.assertRaisesRegex(ValueError, 'Duplicate archive'): verify(self.path)

    def test_invalid_entry_shapes_rejected(self):
        for key, value in [('path', None), ('bytes', True), ('bytes', -1), ('sha256', 'invalid')]:
            with self.subTest(key=key, value=value):
                manifest = json.loads(json.dumps(self.manifest)); manifest['files'][0][key] = value
                with self.assertRaises(ValueError): manifest_entries(json.dumps(manifest))


if __name__ == '__main__': unittest.main()
