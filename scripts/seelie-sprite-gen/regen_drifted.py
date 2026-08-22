#!/usr/bin/env python3
"""Archive selected frames then regen drifted strips (frame 2+)."""
from __future__ import annotations

import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "out"
ARCHIVE = OUT / "_regen_archive" / datetime.now().strftime("%Y%m%d-%H%M%S")
GEN = ROOT / "generate_clips.py"


def archive_from_frame(from_frame: int = 2) -> int:
    import json

    contract = json.loads((ROOT / "clip_contract.json").read_text(encoding="utf-8"))
    n = 0
    for c in contract["clips"]:
        for f in range(from_frame, c["frames"] + 1):
            src = OUT / c["group"] / c["name"] / f"{c['name']}_{f:03d}.png"
            if not src.exists():
                legacy = OUT / c["group"] / c["name"] / f"{c['name']}_{f:03d}.jpg"
                if legacy.exists():
                    src = legacy
            if not src.exists():
                continue
            dst = ARCHIVE / c["group"] / c["name"] / src.name
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
            n += 1
    return n


def main() -> None:
    from_frame = int(sys.argv[1]) if len(sys.argv) > 1 else 2
    archived = archive_from_frame(from_frame)
    print(f"archived {archived} frames under {ARCHIVE}")
    cmd = [
        sys.executable,
        str(GEN),
        "all",
        "--from-frame",
        str(from_frame),
        "--force",
    ]
    print("run:", " ".join(cmd))
    raise SystemExit(subprocess.call(cmd))


if __name__ == "__main__":
    main()
