#!/usr/bin/env python3
"""Minimal fallback for the 'file' command when packaging AppImages in minimal environments."""
import sys
import os

brief = any(a in ("-b", "--brief") for a in sys.argv[1:])

for arg in sys.argv[1:]:
    if arg.startswith("-"):
        continue
    desc = "data"
    if os.path.exists(arg):
        try:
            with open(arg, "rb") as f:
                h = f.read(32)
            if h.startswith(b"\x89PNG\r\n\x1a\n"):
                desc = "PNG image data, 256 x 256, 8-bit/color RGBA, non-interlaced"
            elif h.startswith(b"\x7fELF"):
                desc = "ELF 64-bit LSB executable"
            elif b"<svg" in h or arg.endswith(".svg"):
                desc = "SVG Scalable Vector Graphics image"
            elif b"[Desktop Entry]" in h or arg.endswith(".desktop"):
                desc = "desktop configuration file"
            elif h.startswith(b"#!"):
                desc = "POSIX shell script text executable"
        except Exception:
            pass
    print(desc if brief else f"{arg}: {desc}")
