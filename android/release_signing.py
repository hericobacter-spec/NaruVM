#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Explicit release metadata/signing; never create or embed a publisher key."""
from pathlib import Path
import re
import stat
import hashlib
import shutil
import subprocess
import xml.etree.ElementTree as ET

ANDROID = '{http://schemas.android.com/apk/res/android}'


def configure(parser):
    parser.add_argument('--release', action='store_true', help='Require explicit private signing inputs and disable Android debugging')
    parser.add_argument('--version-name', default='0.1.1-preview', help='Release version name')
    parser.add_argument('--version-code', type=int, default=2, help='Release Android update version code')
    parser.add_argument('--keystore', type=Path, help='Private release keystore, never bundled')
    parser.add_argument('--ks-pass-file', type=Path, help='Private password file with owner-only permissions; never printed or bundled')
    parser.add_argument('--key-alias', help='Alias in the release keystore')
    parser.add_argument('--output', type=Path, help='APK output path; refuses to overwrite an existing file')


def validate(args):
    if not args.release:
        if args.keystore or args.ks_pass_file or args.key_alias:
            raise ValueError('Signing inputs require --release')
        return
    if not args.keystore or not args.ks_pass_file or not args.key_alias:
        raise ValueError('--release requires --keystore, --ks-pass-file and --key-alias')
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?', args.version_name):
        raise ValueError('Invalid release version name')
    if not 1 < args.version_code <= 2_100_000_000:
        raise ValueError('Release version code must be greater than the research code 1')
    for path in [args.keystore, args.ks_pass_file]:
        if not path.is_file() or path.is_symlink():
            raise ValueError('Private signing input must be an existing regular file')
        if stat.S_IMODE(path.stat().st_mode) & 0o077:
            raise ValueError('Private signing inputs require owner-only permissions')


def manifest(source, destination, version_name, version_code):
    ET.register_namespace('android', ANDROID[1:-1])
    tree = ET.parse(source)
    tree.getroot().set(ANDROID + 'versionName', version_name)
    tree.getroot().set(ANDROID + 'versionCode', str(version_code))
    tree.find('application').set(ANDROID + 'debuggable', 'false')
    destination.parent.mkdir(parents=True, exist_ok=True)
    tree.write(destination, encoding='utf-8', xml_declaration=True)
    return destination


def signing_arguments(args):
    # PKCS12 uses the store password for its key. Passing the same file twice
    # makes apksigner consume a second line, instead of rereading the first.
    return ['--ks', args.keystore, '--ks-key-alias', args.key_alias,
            '--ks-pass', 'file:' + str(args.ks_pass_file)]


def native_payload(source, folder, strip_tool, strip):
    folder.mkdir(parents=True, exist_ok=True)
    target = folder / source.name
    shutil.copy2(source, target)
    if strip:
        subprocess.run([strip_tool, '--strip-unneeded', target], check=True)
    raw = target.read_bytes()
    if re.search(rb'[/]Users/[^/\x00\s]+/', raw):
        raise ValueError('Release native payload retains a private host path: ' + source.name)
    return target, dict(name=source.name, source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                       packaged_sha256=hashlib.sha256(raw).hexdigest(), strip_unneeded=strip)


def source_information(version_name):
    return f'''NaruVM {version_name} - experimental Android APK

Source, dependency sources, build configuration, notices and checksums:
https://github.com/hericobacter-spec/NaruVM/releases/tag/v0.1.1-preview

Download BOTH NaruVM-v0.1.1-preview-source.tar.gz and
NaruVM-v0.1.1-preview-dependency-sources.tar.gz from that release.
The release record identifies the exact APK, source revision and configuration.
Original Henge and component grants remain; the combined distribution selects
GPLv3 and applicable later-version LGPLv3 options. No warranty is provided.

Recipient modifications and installation:
docs/apk-release.md and docs/apk-release.ko.md in the matching source package.
Rebuild a modified component, rebuild the APK with the recorded configuration,
then sign with your own key. No publisher key or bootloader unlock is required.
Different signing keys cannot update the same Android package in place: use a
separate disposable emulator/device. Preserve existing guest data.

External packages retain their published versions, source archives and patches.
Historical producer-commit attestations and byte-identical external rebuilds
are not claimed. The release documents observed checks and remaining limits.
Apple software, ROM/IPSW, guest disks, accounts and private keys are excluded.
'''
