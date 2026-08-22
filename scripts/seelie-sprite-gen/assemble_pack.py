#!/usr/bin/env python3
"""Assemble approved PNG frames into Seelie sprite pack (atlas + animations.json)."""
from __future__ import annotations

import json
import shutil
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[1]
CONTRACT = json.loads((ROOT / "clip_contract.json").read_text(encoding="utf-8"))
OUT_FRAMES = ROOT / "out"
PACK = REPO / "assets" / "packs" / "seelie"
FRAME_EXT = ".png"
MARGIN = 10


def frame_file(group: str, name: str, n: int) -> Path:
    return OUT_FRAMES / group / name / f"{name}_{n:03d}{FRAME_EXT}"


def alpha_bbox(im: Image.Image) -> tuple[int, int, int, int] | None:
    if im.mode != "RGBA":
        im = im.convert("RGBA")
    return im.getchannel("A").getbbox()


def crop_visible(im: Image.Image) -> Image.Image:
    im = im.convert("RGBA")
    box = alpha_bbox(im)
    return im.crop(box) if box else im


def reference_body_height() -> int:
    """Use idle frame 1 as global scale reference."""
    ref = crop_visible(Image.open(frame_file("idle", "idle", 1)))
    return ref.height


def fit_cell(im: Image.Image, fw: int, fh: int, ref_body_h: int) -> Image.Image:
    cropped = crop_visible(im)
    inner_w = fw - MARGIN * 2
    inner_h = fh - MARGIN * 2
    scale = min(inner_w / cropped.width, inner_h / cropped.height)
    # Keep all clips roughly the same apparent size as idle.
    scale = min(scale, (inner_h * 0.92) / ref_body_h)
    new_w = max(1, int(cropped.width * scale))
    new_h = max(1, int(cropped.height * scale))
    resized = cropped.resize((new_w, new_h), Image.Resampling.LANCZOS)
    cell = Image.new("RGBA", (fw, fh), (0, 0, 0, 0))
    x = (fw - new_w) // 2
    y = fh - new_h - MARGIN  # feet-anchored
    cell.paste(resized, (x, y), resized)
    return cell


def default_duration(clip: dict) -> int:
    return int(clip.get("durationMs", 140))


def main() -> None:
    fw = CONTRACT["frameWidth"]
    fh = CONTRACT["frameHeight"]
    cols = CONTRACT["columns"]
    clips = CONTRACT["clips"]
    rows = max(c["row"] for c in clips) + 1
    sheet = Image.new("RGBA", (cols * fw, rows * fh), (0, 0, 0, 0))
    ref_body_h = reference_body_height()

    anims: list[dict] = []
    missing: list[str] = []

    for clip in clips:
        name, group, row = clip["name"], clip["group"], clip["row"]
        frames_json: list[dict] = []
        for f in range(1, clip["frames"] + 1):
            src = frame_file(group, name, f)
            if not src.exists():
                missing.append(f"{name}#{f:03d}")
                continue
            cell = fit_cell(Image.open(src), fw, fh, ref_body_h)
            col = (f - 1) % cols
            x, y = col * fw, row * fh
            sheet.paste(cell, (x, y), cell)
            frames_json.append(
                {
                    "Duration": default_duration(clip),
                    "ImagesOffsets": {"Column": col, "Row": row},
                }
            )
        anims.append({"Name": name, "Frames": frames_json})

    if missing:
        raise SystemExit(f"missing {len(missing)} frames: {', '.join(missing[:12])}")

    PACK.mkdir(parents=True, exist_ok=True)
    sheet_path = PACK / "spritesheet.webp"
    sheet.save(sheet_path, "WEBP", lossless=True)
    (PACK / "animations.json").write_text(
        json.dumps(anims, indent=2) + "\n", encoding="utf-8"
    )

    preview_src = ROOT / "refs" / "sakura_preview.png"
    if preview_src.exists():
        shutil.copy2(preview_src, PACK / "preview.png")

    manifest = {
        "formatVersion": "1.0.0",
        "id": "im.cheng.seelie.seelie",
        "name": "Seelie",
        "nameLocalized": {"zh_CN": "仙灵"},
        "author": "HUANG Cheng",
        "version": "2.0.0",
        "description": "Seelie sakura mascot — desktop-life sprite pack with idle, work, touch, and motion clips.",
        "preview": "preview.png",
        "tags": ["seelie", "original", "anime", "fae", "default"],
        "license": "MIT",
        "category": "originals",
        "minAppVersion": "1.0.0",
        "character": {
            "type": "spriteSheet",
            "spriteSheet": "spritesheet.webp",
            "frameWidth": fw,
            "frameHeight": fh,
            "definitions": "animations.json",
            "displayScale": 1.0,
            "desktopMotion": True,
        },
        "idlePool": [
            {"name": "idle", "weight": 6},
            {"name": "idle_fidget", "weight": 2},
            {"name": "idle_look_left", "weight": 1},
            {"name": "idle_look_right", "weight": 1},
            {"name": "idle_stretch", "weight": 1},
            {"name": "idle_snooze", "weight": 1},
        ],
        "stateMap": {
            "Idle": ["idle"],
            "Greeting": ["greet", "waving"],
            "Thinking": ["think", "waiting"],
            "Working": ["work", "running"],
            "Reviewing": ["review"],
            "Failed": ["fail", "failed"],
            "Celebrating": ["celebrate", "jumping"],
            "Petted": ["pet", "jumping"],
            "Grabbed": ["grab", "failed"],
            "Tossed": ["toss", "running"],
        },
        "nameMap": {
            "waving": "greet",
            "waiting": "think",
            "running": "work",
            "failed": "fail",
            "jumping": "celebrate",
            "pat": "pet",
            "wave": "greet",
            "alert": "fail",
            "running-left": "walk_left",
            "running-right": "walk_right",
            "lookleft": "idle_look_left",
            "lookright": "idle_look_right",
        },
        "eventMap": {
            "session.start": "greet",
            "session.end": "idle",
            "session.idle": "idle",
            "session.error": "fail",
            "prompt.submitted": "think",
            "tool.before": "work",
            "tool.after": "idle",
            "tool.failed": "fail",
            "permission.requested": "review",
            "permission.denied": "fail",
            "permission.response": "idle",
            "subagent.started": "work",
            "subagent.stopped": "review",
            "notification.sent": "greet",
            "file.edited": "work",
            "file.watched": "idle",
            "todo.updated": "celebrate",
        },
    }
    (PACK / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )

    qa = OUT_FRAMES / "qa" / "atlas-preview.png"
    qa.parent.mkdir(parents=True, exist_ok=True)
    preview = sheet.copy()
    preview.thumbnail((1200, 4000), Image.Resampling.LANCZOS)
    preview.save(qa, optimize=True)

    print(f"wrote {sheet_path} ({sheet.width}x{sheet.height})")
    print(f"wrote {PACK / 'animations.json'} ({len(anims)} clips)")
    print(f"wrote {PACK / 'manifest.json'}")
    print(f"atlas preview: {qa}")


if __name__ == "__main__":
    main()
