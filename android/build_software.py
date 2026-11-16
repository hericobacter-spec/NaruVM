#!/usr/bin/env python3
"""Stage checksum-pinned, unmodified Android Lavapipe libraries for an offscreen CPU probe."""
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT/'android/software/dependencies.json').read_text())
WORK = ROOT/'.tools/android-lavapipe'

def data_archive(deb):
    data = deb.read_bytes()
    if not data.startswith(b'!<arch>\n'): raise ValueError('Not a Debian ar archive')
    at = 8
    while at + 60 <= len(data):
        header = data[at:at+60]
        size = int(header[48:58]); name = header[:16].decode().strip().rstrip('/')
        body = data[at+60:at+60+size]
        if name.startswith('data.tar'): return body
        at += 60 + size + size % 2
    raise ValueError('Debian data archive absent')

def main():
    debs = WORK/'debs'
    unpack = WORK/('unpacked-'+hashlib.sha256(json.dumps(LOCK,sort_keys=True).encode()).hexdigest()[:12])
    debs.mkdir(parents=True,exist_ok=True); unpack.mkdir(parents=True,exist_ok=True)
    for package in LOCK['packages']:
        deb = debs/Path(package['Filename']).name
        if not deb.is_file():
            temp = deb.with_suffix('.part')
            print('Downloading',package['Package'],flush=True)
            with urllib.request.urlopen(LOCK['source']+package['Filename'],timeout=60) as response, temp.open('wb') as out:
                while chunk := response.read(1<<20): out.write(chunk)
            temp.rename(deb)
        with deb.open('rb') as stream: digest = hashlib.file_digest(stream,'sha256').hexdigest()
        if digest != package['SHA256']: raise SystemExit('Package checksum mismatch: '+package['Package'])
        with tarfile.open(fileobj=io.BytesIO(data_archive(deb)),mode='r:*') as archive:
            for member in archive:
                if not member.isfile(): continue
                relative = PurePosixPath(member.name)
                if relative.is_absolute() or '..' in relative.parts: raise SystemExit('Unsafe archive path')
                # Extract regular shared libraries and attribution, never package scripts/binaries.
                if not ('/lib/' in member.name and '.so' in relative.name) and not (
                    '/share/doc/' in member.name and relative.name.lower().startswith(('copyright','license','licence','copying'))): continue
                target = unpack/Path(*relative.parts)
                target.parent.mkdir(parents=True,exist_ok=True)
                target.write_bytes(archive.extractfile(member).read())
    llvm = Path(os.environ.get('ANDROID_SDK_ROOT',ROOT/'.tools/android-sdk'))/'ndk/30.0.16248370/toolchains/llvm/prebuilt/darwin-x86_64/bin'
    libraries = {}
    for file in unpack.rglob('*'):
        if not file.is_file(): continue
        with file.open('rb') as stream: magic = stream.read(4)
        if magic != b'\x7fELF': continue
        dynamic = subprocess.check_output([llvm/'llvm-readelf','-d',file],text=True)
        soname = re.search(r'\(SONAME\).*\[([^]]+)\]',dynamic)
        name = soname[1] if soname else file.name
        libraries[name] = (file,re.findall(r'\(NEEDED\).*\[([^]]+)\]',dynamic),bool(soname))
    ordered, visiting, seen = [], set(), set()
    system = {'libc.so','libm.so','libdl.so','liblog.so','libandroid.so'}
    def visit(name):
        if name in seen or name in system: return
        if name in visiting: raise SystemExit('Dependency cycle: '+name)
        if name not in libraries: raise SystemExit('Unresolved software Vulkan dependency: '+name)
        visiting.add(name)
        for dep in libraries[name][1]: visit(dep)
        visiting.remove(name); seen.add(name); ordered.append(name)
    visit('libvulkan_lvp.so')
    output = ROOT/'android/build/native/bin'; output.mkdir(parents=True,exist_ok=True)
    staged = []
    for name in ordered:
        alias = ('libhenge_lvp_'+re.sub(r'[^a-zA-Z0-9_]','_',name)+'.so') if libraries[name][2] else name
        shutil.copy2(libraries[name][0],output/alias); staged.append(alias)
    header = ROOT/'android/build/lavapipe_load.h'
    header.write_text('static const char *const dependencies[] = {\n'+''.join(json.dumps(n)+',\n' for n in staged)+'};\n')
    subprocess.run([llvm/'aarch64-linux-android28-clang','-O2','-shared','-fPIC','-ffixed-x18',
        '-Wl,-z,max-page-size=16384','-I',ROOT/'android/build',ROOT/'android/native/lavapipe_loader.c',
        '-ldl','-o',output/'libhenge_lvp_loader.so'],check=True)
    (ROOT/'android/build/software-libraries.json').write_text(json.dumps({'libraries':staged+['libhenge_lvp_loader.so'],
        'attributions':str(unpack)},indent=2)+'\n')
    print('Software Vulkan libraries staged:',len(staged),'(CPU renderer; no hardware acceleration)',flush=True)

if __name__ == '__main__': main()
