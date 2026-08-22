#!/usr/bin/env python3
"""Automated QA for generated clip PNGs."""
from __future__ import annotations

import json
from pathlib import Path

from PIL import Image

from matte import transparency_ratio

ROOT = Path(__file__).resolve().parent
CONTRACT = json.loads((ROOT / "clip_contract.json").read_text(encoding="utf-8"))
OUT = ROOT / "out"


def main() -> None:
    missing: list[str] = []
    bad_alpha: list[str] = []
    bad_size: list[str] = []
    small: list[str] = []

    for c in CONTRACT["clips"]:
        for n in range(1, c["frames"] + 1):
            key = f"{c['name']}#{n:03d}"
            p = OUT / c["group"] / c["name"] / f"{c['name']}_{n:03d}.png"
            if not p.exists():
                missing.append(key)
                continue
            if p.stat().st_size < 50_000:
                small.append(f"{key} ({p.stat().st_size}B)")
            im = Image.open(p)
            if im.size[0] < 1024 or im.size[1] < 1024:
                bad_size.append(f"{key} {im.size}")
            if im.mode != "RGBA":
                bad_alpha.append(f"{key} mode={im.mode}")
            elif transparency_ratio(im) < 0.15:
                bad_alpha.append(f"{key} transparent={transparency_ratio(im)*100:.1f}%")

    report = {
        "total_expected": sum(c["frames"] for c in CONTRACT["clips"]),
        "missing": missing,
        "low_transparency": bad_alpha,
        "small_file": small,
        "low_resolution": bad_size,
    }
    qa_dir = OUT / "qa"
    qa_dir.mkdir(parents=True, exist_ok=True)
    (qa_dir / "qa-report.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps({k: len(v) if isinstance(v, list) else v for k, v in report.items()}, indent=2))
    if missing or bad_alpha or bad_size:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
