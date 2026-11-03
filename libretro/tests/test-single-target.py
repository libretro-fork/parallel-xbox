#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Reject the obsolete Xbox target-selection switch in this single-target core."""
from pathlib import Path
import re, sys
root = Path(sys.argv[1]).resolve()
# The core owns these sources; third-party libraries retain their own configs.
directories = ['accel', 'block', 'hw', 'include', 'system', 'target', 'tcg', 'util', 'ui', 'libretro']
conditional = re.compile(r'^\s*#\s*(?:if|ifdef|ifndef|elif|define|undef)\b[^\n]*\bXBOX\b', re.M)
count = 0
for directory in directories:
    for path in (root / directory).rglob('*'):
        if path.suffix not in ('.c', '.h', '.inc'):
            continue
        text = path.read_text(encoding='utf8')
        assert not conditional.search(text), str(path.relative_to(root))
        count += 1
for name in ('Makefile', 'Makefile.common', 'Makefile.deps'):
    text = (root / name).read_text(encoding='utf8')
    assert not re.search(r'-[DU]XBOX\b', text), name
print(f'Single target: {count} core source/header files and all Makefiles have no Xbox selection switch')
