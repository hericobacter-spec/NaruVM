#!/usr/bin/env python3
"""Resolve this lab's signal addresses using executable maps from the same crash."""
import argparse
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SYMBOLIZER = ROOT / '.tools/android-sdk/ndk/30.0.16248370/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-symbolizer'


def elf_address(path, mapping, pc):
    data = path.read_bytes()
    if data[:6] != b'\x7fELF\x02\x01':
        raise ValueError('Expected a little-endian ELF64 object')
    phoff = struct.unpack_from('<Q', data, 32)[0]
    phsize, phnum = struct.unpack_from('<HH', data, 54)
    file_address = pc - mapping['start'] + mapping['offset']
    for i in range(phnum):
        kind, flags, off, va, _, size, _, _ = struct.unpack_from('<IIQQQQQQ', data, phoff + phsize * i)
        if kind == 1 and flags & 1 and off // 4096 * 4096 <= mapping['offset'] < (off + size + 4095) // 4096 * 4096:
            return file_address + va - off
    raise ValueError('No executable PT_LOAD for the recorded mapping')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--object-dir', type=Path, action='append', default=[])
    args = parser.parse_args()
    text = args.log.read_text(errors='replace')
    mappings = []
    for m in re.finditer(r'^([0-9a-f]+)-([0-9a-f]+) (\S*x\S*) ([0-9a-f]+) \S+ \d+\s*(.*)$', text, re.M):
        mappings.append(dict(start=int(m[1], 16), end=int(m[2], 16), offset=int(m[4], 16), path=m[5].strip()))
    if not mappings:
        parser.error('No executable mappings in this log; addresses cannot be safely attributed')
    directories = args.object_dir + [ROOT / 'android/build/native/bin']
    samples = re.findall(r'^(pc|lr|return_pc)=([0-9a-f]+)$', text, re.M)
    seen = set()
    for label, value in samples:
        raw = int(value, 16)
        candidates = [raw, raw & ((1 << 48) - 1), raw & ((1 << 39) - 1)]
        match = next(((pc, m) for pc in candidates for m in mappings if m['start'] <= pc < m['end']), None)
        if (label, raw) in seen:
            continue
        seen.add((label, raw))
        if not match:
            print(f'{label} raw=0x{raw:x}: unmapped in captured executable maps'); continue
        pc, mapping = match
        name = Path(mapping['path']).name
        obj = next((d / name for d in directories if name and (d / name).is_file()), None)
        print(f'{label} raw=0x{raw:x} mapped=0x{pc:x} module={mapping["path"]}')
        if obj is None:
            print(f'  no matching local ELF; map_file_offset=0x{pc - mapping["start"] + mapping["offset"]:x}'); continue
        address = elf_address(obj, mapping, pc)
        print(f'  object={obj} elf_address=0x{address:x}')
        result = subprocess.run([str(SYMBOLIZER), '--obj=' + str(obj), '--demangle', '--inlines', hex(address)],
                                check=True, capture_output=True, text=True)
        print(result.stdout.rstrip())


if __name__ == '__main__':
    main()
