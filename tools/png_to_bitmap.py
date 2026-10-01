#!/usr/bin/env python3
"""Launcher: python tools/png_to_bitmap.py  (same as python -m bitmaptool gui, run from tools/)."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))

from bitmaptool.gui import main  # noqa: E402

if __name__ == "__main__":
    main()
