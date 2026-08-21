# Seelie Sprite Desktop Life Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Seelie sprite pet vivid (new idle/touch/work/motion clips) and let it walk the desktop (open walks + edge patrol + window perch + comedy fall), while compiling out character-Lottie by default; Live2D/Model3D stay untouched.

**Architecture:** Motion-first — Seelie gets a new clip-contract atlas (placeholders first), `SpriteAnimationEngine` gains chain playback + manifest `nameMap`, a new `DesktopMotionController` moves `MainWindow` using injectable geometry probes, and `SEELIE_LOTTIE_ENABLED` (default OFF) gates only the character Lottie engine/packs. AI art scripts fill the atlas later under the same clip names.

**Tech Stack:** C++17, Qt6 (Core/Gui/Widgets/Test), Win32/AppKit probes, CMake, Python 3 (placeholder + AI gen scripts), Pillow for placeholder sheets.

**Spec:** `docs/superpowers/specs/2026-08-21-seelie-sprite-desktop-life-design.md`

## Global Constraints

- Seelie pack only (`id` `im.cheng.seelie.seelie`); other hatch-pets keep the 9-clip GPT atlas.
- Live2D and Model3D: no behavior/SDK changes.
- Fall only when perched **and** the perched window’s geometry moves; timer leave uses `hop_off` (no fall).
- Shelf landing order: Windows taskbar → macOS Dock (if visible) → else bottom screen edge.
- API keys never committed; read at runtime from user paths.
- Acronyms in identifiers stay UPPERCASE (`IPC`, `FSM`-adjacent names follow existing style); new class is `DesktopMotionController`.
- Default CMake: `SEELIE_LOTTIE_ENABLED=OFF`.

## Recon corrections (verified in code)

- `MainWindow::dispatchAnimationChain` plays `chain.first()` for sprites (`mainwindow.cpp` ~290–314); Live2D alone walks the chain.
- Directory manifests parse `stateMap` / `idlePool` but **never** `nameMap` — `m_nameMap` is only filled in `CharacterPack::loadFromCodexPet()`.
- `LottieEffectOverlay` + `assets/lottie/effects/` still need rlottie. **Plan deviation from a literal “omit rlottie” reading of the spec:** keep rlottie + effect overlay; gate only `LottieAnimationEngine` and Lottie **character** packs. Document this in the commit message for Task 7.
- Existing Seelie atlas is 8×9 Codex layout (`assets/packs/seelie/`); Task 1 replaces it with a new contract sheet (placeholders), keeping a `legacy/` copy only if needed for diff reference — prefer overwrite + git history.

## File map

| File | Responsibility |
|---|---|
| `assets/packs/seelie/manifest.json` | Clip contract maps + `desktopMotion` |
| `assets/packs/seelie/animations.json` | Frame defs for all contract clips |
| `assets/packs/seelie/spritesheet.webp` | Placeholder then AI art |
| `scripts/seelie-sprite-gen/make_placeholders.py` | Generate labeled placeholder atlas |
| `scripts/seelie-sprite-gen/` (later) | Bible prompts, gen, QA |
| `src/SpriteAnimationEngine.h/.cpp` | `hasClip`, `playAnimationChain` |
| `src/CharacterPack.h/.cpp` | Parse `nameMap`, `desktopMotion` |
| `src/DesktopMotionController.h/.cpp` | Wander / perch / fall state machine |
| `src/DesktopGeometry.h/.cpp` (+ `.mm` on macOS) | Screen/window/shelf probes |
| `src/mainwindow.h/.cpp` | Dispatch chains; own controller |
| `src/ConfigManager.*` / `SettingsPanelWidget.*` | `desktopWanderingEnabled` |
| `CMakeLists.txt` / `tests/CMakeLists.txt` | Lottie gate + new sources/tests |
| `tests/test_desktop_motion.cpp` | Motion + geometry seam tests |
| `tests/test_sprite_chain.cpp` | Chain playback tests |

---

### Task 1: Seelie clip-contract placeholders

**Files:**
- Create: `scripts/seelie-sprite-gen/make_placeholders.py`
- Create: `scripts/seelie-sprite-gen/clip_contract.json`
- Modify: `assets/packs/seelie/manifest.json`
- Modify: `assets/packs/seelie/animations.json`
- Modify: `assets/packs/seelie/spritesheet.webp` (regenerated)
- Test: manual pack load (automated in Task 2/3)

**Interfaces:**
- Produces: clip names exactly as in the spec table (`idle`, `idle_fidget`, `idle_look_left`, `idle_look_right`, `idle_stretch`, `idle_snooze`, `greet`, `think`, `work`, `review`, `fail`, `celebrate`, `pet`, `grab`, `toss`, `walk_left`, `walk_right`, `sit`, `hop_off`, `fall`, `land`)
- Produces: `character.desktopMotion: true` on Seelie manifest
- Produces: `stateMap` / `nameMap` / expanded `idlePool` in manifest

- [ ] **Step 1: Write clip_contract.json**

Create `scripts/seelie-sprite-gen/clip_contract.json`:

```json
{
  "frameWidth": 192,
  "frameHeight": 208,
  "columns": 6,
  "clips": [
    {"name": "idle", "frames": 6, "row": 0},
    {"name": "idle_fidget", "frames": 4, "row": 1},
    {"name": "idle_look_left", "frames": 3, "row": 2},
    {"name": "idle_look_right", "frames": 3, "row": 3},
    {"name": "idle_stretch", "frames": 4, "row": 4},
    {"name": "idle_snooze", "frames": 4, "row": 5},
    {"name": "greet", "frames": 4, "row": 6},
    {"name": "think", "frames": 4, "row": 7},
    {"name": "work", "frames": 6, "row": 8},
    {"name": "review", "frames": 4, "row": 9},
    {"name": "fail", "frames": 4, "row": 10},
    {"name": "celebrate", "frames": 4, "row": 11},
    {"name": "pet", "frames": 4, "row": 12},
    {"name": "grab", "frames": 3, "row": 13},
    {"name": "toss", "frames": 4, "row": 14},
    {"name": "walk_left", "frames": 6, "row": 15},
    {"name": "walk_right", "frames": 6, "row": 16},
    {"name": "sit", "frames": 3, "row": 17},
    {"name": "hop_off", "frames": 3, "row": 18},
    {"name": "fall", "frames": 4, "row": 19},
    {"name": "land", "frames": 3, "row": 20}
  ]
}
```

- [ ] **Step 2: Write make_placeholders.py**

Create `scripts/seelie-sprite-gen/make_placeholders.py` that:
1. Loads `clip_contract.json`
2. Builds an RGBA sheet `columns × rows` of `frameWidth×frameHeight`
3. Fills each cell with a stable per-clip hue + white label text (`clipname #i`)
4. Writes `assets/packs/seelie/spritesheet.webp` and regenerates `animations.json` as ClippyJS array (`Name` / `Frames` / `Duration` / `ImagesOffsets`)

```python
#!/usr/bin/env python3
"""Generate labeled placeholder atlas + animations.json for Seelie clip contract."""
from __future__ import annotations
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
PACK = ROOT / "assets" / "packs" / "seelie"
CONTRACT = Path(__file__).with_name("clip_contract.json")

def main() -> None:
    contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
    fw, fh = contract["frameWidth"], contract["frameHeight"]
    cols = contract["columns"]
    clips = contract["clips"]
    rows = max(c["row"] for c in clips) + 1
    sheet = Image.new("RGBA", (cols * fw, rows * fh), (0, 0, 0, 0))
    draw = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype("arial.ttf", 14)
    except OSError:
        font = ImageFont.load_default()

    anims = []
    for i, clip in enumerate(clips):
        hue = int(40 + (i * 37) % 200)
        color = (hue, 120, 200, 255)
        frames = []
        for f in range(clip["frames"]):
            col, row = f % cols, clip["row"]
            if f >= cols:
                # spill extra frames into same row wrapping — contract keeps frames <= cols
                col = f % cols
            x, y = col * fw, row * fh
            draw.rectangle([x, y, x + fw - 1, y + fh - 1], fill=color, outline=(255, 255, 255, 255))
            draw.text((x + 8, y + 8), f"{clip['name']}", fill=(255, 255, 255, 255), font=font)
            draw.text((x + 8, y + 28), f"#{f}", fill=(255, 255, 255, 255), font=font)
            frames.append({
                "Duration": 120 if "walk" in clip["name"] or clip["name"] == "fall" else 160,
                "ImagesOffsets": {"Column": col, "Row": row},
            })
        anims.append({"Name": clip["name"], "Frames": frames})

    out_sheet = PACK / "spritesheet.webp"
    sheet.save(out_sheet, "WEBP", lossless=True)
    (PACK / "animations.json").write_text(json.dumps(anims, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {out_sheet} and animations.json ({len(anims)} clips)")

if __name__ == "__main__":
    main()
```

- [ ] **Step 3: Run placeholder generator**

```bash
pip install pillow
python scripts/seelie-sprite-gen/make_placeholders.py
```

Expected: `wrote ...spritesheet.webp and animations.json (21 clips)`

- [ ] **Step 4: Update Seelie manifest**

Replace `assets/packs/seelie/manifest.json` character/idle/event sections with (keep metadata/tags; bump description):

```json
{
  "formatVersion": "1.0.0",
  "id": "im.cheng.seelie.seelie",
  "name": "Seelie",
  "nameLocalized": { "zh_CN": "仙灵" },
  "author": "HUANG Cheng",
  "version": "2.0.0",
  "description": "Seelie mascot — desktop-life clip contract (placeholders until AI art lands).",
  "preview": "preview.png",
  "tags": ["seelie", "original", "anime", "fae", "default"],
  "license": "MIT",
  "category": "originals",
  "minAppVersion": "1.0.0",
  "character": {
    "type": "spriteSheet",
    "spriteSheet": "spritesheet.webp",
    "frameWidth": 192,
    "frameHeight": 208,
    "definitions": "animations.json",
    "displayScale": 1.0,
    "desktopMotion": true
  },
  "idlePool": [
    { "name": "idle", "weight": 6 },
    { "name": "idle_fidget", "weight": 2 },
    { "name": "idle_look_left", "weight": 1 },
    { "name": "idle_look_right", "weight": 1 },
    { "name": "idle_stretch", "weight": 1 },
    { "name": "idle_snooze", "weight": 1 }
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
    "Tossed": ["toss", "running"]
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
    "running-right": "walk_right"
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
    "todo.updated": "celebrate"
  }
}
```

- [ ] **Step 5: Commit**

```bash
git add scripts/seelie-sprite-gen assets/packs/seelie
git commit -m "feat(seelie): clip-contract placeholder atlas + manifest maps"
```

---

### Task 2: Parse `nameMap` + `desktopMotion` from directory manifests

**Files:**
- Modify: `src/CharacterPack.h`
- Modify: `src/CharacterPack.cpp` (manifest parse path ~525–569)
- Test: `tests/test_character_pack.cpp` (extend if present) or create `tests/test_pack_namemap.cpp`

**Interfaces:**
- Produces: `bool CharacterPack::desktopMotion() const`
- Produces: `parseNameMap(const QJsonObject &)` merging into `m_nameMap` (manifest overrides)
- Consumes: top-level or `character`-nested `nameMap` object (same pick pattern as `stateMap`)

- [ ] **Step 1: Write the failing test**

Create `tests/test_pack_namemap.cpp`:

```cpp
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QFile>
#include "CharacterPack.h"

class TestPackNameMap : public QObject
{
    Q_OBJECT
private slots:
    void loadsNameMapAndDesktopMotionFromManifest();
};

void TestPackNameMap::loadsNameMapAndDesktopMotionFromManifest()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Minimal valid sprite pack files
    QFile anim(dir.path() + "/animations.json");
    QVERIFY(anim.open(QIODevice::WriteOnly));
    anim.write(R"([{"Name":"idle","Frames":[{"Duration":100,"ImagesOffsets":{"Column":0,"Row":0}}]}])");
    anim.close();
    QImage img(192, 208, QImage::Format_ARGB32);
    img.fill(Qt::magenta);
    QVERIFY(img.save(dir.path() + "/sheet.webp", "WEBP"));

    const QByteArray manifest = R"({
      "formatVersion":"1.0.0","id":"test.pack","name":"T","author":"a","version":"1",
      "character":{"type":"spriteSheet","spriteSheet":"sheet.webp",
        "frameWidth":192,"frameHeight":208,"definitions":"animations.json",
        "desktopMotion":true},
      "idlePool":[{"name":"idle","weight":1}],
      "nameMap":{"pat":"pet","wave":"greet"},
      "stateMap":{"Petted":["pet"]}
    })";
    QFile mf(dir.path() + "/manifest.json");
    QVERIFY(mf.open(QIODevice::WriteOnly));
    mf.write(manifest);
    mf.close();

    CharacterPack pack;
    QVERIFY(pack.loadFromDirectory(dir.path()));
    QCOMPARE(pack.nameMap().value("pat"), QString("pet"));
    QCOMPARE(pack.nameMap().value("wave"), QString("greet"));
    QVERIFY(pack.desktopMotion());
    QCOMPARE(pack.stateMap().value("Petted"), QStringList({"pet"}));
}

QTEST_MAIN(TestPackNameMap)
#include "test_pack_namemap.moc"
```

Register in `tests/CMakeLists.txt` `TEST_SOURCES` list (same pattern as other tests).

- [ ] **Step 2: Build test — expect FAIL**

```bash
cd build && cmake --build . --target test_pack_namemap
```

Expected: compile error or `desktopMotion` missing / `nameMap` empty.

- [ ] **Step 3: Implement**

In `CharacterPack.h` add:

```cpp
bool desktopMotion() const { return m_desktopMotion; }
```

and `bool m_desktopMotion = false;`, plus `bool parseNameMap(const QJsonObject &obj);`

In `parseCharacter`, after reading type/sheet fields:

```cpp
m_desktopMotion = character.value(QStringLiteral("desktopMotion")).toBool(false);
```

In `loadFromManifest` after `parseStateMap`:

```cpp
if (!parseNameMap(pickObject("nameMap"))) {
    qWarning() << "CharacterPack: Failed to parse nameMap";
    return false;
}
```

Implement `parseNameMap` — for each string value in the object, `m_nameMap[key] = value` (manifest wins over any prior Codex defaults; directory packs start with empty `m_nameMap`).

- [ ] **Step 4: Run test — expect PASS**

```bash
cd build && ctest -R test_pack_namemap --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add src/CharacterPack.h src/CharacterPack.cpp tests/test_pack_namemap.cpp tests/CMakeLists.txt
git commit -m "feat(packs): parse nameMap + desktopMotion from directory manifests"
```

---

### Task 3: Sprite chain playback

**Files:**
- Modify: `src/AnimationEngine.h` (optional default) **or** only sprite + MainWindow
- Modify: `src/SpriteAnimationEngine.h/.cpp`
- Modify: `src/mainwindow.cpp` (`dispatchAnimationChain`)
- Test: `tests/test_sprite_chain.cpp`

**Interfaces:**
- Produces: `bool SpriteAnimationEngine::hasClip(const QString &name) const`
- Produces: `void SpriteAnimationEngine::playAnimationChain(const QStringList &chain, Priority priority = NormalPriority)`
- Consumes: existing `playAnimation` name resolution (`m_nameMap` then pack map — keep current order)

- [ ] **Step 1: Failing test**

```cpp
#include <QtTest>
#include <QTemporaryDir>
#include <QImage>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "SpriteAnimationEngine.h"
#include "CharacterPack.h"

class TestSpriteChain : public QObject
{
    Q_OBJECT
private slots:
    void playsFirstExistingClipInChain();
    void noOpsWhenNoneExist();
};

static bool writeSpritePack(const QString &dirPath, const QStringList &clipNames,
                            const QJsonObject &nameMap = {})
{
    QJsonArray anims;
    for (int i = 0; i < clipNames.size(); ++i) {
        anims.append(QJsonObject{
            {"Name", clipNames[i]},
            {"Frames", QJsonArray{QJsonObject{
                {"Duration", 100},
                {"ImagesOffsets", QJsonObject{{"Column", i}, {"Row", 0}}}
            }}}
        });
    }
    QFile anim(dirPath + "/animations.json");
    if (!anim.open(QIODevice::WriteOnly)) return false;
    anim.write(QJsonDocument(anims).toJson());
    anim.close();

    QImage img(192 * qMax(1, clipNames.size()), 208, QImage::Format_ARGB32);
    img.fill(Qt::cyan);
    if (!img.save(dirPath + "/sheet.webp", "WEBP")) return false;

    QJsonObject character{
        {"type", "spriteSheet"},
        {"spriteSheet", "sheet.webp"},
        {"frameWidth", 192},
        {"frameHeight", 208},
        {"definitions", "animations.json"}
    };
    QJsonObject manifest{
        {"formatVersion", "1.0.0"},
        {"id", "test.sprite.chain"},
        {"name", "T"},
        {"author", "a"},
        {"version", "1"},
        {"character", character},
        {"idlePool", QJsonArray{QJsonObject{{"name", clipNames.first()}, {"weight", 1}}}},
        {"nameMap", nameMap}
    };
    QFile mf(dirPath + "/manifest.json");
    if (!mf.open(QIODevice::WriteOnly)) return false;
    mf.write(QJsonDocument(manifest).toJson());
    return true;
}

void TestSpriteChain::playsFirstExistingClipInChain()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeSpritePack(dir.path(), {"idle", "pet"},
                             QJsonObject{{"pat", "pet"}}));
    CharacterPack pack;
    QVERIFY(pack.loadFromDirectory(dir.path()));
    SpriteAnimationEngine eng;
    QVERIFY(eng.loadFromCharacterPack(&pack));
    eng.playAnimationChain({"missing", "pat", "pet", "idle"},
                           SpriteAnimationEngine::HighPriority);
    QCOMPARE(eng.currentAnimation(), QString("pet"));
}

void TestSpriteChain::noOpsWhenNoneExist()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeSpritePack(dir.path(), {"idle"}));
    CharacterPack pack;
    QVERIFY(pack.loadFromDirectory(dir.path()));
    SpriteAnimationEngine eng;
    QVERIFY(eng.loadFromCharacterPack(&pack));
    eng.playAnimation("idle");
    const QString before = eng.currentAnimation();
    eng.playAnimationChain({"nope", "also_nope"},
                           SpriteAnimationEngine::HighPriority);
    QCOMPARE(eng.currentAnimation(), before);
}

QTEST_MAIN(TestSpriteChain)
#include "test_sprite_chain.moc"
```

- [ ] **Step 2: Implement `hasClip` + `playAnimationChain`**

```cpp
bool SpriteAnimationEngine::hasClip(const QString &name) const
{
    if (name.isEmpty()) return false;
    QString actual = name;
    if (m_packNameMap.contains(name)) actual = m_packNameMap.value(name);
    else if (m_nameMap.contains(name)) actual = m_nameMap.value(name);
    return m_animations.contains(actual);
}

void SpriteAnimationEngine::playAnimationChain(const QStringList &chain, Priority priority)
{
    for (const QString &name : chain) {
        if (!hasClip(name)) continue;
        playAnimation(name, priority);
        return;
    }
}
```

Note: today pack `nameMap` may only live on `CharacterPack` and be applied via FSM `rebuildChainsFromPack`, while the engine keeps its own Clippy `m_nameMap`. For Seelie, prefer resolving through **both** engine map and a copy of pack `nameMap` stored at `loadFromCharacterPack` (set `m_packNameMap = pack->nameMap()`).

- [ ] **Step 3: Update MainWindow**

In `dispatchAnimationChain`, replace sprite branch:

```cpp
if (m_engine && m_engine->hasAnimations()) {
    m_engine->playAnimationChain(chain, priority);
    return;
}
```

(Keep Model3D as `chain.first()` unless it already has chain support — out of scope.)

- [ ] **Step 4: Run tests PASS + commit**

```bash
ctest -R test_sprite_chain --output-on-failure
git add src/SpriteAnimationEngine.* src/mainwindow.cpp tests/test_sprite_chain.cpp tests/CMakeLists.txt
git commit -m "feat(sprite): playAnimationChain walks fallbacks like Live2D"
```

---

### Task 4: `DesktopMotionController` pure state machine

**Files:**
- Create: `src/DesktopMotionController.h`
- Create: `src/DesktopMotionController.cpp`
- Test: `tests/test_desktop_motion.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
class DesktopMotionController : public QObject {
public:
    enum class Mode { Idle, Wandering, Perched, Falling, HoppingOff };
    struct WindowGeom { QRect frame; qint64 id = 0; }; // id stable while same window
    struct ShelfTarget { QPoint landTopCenter; }; // pet window top-left derived by caller

    using NowFn = std::function<qint64()>;
    using RngFn = std::function<double()>; // [0,1)
    using ActiveWindowFn = std::function<WindowGeom()>; // null rect = none
    using ShelfFn = std::function<ShelfTarget(const QRect &screen)>;
    using ScreenFn = std::function<QRect()>;

    void setEnabled(bool);
    void setPetRect(const QRect &r); // current MainWindow geometry
    void onFsmState(const QString &stateName); // "Idle", "Working", ...
    void tick(); // drive without real QTimer in tests
    Mode mode() const;
    QRect targetPetRect() const; // where MainWindow should move
    QString requestedClip() const; // walk_left / sit / fall / ...

    void notifyPerchedWindowMoved(); // production calls when probe detects move
    // signals: void moveTo(QRect); void playClip(QString);
};
```

- Behavior constants (v1): wander cooldown 8–20s; wander step 40–120px; perch probability ~8% of wander starts; perch dwell 8–20s; fall speed ~800px/s toward shelf.

- [ ] **Step 1: Failing tests** (in `test_desktop_motion.cpp`)

Cover at least:
1. `tick` while disabled → stays Idle, no clip.
2. After cooldown + Idle FSM → enters Wandering, `requestedClip` is `walk_left` or `walk_right` matching dx.
3. Forced perch path → `sit`, mode Perched; timer expiry → `hop_off` then Idle (no fall).
4. While Perched, `notifyPerchedWindowMoved()` → Falling → eventually land clip + Idle; `targetPetRect` y approaches shelf.
5. FSM `Working` cancels Wander/Perch (not mid-fall).

Use injectable `NowFn`/`RngFn`/`ActiveWindowFn`/`ShelfFn`/`ScreenFn`.

- [ ] **Step 2: Implement minimal controller**

Keep all OS calls out of this file — only geometry math + mode transitions. Use a single `QTimer` (1s or 50ms while moving) started/stopped by `setEnabled` / MainWindow; tests call `tick()` directly.

- [ ] **Step 3: Tests PASS + commit**

```bash
git commit -m "feat(motion): DesktopMotionController wander/perch/fall state machine"
```

---

### Task 5: Platform geometry probes

**Files:**
- Create: `src/DesktopGeometry.h`
- Create: `src/DesktopGeometry.cpp` (Win + Linux)
- Create: `src/DesktopGeometry_mac.mm` (macOS Dock + front window)
- Modify: CMake for `.mm` on Darwin

**Interfaces:**
- Produces:

```cpp
namespace DesktopGeometry {
QRect currentScreenAvailable(const QPoint &anchor);
DesktopMotionController::WindowGeom activeWindow();
DesktopMotionController::ShelfTarget shelfForScreen(const QRect &screen);
}
```

- Windows: `GetForegroundWindow` + `GetWindowRect`; taskbar via `SHAppBarMessage(ABM_GETTASKBARPOS)` or work-area diff (`SystemParametersInfo SPI_GETWORKAREA` vs full screen — land on the strip between work area and screen).
- macOS: frontmost window bounds (CG/AX as used elsewhere if any; else Cocoa); Dock via `NSScreen.visibleFrame` vs `frame`.
- Linux: bottom of `QScreen::availableGeometry()` / full geometry gap; active window best-effort (`QGuiApplication::focusWindow` is wrong for other apps — use X11 `_NET_ACTIVE_WINDOW` when available, else skip perch).

- [ ] **Step 1: Unit-test shelf math with fake screen rects** (pure helpers if extracted)

- [ ] **Step 2: Implement platform backends**

- [ ] **Step 3: Manual smoke note in commit body** (Win taskbar + mac Dock)

```bash
git commit -m "feat(motion): DesktopGeometry probes for active window + shelf"
```

---

### Task 6: Wire MainWindow + settings

**Files:**
- Modify: `src/ConfigManager.h/.cpp` — `desktopWanderingEnabled` (default true), signal, persist key `desktopWandering`
- Modify: `src/SettingsPanelWidget.h/.cpp` + `Seelie_zh_CN.ts` — Interaction checkbox “Desktop wandering”
- Modify: `src/mainwindow.h/.cpp` — own `DesktopMotionController`, connect FSM + move window + `dispatchAnimation`
- Modify: `src/main.cpp` — construct/wire like other engines

**Wiring rules:**
- Enable controller only when `config.desktopWanderingEnabled() && activePack->desktopMotion() && activeEngineType == SpriteSheet`.
- On `moveTo(rect)`, `setGeometry` / `move` MainWindow; persist position via existing `positionChanged` only after user drag — **do not** spam-save every wander step (throttle or skip config writes during autonomous moves; add `m_autonomousMove` flag).
- While Perched, poll active window geometry each tick; if `frame` differs → `notifyPerchedWindowMoved()`.
- On grab/pet from MainWindow touch path, FSM already changes — controller reacts via `onFsmState`.

- [ ] **Step 1: Config + settings UI** (mirror `touchReactionsEnabled` pattern exactly)

- [ ] **Step 2: MainWindow integration**

- [ ] **Step 3: Manual run with Seelie pack** — confirm wander + perch + drag-fall

- [ ] **Step 4: Commit**

```bash
git commit -m "feat(motion): wire DesktopMotionController + wandering setting"
```

---

### Task 7: `SEELIE_LOTTIE_ENABLED` default OFF (character Lottie only)

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/mainwindow.h/.cpp`, `src/main.cpp`, pack manager discovery as needed
- Modify: `CLAUDE.md` build flags blurb

**Rules:**
- `option(SEELIE_LOTTIE_ENABLED "Enable Lottie character engine + Lottie packs" OFF)`
- When OFF: omit `LottieAnimationEngine.*` from app sources; wrap includes/members in `#if SEELIE_LOTTIE_ENABLED`; skip Lottie packs in discovery (`engineType == Lottie`).
- **Keep** `LottieEffectOverlay` + rlottie FetchContent (effects still work).
- When ON: today’s behavior.

- [ ] **Step 1: Add option + conditional sources/defs** (mirror `SEELIE_TTS_ENABLED`)

- [ ] **Step 2: `#if SEELIE_LOTTIE_ENABLED` in MainWindow/main**

- [ ] **Step 3: Default configure builds and runs without Lottie character packs**

```bash
cmake .. -DSEELIE_LOTTIE_ENABLED=OFF
cmake --build .
ctest --output-on-failure
```

- [ ] **Step 4: Commit**

```bash
git commit -m "build: SEELIE_LOTTIE_ENABLED off by default (keep effect overlay)"
```

---

### Task 8: AI art pipeline scaffolding

**Files:**
- Create: `scripts/seelie-sprite-gen/README.md`
- Create: `scripts/seelie-sprite-gen/prompts/bible.md` (frozen character description; no secrets)
- Create: `scripts/seelie-sprite-gen/gen_clip.py` (loads API key from env `SEELIE_IMAGE_API_KEY` or path in `SEELIE_IMAGE_KEY_FILE`)
- Create: `scripts/seelie-sprite-gen/qa_strip.py` (palette/bbox heuristics)
- Create: `scripts/seelie-sprite-gen/apply_approved.py` (slice approved strip into sheet + patch animations.json)

**Rules:**
- Do not commit keys. Document: set `SEELIE_IMAGE_KEY_FILE` to the Obsidian/`~/.pi` file the user points at.
- Support provider switch env `SEELIE_IMAGE_PROVIDER=minimax|stepfun|voyah`.
- Every gen call **requires** `--ref scripts/seelie-sprite-gen/refs/bible.png` (user/AI produces bible once from existing Seelie art).
- Human approve directory: `scripts/seelie-sprite-gen/out/<clip>/approved/`

- [ ] **Step 1: README + bible prompt + stub scripts that fail clearly without key**

- [ ] **Step 2: Smoke: generate one `idle_fidget` strip when user provides key path** (manual)

- [ ] **Step 3: Commit scaffolding only**

```bash
git commit -m "chore(seelie): AI sprite gen scaffolding + consistency README"
```

---

### Task 9: Docs + TODO handoff

**Files:**
- Modify: `TODO.md` — mark desktop-life in progress / shipped sections
- Modify: `docs/model3d-packs.md` only if needed (no)
- Modify: `docs/superpowers/specs/2026-08-21-seelie-sprite-desktop-life-design.md` status → implemented when done
- Modify: `schemas/character-pack-v1.schema.json` — add `nameMap`, `stateMap`, `desktopMotion`, model3d already separate

- [ ] **Step 1: Update schema + TODO**

- [ ] **Step 2: Commit**

```bash
git commit -m "docs: mark sprite desktop-life shipped; extend pack schema"
```

---

## Spec coverage checklist

| Spec item | Task |
|---|---|
| Seelie clip contract + placeholders | 1 |
| `nameMap` / `stateMap` / desktopMotion | 1–2 |
| Sprite chain playback | 3 |
| Wander A+C | 4–6 |
| Window perch + timer hop_off | 4–6 |
| Fall only on perched window move | 4–6 |
| Shelf priority Win/Dock/bottom | 5 |
| Settings toggle | 6 |
| Lottie character OFF by default | 7 |
| Keep Live2D/Model3D | (no task — constraint) |
| AI consistency pipeline | 8 |
| Keep effect Lottie/rlottie | 7 recon |

## Execution handoff

Plan complete and saved to `docs/superpowers/plans/2026-08-21-seelie-sprite-desktop-life.md`. Two execution options:

**1. Subagent-Driven (recommended)** — fresh subagent per task, review between tasks  
**2. Inline Execution** — execute tasks in this session with checkpoints  

Which approach?
