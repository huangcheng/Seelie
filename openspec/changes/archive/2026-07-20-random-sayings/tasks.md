## 1. RandomSayingsEngine Core

- [ ] 1.1 Create `src/RandomSayingsEngine.h` with `Saying` struct and `RandomSayingsEngine` class
- [ ] 1.2 Create `src/RandomSayingsEngine.cpp` with saying pool initialization (~20 sayings across 4 categories)
- [ ] 1.3 Implement `pickRandomSaying()` with category-then-weighted selection
- [ ] 1.4 Implement `maybeSaySomething()` with probability check and cooldown enforcement
- [ ] 1.5 Add 60-second cooldown tracking (`m_lastSayingTime`)

## 2. Config Integration

- [ ] 2.1 Add `SayingFrequency` enum to `ConfigManager` (`Never`, `Rarely`, `Sometimes`, `Often`)
- [ ] 2.2 Add `sayingFrequency()` getter and `setSayingFrequency()` setter to `ConfigManager`
- [ ] 2.3 Persist frequency to `config.json` with default `"sometimes"`
- [ ] 2.4 Add frequency probability mapping in `RandomSayingsEngine` (0%, 10%, 25%, 50%)

## 3. UI Integration

- [ ] 3.1 Instantiate `RandomSayingsEngine` in `MainWindow`
- [ ] 3.2 Wire `RandomSayingsEngine` output to `TipBubbleWidget::showBubble()`
- [ ] 3.3 Trigger `maybeSaySomething()` from idle timer path alongside idle animations
- [ ] 3.4 Ensure saying bubble and idle animation play together without conflict

## 4. Settings Panel

- [ ] 4.1 Add saying frequency dropdown to `SettingsPanelWidget`
- [ ] 4.2 Connect dropdown changes to `ConfigManager::setSayingFrequency()`
- [ ] 4.3 Load saved frequency into dropdown on panel open

## 5. Verification

- [ ] 5.1 Build and run on macOS, verify sayings appear during idle
- [ ] 5.2 Test each frequency setting (Never, Rarely, Sometimes, Often)
- [ ] 5.3 Confirm 60-second cooldown prevents rapid saying spam
- [ ] 5.4 Confirm no regressions in existing tip/event system
