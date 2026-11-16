#!/usr/bin/env python3
"""Packaging boundary tests using a synthetic ELF, never a GPU driver."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from turnip_bundle import validate


class TurnipManifestTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        raw = bytearray(128)
        raw[:6] = b'\x7fELF\x02\x01'
        struct.pack_into('<H', raw, 18, 183)
        struct.pack_into('<Q', raw, 32, 64)
        struct.pack_into('<HH', raw, 54, 56, 1)
        struct.pack_into('<I', raw, 64, 1)
        struct.pack_into('<Q', raw, 112, 16384)
        (self.root / 'driver.so').write_bytes(raw)
        (self.root / 'LICENSE').write_text('Synthetic fixture license\n')
        (self.root / 'recipe.txt').write_text('Synthetic fixture; not a runnable driver\n')
        self.data = dict(version='fixture', source_url='https://example.com/source',
                         source_commit='a' * 40, sha256=hashlib.sha256(raw).hexdigest(),
                         driver='driver.so', license='MIT', license_file='LICENSE', build_recipe='recipe.txt')

    def stage(self):
        path = self.root / 'manifest.json'
        path.write_text(json.dumps(self.data))
        return validate(path)

    def test_public_metadata_excludes_local_paths(self):
        driver, assets = self.stage()
        self.assertEqual(driver.name, 'driver.so')
        public = json.loads(assets['provenance.json'])
        self.assertNotIn('driver', public)
        self.assertNotIn(str(self.root), json.dumps(public))
        self.assertIn('unverified', public['runtime_status'])

    def test_wrong_digest(self):
        self.data['sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'SHA256 mismatch'): self.stage()

    def test_arch_and_alignment(self):
        for offset, value, message in [(18, 62, 'AArch64'), (112, 4096, '16KiB')]:
            with self.subTest(message=message):
                raw = bytearray((self.root / 'driver.so').read_bytes())
                struct.pack_into('<H' if offset == 18 else '<Q', raw, offset, value)
                (self.root / 'driver.so').write_bytes(raw)
                self.data['sha256'] = hashlib.sha256(raw).hexdigest()
                with self.assertRaisesRegex(ValueError, message): self.stage()
                self.setUp()

    def test_source_attribution(self):
        for key, value in [('source_url', 'https://name:secret@example.com/source'),
                           ('source_url', 'https://example.com/source?token=secret'),
                           ('source_commit', 'main'), ('license', '')]:
            with self.subTest(key=key, value=value):
                old = self.data[key]
                self.data[key] = value
                with self.assertRaises(ValueError): self.stage()
                self.data[key] = old

    def test_missing_license(self):
        (self.root / 'LICENSE').unlink()
        with self.assertRaises(FileNotFoundError): self.stage()

    def test_invalid_headers(self):
        raw = bytearray((self.root / 'driver.so').read_bytes())
        struct.pack_into('<Q', raw, 32, 10000)
        (self.root / 'driver.so').write_bytes(raw)
        self.data['sha256'] = hashlib.sha256(raw).hexdigest()
        with self.assertRaisesRegex(ValueError, 'program headers'): self.stage()


if __name__ == '__main__':
    unittest.main()
