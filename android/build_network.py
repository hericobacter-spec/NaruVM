#!/usr/bin/env python3
"""Stage pinned libslirp runtime, public headers and notices; execute no package scripts."""
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tarfile
import urllib.request
from build_software import data_archive

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / '.tools/android-net'
LOCK = json.loads((ROOT / 'android/network/dependencies.json').read_text())


def main():
    debs = WORK / 'debs'
    unpack = WORK / ('unpacked-' + hashlib.sha256(json.dumps(LOCK, sort_keys=True).encode()).hexdigest()[:12])
    debs.mkdir(parents=True, exist_ok=True)
    unpack.mkdir(parents=True, exist_ok=True)
    for package in LOCK['packages']:
        deb = debs / Path(package['Filename']).name
        if not deb.is_file():
            temp = deb.with_suffix('.part')
            print('Downloading network dependency:', package['Package'], flush=True)
            with urllib.request.urlopen(LOCK['source'] + package['Filename'], timeout=60) as response, temp.open('wb') as out:
                while chunk := response.read(1 << 20):
                    out.write(chunk)
            temp.rename(deb)
        with deb.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        if digest != package['SHA256']:
            raise SystemExit('Network dependency checksum mismatch: ' + package['Package'])
        with tarfile.open(fileobj=io.BytesIO(data_archive(deb)), mode='r:*') as archive:
            for member in archive:
                if not member.isfile():
                    continue
                relative = PurePosixPath(member.name)
                if relative.is_absolute() or '..' in relative.parts:
                    raise SystemExit('Unsafe archive path')
                if not (('/lib/' in member.name and '.so' in relative.name) or '/include/slirp/' in member.name or (
                        '/share/doc/' in member.name and relative.name.lower().startswith(('copyright', 'license', 'licence', 'copying')))):
                    continue
                target = unpack / Path(*relative.parts)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.extractfile(member).read())
    llvm = ROOT / '.tools/android-sdk/ndk/30.0.16248370/toolchains/llvm/prebuilt/darwin-x86_64/bin'
    libraries = {}
    for file in unpack.rglob('*'):
        if not file.is_file():
            continue
        with file.open('rb') as stream:
            if stream.read(4) != b'\x7fELF':
                continue
        dynamic = subprocess.check_output([llvm / 'llvm-readelf', '-d', file], text=True)
        soname = re.search(r'\(SONAME\).*\[([^]]+)\]', dynamic)
        libraries[soname[1] if soname else file.name] = (file, re.findall(r'\(NEEDED\).*\[([^]]+)\]', dynamic), bool(soname))
    ordered, seen = [], set()
    system = {'libc.so', 'libm.so', 'libdl.so', 'liblog.so', 'libandroid.so', 'libz.so'}

    def visit(name):
        if name in seen or name in system:
            return
        if name not in libraries:
            raise SystemExit('Unresolved network dependency: ' + name)
        seen.add(name)
        for dependency in libraries[name][1]:
            visit(dependency)
        ordered.append(name)

    visit('libslirp.so')
    output = ROOT / 'android/build/native/bin'
    output.mkdir(parents=True, exist_ok=True)
    staged = []
    for name in ordered:
        alias = ('libhenge_net_' + re.sub(r'[^a-zA-Z0-9_]', '_', name) + '.so') if libraries[name][2] else name
        shutil.copy2(libraries[name][0], output / alias)
        staged.append(alias)
    include = next(unpack.rglob('include/slirp/libslirp.h')).parent
    (ROOT / 'android/build/net_load.h').write_text('static const char *const net_dependencies[] = {\n' + ''.join(json.dumps(n) + ',\n' for n in staged) + '};\n')
    (ROOT / 'android/build/network-libraries.json').write_text(json.dumps({'libraries': staged, 'include': str(include), 'attributions': str(unpack)}, indent=2) + '\n')
    print('Pinned network libraries staged:', ordered, flush=True)


if __name__ == '__main__':
    main()
