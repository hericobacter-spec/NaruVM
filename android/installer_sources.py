#!/usr/bin/env python3
"""Verify the exact vendored Apple BSD decoder inputs before compilation."""
import hashlib
import json
from pathlib import Path

def verify(root):
    pin=json.loads((root/'android/installer/dependencies.json').read_text())
    for name,sha in pin['files'].items():
        p=root/'third_party/lzfse'/name
        if p.is_symlink() or not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest()!=sha:
            raise ValueError('LZFSE source pin mismatch: '+name)
    print('INSTALLER_SOURCE_RESULT failures=0 pinned_files='+str(len(pin['files'])))

if __name__=='__main__': verify(Path(__file__).resolve().parents[1])
