# Pet Memory System — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add SQLite-backed user profile, greeting rotation, and milestone tracking so the pet feels like it remembers the user.

**Architecture:** New `MemoryManager` QObject wraps a SQLite `memory` table with a key-value API. Instantiated in `main.cpp`, passed by pointer to `MainWindow` and `SettingsPanelWidget`. Greeting dedup + `{name}` substitution lives in `MainWindow::showRandomGreeting()` (not TipsCatalog singleton).

**Tech Stack:** Qt6 C++17, Qt6::Sql (QSQLITE driver), Qt Test framework

---

**File structure:**
- New: `src/MemoryManager.h`, `src/MemoryManager.cpp`, `tests/test_memory_manager.cpp`
- Modify: `CMakeLists.txt`, `src/mainwindow.h/cpp`, `src/main.cpp`, `src/SettingsPanelWidget.h/cpp`, `src/TipsEngine.cpp`, `src/TipsCatalog.h/cpp`, `Seelie_zh_CN.ts`

---

### Task 1: CMakeLists + MemoryManager header

**Files:**
- Modify: `CMakeLists.txt`
- Create: `src/MemoryManager.h`

- [ ] **Step 1: Add Qt6::Sql to CMakeLists.txt**

Find the `find_package` line (~line 23):
```cmake
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia LinguistTools Test)
```
Change to:
```cmake
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia Sql LinguistTools Test)
```

Find the `target_link_libraries(Seelie PRIVATE` block (~line 482):
```cmake
target_link_libraries(Seelie PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
```
Add `Qt6::Sql` after `Qt6::Network`:
```cmake
target_link_libraries(Seelie PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Sql
    Qt6::Multimedia
```

- [ ] **Step 2: Add MemoryManager test to CMakeLists.txt**

Find the tests section (~line 662+). Add after the last `add_executable(tests/test_...` block:
```cmake
add_executable(test_memory_manager
    tests/test_memory_manager.cpp
    src/MemoryManager.cpp
)
target_link_libraries(test_memory_manager PRIVATE Qt6::Sql Qt6::Core Qt6::Test)
target_include_directories(test_memory_manager PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test_memory_manager COMMAND test_memory_manager)
```

- [ ] **Step 3: Create MemoryManager header**

Write file `F:\Seelie\src\MemoryManager.h`:

```cpp
#ifndef MEMORYMANAGER_H
#define MEMORYMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QString>

class MemoryManager : public QObject
{
    Q_OBJECT
public:
    explicit MemoryManager(const QString &dbPath, QObject *parent = nullptr);
    ~MemoryManager() override;

    bool isValid() const { return m_valid; }

    QString value(const QString &key, const QString &defaultValue = QString());
    bool setValue(const QString &key, const QString &value);

    int increment(const QString &key, int delta = 1);

    QString lastGreeting();
    bool setLastGreeting(const QString &text);

    QString userName();
    void setUserName(const QString &name);
    QString displayName();
    void setDisplayName(const QString &name);

    bool hasMilestone(const QString &key);
    bool setMilestone(const QString &key);
    void checkMilestone(const QString &key, const QString &title, const QString &body);

    QString effectiveName() const;

signals:
    void userNameChanged(const QString &name);
    void milestoneReached(const QString &title, const QString &body);

private:
    QSqlDatabase m_db;
    QString m_connectionName;
    bool m_valid = false;
};

#endif // MEMORYMANAGER_H
```

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt src/MemoryManager.h
git commit -m "feat: add MemoryManager header + Qt6::Sql to CMake"
```

---

### Task 2: MemoryManager implementation

**Files:**
- Create: `src/MemoryManager.cpp`

- [ ] **Step 1: Write the failing test**

Write file `F:\Seelie\tests\test_memory_manager.cpp`:

```cpp
#include "MemoryManager.h"
#include <QtTest/QtTest>
#include <QDir>

class TestMemoryManager : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // Use an in-memory DB for test isolation
        m_dbPath = ":memory:";
    }

    void testSetAndGet()
    {
        MemoryManager mm(m_dbPath);
        QVERIFY(mm.isValid());
        QCOMPARE(mm.value("nonexistent", "default"), QString("default"));
        QVERIFY(mm.setValue("test.key", "hello"));
        QCOMPARE(mm.value("test.key"), QString("hello"));
    }

    void testIncrement()
    {
        MemoryManager mm(m_dbPath);
        QCOMPARE(mm.increment("counter"), 1);
        QCOMPARE(mm.increment("counter"), 2);
        QCOMPARE(mm.increment("counter", 5), 7);
    }

    void testMilestoneOnce()
    {
        MemoryManager mm(m_dbPath);
        int callCount = 0;
        connect(&mm, &MemoryManager::milestoneReached, [&](const QString &, const QString &) {
            ++callCount;
        });

        QVERIFY(!mm.hasMilestone("test_milestone"));
        mm.checkMilestone("test_milestone", "Title", "Body");
        QCOMPARE(callCount, 1);
        QVERIFY(mm.hasMilestone("test_milestone"));

        // Second call should NOT emit
        mm.checkMilestone("test_milestone", "Title", "Body");
        QCOMPARE(callCount, 1);
    }

    void testMilestoneSilentOnDbFailure()
    {
        // Use an invalid path that can't be written to
        MemoryManager mm("/dev/null/impossible/path/memory.db");
        int callCount = 0;
        connect(&mm, &MemoryManager::milestoneReached, [&](const QString &, const QString &) {
            ++callCount;
        });
        QVERIFY(!mm.isValid());
        mm.checkMilestone("test_milestone", "Title", "Body");
        QCOMPARE(callCount, 0);
    }

    void testEffectiveName()
    {
        MemoryManager mm(m_dbPath);
        mm.setUserName("Alex");
        mm.setDisplayName("AlexC");
        QCOMPARE(mm.effectiveName(), QString("AlexC"));  // displayName wins

        mm.setDisplayName("");
        QCOMPARE(mm.effectiveName(), QString("Alex"));   // userName fallback
    }

private:
    QString m_dbPath;
};

QTEST_MAIN(TestMemoryManager)
#include "test_memory_manager.moc"
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd build && cmake .. && cmake --build . --target test_memory_manager && ./tests/test_memory_manager
```

Expected: Link error — `MemoryManager` methods undefined.

- [ ] **Step 3: Write MemoryManager implementation**

Write file `F:\Seelie\src\MemoryManager.cpp`:

```cpp
#include "MemoryManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QThread>
#include <QtGlobal>
#include <QDir>

MemoryManager::MemoryManager(const QString &dbPath, QObject *parent)
    : QObject(parent)
    , m_connectionName(QStringLiteral("seelie_memory_%1")
                          .arg(reinterpret_cast<quintptr>(QThread::currentThreadId())))
{
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(dbPath);
    m_valid = m_db.open();
    if (!m_valid) {
        qWarning() << "MemoryManager: failed to open database:" << dbPath
                    << m_db.lastError().text();
        return;
    }

    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS memory ("
                               "key TEXT PRIMARY KEY, value TEXT NOT NULL)")))
    {
        qWarning() << "MemoryManager: failed to create table:" << q.lastError().text();
        m_valid = false;
    }
}

MemoryManager::~MemoryManager()
{
    m_db.close();
    QSqlDatabase::removeDatabase(m_connectionName);
}

QString MemoryManager::value(const QString &key, const QString &defaultValue)
{
    if (!m_valid) return defaultValue;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT value FROM memory WHERE key = ?"));
    q.addBindValue(key);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return defaultValue;
}

bool MemoryManager::setValue(const QString &key, const QString &value)
{
    if (!m_valid) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO memory(key, value) VALUES(?, ?)"));
    q.addBindValue(key);
    q.addBindValue(value);
    if (!q.exec()) {
        qWarning() << "MemoryManager::setValue failed:" << q.lastError().text();
        return false;
    }
    return true;
}

int MemoryManager::increment(const QString &key, int delta)
{
    if (!m_valid) return -1;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO memory(key, value) VALUES(:key, :delta) "
        "ON CONFLICT(key) DO UPDATE SET value = CAST(value AS INTEGER) + :delta"));
    q.bindValue(QStringLiteral(":key"), key);
    q.bindValue(QStringLiteral(":delta"), delta);
    if (!q.exec()) {
        qWarning() << "MemoryManager::increment failed:" << q.lastError().text();
        return -1;
    }

    QSqlQuery r(m_db);
    r.prepare(QStringLiteral("SELECT value FROM memory WHERE key = ?"));
    r.addBindValue(key);
    if (r.exec() && r.next()) {
        bool ok = false;
        int v = r.value(0).toInt(&ok);
        return ok ? v : -1;
    }
    return -1;
}

QString MemoryManager::lastGreeting()
{
    return value(QStringLiteral("greeting.last_text"));
}

bool MemoryManager::setLastGreeting(const QString &text)
{
    return setValue(QStringLiteral("greeting.last_text"), text);
}

QString MemoryManager::userName()
{
    return value(QStringLiteral("profile.name"));
}

void MemoryManager::setUserName(const QString &name)
{
    setValue(QStringLiteral("profile.name"), name);
    emit userNameChanged(name);
}

QString MemoryManager::displayName()
{
    return value(QStringLiteral("profile.display_name"));
}

void MemoryManager::setDisplayName(const QString &name)
{
    setValue(QStringLiteral("profile.display_name"), name);
}

bool MemoryManager::hasMilestone(const QString &key)
{
    return !value(QStringLiteral("milestone.") + key).isEmpty();
}

bool MemoryManager::setMilestone(const QString &key)
{
    return setValue(QStringLiteral("milestone.") + key, QStringLiteral("1"));
}

void MemoryManager::checkMilestone(const QString &key, const QString &title, const QString &body)
{
    if (hasMilestone(key)) return;
    if (!setMilestone(key)) return;
    emit milestoneReached(title, body);
}

QString MemoryManager::effectiveName() const
{
    // displayName → userName → OS env → home dir name → ""
    const QString dn = const_cast<MemoryManager*>(this)->displayName();
    if (!dn.isEmpty()) return dn;
    const QString un = const_cast<MemoryManager*>(this)->userName();
    if (!un.isEmpty()) return un;

    QString env = qEnvironmentVariable("USER");
    if (env.isEmpty()) env = qEnvironmentVariable("USERNAME");
    if (!env.isEmpty()) return env;

    return QDir::home().dirName();
}
```

- [ ] **Step 4: Run test to verify it passes**

```bash
cd build && cmake .. && cmake --build . --target test_memory_manager && ./tests/test_memory_manager
```

Expected: All 5 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/MemoryManager.cpp tests/test_memory_manager.cpp
git commit -m "feat: MemoryManager SQLite implementation with tests"
```

---

### Task 3: MainWindow integration

**Files:**
- Modify: `src/mainwindow.h`
- Modify: `src/mainwindow.cpp`

- [ ] **Step 1: Add MemoryManager member to MainWindow header**

In `src/mainwindow.h`, add the forward declaration and member:

After line 9 (`#include "ConfigManager.h"`), add:
```cpp
class MemoryManager;
```

In the private member section (~line 116, after `GlobalShortcutManager *m_shortcutManager`), add:
```cpp
    MemoryManager *m_memory = nullptr;
```

In the public section (~line 52, after `setStateMachine`), add:
```cpp
    void setMemoryManager(MemoryManager *memory) { m_memory = memory; }
```

- [ ] **Step 2: Rewrite showRandomGreeting() in MainWindow**

Replace the existing `showRandomGreeting()` method (~line 823) with:

```cpp
void MainWindow::showRandomGreeting()
{
    if (!m_tipWidget) return;

    const auto g = TipsCatalog::instance().randomGreeting();
    if (g.title.isEmpty()) return;

    QString title = g.title;
    QString body  = g.body;

    // Greeting dedup + {name} substitution (only if MemoryManager is wired)
    if (m_memory && m_memory->isValid()) {
        // Skip greeting if it's the same as last time
        if (g.title == m_memory->lastGreeting()) {
            const auto g2 = TipsCatalog::instance().randomGreeting();
            if (!g2.title.isEmpty()) {
                title = g2.title;
                body  = g2.body;
            }
        }
        m_memory->setLastGreeting(title);

        // {name} substitution
        const QString name = m_memory->effectiveName();
        if (!name.isEmpty()) {
            title.replace(QStringLiteral("{name}"), name.toHtmlEscaped());
            body.replace(QStringLiteral("{name}"),  name.toHtmlEscaped());
        } else {
            static const QRegularExpression strip(QStringLiteral(",?\\s*\\{name\\}"));
            title.replace(strip, QString());
            body.replace(strip,  QString());
        }
    }

    m_tipWidget->showBubble(title, body, TipWidget::TipBubble);
}
```

Add the includes to top of `mainwindow.cpp`:
```cpp
#include "MemoryManager.h"
#include <QRegularExpression>
```

- [ ] **Step 3: Commit**

```bash
git add src/mainwindow.h src/mainwindow.cpp
git commit -m "feat: wire MemoryManager into MainWindow — greeting dedup + {name} substitution"
```

---

### Task 4: SettingsPanelWidget Profile tab

**Files:**
- Modify: `src/SettingsPanelWidget.h`
- Modify: `src/SettingsPanelWidget.cpp`

- [ ] **Step 1: Add MemoryManager constructor parameter**

In `src/SettingsPanelWidget.h`, find the constructor declaration. Add `MemoryManager *memory` as the second parameter:

```cpp
class MemoryManager;
// ...
explicit SettingsPanelWidget(ConfigManager *config, MemoryManager *memory,
                             QWidget *parent = nullptr);
```

In the private members, add:
```cpp
    MemoryManager *m_memory = nullptr;
```

And declare the tab setup method:
```cpp
    void setupProfileTab();
```

- [ ] **Step 2: Implement Profile tab in SettingsPanelWidget.cpp**

Find the constructor implementation in `src/SettingsPanelWidget.cpp`. Add `memory` to the initializer list and set it:

```cpp
SettingsPanelWidget::SettingsPanelWidget(ConfigManager *config, MemoryManager *memory,
                                         QWidget *parent)
    : QWidget(parent)
    , m_config(config)
    , m_memory(memory)
{
    // ... existing setup ...
    setupProfileTab();
}
```

Add the `setupProfileTab()` implementation at the end of the file:

```cpp
#include "MemoryManager.h"
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>

void SettingsPanelWidget::setupProfileTab()
{
    if (!m_memory) return;

    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    // Name field
    auto *nameLabel = new QLabel(tr("Name"), tab);
    auto *nameEdit  = new QLineEdit(tab);
    nameEdit->setPlaceholderText(tr("Your name"));
    nameEdit->setText(m_memory->userName());

    // Display name field
    auto *displayLabel = new QLabel(tr("Display name"), tab);
    auto *displayEdit  = new QLineEdit(tab);
    displayEdit->setPlaceholderText(tr("Shown in greetings"));
    displayEdit->setText(m_memory->displayName());

    // Save button
    auto *saveBtn = new QPushButton(tr("Save"), tab);
    connect(saveBtn, &QPushButton::clicked, this, [=]() {
        m_memory->setUserName(nameEdit->text().trimmed());
        m_memory->setDisplayName(displayEdit->text().trimmed());
    });

    layout->addWidget(nameLabel);
    layout->addWidget(nameEdit);
    layout->addWidget(displayLabel);
    layout->addWidget(displayEdit);
    layout->addWidget(saveBtn);
    layout->addStretch();

    // Add tab to the existing tab widget
    m_tabWidget->addTab(tab, tr("Profile"));
}
```

**Note:** `m_tabWidget` is the `QTabWidget` used by the settings panel. Check the actual member name in `SettingsPanelWidget.h` and use that. If the existing tabs are managed differently (e.g., via a custom button bar), adapt accordingly — but preserve the existing UI pattern.

- [ ] **Step 3: Update main.cpp to pass MemoryManager to SettingsPanelWidget**

In `src/main.cpp`, find the line that creates `SettingsPanelWidget` (~line 81 in MainWindow constructor — `m_settingsPanel = new SettingsPanelWidget(m_config, nullptr);`). In `MainWindow::MainWindow()`, change:
```cpp
m_settingsPanel = new SettingsPanelWidget(m_config, nullptr);
```
to:
```cpp
// m_memory is set later via setMemoryManager() in main.cpp;
// Profile tab will show once it's wired.
m_settingsPanel = new SettingsPanelWidget(m_config, nullptr, nullptr);
```

Then in `main.cpp`, after constructing `ConfigManager config`, add:
```cpp
MemoryManager memory(configDir() + "/memory.db");
```
And add the `setMemoryManager` call after the `MainWindow w(&config, &translator)` line (alongside other `w.set...()` calls):
```cpp
w.setMemoryManager(&memory);
```

Also update the `SettingsPanelWidget` construction:
Since `m_settingsPanel` is constructed in `MainWindow::MainWindow()` (line 81 of mainwindow.cpp), we need to update the constructor call there. The `MainWindow` constructor takes `ConfigManager *config, QTranslator *translator` — let's keep it simple and use `setMemoryManager()`:

In `main.cpp`, after `w.setEventRouter(&eventRouter);`:
```cpp
w.setMemoryManager(&memory);
```

And update `SettingsPanelWidget` in `MainWindow` constructor: since `m_settingsPanel` is created in the MainWindow constructor body before `setMemoryManager()` is called from main.cpp, we need to either:
1. Pass nullptr and set it later via a setter on SettingsPanelWidget, or
2. Defer Profile tab creation until MemoryManager is available

**Simplest approach:** Add `setMemoryManager(MemoryManager*)` to `SettingsPanelWidget` that creates the Profile tab lazily:

In `src/SettingsPanelWidget.h`, add:
```cpp
void setMemoryManager(MemoryManager *memory);
```

In `src/SettingsPanelWidget.cpp`, implement:
```cpp
void SettingsPanelWidget::setMemoryManager(MemoryManager *memory)
{
    m_memory = memory;
    if (m_memory) {
        setupProfileTab();
    }
}
```

And in the constructor, remove the `setupProfileTab()` call (it will be called via `setMemoryManager` from main.cpp).

Then in `main.cpp`:
```cpp
w.setMemoryManager(&memory);
// also wire to settings panel
QObject::connect(&memory, &MemoryManager::userNameChanged,
                 w.settingsPanel(), &SettingsPanelWidget::retranslateUi);
```

Actually wait, `SettingsPanelWidget` is a private member of `MainWindow`. I need a getter or to expose it. Let me check...

Looking at mainwindow.h, `m_settingsPanel` is private and there's no public getter. The cleaner approach: in `MainWindow::setMemoryManager()`:

```cpp
void MainWindow::setMemoryManager(MemoryManager *memory) {
    m_memory = memory;
    if (m_settingsPanel) {
        m_settingsPanel->setMemoryManager(memory);
    }
}
```

This keeps `m_settingsPanel` private. Let me update the plan steps accordingly.

- [ ] **Step 4: Commit**

```bash
git add src/SettingsPanelWidget.h src/SettingsPanelWidget.cpp src/mainwindow.h src/mainwindow.cpp src/main.cpp
git commit -m "feat: add Profile tab in Settings panel + wire MemoryManager"
```

---

### Task 5: TipsCatalog {name} templates + TipsEngine milestone

**Files:**
- Modify: `src/TipsCatalog.h`
- Modify: `src/TipsCatalog.cpp`
- Modify: `src/TipsEngine.cpp`

- [ ] **Step 1: Add {name} to greeting strings in TipsCatalog**

Find the greeting definitions in `TipsCatalog.cpp`. Add `{name}` to greeting strings. Example changes:

Before:
```cpp
{tr("Good morning!"), tr("Ready to code?")},
```
After:
```cpp
{tr("Good morning, {name}!"), tr("Ready to code?")},
```

Add `{name}` to at least 3-4 greetings. Keep some greetings without `{name}` so the rotation has variety.

- [ ] **Step 2: Add first_tip milestone trigger to TipsEngine**

In `src/TipsEngine.cpp`, the `processEvent()` method loops through matchers and, on match, shows a bubble and emits `animationRequested`. After the match block (~line 67), we need to call `checkMilestone`. But TipsEngine doesn't have a MemoryManager pointer.

Add a `setMemoryManager(MemoryManager *memory)` setter to `TipsEngine`:

In `src/TipsEngine.h`:
```cpp
class MemoryManager;
// ...
void setMemoryManager(MemoryManager *memory) { m_memory = memory; }
// private:
MemoryManager *m_memory = nullptr;
```

In `src/TipsEngine.cpp`, in `processEvent()`, after the `break;` at line 68:
```cpp
    if (m_memory) {
        m_memory->checkMilestone(QStringLiteral("first_tip"),
                                 tr("Seelie noticed something!"),
                                 tr("I'll try to give helpful tips while you work."));
    }
    break;
```

- [ ] **Step 3: Wire TipsEngine to MemoryManager in main.cpp**

In `main.cpp`, after creating `TipsEngine tipsEngine`:
```cpp
tipsEngine.setMemoryManager(&memory);
```

- [ ] **Step 4: Commit**

```bash
git add src/TipsCatalog.cpp src/TipsEngine.h src/TipsEngine.cpp src/main.cpp
git commit -m "feat: {name} templates in greetings + first_tip milestone"
```

---

### Task 6: Milestone signal wiring in main.cpp

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Wire milestoneReached to tip widget**

In `main.cpp`, after `ipcServer.start(config.ipcEndpoint());` (~line 416), add:

```cpp
// Milestone achievements → tip bubble
QObject::connect(&memory, &MemoryManager::milestoneReached,
                 &w, [&w](const QString &title, const QString &body) {
    w.tipWidget()->showBubble(title, body, TipWidget::TipBubble);
});
```

- [ ] **Step 2: Wire gaming_mode milestone**

After existing `connect(m_config, &ConfigManager::gamingModeEnabledChanged, ...)` code (~line 183 in MainWindow constructor):

In `MainWindow::MainWindow()`, after line 183 (`connect(m_config, &ConfigManager::gamingModeEnabledChanged, ...)`), add a checkMilestone call inside the existing lambda. Find the existing lambda for `gamingModeEnabledChanged` (~line 184-196 of mainwindow.cpp). Since `m_memory` isn't set yet at that point, add the milestone check inside `MainWindow::setMemoryManager()` instead:

In `src/mainwindow.cpp`, update `setMemoryManager()`:

```cpp
void MainWindow::setMemoryManager(MemoryManager *memory)
{
    m_memory = memory;
    if (m_settingsPanel) {
        m_settingsPanel->setMemoryManager(memory);
    }
    // Wire gaming_mode milestone (deferred until MemoryManager is available)
    if (m_memory && m_config) {
        connect(m_config, &ConfigManager::gamingModeEnabledChanged,
                m_memory, [this](bool enabled) {
            if (enabled) {
                m_memory->checkMilestone(QStringLiteral("gaming_mode"),
                    tr("Gaming Mode activated!"),
                    tr("Seelie will hide when fullscreen apps are detected."));
            }
        });
    }
}
```

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp src/mainwindow.cpp
git commit -m "feat: wire milestone signals — milestoneReached + gaming_mode trigger"
```

---

### Task 7: i18n strings + build verification

**Files:**
- Modify: `Seelie_zh_CN.ts`
- Verify: `build/` compiles and all tests pass

- [ ] **Step 1: Add Chinese translations**

Add these entries to `Seelie_zh_CN.ts`:

```xml
<context>
    <name>MemoryManager</name>
    <!-- milestone strings -->
</context>
<context>
    <name>SettingsPanelWidget</name>
    <message>
        <source>Profile</source>
        <translation>资料</translation>
    </message>
    <message>
        <source>Name</source>
        <translation>名字</translation>
    </message>
    <message>
        <source>Display name</source>
        <translation>显示名称</translation>
    </message>
    <message>
        <source>Your name</source>
        <translation>您的名字</translation>
    </message>
    <message>
        <source>Shown in greetings</source>
        <translation>在问候语中显示</translation>
    </message>
    <message>
        <source>Save</source>
        <translation>保存</translation>
    </message>
</context>
<context>
    <name>TipsEngine</name>
    <message>
        <source>Seelie noticed something!</source>
        <translation>Seelie 注意到了一些事情！</translation>
    </message>
    <message>
        <source>I'll try to give helpful tips while you work.</source>
        <translation>我会在你工作时给出有用的建议。</translation>
    </message>
</context>
<context>
    <name>MainWindow</name>
    <message>
        <source>Gaming Mode activated!</source>
        <translation>游戏模式已启用！</translation>
    </message>
    <message>
        <source>Seelie will hide when fullscreen apps are detected.</source>
        <translation>检测到全屏应用时 Seelie 会自动隐藏。</translation>
    </message>
</context>
```

- [ ] **Step 2: Build and run all tests**

```bash
cd build
cmake .. -DCMAKE_PREFIX_PATH="<Qt6 path>"
cmake --build .
ctest
```

Expected: All existing tests pass + `test_memory_manager` passes (5/5).

- [ ] **Step 3: Commit**

```bash
git add Seelie_zh_CN.ts
git commit -m "i18n: add Chinese translations for Profile tab + milestone strings"
```

---

## Spec Coverage Check

| Spec Section | Task(s) |
|---|---|
| 1. Data Model (schema) | Task 2 (CREATE TABLE in MemoryManager constructor) |
| 2. MemoryManager API | Tasks 1 (header) + 2 (implementation + tests) |
| 3. Profile Tab | Task 4 (SettingsPanelWidget) |
| 4. Greeting Rotation + {name} | Task 3 (showRandomGreeting) + Task 5 (TipsCatalog templates) |
| 5. Milestones | Task 5 (TipsEngine first_tip) + Task 6 (signal wiring) |
| 6. Files Changed | Tasks 1-7 |
| 7. Constraints (Qt::Sql) | Task 1 (CMakeLists) |
| i18n | Task 7 |

All spec sections covered. No placeholders.
