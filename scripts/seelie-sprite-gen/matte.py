#!/usr/bin/env python3
"""Chroma-key matte on lossless PNGs (never JPEG roundtrip)."""
from __future__ import annotations

from PIL import Image


def chroma_key_green(
    im: Image.Image,
    *,
    tol: int = 55,
    soft: int = 30,
) -> Image.Image:
    """Turn a green-screen PNG into RGBA with transparency."""
    rgba = im.convert("RGBA")
    px = rgba.load()
    w, h = rgba.size
    for y in range(h):
        for x in range(w):
            r, g, b, _ = px[x, y]
            green_dom = int(g) - max(int(r), int(b))
            if green_dom > tol:
                px[x, y] = (r, g, b, 0)
            elif green_dom > tol - soft:
                # feather edge
                t = (green_dom - (tol - soft)) / max(soft, 1)
                alpha = int(255 * (1.0 - t))
                px[x, y] = (r, g, b, alpha)
    return rgba


def transparency_ratio(im: Image.Image) -> float:
    if im.mode != "RGBA":
        return 0.0
    a = im.getchannel("A")
    hist = a.histogram()
    transparent = sum(hist[:16])
    total = sum(hist)
    return transparent / total if total else 0.0


def _corner_rgb(im: Image.Image) -> tuple[int, int, int]:
    rgba = im.convert("RGBA")
    w, h = rgba.size
    pts = [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)]
    rs = gs = bs = 0
    for x, y in pts:
        r, g, b, _ = rgba.getpixel((x, y))
        rs += r
        gs += g
        bs += b
    n = len(pts)
    return rs // n, gs // n, bs // n


def chroma_key_color(
    im: Image.Image,
    key: tuple[int, int, int],
    *,
    tol: int = 42,
    soft: int = 28,
) -> Image.Image:
    """Key a solid backdrop colour (black/white/grey fallbacks)."""
    rgba = im.convert("RGBA")
    px = rgba.load()
    w, h = rgba.size
    kr, kg, kb = key
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < 16:
                continue
            dr = int(r) - kr
            dg = int(g) - kg
            db = int(b) - kb
            dist = max(abs(dr), abs(dg), abs(db))
            if dist <= tol:
                px[x, y] = (r, g, b, 0)
            elif dist <= tol + soft:
                t = (dist - tol) / max(soft, 1)
                alpha = int(a * t)
                px[x, y] = (r, g, b, alpha)
    return rgba


def auto_matte(im: Image.Image) -> Image.Image:
    """Green-screen matte with black/white corner fallback when needed."""
    out = chroma_key_green(im)
    if transparency_ratio(out) >= 0.45:
        return out
    corner = _corner_rgb(im)
    keyed = chroma_key_color(im, corner)
    if transparency_ratio(keyed) > transparency_ratio(out):
        return keyed
    return out
