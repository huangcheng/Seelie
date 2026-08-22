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
