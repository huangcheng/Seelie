#!/usr/bin/env python3
"""Generate Seelie clip frame strips with reference lock + per-frame pose prompts.

Outputs one lossless PNG per frame: out/<group>/<clip>/<clip>_001.png … <clip>_00N.png

Always:
  1) LOCK block first (identity + clothing + green-screen for matte)
  2) per-frame ACTION pose (frame i of N)
  3) refs/sakura_preview.png as image reference (frame 1); clip frame 1 anchor for 2+
  4) API output_format=png (never JPEG); chroma-key matte on lossless PNG if needed

Credentials: see image_gateway.py / image_gateway.local.json.example
"""
from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
CONTRACT = json.loads((ROOT / "clip_contract.json").read_text(encoding="utf-8"))
REF = ROOT / "refs" / "sakura_preview.png"
GATEWAY = ROOT / "image_gateway.py"
FRAME_EXT = ".png"

CLIP_BY_NAME = {c["name"]: c for c in CONTRACT["clips"]}

# Base pose per clip (frame-specific poses override via FRAME_SEQUENCES).
PROMPTS = {
    "idle": "front view, soft neutral idle standing",
    "idle_fidget": "front view, cute fidget shifting weight",
    "idle_look_left": "front-ish view, head turning to look left",
    "idle_look_right": "front-ish view, head turning to look right",
    "idle_stretch": "front view, stretch cycle with arms rising",
    "idle_snooze": "front view, sleepy snooze cycle",
    "greet": "front view, friendly wave hello",
    "think": "front view, thinking pose hand near chin",
    "work": "front view, determined working gesture cycle",
    "review": "front view, focused reviewing posture",
    "fail": "front view, sad apologetic fail reaction",
    "celebrate": "front view, celebrate cheer arms up",
    "pet": "front view, blissful happy head tilt enjoying affection",
    "grab": "front view, surprised lifted pose, no visible hands",
    "toss": "front view, tumbling mid-air surprised cute pose",
    "walk_left": "strict side profile facing LEFT, tiny chibi desktop-pet shuffle, compact steps, feet on one ground line",
    "walk_right": "strict side profile facing RIGHT, tiny chibi desktop-pet shuffle, compact steps, feet on one ground line",
    "sit": "front view, sitting down then perched with legs dangling",
    "hop_off": "front view, hop downward in mid-air",
    "fall": "front view, falling downward cute flailing",
    "land": "front view, landing squat and recover to stand",
}

# Explicit per-frame poses for loops and physics (index 0 = frame 1).
FRAME_SEQUENCES: dict[str, list[str]] = {
    "idle": [
        "neutral standing, arms relaxed, calm smile",
        "tiny inhale, shoulders slightly up, same expression",
        "peak gentle breathe in, subtle chest rise",
        "exhale start, shoulders relaxing",
        "neutral again, micro weight shift to left foot",
        "weight shift to right foot, arms sway slightly",
        "eyes half-blink, otherwise neutral",
        "return to full neutral idle pose",
    ],
    "walk_left": [
        "strict side profile facing LEFT, tiny chibi shuffle, left foot planted, right foot lifts slightly",
        "strict side profile facing LEFT, right foot passes close to left ankle, knees softly bent",
        "strict side profile facing LEFT, right foot touches down under body, short step only",
        "strict side profile facing LEFT, weight on right foot, left foot begins forward",
        "strict side profile facing LEFT, left foot passes close to right ankle, compact stride",
        "strict side profile facing LEFT, left foot touches down under body, short step only",
        "strict side profile facing LEFT, both feet under hips, subtle body bob up",
        "strict side profile facing LEFT, reset to contact pose, feet on same invisible ground line",
    ],
    "walk_right": [
        "side view RIGHT, left foot forward contact pose",
        "side view RIGHT, left foot down weight on left leg",
        "side view RIGHT, legs passing mid-stride",
        "side view RIGHT, right foot forward contact pose",
        "side view RIGHT, right foot down weight on right leg",
        "side view RIGHT, legs passing mid-stride opposite",
        "side view RIGHT, body rising up step",
        "side view RIGHT, peak up step before next contact",
    ],
    "idle_stretch": [
        "arms at sides, about to stretch",
        "arms rising to shoulder height",
        "arms up high stretch peak, mouth tiny yawn",
        "arms lowering from peak",
        "arms back down relaxed",
    ],
    "celebrate": [
        "arms starting to rise, smile growing",
        "arms halfway up, happy eyes",
        "arms fully up V cheer peak, big smile",
        "hold cheer peak slightly",
        "arms lowering a little, still happy",
        "settle with small bounce, pleased smile",
    ],
    "fall": [
        "feet just leaving ground, surprised face",
        "tilting backward start of fall",
        "mid-air tumble sideways",
        "full tumble peak, limbs out",
        "rotating downward, worried cute face",
        "almost horizontal before landing",
    ],
    "land": [
        "deep squat impact, knees bent, arms out for balance",
        "absorb shock lowest point",
        "rising from squat",
        "almost standing straight",
        "recovered neutral stand",
    ],
    "hop_off": [
        "crouch before hop",
        "push off upward",
        "peak of hop mid-air",
        "descending",
        "about to land feet down",
    ],
    "toss": [
        "just lifted, surprised",
        "tilting in air",
        "mid tumble rotation",
        "inverted tumble peak",
        "unwinding rotation",
        "settling tumbling end",
    ],
    "grab": [
        "normal stand",
        "slight surprise, rising on toes",
        "lifted higher, arms out",
        "peak lifted surprised pose",
        "settling lifted okay pose",
    ],
    "greet": [
        "neutral smile",
        "hand starting to rise",
        "hand mid wave",
        "wave peak, bright smile",
        "hand lowering, still smiling",
    ],
    "sit": [
        "standing, starting to sit",
        "mid sit knees bending",
        "seated legs dangling pose",
        "settled seated idle",
    ],
    "idle_snooze": [
        "eyes open but heavy and drowsy",
        "eyes half closed sleepy",
        "eyes mostly closed dozing",
        "eyes closed peaceful snooze",
        "deep snooze eyes shut, head nodding slightly",
    ],
    "idle_fidget": [
        "neutral stand",
        "shift weight left, hands fidget at sides",
        "shift weight right, tiny hop in place",
        "return center, one hand touches hoodie hem",
        "settle neutral with small sway",
    ],
    "idle_look_left": [
        "face forward",
        "head starting to turn left",
        "head turned left looking",
        "hold look left curious",
    ],
    "idle_look_right": [
        "face forward",
        "head starting to turn right",
        "head turned right looking",
        "hold look right curious",
    ],
}

DECORATION_RISK = {
    "idle_stretch",
    "celebrate",
    "fall",
    "toss",
    "hop_off",
    "walk_left",
    "walk_right",
}

ISOLATION_RISK = {
    "grab",
    "pet",
    "sit",
    "hop_off",
    "toss",
    "land",
    "review",
}

# Side-view clips always lock to bible ref (never anchor frame 1 of a front pose).
SIDE_VIEW_CLIPS = {"walk_left", "walk_right"}

LOCK = (
    "Keep the EXACT same character as the reference image — identical face, "
    "WHITE bob hair with a single pink streak (NOT solid pink hair, NOT all-pink bob), "
    "pink eyes, oversized light-pink hoodie with HOOD UP, "
    "cat-ear hood with sakura flowers on each ear, sakura prints on kangaroo pocket "
    "and sleeves, pink shoulder bows, white drawstrings ending in sakura flower toggles, "
    "pink pleated skirt with white ruffle AND sakura petal pattern, "
    "white sneakers with pink laces AND sakura motifs. "
    "Pastel pink and white only. Soft 3D chibi collectible look. "
    "Flat chroma KEY GREEN screen background (#00FF00) for matting — NOT white, NOT scenery. "
    "ONLY the girl character — isolated, centered, full body. "
    "No other people, no human hands, no arms from off-screen, no walls, no ledges, "
    "no furniture, no floor plane, no props, no tools, no particles, no shadows on surfaces. "
    "No text, no extra characters, no purple. "
    "Do not simplify the outfit into a plain undecorated hoodie."
)


def phase_pose(base: str, frame_idx: int, frame_total: int) -> str:
    """Generic in-between poses when no explicit sequence exists."""
    if frame_total <= 1:
        return base
    t = frame_idx / (frame_total - 1)
    if t <= 0.2:
        phase = "start of motion, pose just beginning"
    elif t <= 0.45:
        phase = "early-mid in-between pose"
    elif t <= 0.7:
        phase = "peak of the action"
    elif t < 1.0:
        phase = "late in-between settling"
    else:
        phase = "end pose hold"
    return f"{base}, {phase}"


def frame_pose(name: str, frame_idx: int, frame_total: int) -> str:
    seq = FRAME_SEQUENCES.get(name)
    if seq:
        if frame_idx < len(seq):
            return seq[frame_idx]
        return seq[-1]
    return phase_pose(PROMPTS[name], frame_idx, frame_total)


def build_prompt(name: str, frame_idx: int, frame_total: int) -> str:
    pose = frame_pose(name, frame_idx, frame_total)
    extra = ""
    if name in DECORATION_RISK:
        extra += (
            " Keep every sakura print, ear blossom, shoulder bow, and shoe motif "
            "clearly visible even with this pose."
        )
    if name in ISOLATION_RISK:
        extra += (
            " Show ONLY the doll on chroma green screen — absolutely no hands, "
            "walls, ledges, furniture, or props."
        )
    if name in SIDE_VIEW_CLIPS:
        extra += (
            " Camera locked side view. Small cute desktop-pet shuffle — NO wide stride, "
            "NO sprint, NO leaning. Both shoes stay on the same horizontal ground line; "
            "character stays same scale and centered in frame."
        )
    return (
        f"{LOCK} Single full-body character, centered. "
        f"Animation frame {frame_idx + 1} of {frame_total} in clip '{name}'. "
        f"ACTION (pose only): {pose}.{extra}"
    )


def frame_path(name: str, group: str, frame_num: int) -> Path:
    return ROOT / "out" / group / name / f"{name}_{frame_num:03d}{FRAME_EXT}"


def resolve_existing_frame(name: str, group: str, frame_num: int) -> Path | None:
    """Find frame on disk (.png preferred, .jpg legacy)."""
    for ext in (FRAME_EXT, ".jpg"):
        p = ROOT / "out" / group / name / f"{name}_{frame_num:03d}{ext}"
        if p.exists() and p.stat().st_size > 1000:
            return p
    return None


def resolve_ref(name: str, group: str, frame_num: int, chain_refs: bool) -> Path:
    if frame_num == 1:
        return REF
    if chain_refs:
        prev = resolve_existing_frame(name, group, frame_num - 1)
        if prev is not None:
            return prev
    if name in SIDE_VIEW_CLIPS:
        return REF
    # Front-facing clips: anchor to this clip's frame 1 to avoid compounding drift.
    anchor = resolve_existing_frame(name, group, 1)
    if anchor is not None:
        return anchor
    return REF


def gen_frame(
    name: str,
    group: str,
    frame_num: int,
    frame_total: int,
    *,
    force: bool = False,
    chain_refs: bool = False,
) -> Path:
    dest = frame_path(name, group, frame_num)
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists() and dest.stat().st_size > 1000 and not force:
        print(f"skip {name} #{frame_num:03d}")
        return dest

    ref = resolve_ref(name, group, frame_num, chain_refs)

    cmd = [
        sys.executable,
        str(GATEWAY),
        "gen",
        "--prompt",
        build_prompt(name, frame_num - 1, frame_total),
        "--ref",
        str(ref),
        "--ref-mode",
        "image",
        "--out",
        str(dest),
        "--size",
        "2k",
        "--format",
        "png",
        "--matte",
        "green",
    ]
    print(f"gen {name} #{frame_num:03d}/{frame_total:03d} ref={ref.name} ...", flush=True)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.stdout.strip():
        print(r.stdout.strip())
    if r.returncode != 0:
        print(r.stderr, file=sys.stderr)
        raise SystemExit(f"failed {name} #{frame_num:03d}: {r.returncode}")
    if not dest.exists():
        raise SystemExit(f"missing output {dest}")
    return dest


def gen_clip(
    name: str,
    group: str,
    *,
    force: bool = False,
    chain_refs: bool = False,
    from_frame: int = 1,
    only_frames: set[int] | None = None,
) -> None:
    clip = CLIP_BY_NAME[name]
    total = clip["frames"]
    for n in range(1, total + 1):
        if n < from_frame:
            continue
        if only_frames and n not in only_frames:
            continue
        gen_frame(name, group, n, total, force=force, chain_refs=chain_refs)


def parse_args(argv: list[str]) -> tuple[bool, bool, int, str, set[str], set[int]]:
    force = "--force" in argv
    chain_refs = "--chain-refs" in argv
    from_frame = 1
    if "--from-frame" in argv:
        i = argv.index("--from-frame")
        if i + 1 < len(argv):
            from_frame = max(1, int(argv[i + 1]))
    skip = {"--force", "--chain-refs", "--from-frame"}
    if "--from-frame" in argv:
        skip.add(argv[argv.index("--from-frame") + 1])
    args = [a for a in argv if a not in skip and not a.startswith("--")]
    group = args[0] if args else "all"
    only_clips: set[str] = set()
    only_frames: set[int] = set()
    if len(args) > 1:
        for token in args[1].split(","):
            token = token.strip()
            if not token:
                continue
            if "#" in token:
                clip, _, frame_s = token.partition("#")
                only_clips.add(clip)
                only_frames.add(int(frame_s))
            else:
                only_clips.add(token)
    return force, chain_refs, from_frame, group, only_clips, only_frames


def main() -> None:
    force, chain_refs, from_frame, group, only_clips, only_frames = parse_args(
        sys.argv[1:]
    )
    if not REF.exists():
        raise SystemExit(f"missing ref {REF}")
    if not GATEWAY.exists():
        raise SystemExit(f"missing {GATEWAY}")

    total_cells = 0
    for c in CONTRACT["clips"]:
        if group != "all" and c["group"] != group:
            continue
        if only_clips and c["name"] not in only_clips:
            continue
        frames = (
            [n for n in range(from_frame, c["frames"] + 1)]
            if not only_frames
            else sorted(only_frames)
        )
        gen_clip(
            c["name"],
            c["group"],
            force=force,
            chain_refs=chain_refs,
            from_frame=from_frame,
            only_frames=only_frames or None,
        )
        total_cells += len(frames)

    print(f"done {group} ({total_cells} frame slots targeted)")


if __name__ == "__main__":
    main()
