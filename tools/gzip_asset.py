#!/usr/bin/env python3
"""Write a deterministic gzip of SRC to DST (mtime=0 for stable rebuilds)."""
from __future__ import annotations

import gzip
import pathlib
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} SRC DST", file=sys.stderr)
        return 2
    src = pathlib.Path(sys.argv[1])
    dst = pathlib.Path(sys.argv[2])
    data = src.read_bytes()
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(gzip.compress(data, compresslevel=9, mtime=0))
    print(f"gzip {src.name}: {len(data)} -> {dst.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
