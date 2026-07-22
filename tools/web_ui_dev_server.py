#!/usr/bin/env python3
"""Local mock server for PixelMap web UI (no ESP32 required)."""

from __future__ import annotations

import json
import math
import mimetypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[1]
UI_DIR = ROOT / "components" / "web_ui"
INDEX = UI_DIR / "index.html"

HOST = "127.0.0.1"
PORT = 8080

CONFIG = {
    "ver": "0.1.1",
    "ssid": "StudioWiFi",
    "pass": "dev-secret",
    "host": "pixelmap-dev",
    "gpio": 16,
    "clk": -1,
    "sled": 2,
    "sledh": True,
    "count": 60,
    "scnt": 1,
    "slens": [60],
    "sgpios": [16],
    "bri": 128,
    "gamma": 220,
    "aw": True,
    "chip": "WS2812B",
    "order": "GRB",
    "fx": 6,
    "speed": 1.0,
    "scale": 1.0,
    "fxint": 255,
    "ph": 0,
    "ps": 255,
    "pv": 255,
    "sh": 160,
    "ss": 255,
    "sv": 255,
    "fxp": [128, 64, 0, 0, 0, 0, 0, 0],
    "fxpos": [128, 128, 128],
    "fxrot": [0, 0, 0],
    "fxch": list(range(1, 24)),
    "fxmod": [
        {"shape": 0, "depth": 128, "rate": 40, "phase": 0} for _ in range(23)
    ],
    "dmxmode": 1,
    "fxmask": 0x7FFFFF,
    "aun": 0,
    "sun": 0,
    "ucnt": 4,
    "aen": False,
    "sen": False,
    "mw": 10,
    "mh": 6,
    "md": 4,
    "mdim": 1,
    "mlay": 0,
    "mfill": 0,
    "mopentb": False,
    "mspc": 1.0,
    "pove": False,
    "povm": 1,
    "poyl": 1,
    "povbl": 3,
    "povrpm": 600,
    "povspd": 4.0,
    "povrad": 0.25,
    "povpath": 1.0,
}


def _normalize_uniform(pts: list[dict]) -> list[dict]:
    if not pts:
        return pts
    xs = [p["x"] for p in pts]
    ys = [p["y"] for p in pts]
    zs = [p["z"] for p in pts]
    minx, maxx = min(xs), max(xs)
    miny, maxy = min(ys), max(ys)
    minz, maxz = min(zs), max(zs)
    s = max(maxx - minx, maxy - miny, maxz - minz, 1e-6)
    cx = 0.5 * (minx + maxx)
    cy = 0.5 * (miny + maxy)
    cz = 0.5 * (minz + maxz)
    out = []
    for p in pts:
        out.append(
            {
                "i": p["i"],
                "x": (p["x"] - cx) / s + 0.5,
                "y": (p["y"] - cy) / s + 0.5,
                "z": (p["z"] - cz) / s + 0.5,
                "g": p.get("g", 0),
            }
        )
    return out


def _count_disk(radius: float, spc: float) -> int:
    n = int(math.ceil(radius / spc + 1e-4))
    r2 = radius * radius + 1e-4
    c = 0
    for yi in range(-n, n + 1):
        for xi in range(-n, n + 1):
            x, y = xi * spc, yi * spc
            if x * x + y * y <= r2:
                c += 1
    return c


def _count_ball(radius: float, spc: float) -> int:
    n = int(math.ceil(radius / spc + 1e-4))
    r2 = radius * radius + 1e-4
    c = 0
    for zi in range(-n, n + 1):
        for yi in range(-n, n + 1):
            for xi in range(-n, n + 1):
                x, y, z = xi * spc, yi * spc, zi * spc
                if x * x + y * y + z * z <= r2:
                    c += 1
    return c


def _diam_radius(diam: int, spc: float) -> float:
    if diam < 2:
        return 0.0
    return 0.5 * (diam - 1) * spc


def _build_circle(diam: int, spc: float, max_n: int, fill: int) -> list[dict]:
    radius = _diam_radius(diam, spc)
    raw: list[dict] = []
    if fill == 1:
        lim = int(math.ceil(radius / spc + 1e-4)) if radius > 0 else 0
        r2 = radius * radius + 1e-4
        cells = []
        for yi in range(-lim, lim + 1):
            for xi in range(-lim, lim + 1):
                x, y = xi * spc, yi * spc
                if x * x + y * y <= r2:
                    cells.append((x, y))
        stride = 1 if len(cells) <= max_n else (len(cells) + max_n - 1) // max_n
        for idx, (x, y) in enumerate(cells):
            if len(raw) >= max_n:
                break
            if idx % stride:
                continue
            raw.append({"i": len(raw), "x": x, "y": y, "z": 0.0, "g": 0})
    else:
        # Concentric rings out to diameter radius; skip incomplete outer rings
        raw.append({"i": 0, "x": 0.0, "y": 0.0, "z": 0.0, "g": 0})
        rings = int(math.floor(radius / spc + 1e-4)) if radius > 0 and spc > 0 else 0
        for ring in range(1, rings + 1):
            if len(raw) >= max_n:
                break
            r = ring * spc
            on = max(6, int(2.0 * math.pi * r / spc + 0.5))
            if on > max_n - len(raw):
                break
            for k in range(on):
                ang = 2.0 * math.pi * k / on
                raw.append(
                    {"i": len(raw), "x": math.cos(ang) * r, "y": math.sin(ang) * r, "z": 0.0, "g": 0}
                )
    return raw


def _build_sphere(diam: int, spc: float, max_n: int, fill: int) -> list[dict]:
    radius = max(spc * 0.5, _diam_radius(diam, spc))
    raw: list[dict] = []
    if fill == 1:
        lim = int(math.ceil(radius / spc + 1e-4))
        r2 = radius * radius + 1e-4
        cells = []
        for zi in range(-lim, lim + 1):
            for yi in range(-lim, lim + 1):
                for xi in range(-lim, lim + 1):
                    x, y, z = xi * spc, yi * spc, zi * spc
                    if x * x + y * y + z * z <= r2:
                        cells.append((x, y, z))
        stride = 1 if len(cells) <= max_n else (len(cells) + max_n - 1) // max_n
        for idx, (x, y, z) in enumerate(cells):
            if len(raw) >= max_n:
                break
            if idx % stride:
                continue
            raw.append({"i": len(raw), "x": x, "y": y, "z": z, "g": 0})
    else:
        want = max(1, int(round(4.0 * math.pi * (radius / spc) ** 2)))
        want = min(want, max_n)
        ga = math.pi * (3.0 - math.sqrt(5.0))
        for k in range(want):
            t = 0.0 if want <= 1 else (k + 0.5) / want
            y = 1.0 - 2.0 * t
            r_xz = math.sqrt(max(0.0, 1.0 - y * y))
            th = ga * k
            raw.append(
                {
                    "i": k,
                    "x": math.cos(th) * r_xz * radius,
                    "y": y * radius,
                    "z": math.sin(th) * r_xz * radius,
                    "g": 0,
                }
            )
    return raw


def _ensure_box_corners(raw: list[dict], w: int, h: int, d: int, spc: float, max_count: int) -> None:
    """Keep full AABB extent after stride sampling by stamping corners."""
    xs = (0, max(0, w - 1))
    ys = (0, max(0, h - 1))
    zs = (0,) if d <= 1 else (0, max(0, d - 1))
    x1, y1, z1 = xs[-1] * spc, ys[-1] * spc, zs[-1] * spc

    def near(a: float, b: float) -> bool:
        return abs(a - b) <= 1e-3

    def is_corner(p: dict) -> bool:
        xe = near(p["x"], 0) or near(p["x"], x1)
        ye = near(p["y"], 0) or near(p["y"], y1)
        ze = True if d <= 1 else (near(p["z"], 0) or near(p["z"], z1))
        return xe and ye and ze

    for z in zs:
        for y in ys:
            for x in xs:
                px, py, pz = x * spc, y * spc, z * spc
                if any(near(p["x"], px) and near(p["y"], py) and near(p["z"], pz) for p in raw):
                    continue
                pt = {"i": 0, "x": px, "y": py, "z": pz, "g": 0}
                if len(raw) < max_count:
                    pt["i"] = len(raw)
                    raw.append(pt)
                else:
                    # Steal a non-corner so later faces are not wiped from the end
                    slot = next((i for i in range(len(raw) - 1, -1, -1) if not is_corner(raw[i])), None)
                    if slot is None:
                        continue
                    pt["i"] = slot
                    raw[slot] = pt


def _build_box(w: int, h: int, d: int, spc: float, max_count: int, fill: int, open_tb: bool) -> list[dict]:
    if fill == 1:
        # Stride-sample so a short strip still spans the full W×H×D box
        total = w * h * d
        stride = 1 if total <= max_count else (total + max_count - 1) // max_count
        raw: list[dict] = []
        seen = 0
        for z in range(d):
            for y in range(h):
                for x in range(w):
                    if len(raw) >= max_count:
                        _ensure_box_corners(raw, w, h, d, spc, max_count)
                        return raw
                    if seen % stride != 0:
                        seen += 1
                        continue
                    seen += 1
                    i = len(raw)
                    raw.append({"i": i, "x": x * spc, "y": y * spc, "z": z * spc, "g": 0})
        _ensure_box_corners(raw, w, h, d, spc, max_count)
        return raw
    # Corners first (full extent), then per-face budgets for the remaining strip
    def face_id(x: int, y: int, z: int) -> int:
        if z == 0:
            return 0
        if d > 1 and z == d - 1:
            return 1
        if not open_tb and y == 0:
            return 2
        if not open_tb and h > 1 and y == h - 1:
            return 3
        if x == 0:
            return 4
        if w > 1 and x == w - 1:
            return 5
        return -1

    raw: list[dict] = []
    corner_set: set[tuple[int, int, int]] = set()
    zs_c = (0,) if d <= 1 else (0, d - 1)
    for z in zs_c:
        for y in (0, h - 1):
            for x in (0, w - 1):
                if len(raw) >= max_count:
                    break
                key = (x, y, z)
                if key in corner_set:
                    continue
                corner_set.add(key)
                i = len(raw)
                raw.append({"i": i, "x": x * spc, "y": y * spc, "z": z * spc, "g": 0})
            if len(raw) >= max_count:
                break
        if len(raw) >= max_count:
            break

    faces: list[list[tuple[int, int, int]]] = [[] for _ in range(6)]
    for z in range(d):
        for y in range(h):
            for x in range(w):
                if (x, y, z) in corner_set:
                    continue
                f = face_id(x, y, z)
                if f >= 0:
                    faces[f].append((x, y, z))
    fc = [len(f) for f in faces]
    total = sum(fc)
    budget = max_count - len(raw)
    faces_nz = sum(1 for c in fc if c)
    if budget <= 0 or total == 0:
        return raw
    allot = [0] * 6
    if budget >= faces_nz:
        for f, c in enumerate(fc):
            if c:
                allot[f] = 1
        left = budget - sum(allot)
        rem_cells = total - faces_nz
        if left > 0 and rem_cells > 0:
            given = 0
            for f, c in enumerate(fc):
                if c <= 1:
                    continue
                extra = (left * (c - 1)) // rem_cells
                extra = min(extra, c - allot[f])
                allot[f] += extra
                given += extra
            f = 0
            while given < left and f < 6:
                if fc[f] > allot[f]:
                    allot[f] += 1
                    given += 1
                f += 1
    else:
        assigned = 0
        for f, c in enumerate(fc):
            if c and assigned < budget:
                allot[f] = 1
                assigned += 1
    for f, cells in enumerate(faces):
        want = allot[f]
        if want <= 0 or not cells:
            continue
        stride = 1 if len(cells) <= want else (len(cells) + want - 1) // want
        got = 0
        for idx, (x, y, z) in enumerate(cells):
            if got >= want or len(raw) >= max_count:
                break
            if idx % stride != 0:
                continue
            i = len(raw)
            raw.append({"i": i, "x": x * spc, "y": y * spc, "z": z * spc, "g": 0})
            got += 1
    return raw


def _build_cylinder(diam: int, height_px: int, spc: float, max_n: int, open_tb: bool) -> list[dict]:
    diam = max(2, diam)
    height_px = max(1, height_px)
    radius = max(spc, _diam_radius(diam, spc))
    circ = max(6, int(round(math.pi * (diam - 1))))
    rings = height_px
    height = (rings - 1) * spc if rings > 1 else 0.0
    raw: list[dict] = []
    for r in range(rings):
        y = 0.0 if rings <= 1 else (r / (rings - 1) - 0.5) * height
        for c in range(circ):
            if len(raw) >= max_n:
                return raw
            a = 2.0 * math.pi * c / circ
            raw.append({"i": len(raw), "x": math.cos(a) * radius, "y": y, "z": math.sin(a) * radius, "g": 0})
    if not open_tb:
        lim = int(math.ceil(radius / spc + 1e-4))
        r2 = radius * radius + 1e-4
        for cap in (0, 1):
            y = -0.5 * height if cap == 0 else 0.5 * height
            for yi in range(-lim, lim + 1):
                for xi in range(-lim, lim + 1):
                    if len(raw) >= max_n:
                        return raw
                    x, z = xi * spc, yi * spc
                    if x * x + z * z > r2:
                        continue
                    raw.append({"i": len(raw), "x": x, "y": y, "z": z, "g": 0})
    return raw


def _build_dome(diam: int, spc: float, max_n: int) -> list[dict]:
    radius = max(spc, _diam_radius(max(2, diam), spc))
    want = max(1, int(round(2.0 * math.pi * (radius / spc) ** 2)))
    want = min(want, max_n)
    ga = math.pi * (3.0 - math.sqrt(5.0))
    raw: list[dict] = []
    gen = want * 2 + 8
    for k in range(gen):
        if len(raw) >= want:
            break
        t = (k + 0.5) / gen
        y = 1.0 - 2.0 * t
        if y < -1e-4:
            continue
        r_xz = math.sqrt(max(0.0, 1.0 - y * y))
        th = ga * k
        raw.append(
            {
                "i": len(raw),
                "x": math.cos(th) * r_xz * radius,
                "y": y * radius,
                "z": math.sin(th) * r_xz * radius,
                "g": 0,
            }
        )
    return raw


def _build_pyramid(base: int, height_px: int, spc: float, max_n: int) -> list[dict]:
    edge = max(2, base)
    rise = max(2, height_px)
    half = 0.5 * (edge - 1) * spc
    apex_y = (rise - 1) * spc
    corners = [(-half, 0.0, -half), (half, 0.0, -half), (half, 0.0, half), (-half, 0.0, half)]
    raw: list[dict] = []

    def push(x, y, z):
        if len(raw) >= max_n:
            return False
        raw.append({"i": len(raw), "x": x, "y": y, "z": z, "g": 0})
        return True

    for e in range(4):
        a, b = corners[e], corners[(e + 1) & 3]
        for s in range(edge):
            if e > 0 and s == 0:
                continue
            t = 0.0 if edge <= 1 else s / (edge - 1)
            if not push(a[0] + (b[0] - a[0]) * t, 0.0, a[2] + (b[2] - a[2]) * t):
                return raw
    for e in range(4):
        a = corners[e]
        for s in range(1, rise):
            t = s / (rise - 1)
            if not push(a[0] * (1 - t), apex_y * t, a[2] * (1 - t)):
                return raw
    for e in range(4):
        a, b = corners[e], corners[(e + 1) & 3]
        for r in range(1, rise):
            v = r / rise
            cols = max(1, edge - (r * (edge - 1)) // rise)
            for c in range(cols):
                u = 0.5 if cols <= 1 else c / (cols - 1)
                bx = a[0] + (b[0] - a[0]) * u
                bz = a[2] + (b[2] - a[2]) * u
                if not push(bx * (1 - v), apex_y * v, bz * (1 - v)):
                    return raw
    return raw


def build_map(
    w: int,
    h: int,
    d: int = 1,
    dim: int = 0,
    lay: int = 0,
    spc: float = 1.0,
    max_count: int = 60,
    fill: int = 0,
    open_tb: bool = False,
) -> list[dict]:
    w = max(1, min(int(w), 128))
    h = max(1, min(int(h), 128))
    d = max(1, min(int(d), 64)) if dim == 1 else 1
    spc = max(1e-4, float(spc))
    max_count = max(1, int(max_count))
    fill = 1 if int(fill) == 1 else 0
    open_tb = bool(open_tb)

    if dim != 1 and lay == 2:
        lay = 1
    if dim == 1 and lay == 1:
        lay = 2
    if dim != 1 and lay in (3, 4, 5, 6):
        lay = 0
    if dim == 1 and lay == 0:
        lay = 3
        fill = 1
    if fill == 1:
        open_tb = False

    if lay == 1:  # circle — w = diameter in pixels
        return _normalize_uniform(_build_circle(w, spc, max_count, fill))

    if lay == 2:  # sphere — w = diameter
        return _normalize_uniform(_build_sphere(w, spc, max_count, fill))

    if lay == 3:  # box
        return _normalize_uniform(_build_box(w, h, d, spc, max_count, fill, open_tb))

    if lay == 4:  # cylinder — w=diameter, h=height layers
        return _normalize_uniform(_build_cylinder(w, h, spc, max_count, open_tb))

    if lay == 5:  # dome — w=diameter
        return _normalize_uniform(_build_dome(w, spc, max_count))

    if lay == 6:  # pyramid — w=base, h=height
        return _normalize_uniform(_build_pyramid(w, h, spc, max_count))

    if lay == 7:  # custom — keep existing MAP if present
        return []

    # 2D grid — stride-sample so a short strip still spans full W×H
    total = w * h
    stride = 1 if total <= max_count else (total + max_count - 1) // max_count
    raw: list[dict] = []
    seen = 0
    for y in range(h):
        for x in range(w):
            if len(raw) >= max_count:
                _ensure_box_corners(raw, w, h, 1, spc, max_count)
                return _normalize_uniform(raw)
            if seen % stride != 0:
                seen += 1
                continue
            seen += 1
            i = len(raw)
            raw.append({"i": i, "x": x * spc, "y": y * spc, "z": 0.0, "g": 0})
    _ensure_box_corners(raw, w, h, 1, spc, max_count)
    return _normalize_uniform(raw)


MAP = build_map(
    CONFIG["mw"],
    CONFIG["mh"],
    CONFIG["md"],
    CONFIG["mdim"],
    CONFIG["mlay"],
    CONFIG["mspc"],
    CONFIG["count"],
    CONFIG["mfill"],
    CONFIG.get("mopentb", False),
)

LUA_SCRIPT_MAX = 4095
LUA_SCRIPT = """-- Per-pixel custom effect (Lua)
local d = length(x - 0.5, y - 0.5, z - 0.5)
local wave = 0.5 + 0.5 * sin(d * 16 - t * 4)
return hsv((t * 40 + d * 80) % 256, 230, wave * 255)
"""


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt: str, *args) -> None:
        print(f"[mock] {self.address_string()} {fmt % args}")

    def _send(self, code: int, body: bytes, ctype: str) -> None:
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _json(self, code: int, obj) -> None:
        self._send(code, json.dumps(obj).encode("utf-8"), "application/json")

    def _read_json(self):
        length = int(self.headers.get("Content-Length", "0") or "0")
        raw = self.rfile.read(length) if length else b"{}"
        return json.loads(raw.decode("utf-8") or "{}")

    def do_GET(self) -> None:
        path = urlparse(self.path).path
        if path in ("/", "/index.html"):
            self._send(200, INDEX.read_bytes(), "text/html; charset=utf-8")
            return
        if path == "/api/config":
            pub = dict(CONFIG)
            pub["passSet"] = bool(pub.get("pass"))
            pub.pop("pass", None)
            if "fxmask" not in pub:
                pub["fxmask"] = 0x7FFFFF
            self._json(200, pub)
            return
        if path == "/api/map":
            self._json(200, MAP)
            return
        if path == "/api/fx/lua":
            self._json(
                200,
                {
                    "script": LUA_SCRIPT,
                    "ready": True,
                    "error": "ok",
                    "max": LUA_SCRIPT_MAX,
                },
            )
            return

        rel = path.lstrip("/")
        candidate = (UI_DIR / rel).resolve()
        if str(candidate).startswith(str(UI_DIR.resolve())) and candidate.is_file():
            ctype = mimetypes.guess_type(str(candidate))[0] or "application/octet-stream"
            self._send(200, candidate.read_bytes(), ctype)
            return

        self._json(404, {"error": "not found", "path": path})

    def do_POST(self) -> None:
        global MAP, LUA_SCRIPT
        path = urlparse(self.path).path
        try:
            payload = self._read_json()
        except json.JSONDecodeError:
            self._json(400, {"ok": False, "error": "bad json"})
            return

        if path == "/api/config":
            # Empty / omitted pass keeps the previous password (device parity).
            new_pass = payload.pop("pass", None) if isinstance(payload, dict) else None
            CONFIG.update(payload)
            if isinstance(new_pass, str) and new_pass:
                CONFIG["pass"] = new_pass
            # One protocol at a time; keep Art-Net / sACN universe in sync
            if CONFIG.get("aen") and CONFIG.get("sen"):
                CONFIG["sen"] = False
            uni = CONFIG.get("aun", CONFIG.get("sun", 0))
            CONFIG["aun"] = uni
            CONFIG["sun"] = uni
            if CONFIG.get("chip") in ("APA102", "SK9822", "CUSTOM"):
                CONFIG["chip"] = "WS2812B"
            default_gpios = [16, 2, 4, 13, 14, 15, 18, 19]
            slens = CONFIG.get("slens")
            sgpios = CONFIG.get("sgpios")
            scnt = int(CONFIG.get("scnt", 1) or 1)
            if isinstance(slens, list) and slens:
                scnt = max(1, min(8, len(slens)))
                lengths = [max(1, min(4096, int(x))) for x in slens[:scnt]]
            else:
                scnt = max(1, min(8, scnt))
                total = max(1, int(CONFIG.get("count", 60) or 60))
                lengths = [total]
                if scnt > 1:
                    lengths = [max(1, total // scnt)] * scnt
                    lengths[-1] = total - sum(lengths[:-1])
            if isinstance(sgpios, list) and sgpios:
                gpios = [max(0, min(48, int(x))) for x in sgpios[:scnt]]
            else:
                gpios = []
            while len(gpios) < scnt:
                gpios.append(default_gpios[len(gpios)] if len(gpios) < len(default_gpios) else 16)
            gpios = gpios[:scnt]
            CONFIG["scnt"] = scnt
            CONFIG["slens"] = lengths
            CONFIG["sgpios"] = gpios
            CONFIG["count"] = sum(lengths)
            CONFIG["gpio"] = gpios[0]
            if "mfill" in CONFIG:
                CONFIG["mfill"] = 1 if int(CONFIG["mfill"]) == 1 else 0
            self._json(200, {"ok": True, "note": "mock save (not flashed to a device)"})
            return

        if path == "/api/map":
            if not isinstance(payload, list):
                self._json(400, {"ok": False, "error": "map must be an array"})
                return
            MAP = payload
            self._json(200, {"ok": True})
            return

        if path == "/api/map/grid":
            w = int(payload.get("w", CONFIG["mw"]))
            h = int(payload.get("h", CONFIG["mh"]))
            d = int(payload.get("d", CONFIG.get("md", 1)))
            dim = int(payload.get("dim", CONFIG.get("mdim", 0)))
            lay = int(payload.get("lay", CONFIG.get("mlay", 0)))
            spc = float(payload.get("spc", CONFIG.get("mspc", 1.0)))
            fill = int(payload.get("fill", CONFIG.get("mfill", 0)))
            open_tb = bool(payload.get("opentb", CONFIG.get("mopentb", False)))
            CONFIG["mw"], CONFIG["mh"], CONFIG["md"], CONFIG["mdim"] = w, h, d, dim
            CONFIG["mlay"], CONFIG["mspc"] = lay, spc
            CONFIG["mfill"] = 1 if fill == 1 else 0
            CONFIG["mopentb"] = open_tb
            max_n = int(CONFIG.get("count", 60))
            if lay == 7:
                # Custom formula maps are applied via POST /api/map
                self._json(200, {"ok": True, "count": len(MAP), "max": max_n})
                return
            MAP = build_map(w, h, d, dim, lay, spc, max_n, fill, open_tb)
            unused = max(0, max_n - len(MAP))
            self._json(200, {"ok": True, "count": len(MAP), "max": max_n, "unused": unused})
            return

        if path == "/api/fx/lua":
            script = payload.get("script")
            if not isinstance(script, str):
                self._json(400, {"ok": False, "ready": False, "error": "script required"})
                return
            if len(script) > LUA_SCRIPT_MAX:
                self._json(400, {"ok": False, "ready": False, "error": "script too large"})
                return
            LUA_SCRIPT = script
            CONFIG["fx"] = 25
            self._json(200, {"ok": True, "ready": True, "error": "ok"})
            return

        self._json(404, {"ok": False, "error": "not found", "path": path})


def main() -> None:
    if not INDEX.is_file():
        raise SystemExit(f"UI not found: {INDEX}")
    server = ThreadingHTTPServer((HOST, PORT), Handler)
    print(f"PixelMap web UI preview: http://{HOST}:{PORT}/")
    print("Mock APIs: /api/config, /api/map, /api/map/grid, /api/fx/lua  (Ctrl+C to stop)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
