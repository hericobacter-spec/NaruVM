"""Verify the untouched upstream checkout and the separately recorded Android translator port."""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / 'android/gpu/dependencies.json').read_text())
SOURCE = ROOT / '.tools/metal2vulkan-rog'

def verify_source():
    original = ROOT / '.tools/metal2vulkan'
    for folder in [original, SOURCE]:
        if not folder.is_dir():
            raise SystemExit('Run python3 android/fetch_gpu_deps.py first.')
        head = subprocess.check_output(['git', '-C', str(folder), 'rev-parse', 'HEAD'], text=True).strip()
        if folder == original and head != LOCK['metal2vulkan']['commit']:
            raise SystemExit('GPU base differs from the recorded pin; source preserved: ' + str(folder))
    if subprocess.run(['git', '-C', str(SOURCE), 'merge-base', '--is-ancestor',
                       LOCK['metal2vulkan']['commit'], 'HEAD'], capture_output=True).returncode:
        raise SystemExit('GPU port does not descend from its upstream pin; source preserved.')
    dirty = subprocess.check_output(['git', '-C', str(original), 'status', '--porcelain'], text=True)
    if dirty:
        raise SystemExit('Original GPU checkout is modified; source preserved.')
    port = LOCK['android_port']
    patch = ROOT / 'android/gpu/attachment-rog.patch'
    if hashlib.sha256(patch.read_bytes()).hexdigest() != port['patch_sha256']:
        raise SystemExit('GPU port patch checksum mismatch.')
    changed = subprocess.check_output(['git', '-C', str(SOURCE), 'diff', '--name-only', LOCK['metal2vulkan']['commit']], text=True).splitlines()
    untracked = subprocess.check_output(['git', '-C', str(SOURCE), 'ls-files', '--others', '--exclude-standard'], text=True).splitlines()
    if set(changed + untracked) != set(port['files']):
        raise SystemExit('GPU port file set differs from its recorded patch; source preserved.')
    actual = subprocess.check_output(['git', '-C', str(SOURCE), 'diff', '--binary', LOCK['metal2vulkan']['commit']])
    if hashlib.sha256(actual).hexdigest() != port['patch_sha256']:
        raise SystemExit('GPU working tree differs from its recorded patch bytes; source preserved.')
    for name, digest in port['files'].items():
        if hashlib.sha256((SOURCE / name).read_bytes()).hexdigest() != digest:
            raise SystemExit('GPU port source checksum mismatch: ' + name)
