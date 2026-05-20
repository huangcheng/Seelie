# Artworks

Original high-resolution source artwork the Seelie project was built
from. Everything in this directory is an **input** — committed so the
project's visual identity is reproducible and re-deriveable. Build
outputs (`.icns`, `.ico`, the codex-pet atlas, the qrc-packed PNG)
live under `assets/` and are regenerated from these sources.

Provenance: all images here were generated 2026-05-17 via the MiniMax
`image-01` API using the `minimax-multimodal-toolkit` skill. The
Codex-pet atlas was generated separately via OpenAI's `hatch-pet`
skill, using `mascot/seelie-avatar_001.jpg` as its reference image.

## Layout

```
artworks/
├── mascot/             persona references and full-body illustrations
└── icon-source/        HD icon masters (1024×1024)
```

## `mascot/`

The Seelie persona — pale blue-to-lavender twintail hair, large warm
cyan eyes, subtly pointed elf ears, oversized white hoodie with lavender
trim, glowing cyan crystal pendant.

- **`seelie-avatar_001.jpg`** — the canonical reference avatar. This is
  the image fed to `hatch-pet` as the row-base for every Codex-pet
  animation row, so identity stays locked across the 8×9 atlas. Don't
  delete or regenerate without re-running hatch-pet — drift will show
  up across the in-app sprite pack.
- **`seelie-idle_001.jpg`** — alternate close-up portrait, idle pose.
- **`seelie-coding_001.jpg`** — portrait, focused/coding mood (dark
  backdrop with star particles).
- **`seelie-celebrate_001.jpg`** — celebrating pose, brand reference
  for the celebrate sprite row.
- **`seelie-splash_001.jpg`** + **`seelie-splash-v2_001.jpg`** — full
  body sitting on an ice/crystal cube with a floating laptop. Kept as
  promotional / splash-screen candidate art; not currently shipped.

## `icon-source/`

HD masters that the built icon bundles (`assets/seelie.icns`,
`assets/seelie.ico`, `assets/icons/seelie.png`) ultimately derive from.

- **`seelie-icon-rounded_1024.png`** — 1024×1024 rounded-corner
  squircle. iOS / macOS app-icon style.
- **`seelie-icon-square_1024.png`** — 1024×1024 square variant.

The macOS `.icns` source iconset lives at `assets/seelie.iconset/`
(not in this directory) because it's a direct build input to `iconutil`.
To regenerate `assets/seelie.icns` after editing any size inside the
iconset:

```
iconutil -c icns assets/seelie.iconset -o assets/seelie.icns
```

## Qobster (`codex-pet/qobster/` → `assets/packs/qobster/`)

Qobster — QQ Penguin in a Lobster Costume — is a second built-in sprite-sheet
pack, generated as an 8×9 atlas via OpenAI's `hatch-pet` skill on 2026-05-20.
The atlas geometry is identical to Seelie (1536×1872, 192×208 cells, 9 animation
rows). The source `codex-pet/` bundle was created outside this repo and imported
into both distribution channels:

- **Internal pack**: `assets/packs/qobster/` (manifest.json + animations.json + spritesheet.webp + preview.png)
- **Codex pet**: `codex-pet/qobster/` (pet.json + spritesheet.webp — byte-identical atlas)

## What does NOT belong here

- The built sprite atlas (`assets/packs/seelie/spritesheet.webp`) — that
  is derived from the hatch-pet run, not from anything here.
- The compiled icon bundles (`assets/seelie.icns`, `assets/seelie.ico`,
  `assets/icons/seelie.png`) — those are built artifacts.
- The macOS iconset (`assets/seelie.iconset/`) — that's a build input,
  not a source artwork.
- Per-run hatch-pet intermediates (`codex-pet-runs/seelie/`) — gitignored.
