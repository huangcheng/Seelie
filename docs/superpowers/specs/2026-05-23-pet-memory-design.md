# Pet Memory System — Design Spec

**Date:** 2026-05-23 (revised after audit)
**Status:** approved
**Scope:** MVP (greeting rotation, user name, display name, milestones)

## Goal

Make Seelie feel like she remembers the user by persisting simple contextual data: user name, greeting history, and one-time event milestones.

## Architecture

Single `MemoryManager` QObject owns a SQLite database at `~/.config/Seelie/memory.db`. Eagerly opens the DB in its constructor and exposes a `bool isValid()` gate. All other components query it via a simple key-value API. Instantiated in `main.cpp` and passed by pointer, same pattern as `ConfigManager`.

---

## 1. Data Model

**File:** `~/.config/Seelie/memory.db` (SQLite)

```sql
CREATE TABLE IF NOT EXISTS memory (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
```

No secondary index needed — `PRIMARY KEY` already creates an implicit unique index.

**Keys:**

| Key | Type | Example |
|-----|------|---------|
| `profile.name` | string | "Alex" |
| `profile.display_name` | string | "AlexC" |
| `greeting.last_text` | string | "Good morning!" |
| `milestone.gaming_mode` | integer (0/1) | 1 |
| `milestone.pack_install` | integer (0/1) | 1 |
| `milestone.first_tip` | integer (0/1) | 1 |

**Why SQLite:** Qt has built-in `QtSql` module with `QSQLITE` driver. Zero external dependencies. Single file, easy to backup. Thread-safe via SQLite's internal locking.

---

## 2. MemoryManager API

**File:** `src/MemoryManager.h` / `src/MemoryManager.cpp`

```cpp
class MemoryManager : public QObject {
    Q_OBJECT
public:
    explicit MemoryManager(const QString &dbPath, QObject *parent = nullptr);
    ~MemoryManager() override;

    /// True if the DB opened successfully. Callers should check this before
    /// using other methods — all read methods return defaultValue, all write
    /// methods are no-ops, and all signals are suppressed when !isValid().
    bool isValid() const { return m_valid; }

    // Generic KV (non-const — may retry lazy operations)
    QString value(const QString &key, const QString &defaultValue = QString());
    bool setValue(const QString &key, const QString &value);   // returns success

    // Counters: UPSERT + SELECT, returns new value (or -1 on failure)
    int increment(const QString &key, int delta = 1);

    // Greeting rotation
    QString lastGreeting();
    bool setLastGreeting(const QString &text);

    // Profile convenience
    QString userName();
    void setUserName(const QString &name);
    QString displayName();
    void setDisplayName(const QString &name);

    // Milestones
    bool hasMilestone(const QString &key);
    bool setMilestone(const QString &key);                        // returns success
    /// Only emits milestoneReached() if the milestone was NOT previously set
    /// AND the DB write succeeded. Safe to call repeatedly — it's a no-op
    /// the second time.
    void checkMilestone(const QString &key, const QString &title, const QString &body);

    /// Resolves the effective display name to use in greetings.
    /// Resolution order: displayName() → userName() → OS username → "".
    QString effectiveName() const;

signals:
    void userNameChanged(const QString &name);
    void milestoneReached(const QString &title, const QString &body);

private:
    QSqlDatabase m_db;
    QString m_connectionName;   // unique per-instance, removed in destructor
    bool m_valid = false;
};
```

**Eager initialization:** The constructor opens the database immediately and runs `CREATE TABLE IF NOT EXISTS`. If opening fails, `m_valid` stays `false` and all public methods degrade gracefully (reads return defaults, writes are no-ops, signals suppressed). No dialog, no crash.

**Parameterized queries:** All reads/writes use `QSqlQuery` with bound parameters to prevent SQL injection.

**increment() implementation:**
```sql
INSERT INTO memory(key, value) VALUES(:key, :delta)
ON CONFLICT(key) DO UPDATE SET value = CAST(value AS INTEGER) + :delta;
SELECT value FROM memory WHERE key = :key;
```
Returns the new value (from the SELECT). Returns -1 if the DB is invalid.

**checkMilestone() implementation:**
```cpp
void MemoryManager::checkMilestone(const QString &key,
                                   const QString &title,
                                   const QString &body) {
    if (hasMilestone(key)) return;           // already done
    if (!setMilestone(key)) return;          // DB write failed — don't emit
    emit milestoneReached(title, body);      // only fires once, ever
}
```

**Connection naming:** Uses `"seelie_memory"` with `QThread::currentThreadId()` suffix. `QSqlDatabase::removeDatabase()` called in destructor to avoid Qt warnings on shutdown.

---

## 3. Profile Tab in Settings Panel

**File:** `src/SettingsPanelWidget.cpp` / `src/SettingsPanelWidget.h`

**Constructor change:** Add `MemoryManager *memory` parameter:
```cpp
explicit SettingsPanelWidget(ConfigManager *config, MemoryManager *memory,
                             QWidget *parent = nullptr);
```

**New tab** added to the existing tab bar, labeled "Profile" (`tr("Profile")`). Positioned after the current last tab.

**UI Layout:**
```
Profile
├── Name:        [________________]  (placeholder: "Your name")
├── Display name:[________________]  (placeholder: "Shown in greetings")
└── [Save]
```

**Behavior:**
- Text fields pre-filled from `MemoryManager::userName()` / `displayName()` on tab show
- `Save` button calls `MemoryManager::setUserName()` / `setDisplayName()`
- Empty name field → uses OS username fallback: `qlEnvironmentVariable("USER")`, then `qlEnvironmentVariable("USERNAME")`, then `QDir::home().dirName()`
- Changes take effect immediately (no restart)
- Styling uses existing Persona 5 QSS (orange accent `#F36F1A`, Segoe UI, white background)

**Tab bar width:** The settings panel is fixed at 300×400. The existing two tabs (`General`, `TTS`) use 70px-wide buttons at 10pt font. Adding a third `Profile` / `资料` tab fits — "Profile" is 47px at 10pt Segoe UI, well within 70px. Chinese "资料" is even shorter (~22px).

**i18n:** All UI strings use `tr()`. Added to `Seelie_zh_CN.ts`.

---

## 4. Greeting Rotation + `{name}` Template

**Fully handled in `MainWindow::showRandomGreeting()`** — NOT in `TipsCatalog`.

**Why not TipsCatalog:** `TipsCatalog` is a singleton with no access to `MemoryManager`. Rather than threading a pointer through the singleton, keep the greeting logic in `MainWindow` where `MemoryManager` is already available.

**Updated `MainWindow::showRandomGreeting()` logic:**

```cpp
void MainWindow::showRandomGreeting() {
    if (!m_tipWidget) return;
    if (!m_memory) return;

    // 1. Fetch all greeting candidates (already translated)
    auto candidate = TipsCatalog::instance().randomGreeting();

    // 2. Skip if this is the same greeting as last time
    if (candidate.title == m_memory->lastGreeting())
        candidate = TipsCatalog::instance().randomGreeting();  // try again

    if (candidate.title.isEmpty()) return;

    // 3. Record this greeting so it won't repeat next time
    m_memory->setLastGreeting(candidate.title);

    // 4. {name} substitution (after translation)
    QString title = candidate.title;
    QString body  = candidate.body;
    const QString name = m_memory->effectiveName();
    if (!name.isEmpty()) {
        title.replace(QStringLiteral("{name}"), name.toHtmlEscaped());
        body.replace(QStringLiteral("{name}"),  name.toHtmlEscaped());
    } else {
        // Strip ", {name}" to avoid awkward punctuation
        title.replace(QRegularExpression(QStringLiteral(",?\\s*\\{name\\}")), QString());
        body.replace(QRegularExpression(QStringLiteral(",?\\s*\\{name\\}")),  QString());
    }

    m_tipWidget->showBubble(title, body, TipWidget::TipBubble);
}
```

**`effectiveName()` resolution order:**
1. `displayName()` from SQLite
2. `userName()` from SQLite
3. `qlEnvironmentVariable("USER")` / `qlEnvironmentVariable("USERNAME")`
4. `QDir::home().dirName()` (macOS/Linux)
5. Empty string (greetings rendered without a name)

**i18n:** Greeting strings in `TipsCatalog` use `tr()` and contain literal `{name}`. The substitution happens AFTER `tr()` resolves. Translators see and translate the surrounding text but never touch `{name}`.

---

## 5. Event Milestones

**One-time achievements** that fire a tip bubble on first occurrence:

| Milestone Key | Trigger (exact location) | Tip Title | Tip Body |
|---------------|---------|-----------|----------|
| `gaming_mode` | `MainWindow` constructor: `connect(m_config, &ConfigManager::gamingModeEnabledChanged, m_memory, [this](bool e) { if(e) m_memory->checkMilestone(...); })` | `tr("Gaming Mode activated!")` | `tr("Seelie will hide when fullscreen apps are detected.")` |
| `pack_install` | `MainWindow::setCharacterPackManager()`: after `connect(m_packManager, &CharacterPackManager::activePackChanged, ...)` in the setter | `tr("New character installed!")` | `tr("%1 is ready to go!").arg(packName)` |
| `first_tip` | `TipsEngine::processEvent()` after the `break;` at line 68 — inside the matched block, after `emit animationRequested()` | `tr("Seelie noticed something!")` | `tr("I'll try to give helpful tips while you work.")` |

**Signal wiring (in `main.cpp`):**

```cpp
// Milestone signal → bubble (intermediate lambda fixes arity mismatch:
// milestoneReached has 2 args, showBubble has 5)
QObject::connect(&memory, &MemoryManager::milestoneReached,
                 &w, [&w](const QString &title, const QString &body) {
    w.tipWidget()->showBubble(title, body, TipWidget::TipBubble);
});
```

**checkMilestone() semantics:**
- Only emits `milestoneReached` once per milestone key, ever
- DB write failure → signal suppressed (no false positive)
- Calling `checkMilestone("key", ...)` when `hasMilestone("key")` is already true → no-op (no redundant signal)

**i18n:** All milestone strings wrapped in `tr()`. Added to `.ts` file.

---

## 6. Files Changed

| File | Change |
|------|--------|
| `src/MemoryManager.h` | New — public API |
| `src/MemoryManager.cpp` | New — SQLite implementation |
| `src/SettingsPanelWidget.h` | Add `MemoryManager *memory` constructor param + Profile tab methods |
| `src/SettingsPanelWidget.cpp` | Add Profile tab UI + save logic + i18n |
| `src/mainwindow.h` | Add `MemoryManager *m_memory` member + `setMemoryManager()` |
| `src/mainwindow.cpp` | Rewrite `showRandomGreeting()` with dedup + `{name}` substitution |
| `src/main.cpp` | Instantiate MemoryManager, wire signals |
| `src/TipsEngine.cpp` | Add `checkMilestone("first_tip", ...)` after pattern match |
| `TipsCatalog.h/cpp` | Add `{name}` templates to greeting strings |
| `Seelie_zh_CN.ts` | Add Profile tab strings, greeting strings, milestone strings |
| `CMakeLists.txt` | Add `Sql` to `find_package(Qt6 ...)` COMPONENTS and `target_link_libraries` |

---

## 7. Constraints

- **Qt6::Sql required.** Add `Sql` to `find_package()` COMPONENTS (line 23 of CMakeLists.txt) and `target_link_libraries` (line 482+).
- **SQLite file is user-scoped** (`~/.config/Seelie/memory.db`), no multi-user concerns.
- **No migration framework** — schema uses `CREATE TABLE IF NOT EXISTS` only. Future schema changes check `PRAGMA user_version` in the constructor.
- **All MemoryManager operations are main-thread only** (consistent with `QSqlDatabase` thread affinity).
- **DB failure is silent** — no crash, no dialog. Read methods return defaults, writes are no-ops, signals are suppressed.

## Out of Scope

- Stats tracking (high-frequency writes for data that is "not user-facing" per spec — deferred until a weekly-summary feature justifies the write load)
- Birthday/anniversary tracking
- Timezone-aware "Good morning/afternoon/evening"
- Weekly summary reports
- Cloud sync of memory data
- Multiple user profiles per installation
