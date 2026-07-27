#!/usr/bin/env python3
"""Build boards/catalog.json from boards/*.json (excluding catalog.json itself)."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BOARDS_DIR = ROOT / "boards"
DEFAULT_OUT = BOARDS_DIR / "catalog.json"


def load_boards() -> list[dict]:
    boards: list[dict] = []
    for path in sorted(BOARDS_DIR.glob("*.json")):
        if path.name == "catalog.json":
            continue
        data = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            raise SystemExit(f"{path.name}: expected a JSON object")
        bid = data.get("id") or path.stem
        data["id"] = bid
        if "name" not in data:
            data["name"] = bid
        if "mcu" not in data:
            raise SystemExit(f"{path.name}: missing required 'mcu' (esp32 / esp32s3)")
        if "pins" not in data or not isinstance(data["pins"], dict):
            raise SystemExit(f"{path.name}: missing 'pins' object")
        pins = data["pins"]
        # Normalize legacy led_data_1 / led_data_2 into led_data[]
        if "led_data" not in pins:
            led = []
            for i in range(1, 9):
                key = f"led_data_{i}"
                if key in pins:
                    v = int(pins[key])
                    if v >= 0:
                        led.append(v)
            if led:
                pins["led_data"] = led
            elif "led_data_1" in pins:
                pins["led_data"] = [int(pins["led_data_1"])]
        if not isinstance(pins.get("led_data"), list) or not pins["led_data"]:
            raise SystemExit(f"{path.name}: pins.led_data must be a non-empty array")
        boards.append(data)
    boards.sort(key=lambda b: (0 if b.get("default") else 1, b.get("mcu", ""), b.get("name", "")))
    return boards


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-o", "--output", type=Path, default=DEFAULT_OUT)
    args = ap.parse_args()
    boards = load_boards()
    catalog = {
        "version": 1,
        "boards": boards,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(catalog, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {args.output} ({len(boards)} boards)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
