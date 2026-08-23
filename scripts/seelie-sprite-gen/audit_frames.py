#!/usr/bin/env python3
"""List generated frames that need regeneration (size / matte / clip inconsistency)."""
from __future__ import annotations

import json
import sys
from pathlib import Path

from PIL import Image

from matte import transparency_ratio

ROOT = Path(__file__).resolve().parent
CONTRACT = json.loads((ROOT / "clip_contract.json").read_text(encoding="utf-8"))
OUT = ROOT / "out"
EXPECTED_SIZE = (2048, 2048)
MIN_TRANSPARENCY = 0.45


def alpha_bbox(im: Image.Image) -> tuple[int, int, int, int] | None:
    return im.convert("RGBA").getchannel("A").getbbox()


def main() -> int:
    flagged: list[dict] = []

    for clip in CONTRACT["clips"]:
        name, group = clip["name"], clip["group"]
        bboxes: list[tuple[int, int, int, int]] = []
        for n in range(1, clip["frames"] + 1):
            key = f"{name}#{n:03d}"
            path = OUT / group / name / f"{name}_{n:03d}.png"
            reasons: list[str] = []

            if not path.exists():
                flagged.append({"key": key, "group": group, "clip": name, "frame": n, "reasons": ["missing"]})
                continue

            im = Image.open(path)
            if im.size != EXPECTED_SIZE:
                reasons.append(f"size={im.size}")
            tr = transparency_ratio(im)
            if tr < MIN_TRANSPARENCY:
                reasons.append(f"transparent={tr:.2f}")

            box = alpha_bbox(im)
            if box is None:
                reasons.append("empty_alpha")
            else:
                bboxes.append(box)

            if reasons:
                flagged.append({"key": key, "group": group, "clip": name, "frame": n, "reasons": reasons})

        if len(bboxes) >= 2:
            heights = [b[3] - b[1] for b in bboxes]
            widths = [b[2] - b[0] for b in bboxes]
            h_spread = max(heights) - min(heights)
            w_spread = max(widths) - min(widths)
            med_h = sorted(heights)[len(heights) // 2]
            med_w = sorted(widths)[len(widths) // 2]
            for n, box in enumerate(bboxes, start=1):
                h = box[3] - box[1]
                w = box[2] - box[0]
                outlier = False
                if med_h > 0 and abs(h - med_h) / med_h > 0.12:
                    outlier = True
                if med_w > 0 and abs(w - med_w) / med_w > 0.12:
                    outlier = True
                if outlier:
                    key = f"{name}#{n:03d}"
                    if not any(f["key"] == key for f in flagged):
                        flagged.append({
                            "key": key,
                            "group": group,
                            "clip": name,
                            "frame": n,
                            "reasons": [f"bbox_outlier {w}x{h} med={med_w}x{med_h}"],
                        })

    qa = OUT / "qa"
    qa.mkdir(parents=True, exist_ok=True)
    (qa / "regen-list.json").write_text(json.dumps(flagged, indent=2) + "\n", encoding="utf-8")

    by_clip: dict[str, list[int]] = {}
    for item in flagged:
        by_clip.setdefault(item["clip"], []).append(item["frame"])

    print(f"flagged {len(flagged)} frame(s)")
    for clip, frames in sorted(by_clip.items()):
        print(f"  {clip}: {sorted(set(frames))}")

  # Regen CLI hint
    if flagged and "--print-cmd" in sys.argv:
        tokens = []
        for clip, frames in sorted(by_clip.items()):
            group = next(c["group"] for c in CONTRACT["clips"] if c["name"] == clip)
            for f in sorted(set(frames)):
                tokens.append(f"{clip}#{f:03d}")
        print("python generate_clips.py", group if len(by_clip) == 1 else "all", ",".join(tokens), "--force")

    return 1 if flagged else 0


if __name__ == "__main__":
    raise SystemExit(main())
