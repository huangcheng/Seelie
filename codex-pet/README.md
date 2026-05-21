# Seelie Codex Pets

Built-in mascots packaged in **OpenAI Codex CLI's pet format**, ready
to upload to https://codex-pets.net.

Each pet has a dual-distribution setup — the same atlas ships both inside
the Seelie desktop app and as a Codex CLI pet:

| | Internal Seelie pack | Codex pet (this directory) |
|---|---|---|
| Manifest | `assets/packs/<id>/manifest.json` (Seelie schema) | `pet.json` (Codex CLI schema, 4 fields) |
| Loader | Seelie's `SpritePackManager` + `SpriteAnimationEngine` | Codex CLI's pet picker |
| Atlas | `assets/packs/<id>/spritesheet.webp` | `spritesheet.webp` *(byte-identical)* |
| Distribution | Bundled inside `Seelie.app` | Uploaded to codex-pets.net |

## Pets

- **`seelie/`** — Seelie, the project mascot. A small fae-spirit virtual idol girl.
- **`qobster/`** — Qobster, QQ Penguin in a Lobster Costume. A quirky crossover mascot.
- **`lite-bot/`** — LiteBot, a built-in mascot imported from an external hatch-pet run.
- **`sweetheart-meow/`** — Sweetheart Meow (甜心喵), the cat mascot from Tencent's WorkBuddy.
- **`workbuddy/`** — WorkBuddy, Tencent's AI agent office mascot.
- **`puppy-meow/`** — Puppy Meow, the pup mascot from Tencent's WorkBuddy.
- **`worker-meow/`** — Worker Meow, the coder mascot from Tencent CodeBuddy.
- **`artist-meow/`** — Artist Meow, the artist mascot from Tencent WorkBuddy.
- **`warrior-meow/`** — Warrior Meow, the warrior mascot from Tencent WorkBuddy.
- **`marvis/`** — Marvis, the QQ dragon mascot.
- **`qwen-bear/`** — Qwen Bear, the bear mascot from Alibaba's Qwen.

## Per-pet files

Each pet subdirectory contains exactly two files (what codex-pets.net expects):

- **`pet.json`** — 4-field Codex CLI manifest (`id`, `displayName`, `description`, `spritesheetPath`)
- **`spritesheet.webp`** — 1536×1872 WebP, 8×9 atlas, 192×208 cells

Upload them as-is, or zip at the pet subdirectory root:

```
cd codex-pet/<id>
zip ../<id>-codex-pet.zip pet.json spritesheet.webp
```

## Regenerating

To rebuild a pet atlas (preserving identity across rows), see
`.claude/skills/seelie-codex-pet/SKILL.md`. After a fresh hatch-pet run
produces a new `~/.codex/pets/<id>/`, copy the two files into both
distribution channels:

```
# Codex CLI distribution
cp ~/.codex/pets/<id>/pet.json         codex-pet/<id>/pet.json
cp ~/.codex/pets/<id>/spritesheet.webp codex-pet/<id>/spritesheet.webp

# Seelie internal pack (keep in sync)
cp ~/.codex/pets/<id>/spritesheet.webp assets/packs/<id>/spritesheet.webp
```
