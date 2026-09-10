#!/usr/bin/env python3
"""Wrapper so `./hixtool.py ...` works without installing anything."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hixtool.__main__ import main  # noqa: E402

if __name__ == "__main__":
    main()
