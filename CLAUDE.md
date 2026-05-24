# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Seelie — a native Qt6/C++ desktop pet that reacts to AI coding tool events (Claude Code, Codex, OpenCode). Lightweight, cross-platform (macOS/Windows/Linux), < 10MB RAM. Features a sprite pack engine for customizable characters.

## Build Commands

```bash
# macOS
mkdir -p build && cd build
cmake .. -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
cmake --build .

# Optional build flags:
#   -DSEELIE_TTS_ENABLED=OFF     drop the TTS AI tab + providers
#   -DSEELIE_INCLUDE_NSFW=ON     bundle packs whose manifest tags include "nsfw"

# Run
open build/Seelie.app             # macOS
./build/Seelie                    # Linux

# Tests (all)
cd build && ctest

# Single test
cd build && ./tests/test_ipc_animations

# Gateway CLI (uses --flag syntax, not subcommands)
seelie-gateway --source claude-code --event session.start   # send test event
seelie-gateway --ping                                        # health check
```

## Architecture

### C++ Core (src/)

The app follows a pipeline: **IPC → EventRouter → Animation/Effects/Tips → UI**

- **IPCServer / UDPWorker** — UDP server on `127.0.0.1:52847` (runs UDPWorker on a separate QThread). Accepts newline-delimited JSON messages (`event`, `tip`, `ping`/`pong`).
- **EventRouter** — Maps 17 canonical event names (e.g. `session.start`, `tool.before`, `file.edited`) to `EventAction` structs containing animation name, effect name, tip title/body. The event set is fixed — gateways normalize tool-specific events into these.
- **LottieAnimationEngine** — Primary animation engine using rlottie to play Lottie JSON character animations from sprite packs.
- **SpriteAnimationEngine** — Legacy fallback: plays frame-based animations from sprite sheets using definitions in `animations.json`.
- **LottieEffectOverlay** — Renders visual effects (sparkles, confetti, alert-pulse, etc.) from `assets/lottie/effects/` with offset positioning above the character.
- **SpritePack / SpritePackManager** — Pack data structure with manifest parsing; discovers, loads, and switches between `.spk` sprite packs.
- **TipBubbleWidget** — Win98-style speech bubble with asymmetric tail, fade animations, auto-dismiss (6s status / 12s tips).
- **TipsEngine** — Pattern matcher on a 30-second event window. Detects behaviors (repeated errors, rapid edits, idle, permission denials) and suggests contextual tips. 5-minute cooldown per tip type.
- **ConfigManager** — Persists to `~/.config/Seelie/config.json` (window position, language, auto-start, IPC endpoint).
- **SettingsPanelWidget** — UI panel for configuring settings (language, sprite pack, endpoint).
- **UpdateChecker** — Checks for new releases via network.
- **MainWindow** — Frameless, always-on-top, transparent 124×200 window. Owns the animation engine, effects overlay, tip bubble, settings panel, and system tray.

### Node.js Gateway (gateways/)

- **seelie-gateway/** (`@eastlake/seelie-gateway`) — CLI tool for sending events and health checks. Contains `lib/ipc.mjs` for the UDP transport layer.

Gateways are pure ES modules (`.mjs`) with zero npm dependencies — only Node.js built-ins. Requires Node.js >= 18.

### IPC Protocol

Transport: **UDP**, newline-delimited JSON. Fire-and-forget for events (no response expected). Messages:
```json
{"type":"event","source":"claude-code","event":"tool.before","toolName":"Read"}
{"type":"tip","title":"Hello","body":"World","animation":"wave"}
{"type":"ping"}
```

### 17 Canonical Events

`session.start`, `session.end`, `session.idle`, `session.error`, `prompt.submitted`, `tool.before`, `tool.after`, `tool.failed`, `permission.requested`, `permission.denied`, `permission.response`, `subagent.started`, `subagent.stopped`, `notification.sent`, `file.edited`, `file.watched`, `todo.updated`

## Key Conventions

- C++17 with Qt6 (Core, Gui, Widgets, Network, LinguistTools, Test). Qt signals/slots throughout.
- rlottie v0.2 fetched via CMake FetchContent (patched for Apple Silicon NEON and GCC 13+ `<limits>` include).
- Animation names: PascalCase in C++ (`SpriteAnimationEngine`), snake_case over IPC. The engine handles mapping.
- **Acronyms in identifiers stay UPPERCASE** — `TTSEngine`, `IPCServer`, `UDPWorker`, `LLMProfile`, `ECGWidget`, `HTTPProvider`. Do NOT PascalCase them (`TtsEngine`, `IpcServer`, …). Methods follow the same rule mid-name: `setLLMProfiles`, `setIPCServer`. Variables and members stay camelCase with lowercase acronyms (`m_ttsEngine`, `m_ipcServer`, `httpServer`). Filenames match the class: `IPCServer.h`, `TTSProviderRegistry.cpp`, `OpenAITTSProvider.cpp`. Header guards split the acronym with an underscore: `IPC_SERVER_H`, `ITTS_PROVIDER_H`.
- i18n: Chinese translation in `Seelie_zh_CN.ts`. All user-visible strings use `tr()`.
- Tests use Qt Test framework on a separate UDP port (52848) to avoid conflicts with running app.
- App version set in `CMakeLists.txt` (`project(Seelie VERSION x.y.z)`), passed to C++ via `PROJECT_VERSION` compile definition.
- macOS bundle hides Dock icon via `scripts/hide_dock_icon.py` post-build step.
