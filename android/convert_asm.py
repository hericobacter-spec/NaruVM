#!/usr/bin/env python3
"""Translate Mach-O test symbol/relocation syntax to ELF, preserving instructions."""
import re
import sys
from pathlib import Path

source = Path(sys.argv[1]).read_text()
source = re.sub(r'\b_([A-Za-z]\w*)', r'\1', source)
source = re.sub(r'(\w+)@PAGEOFF', r':lo12:\1', source)
source = source.replace('@PAGE', '')
source += '\n.section .note.GNU-stack,"",%progbits\n'
Path(sys.argv[2]).write_text(source)
