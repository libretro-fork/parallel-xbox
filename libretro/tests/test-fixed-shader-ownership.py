# SPDX-License-Identifier: GPL-2.0-or-later
"""Retained fixed-shader ownership entry point: immutable words and exact native lends."""
from pathlib import Path
import runpy,sys
sys.argv.append("--ownership-only")
runpy.run_path(str(Path(__file__).with_name('test-spv-utility.py')),run_name='__main__')
