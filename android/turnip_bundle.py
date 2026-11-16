#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate an optional self-contained Turnip ICD and its build/source attribution.

This stages a user-supplied driver only; it downloads/installs no system driver.
The runtime still requires the disk-free Reims pixel probe to pass.
"""
import hashlib
import json
from pathlib import Path
import re
import struct
from urllib.parse import urlsplit


def validate(manifest):
    path = Path(manifest).resolve()
    data = json.loads(path.read_text())
    for key in ['version', 'source_url', 'source_commit', 'sha256', 'driver', 'license', 'license_file', 'build_recipe']:
        if not isinstance(data.get(key), str) or not data[key]:
            raise ValueError('Turnip manifest requires ' + key)
    url = urlsplit(data['source_url'])
    if url.scheme != 'https' or not url.netloc or url.username or url.password or url.query or url.fragment:
        raise ValueError('Use a public HTTPS source URL without credentials/query/fragment')
    if not re.fullmatch('[0-9a-f]{40}', data['source_commit']):
        raise ValueError('Record the full source commit')
    if not re.fullmatch('[0-9a-f]{64}', data['sha256']):
        raise ValueError('Record the driver SHA256')
    driver = (path.parent / data['driver']).resolve()
    raw = driver.read_bytes()
    if hashlib.sha256(raw).hexdigest() != data['sha256']:
        raise ValueError('Turnip driver SHA256 mismatch')
    if len(raw) < 64 or raw[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', raw, 18)[0] != 183:
        raise ValueError('Turnip driver must be little-endian AArch64 ELF64')
    phoff = struct.unpack_from('<Q', raw, 32)[0]
    phentsize, phnum = struct.unpack_from('<HH', raw, 54)
    if phentsize < 56 or not phnum or phoff + phentsize * phnum > len(raw):
        raise ValueError('Invalid ELF program headers')
    loads = 0
    for i in range(phnum):
        at = phoff + i * phentsize
        if struct.unpack_from('<I', raw, at)[0] == 1:
            align = struct.unpack_from('<Q', raw, at + 48)[0]
            if align < 16384:
                raise ValueError('Rebuild Turnip with 16KiB-compatible ELF load alignment')
            loads += 1
    if not loads:
        raise ValueError('ELF has no loadable segments')
    assets = {}
    for key in ['license_file', 'build_recipe']:
        content = (path.parent / data[key]).read_bytes()
        if not content or len(content) > 1024 * 1024 or b'\0' in content:
            raise ValueError('Expected a bounded text ' + key)
        content.decode('utf-8')
        assets[key + '.txt'] = content
    public = {key: data[key] for key in ['version', 'source_url', 'source_commit', 'sha256', 'license']}
    public['runtime_status'] = 'unverified; requires Reims pixel preflight; no device support implied'
    assets['provenance.json'] = (json.dumps(public, indent=2) + '\n').encode()
    return driver, assets
