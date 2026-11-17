#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Replace an existing ARM64 native member, align and sign with a recipient key.

Uses an unsigned ZIP copy so an old APK signing block cannot survive replacement.
No publisher key, device access, package install or guest data is used.
"""
import argparse
from pathlib import Path, PurePosixPath
import os
import subprocess
import tempfile
import zipfile
import release_signing


def replace(source, output, replacements):
    with zipfile.ZipFile(source) as original, zipfile.ZipFile(output, 'x') as modified:
        names = original.namelist()
        if len(names) != len(set(names)):
            raise ValueError('Duplicate APK member')
        for name, path in replacements.items():
            if PurePosixPath(name).name != name or not name.startswith('lib') or not name.endswith('.so'):
                raise ValueError('Replacement must name one native library basename')
            if 'lib/arm64-v8a/' + name not in names:
                raise ValueError('Replacement is not in the original APK')
            raw = path.read_bytes()
            if len(raw) < 64 or raw[:6] != b'\x7fELF\x02\x01' or raw[18:20] != b'\xb7\x00':
                raise ValueError('Replacement must be a little-endian ARM64 ELF')
        for item in original.infolist():
            relative = PurePosixPath(item.filename)
            if relative.is_absolute() or '..' in relative.parts:
                raise ValueError('Unsafe APK member')
            if item.filename.upper().startswith('META-INF/') and item.filename.upper().endswith(('.RSA', '.DSA', '.EC', '.SF', 'MANIFEST.MF')):
                continue
            replacement = replacements.get(relative.name) if str(relative.parent) == 'lib/arm64-v8a' else None
            modified.writestr(item, replacement.read_bytes() if replacement else original.read(item.filename))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apk', type=Path, required=True)
    parser.add_argument('--replace-native', action='append', required=True, metavar='LIBNAME=FILE')
    parser.add_argument('--sdk', type=Path, default=Path(os.environ.get('ANDROID_SDK_ROOT', Path(__file__).resolve().parents[1] / '.tools/android-sdk')))
    release_signing.configure(parser)
    args = parser.parse_args()
    try:
        release_signing.validate(args)
        if not args.release or not args.output or args.output.exists():
            raise ValueError('Use --release and a new --output file')
        replacements = {}
        for value in args.replace_native:
            name, filename = value.split('=', 1)
            if name in replacements:
                raise ValueError('Duplicate replacement')
            replacements[name] = Path(filename)
        tools = args.sdk / 'build-tools/36.0.0'
        jdk = Path(os.environ.get('JAVA_HOME', args.sdk.resolve().parent / 'jdk-21/Contents/Home'))
        env = dict(os.environ, JAVA_HOME=str(jdk), PATH=str(jdk / 'bin') + os.pathsep + os.environ['PATH'])
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='naruvm-recipient-') as folder:
            unsigned, aligned = Path(folder) / 'unsigned.apk', Path(folder) / 'aligned.apk'
            replace(args.apk, unsigned, replacements)
            subprocess.run([tools / 'zipalign', '-P', '16', '4', unsigned, aligned], check=True, env=env)
            subprocess.run([tools / 'apksigner', 'sign', *release_signing.signing_arguments(args), '--out', args.output, aligned], check=True, env=env)
        subprocess.run([tools / 'apksigner', 'verify', '--verbose', args.output], check=True, env=env)
        subprocess.run([tools / 'zipalign', '-c', '-P', '16', '4', args.output], check=True, env=env)
    except (ValueError, OSError, zipfile.BadZipFile) as error:
        parser.error(str(error))
    print('RECIPIENT_APK_RESULT failures=0 replaced=' + str(len(replacements)) + ' installed=0')


if __name__ == '__main__':
    main()
