#!/usr/bin/env python3
"""Regenerate only frames flagged by audit_frames.py."""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REGEN_LIST = ROOT / "out" / "qa" / "regen-list.json"

# Import generation helpers from sibling module.
sys.path.insert(0, str(ROOT))
from generate_clips import CLIP_BY_NAME, gen_frame  # noqa: E402


def main() -> None:
    if not REGEN_LIST.exists():
        raise SystemExit(f"missing {REGEN_LIST} — run audit_frames.py first")

    items = json.loads(REGEN_LIST.read_text(encoding="utf-8"))
    if not items:
        print("nothing to regenerate")
        return

    print(f"regenerating {len(items)} flagged frame(s)...", flush=True)
    for item in items:
        clip = item["clip"]
        group = item["group"]
        frame = int(item["frame"])
        total = CLIP_BY_NAME[clip]["frames"]
        print(f"--- {clip} #{frame:03d} ({', '.join(item.get('reasons', []))})", flush=True)
        gen_frame(clip, group, frame, total, force=True, chain_refs=True)

    print("done")


if __name__ == "__main__":
    main()
