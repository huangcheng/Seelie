# Seelie sprite generation notes

## Output format (lossless PNG + alpha)

- API: `output_format: png` (Seedream 5.0 Lite) — **never JPEG** for sprite frames.
- JPEG→PNG conversion cannot recover lost detail or invent transparency.
- This gateway returns lossless PNG from the model; if the model omits alpha (RGB PNG),
  we chroma-key the **green screen** (#00FF00) on that lossless PNG into RGBA.
- Frames: `out/<group>/<clip>/<clip>_NNN.png` (2048×2048, RGBA).

## Frame budget (clip_contract.json)

| Type | Frames | Duration | Notes |
|------|--------|----------|-------|
| Walk loops | **8** | 110 ms | Match Codex hatch smoothness |
| Base idle | **8** | 140 ms | Breathing / micro-motion |
| Reactions | **5–6** | 120–150 ms | greet, fail, celebrate, work… |
| Physics | **5–6** | 100–110 ms | fall, toss, hop, land, grab |
| Micro idle | **4–5** | 140–180 ms | look, snooze, fidget |

**Total: 115 frames** across 21 clips (`columns: 8` in atlas).

## Consistency rules (do not skip)

1. Always attach `refs/sakura_preview.png` as the image reference (frame 1).
2. Frame 2+ anchor to the same clip's frame 1 (not chained N-1) to avoid identity drift.
3. Prompt order: **LOCK first**, **per-frame ACTION** second.
4. **Green-screen matte** — flat #00FF00 behind character; character-only, no props/hands.
5. One bad frame → regen: `generate_clips.py motion walk_left#4 --force`.

## Run

```bash
python scripts/seelie-sprite-gen/image_gateway.py probe --format png

# all frames as RGBA PNG (115 API calls)
python scripts/seelie-sprite-gen/generate_clips.py all --force

# single frame
python scripts/seelie-sprite-gen/generate_clips.py idle idle#003 --force

# QA sheets (checkerboard shows transparency)
python scripts/seelie-sprite-gen/make_contact_sheet.py

# assemble into Seelie pack (atlas + animations.json + manifest)
python scripts/seelie-sprite-gen/assemble_pack.py
```

Do not commit API hostnames, keys, or provider brand names in this tree.
