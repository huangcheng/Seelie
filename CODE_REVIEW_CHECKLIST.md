# Code Review Checklist — Preventing Common Bugs in Seelie

This checklist captures the mistakes that were fixed during the May 2026 comprehensive audit. Use it when writing or reviewing new features.

---

## 1. Threading & IPC

### UDP / Socket Handling
- [ ] **Always** cap the number of datagrams processed per `readyRead` signal. Unbounded loops exhaust the main-thread event queue.
  ```cpp
  constexpr int kMaxDatagramsPerRead = 100;
  int processed = 0;
  while (m_socket->hasPendingDatagrams() && ++processed <= kMaxDatagramsPerRead) { ... }
  ```
- [ ] **Never** pass `maxSize = 0` to `readDatagram()`. Use a real-sized buffer to drain oversized datagrams.
  ```cpp
  QByteArray sink(qMin(pending, kMaxDatagramBytes), 0);
  m_socket->readDatagram(sink.data(), sink.size());
  ```
- [ ] **Always** check the return value of `writeDatagram()`. Log or emit an error if it returns `-1`.
- [ ] **Always** set `QUdpSocket::ReuseAddressHint` (or `SO_REUSEADDR`) when binding, so a restart after thread timeout can reclaim the port.

### Thread Lifecycle
- [ ] **Never** rely on `QThread::finished → deleteLater` to delete a worker. The event loop may have exited before the deferred delete is processed. Explicitly `delete` the worker after `wait()` returns.
- [ ] **Always** give `thread->wait()` a bounded timeout (e.g., 5000 ms), but handle the timeout case gracefully (close the socket before leaking the thread).
- [ ] **Never** emit signals from inside a destructor. Queued connections to a destroyed sender are dropped unpredictably.
- [ ] **Always** capture cross-thread pointers by value in lambdas, not by reading a member variable from another thread.

### Signal / Slot Safety
- [ ] **Always** check the return value of `QMetaObject::invokeMethod()`. If it returns `false`, the target object may be dying.
- [ ] **Always** use `Qt::QueuedConnection` (or `Qt::AutoConnection`) for cross-thread signals. Never use `BlockingQueuedConnection` to a thread's own objects.

---

## 2. Memory Management & Ownership

### QObject Parent-Child Tree
- [ ] **Always** establish the parent-child relationship at construction time **or** document manual ownership explicitly.
  ```cpp
  // Good — Qt deletes it for us
  m_child = new ChildWidget(this);

  // Good — documented manual ownership
  m_child = new ChildWidget(nullptr);   // separate top-level, deleted in ~Parent()
  ```
- [ ] **Never** `delete` an object that has a QObject parent. That causes a double-free when the parent is destroyed.
- [ ] **Always** parent `QMenu` instances to a `QWidget` (or explicitly delete them). `QSystemTrayIcon::setContextMenu()` does **not** take ownership.

### Dangling Pointers
- [ ] **Always** use `QPointer<QObjectType>` for non-owning references to `QObject`s that you do not control.
  ```cpp
  // Before (dangerous)
  QWidget *m_petWindow = nullptr;

  // After (safe)
  QPointer<QWidget> m_petWindow;
  ```
- [ ] **Never** dereference a raw pointer to another widget without a null check, unless you are 100 % certain of the lifetime ordering (and have documented it).

### Connection Hygiene
- [ ] **Always** disconnect old signals before reconnecting in setter methods.
  ```cpp
  void MyWidget::setManager(Manager *manager)
  {
      if (m_manager)
          disconnect(m_manager, nullptr, this, nullptr);
      m_manager = manager;
      if (m_manager)
          connect(m_manager, &Manager::changed, this, &MyWidget::onChanged);
  }
  ```

### Dialog Lifecycle
- [ ] **Never** combine `WA_DeleteOnClose` **and** `dismissed → deleteLater` on the same dialog. Pick one deletion strategy and stick to it.

---

## 3. UI Widget Lifecycle

### Window Flags
- [ ] **Always** add `Qt::Tool | Qt::WindowDoesNotAcceptFocus` to frameless popups (settings panels, alerts, pack manager). Without these, the window appears in the Dock / taskbar and steals focus.
  ```cpp
  setWindowFlags(
      Qt::FramelessWindowHint |
      Qt::WindowStaysOnTopHint |
      Qt::Tool |
      Qt::WindowDoesNotAcceptFocus
  );
  ```

### Event Loop Safety
- [ ] **Never** spin a local `QEventLoop` without a safety net. If the widget can be destroyed while the loop is running, the loop blocks forever.
  ```cpp
  QEventLoop loop;
  connect(this, &MyDialog::dismissed, &loop, &QEventLoop::quit);
  connect(this, &MyDialog::destroyed,  &loop, &QEventLoop::quit);  // safety net
  loop.exec();
  ```
- [ ] **Always** guard against self-destruction during a nested event loop.
  ```cpp
  QPointer<MyWidget> guard(this);
  bool confirmed = dialog.execConfirm(...);
  if (!guard) return;   // we were deleted while the loop ran
  ```
- [ ] **Always** add a reentrancy guard to modal functions.
  ```cpp
  bool MyDialog::execConfirm(...)
  {
      if (m_inConfirmMode) return false;
      m_inConfirmMode = true;
      ...
  }
  ```

### Animation & Timer Cleanup
- [ ] **Always** stop a `QPropertyAnimation` before deleting it.
  ```cpp
  if (m_anim) {
      m_anim->stop();
      delete m_anim;
      m_anim = nullptr;
  }
  ```
- [ ] **Always** stop timers in the destructor.
  ```cpp
  ~MyWidget()
  {
      m_dismissTimer.stop();
      if (m_anim) m_anim->stop();
  }
  ```

### Platform Handle Churn
- [ ] **Never** call `winId()` (or any function that forces native window creation) on every `showEvent()`. Guard it with a one-shot flag.
  ```cpp
  void MyWidget::showEvent(QShowEvent *event)
  {
      QWidget::showEvent(event);
      if (!m_nativeHandleFixed) {
          MacFocusFix::makeNonActivating(this);
          m_nativeHandleFixed = true;
      }
  }
  ```

### Drag & Drop
- [ ] **Never** call `acceptProposedAction()` unconditionally. Only accept if you actually handled the drop.
  ```cpp
  bool handled = false;
  for (const QUrl &url : event->mimeData()->urls()) {
      if (processFile(url)) handled = true;
  }
  if (handled) event->acceptProposedAction();
  else         event->ignore();
  ```

---

## 4. Animation & State Machines

### Engine Lifecycle
- [ ] **Always** call `stop()` before clearing or replacing animation data (sprite sheets, frame definitions, Lottie models). Running timers + stale data = crashes.
- [ ] **Always** reset the current animation handle when the underlying resource changes.
  ```cpp
  void MyEngine::loadFromCharacterPack(...)
  {
      stop();                 // halt the timer
      m_current = {};         // clear the active animation handle
      m_animations.clear();   // now safe to discard data
      ...
  }
  ```

### Bounds Checking
- [ ] **Never** call `QVector::at()` with an unchecked index inside `paintEvent()`. Always verify `index < container.size()`.

### State Consistency
- [ ] **Always** update saved / fallback state when the base state changes during an overlay / one-shot.
  ```cpp
  void PetStateMachine::enterBase(State s)
  {
      if (m_overlayState != State::Idle)
          m_savedSustained = s;   // track reality so restore is correct
      m_baseState = s;
  }
  ```
- [ ] **Always** restart timers that guard continuous actions (e.g., walking idle timeout) even on no-op early returns.

### OpenGL / GPU Resources
- [ ] **Always** unbind a framebuffer object before deleting it.
  ```cpp
  if (m_fbo) {
      m_fbo->release();
      delete m_fbo;
      m_fbo = nullptr;
  }
  ```
- [ ] **Always** validate texture IDs are non-zero before binding.

### Performance
- [ ] **Never** log inside `paintEvent()` or per-frame `tick()` methods. Console I/O blocks the main thread.
- [ ] **Always** guard against division-by-zero when computing frame deltas from configurable `frameRate`.
  ```cpp
  if (effect.frameRate <= 0.0) continue;
  double msPerFrame = 1000.0 / effect.frameRate;
  ```

---

## 5. Network & TTS

### HTTP Request Lifecycle
- [ ] **Always** call `reply->deleteLater()` (or `delete reply`) after `reply->abort()`. Abort alone leaks the `QNetworkReply`.
  ```cpp
  void Provider::cancel(quint64 id)
  {
      QNetworkReply *reply = m_inFlight.take(id);
      if (reply) {
          reply->abort();
          reply->deleteLater();   // ← do not forget
      }
  }
  ```
- [ ] **Always** cancel in-flight requests before destroying a provider or engine.

### Logging Security
- [ ] **Never** log raw HTTP response bodies without sanitizing. Error messages may contain `Authorization` headers or API keys.
  ```cpp
  QString redactSensitiveInfo(QString msg)
  {
      msg.replace(QRegularExpression("Bearer\\s+\\S+"), "Bearer [REDACTED]");
      msg.replace(QRegularExpression("Ocp-Apim-Subscription-Key:\\s*\\S+"), "Ocp-Apim-Subscription-Key: [REDACTED]");
      return msg;
  }
  ```

### Input Validation
- [ ] **Always** escape user-controlled strings before injecting them into structured formats (XML/SSML, JSON, HTML).
  ```cpp
  static QString xmlEscape(const QString &text) { ... }
  QString ssml = QString("<voice name=\"%1\">%2</voice>")
                     .arg(xmlEscape(voice), xmlEscape(text));
  ```
- [ ] **Always** validate filesystem identifiers before using them in paths. Reject `..`, `/`, `\`, and `:`.

### Version / Protocol Parsing
- [ ] **Always** validate protocol version bytes and semantic version strings on the client side before acting on them.
- [ ] **Always** enforce HTTPS for update/download URLs. Reject `http://` or scheme-less URLs.

### Time & Clocks
- [ ] **Never** use `QDateTime::currentDateTime()` for cooldowns or timeouts. It is affected by NTP sync, DST, and user adjustments. Use `QElapsedTimer` or `QDateTime::currentMSecsSinceEpoch()` for monotonic-like behavior.

---

## 6. Error Handling

### Return Values
- [ ] **Always** check the return value of:
  - `QAudioSink::start()` / `QAudioDecoder::start()`
  - `QMetaObject::invokeMethod()`
  - `QNetworkReply` operations
  - File I/O (`flush()`, `write()`, `open()`)
- [ ] **Always** handle the failure path — emit an error signal, log, or abort gracefully. Do not silently continue.

### Timeouts
- [ ] **Always** bound blocking waits (`QThread::wait()`, `QNetworkReply` synchronous calls) with a timeout.
- [ ] **Always** handle the timeout case explicitly (retry, leak mitigation, or error emission).

---

## 7. Security Quick Reference

| Threat | Mitigation |
|--------|-----------|
| Path traversal in pack IDs | Reject `..`, `/`, `\`, `:` before path construction |
| UDP spoofing (update checker) | Require HTTPS URLs; validate version strings |
| SSML injection | `xmlEscape()` all user-controlled attributes |
| Credential leakage in logs | `redactSensitiveInfo()` before logging HTTP bodies |
| Unbounded cache growth | Evict by actual accounted size, not directory listing |
| Unbounded event queues | Cap iterations per signal handler |
| Double-free | Parent the object **OR** delete it manually — never both |

---

## 8. Final Pre-Submit Checklist

Before opening a PR, run through this mini-list for every new or modified file:

1. [ ] Does every `new` have a clear owner (parent QObject or manual `delete`)?
2. [ ] Are there any raw `QObject*` members that should be `QPointer`?
3. [ ] Are signal connections in setters guarded against duplication?
4. [ ] Are animations / timers stopped before their target objects are destroyed?
5. [ ] Are `QEventLoop` or modal calls protected against widget destruction?
6. [ ] Is user input validated before use in paths, URLs, or structured text?
7. [ ] Are return values checked and failures handled?
8. [ ] Are there any `qDebug()` / `qWarning()` calls inside per-frame or per-datagram hot paths?
9. [ ] Does the code compile and pass `cd build && ctest`?

---

*Derived from the May 2026 comprehensive audit. If you find a new category of bug, add it here so the whole team benefits.*
