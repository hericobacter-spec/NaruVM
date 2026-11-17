#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Release metadata and private-signing boundary checks; no real key material."""
import argparse
from pathlib import Path
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import release_signing as release
from repackage_apk import replace


class ReleaseSigningTests(unittest.TestCase):
    def args(self, *values):
        parser = argparse.ArgumentParser()
        release.configure(parser)
        return parser.parse_args(values)

    def test_release_never_falls_back_to_debug_key(self):
        with self.assertRaisesRegex(ValueError, 'requires'):
            release.validate(self.args('--release'))

    def test_signing_inputs_require_explicit_release(self):
        with self.assertRaisesRegex(ValueError, 'require --release'):
            release.validate(self.args('--key-alias', 'fixture'))

    def test_private_permissions_and_version_code(self):
        with tempfile.TemporaryDirectory() as folder:
            key, password = Path(folder) / 'key', Path(folder) / 'password'
            for path in [key, password]:
                path.write_bytes(b'fixture, not key material')
                path.chmod(0o600)
            values = ['--release', '--keystore', str(key), '--ks-pass-file', str(password), '--key-alias', 'fixture']
            release.validate(self.args(*values))
            password.chmod(0o644)
            with self.assertRaisesRegex(ValueError, 'owner-only'):
                release.validate(self.args(*values))
            password.chmod(0o600)
            with self.assertRaisesRegex(ValueError, 'greater than'):
                release.validate(self.args(*values, '--version-code', '1'))

    def test_release_manifest_preserves_app_and_blocks_debugging(self):
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as folder:
            output = release.manifest(root / 'android/AndroidManifest.xml', Path(folder) / 'AndroidManifest.xml', '0.1.1-preview', 2)
            original, result = ET.parse(root / 'android/AndroidManifest.xml'), ET.parse(output)
            self.assertEqual(result.getroot().get('package'), original.getroot().get('package'))
            self.assertEqual(result.find('application').get(release.ANDROID + 'debuggable'), 'false')
            self.assertEqual(result.getroot().get(release.ANDROID + 'versionCode'), '2')
            self.assertEqual(len(result.findall('uses-permission')), len(original.findall('uses-permission')))
            self.assertEqual(result.find('application').get(release.ANDROID + 'allowBackup'), 'false')

    def test_source_information_names_both_archives_and_no_key(self):
        text = release.source_information('0.1.1-preview')
        self.assertIn('preview-source.tar.gz', text)
        self.assertIn('preview-dependency-sources.tar.gz', text)
        self.assertIn('your own key', text)
        self.assertNotIn('pass:', text)

    def test_pkcs12_password_file_is_consumed_once(self):
        args = self.args('--keystore', 'private.p12', '--ks-pass-file', 'private.txt', '--key-alias', 'fixture')
        options = release.signing_arguments(args)
        self.assertEqual(options.count('file:private.txt'), 1)
        self.assertNotIn('--key-pass', options)

    def test_recipient_repackage_preserves_app_and_replaces_only_library(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            original, result, library = root / 'original.apk', root / 'modified.apk', root / 'library.so'
            raw = bytearray(64)
            raw[:6], raw[18:20] = b'\x7fELF\x02\x01', b'\xb7\x00'
            library.write_bytes(raw + b'recipient change')
            with zipfile.ZipFile(original, 'w') as apk:
                for name, content in [('classes.dex', b'unchanged dex'), ('lib/arm64-v8a/libfixture.so', b'original'),
                                      ('META-INF/CERT.RSA', b'old signature'), ('META-INF/LICENSE', b'retained notice')]:
                    apk.writestr(name, content)
            replace(original, result, {'libfixture.so': library})
            with zipfile.ZipFile(result) as apk:
                self.assertEqual(apk.read('classes.dex'), b'unchanged dex')
                self.assertEqual(apk.read('lib/arm64-v8a/libfixture.so'), library.read_bytes())
                self.assertEqual(apk.read('META-INF/LICENSE'), b'retained notice')
                self.assertNotIn('META-INF/CERT.RSA', apk.namelist())
            with zipfile.ZipFile(original) as apk:
                self.assertEqual(apk.read('lib/arm64-v8a/libfixture.so'), b'original')

    def test_recipient_repackage_rejects_wrong_abi_and_new_paths(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            original, library = root / 'original.apk', root / 'library.so'
            library.write_bytes(b'not an ARM64 ELF')
            with zipfile.ZipFile(original, 'w') as apk:
                apk.writestr('lib/arm64-v8a/libfixture.so', b'original')
            for i, (name, error) in enumerate([('libfixture.so', 'ARM64 ELF'), ('../libfixture.so', 'basename'), ('libnew.so', 'not in the original')]):
                with self.assertRaisesRegex(ValueError, error):
                    replace(original, root / ('result' + str(i)), {name: library})


if __name__ == '__main__':
    unittest.main()
