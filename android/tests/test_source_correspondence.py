#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Negative origin-audit checks; tiny fixtures, no downloads/builds/devices."""
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from audit_source_correspondence import digest, native_rows, ar_members


class OriginBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name); self.native = self.root / 'native'; self.native.mkdir()
        self.apk = self.root / 'fixture.apk'; self.raw = b'\x7fELF authored fixture'

    def archive(self, entries):
        with zipfile.ZipFile(self.apk, 'w') as apk:
            for name, raw in entries: apk.writestr(name, raw)

    def test_renamed_raw_package_member_preserves_unattested_status(self):
        self.archive([('lib/arm64-v8a/librenamed.so', self.raw)])
        rows = native_rows(self.apk, self.native, {digest(self.raw): [
            {'package': 'fixture', 'binary_producing_recipe_attested': False}]}, self.root)
        self.assertEqual(rows[0]['origin'], 'pinned-deb-byte-identical')
        self.assertFalse(rows[0]['package_matches'][0]['binary_producing_recipe_attested'])

    def test_changed_bytes_cannot_match_package_identity(self):
        self.archive([('lib/arm64-v8a/librenamed.so', self.raw + b' changed')])
        with self.assertRaises(OSError):
            native_rows(self.apk, self.native, {digest(self.raw): [{'package': 'fixture'}]}, self.root)

    def test_existing_output_without_build_recipe_is_rejected(self):
        self.archive([('lib/arm64-v8a/libunknown.so', self.raw)])
        (self.native / 'libunknown.so').write_bytes(self.raw)
        with self.assertRaisesRegex(ValueError, 'No build recipe'):
            native_rows(self.apk, self.native, {}, self.root)

    def test_mismatched_existing_output_is_rejected(self):
        self.archive([('lib/arm64-v8a/libhenge_gpu.so', self.raw)])
        (self.native / 'libhenge_gpu.so').write_bytes(self.raw + b' changed')
        with self.assertRaisesRegex(ValueError, 'does not match local build output'):
            native_rows(self.apk, self.native, {}, self.root)

    def test_duplicate_apk_members_are_rejected(self):
        import warnings
        with warnings.catch_warnings():
            warnings.simplefilter('ignore', UserWarning)
            self.archive([('lib/arm64-v8a/libsame.so', self.raw)] * 2)
        with self.assertRaisesRegex(ValueError, 'Duplicate APK'):
            native_rows(self.apk, self.native, {}, self.root)

    def test_truncated_ar_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Invalid Debian member header'):
            list(ar_members(b'!<arch>\nshort'))


if __name__ == '__main__': unittest.main()
