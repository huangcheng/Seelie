# Pet Memory System — Design Spec

**Date:** 2026-05-23
**Status:** approved
**Scope:** MVP (greeting rotation, user name, display name, milestones)

## Goal

Make Seelie feel like she remembers the user by persisting simple contextual data: user name, greeting history, event stats, and one-time milestones.

## Architecture

Single `MemoryManager` QObject owns a SQLite database at `~/.config/Seelie/memory.db`. All other components (TipsCatalog, SettingsPanelWidget, EventRouter) query it via a simple key-value API. No singleton — instantiated in `main.cpp` and passed by pointer, same pattern as `ConfigManager`.

---

## 1. Data Model

**File:** `~/.config/Seelie/memory.db` (SQLite, created lazily on first use)

```sql
CREATE TABLE IF NOT EXISTS memory (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL,
    updated_at INTEGER DEFAULT (strftime('%s', 'now'))
);
CREATE INDEX IF NOT EXISTS idx_memory_key ON memory(key);
```

**Keys:**

| Key | Type | Example |
|-----|------|---------|
| `profile.name` | string | "Alex" |
| `profile.display_name` | string | "AlexC" |
| `greeting.last_text` | string | "Good morning!" |
| `greeting.last_shown_at` | integer (epoch) | 1716460800 |
| `stats.session_count` | integer | 42 |
| `stats.tool_count` | integer | 156 |
| `stats.file_edit_count` | integer | 89 |
| `milestone.gaming_mode` | integer (0/1) | 1 |
| `milestone.pack_install` | integer (0/1) | 1 |
| `milestone.first_tip` | integer (0/1) | 1 |

**Why SQLite:** Qt has built-in `QtSql` module with `QSQLITE` driver. Zero external dependencies. Single file, easy to backup/migrate. Thread-safe via SQLite's internal locking.

---

## 2. MemoryManager API

**File:** `src/MemoryManager.h` / `src/MemoryManager.cpp`

```cpp
class MemoryManager : public QObject {
    Q_OBJECT
public:
    explicit MemoryManager(QObject *parent = nullptr);
    
    // Generic KV
    QString value(const QString &key, const QString &defaultValue = QString()) const;
    void setValue(const QString &key, const QString &value);
    
    // Counters (atomic increment via INSERT ... ON CONFLICT ... DO UPDATE)
    int increment(const QString &key, int delta = 1);
    
    // Greeting rotation
    QString lastGreeting() const;
    void setLastGreeting(const QString &text);
    
    // Profile convenience
    QString userName() const;
    void setUserName(const QString &name);
    QString displayName() const;
    void setDisplayName(const QString &name);
    
    // Milestones
    bool hasMilestone(const QString &key) const;
    void setMilestone(const QString &key);
    void checkMilestone(const QString &key, const QString &title, const QString &body);
    
signals:
    void userNameChanged(const QString &name);
    void milestoneReached(const QString &title, const QString &body);
    
private:
    bool ensureInitialized();
    QSqlDatabase m_db;
    bool m_initialized = false;
};
```

**Lazy initialization:** `ensureInitialized()` opens the database and runs `CREATE TABLE IF NOT EXISTS` on the first call to any public method. No migration at startup, no error dialog if the file is missing.

**Parameterized queries:** All reads/writes use `QSqlQuery` with bound parameters to prevent SQL injection.

**increment() atomicity:** Uses SQLite's `INSERT INTO memory(key, value) VALUES(?, ?) ON CONFLICT(key) DO UPDATE SET value = CAST(value AS INTEGER) + ?` for race-safe increments.

---

## 3. Profile Tab in Settings Panel

**File:** `src/SettingsPanelWidget.cpp` / `src/SettingsPanelWidget.h`

**New tab** added to the existing tab bar, positioned after the current last tab.

**UI Layout:**
```
Profile
├── Name:        [________________]  (placeholder: "Your name")
├── Display name:[________________]  (placeholder: "Shown in greetings")
└── [Save]
```

**Behavior:**
- Text fields pre-filled from `MemoryManager::userName()` / `displayName()` on tab show
- `Save` button writes to SQLite, emits `MemoryManager::userNameChanged()`
- Empty name → falls back to OS username (`qgetenv("USER")` or `qgetenv("USERNAME")`)
- Changes take effect immediately (no restart)
- Styling uses existing Persona 5 QSS (orange accent `#F36F1A`, Segoe UI, white background)

**i18n:** All labels use `tr()`. Strings added to `Seelie_zh_CN.ts`.

---

## 4. Greeting Rotation + `{name}` Template

**File:** `src/TipsCatalog.cpp`

**Current behavior:** `randomGreeting()` picks randomly from a static pool. Can repeat.

**New behavior:**
1. Load all greeting candidates (already translated via `tr()`)
2. Query `MemoryManager::lastGreeting()`
3. Filter out the last-used text from candidates
4. If only one candidate remains, return it anyway
5. After showing, call `MemoryManager::setLastGreeting(text)`

**`{name}` substitution:**
- After loading the translated greeting string, replace literal `{name}` with the resolved display name
- Resolution order: `displayName()` → `userName()` → OS username → `""`
- If resolved name is empty, strip the `, {name}` portion entirely to avoid awkward punctuation
- Example: `"Good morning, {name}!"` → `"Good morning, Alex!"` or `"Good morning!"`

**Edge cases:**
- Name with spaces → works naturally ("Alex Chen")
- Name with HTML special chars → escaped before rendering in `TipWidget`
- Empty translated string → skip substitution, return as-is

---

## 5. Event Milestones

**One-time achievements** that fire a tip bubble on first occurrence:

| Milestone Key | Trigger | Tip Title | Tip Body |
|---------------|---------|-----------|----------|
| `gaming_mode` | `gamingModeEnabledChanged(true)` | `tr("Gaming Mode activated!")` | `tr("Seelie will hide when fullscreen apps are detected.")` |
| `pack_install` | `CharacterPackManager::installPack()` returns true | `tr("New character installed!")` | `tr("%1 is ready to go!").arg(packName)` |
| `first_tip` | First `TipsEngine` pattern match | `tr("Seelie noticed something!")` | `tr("I'll try to give helpful tips while you work.")` |
| `session_10` | `stats.session_count == 10` | `tr("10 sessions together!")` | `tr("Thanks for keeping me company.")` |
| `tool_100` | `stats.tool_count == 100` | `tr("100 tools used!")` | `tr("You're getting things done!")` |

**Implementation:**
```cpp
void MemoryManager::checkMilestone(const QString &key,
                                   const QString &title,
                                   const QString &body)
{
    if (!hasMilestone(key)) {
        setMilestone(key);
        emit milestoneReached(title, body);
    }
}
```

**Wiring:** `MemoryManager::milestoneReached` → `TipWidget::showBubble()` via MainWindow signal routing (same pattern as `EventRouter::eventProcessed` → `PetStateMachine`).

**i18n:** All milestone strings wrapped in `tr()`. Added to `.ts` file.

---

## 6. Stats Tracking

**Counters incremented on canonical events** (in `TipsEngine::processEvent` or `EventRouter::routeEvent`):

```cpp
m_memory->increment("stats.session_count");   // on session.start
m_memory->increment("stats.tool_count");       // on tool.before
m_memory->increment("stats.file_edit_count");  // on file.edited
```

**Usage in MVP:** Stored but not user-facing. Enables future features (weekly summaries, "You coded for 3 hours today!").

---

## 7. Files Changed

| File | Change |
|------|--------|
| `src/MemoryManager.h` | New — public API |
| `src/MemoryManager.cpp` | New — SQLite implementation |
| `src/SettingsPanelWidget.h` | Add Profile tab method declarations |
| `src/SettingsPanelWidget.cpp` | Add Profile tab UI + save logic |
| `src/TipsCatalog.cpp` | `{name}` substitution + greeting dedup |
| `src/main.cpp` | Instantiate MemoryManager, wire signals |
| `Seelie_zh_CN.ts` | Add new greeting strings + milestone strings |
| `CMakeLists.txt` | Link `Qt6::Sql` (if not already) |

---

## 8. Constraints

- Qt6 SQL module (`Qt6::Sql`) must be available. Added to `CMakeLists.txt` `find_package()` and `target_link_libraries()`.
- SQLite file is user-scoped (`~/.config/Seelie/memory.db`), no multi-user concerns.
- No migration framework for MVP — schema uses `CREATE TABLE IF NOT EXISTS` only. Future schema changes handled by versioned `ensureInitialized()`.
- All memory operations are main-thread only (same thread as `QSqlDatabase`).

## Out of Scope

- Birthday/anniversary tracking
- Timezone-aware "Good morning/afternoon/evening"
- Weekly summary reports
- Cloud sync of memory data
- Multiple user profiles per installation
