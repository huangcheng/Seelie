#!/usr/bin/env python3
"""Build hatch-pet-style strip contact sheets for Seelie clip QA.

Outputs:
  out/qa/contact-sheet-strips.png  — one row per clip, frames left→right (primary)
  out/qa/contact-sheet.png         — first frame per clip grid (quick identity check)

Missing frames render as red MISSING cells.
"""
from __future__ import annotations

import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent
CONTRACT = json.loads((ROOT / "clip_contract.json").read_text(encoding="utf-8"))
OUT_DIR = ROOT / "out" / "qa"
OUT_STRIPS = OUT_DIR / "contact-sheet-strips.png"
OUT_GRID = OUT_DIR / "contact-sheet.png"

MAX_COLS = CONTRACT.get("columns", 8)
FRAME_CELL = 160
FRAME_H = 180
LABEL_W = 200
ROW_PAD = 8
PAD = 12
BG = (28, 28, 32)
LABEL_BG = (40, 40, 48)
LABEL_FG = (235, 235, 240)
MISSING_BG = (60, 40, 40)
GROUP_COLORS = {
    "idle": (90, 160, 200),
    "work": (120, 180, 120),
    "touch": (200, 140, 160),
    "motion": (180, 150, 100),
}


def font(size: int):
    for name in (
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/System/Library/Fonts/SFNS.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ):
        p = Path(name)
        if p.exists():
            return ImageFont.truetype(str(p), size)
    return ImageFont.load_default()


def frame_path(name: str, group: str, n: int) -> Path | None:
    for ext in (".png", ".jpg"):
        p = ROOT / "out" / group / name / f"{name}_{n:03d}{ext}"
        if p.exists():
            return p
    return None


def load_frame(name: str, group: str, n: int) -> Image.Image | None:
    path = frame_path(name, group, n)
    if path is None:
        return None
    im = Image.open(path).convert("RGBA")
    return composite_checker(im)


def composite_checker(im: Image.Image, cell: int = 16) -> Image.Image:
    """Flatten RGBA onto a checkerboard so transparency is visible in QA sheets."""
    w, h = im.size
    bg = Image.new("RGB", (w, h), BG)
    draw = ImageDraw.Draw(bg)
    c1, c2 = (48, 48, 54), (36, 36, 42)
    for y in range(0, h, cell):
        for x in range(0, w, cell):
            color = c1 if ((x // cell) + (y // cell)) % 2 == 0 else c2
            draw.rectangle([x, y, x + cell - 1, y + cell - 1], fill=color)
    bg.paste(im, (0, 0), im)
    return bg


def fit(img: Image.Image, box_w: int, box_h: int) -> Image.Image:
    img = img.copy()
    img.thumbnail((box_w, box_h), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (box_w, box_h), BG)
    canvas.paste(img, ((box_w - img.width) // 2, (box_h - img.height) // 2))
    return canvas


def missing_cell(box_w: int, box_h: int, label: str, f: ImageFont.FreeTypeFont) -> Image.Image:
    cell = Image.new("RGB", (box_w, box_h), MISSING_BG)
    d = ImageDraw.Draw(cell)
    d.text((8, box_h // 2 - 16), "MISSING", fill=(240, 180, 180), font=f)
    d.text((8, box_h // 2 + 4), label, fill=(220, 200, 200), font=f)
    return cell


def build_strips() -> tuple[Image.Image, list[str]]:
    clips = CONTRACT["clips"]
    row_h = FRAME_H + ROW_PAD
    sheet_w = PAD + LABEL_W + MAX_COLS * (FRAME_CELL + 4) + PAD
    sheet_h = PAD + len(clips) * row_h + PAD
    sheet = Image.new("RGB", (sheet_w, sheet_h), BG)
    draw = ImageDraw.Draw(sheet)
    small_f = font(13)
    title_f = font(15)
    missing: list[str] = []

    for row_i, c in enumerate(clips):
        name, group, nframes = c["name"], c["group"], c["frames"]
        y0 = PAD + row_i * row_h
        accent = GROUP_COLORS.get(group, (160, 160, 160))

        draw.rectangle([PAD, y0, PAD + LABEL_W - 4, y0 + FRAME_H], fill=LABEL_BG)
        draw.rectangle([PAD, y0, PAD + 5, y0 + FRAME_H], fill=accent)
        draw.text((PAD + 12, y0 + 10), name, fill=LABEL_FG, font=title_f)
        draw.text((PAD + 12, y0 + 32), f"{group} · {nframes}f", fill=(180, 180, 190), font=small_f)

        for f in range(1, nframes + 1):
            x0 = PAD + LABEL_W + (f - 1) * (FRAME_CELL + 4)
            img = load_frame(name, group, f)
            if img is None:
                missing.append(f"{name}#{f:03d}")
                cell = missing_cell(FRAME_CELL, FRAME_H, f"#{f:03d}", small_f)
            else:
                cell = fit(img, FRAME_CELL, FRAME_H)
            sheet.paste(cell, (x0, y0))
            draw.rectangle(
                [x0, y0 + FRAME_H - 18, x0 + FRAME_CELL, y0 + FRAME_H],
                fill=(0, 0, 0, 128),
            )
            draw.text((x0 + 4, y0 + FRAME_H - 16), f"{f}", fill=(220, 220, 220), font=small_f)

    return sheet, missing


def build_grid() -> tuple[Image.Image, list[str]]:
    clips = CONTRACT["clips"]
    cols = 4
    cell_w, cell_h, label_h = 320, 360, 36
    rows = (len(clips) + cols - 1) // cols
    sheet_w = PAD + cols * (cell_w + PAD)
    sheet_h = PAD + rows * (cell_h + label_h + PAD)
    sheet = Image.new("RGB", (sheet_w, sheet_h), BG)
    draw = ImageDraw.Draw(sheet)
    small_f = font(14)
    title_f = font(18)
    missing: list[str] = []

    for i, c in enumerate(clips):
        name, group, nframes = c["name"], c["group"], c["frames"]
        col, row = i % cols, i // cols
        x0 = PAD + col * (cell_w + PAD)
        y0 = PAD + row * (cell_h + label_h + PAD)

        img = load_frame(name, group, 1)
        if img is None:
            missing.append(name)
            cell = missing_cell(cell_w, cell_h, "frame 1", title_f)
        else:
            cell = fit(img, cell_w, cell_h)
        sheet.paste(cell, (x0, y0))

        draw.rectangle([x0, y0 + cell_h, x0 + cell_w, y0 + cell_h + label_h], fill=LABEL_BG)
        accent = GROUP_COLORS.get(group, (160, 160, 160))
        draw.rectangle([x0, y0 + cell_h, x0 + 6, y0 + cell_h + label_h], fill=accent)
        draw.text(
            (x0 + 14, y0 + cell_h + 8),
            f"{name} · {group} · {nframes}f",
            fill=LABEL_FG,
            font=small_f,
        )

    return sheet, missing


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    strips, miss_strips = build_strips()
    grid, miss_grid = build_grid()
    strips.save(OUT_STRIPS, optimize=True)
    grid.save(OUT_GRID, optimize=True)
    print(f"saved {OUT_STRIPS} ({strips.width}x{strips.height})")
    print(f"saved {OUT_GRID} ({grid.width}x{grid.height})")
    missing = sorted(set(miss_strips))
    if missing:
        print(f"missing {len(missing)} frames:", ", ".join(missing[:20]), end="")
        if len(missing) > 20:
            print(f" … +{len(missing) - 20} more")
        else:
            print()
        raise SystemExit(1)


if __name__ == "__main__":
    main()
