## 1. Expand Animation Name Map

- [ ] 1.1 Add missing animation mappings to `buildNameMap()` in `src/SpriteAnimationEngine.cpp`:
  - `checking` → `CheckingSomething`
  - `empty_trash` → `EmptyTrash`
  - `hearing` → `Hearing_1`
  - `look_down_left` → `LookDownLeft`
  - `look_down_right` → `LookDownRight`
  - `look_up_left` → `LookUpLeft`
  - `look_up_right` → `LookUpRight`
- [ ] 1.2 Add new idle animations to `m_idleAnims` pool: `IdleHeadScratch`, `IdleFingerTap`, `IdleEyeBrowRaise`, `IdleRopePile`, `IdleSnooze`
- [ ] 1.3 Add corresponding weights for new idle animations in `m_idleWeights`

## 2. Variable Idle Timeout

- [ ] 2.1 Replace fixed `m_idleTimeoutMs` with dynamic interval computation
- [ ] 2.2 In `startIdleAnimation()` and animation-finish handlers, set timer interval to `QRandomGenerator::global()->bounded(3000) + 1000` before calling `m_idleTimer.start()`

## 3. Anti-Repeat Idle Selection

- [ ] 3.1 Add `QString m_lastIdleAnim` member to `SpriteAnimationEngine`
- [ ] 3.2 Update `startIdleAnimation()` to compare selected animation against `m_lastIdleAnim`
- [ ] 3.3 Implement one re-roll when the selection matches the last-played animation (skip if pool size ≤ 1)
- [ ] 3.4 Record the final selected animation into `m_lastIdleAnim` before playing

## 4. Surprise Non-Idle Picks

- [ ] 4.1 Add `QStringList m_surpriseAnims` member with directional looks: `LookLeft`, `LookRight`, `LookUp`, `LookDown`, `LookUpLeft`, `LookUpRight`, `LookDownLeft`, `LookDownRight`
- [ ] 4.2 In `startIdleAnimation()`, add 10% probability branch that selects from `m_surpriseAnims` instead of `m_idleAnims`
- [ ] 4.3 Ensure surprise picks also respect the anti-repeat logic via `m_lastIdleAnim`

## 5. Verification

- [ ] 5.1 Build and run on macOS, confirm all 43 animations are reachable via `playAnimation()`
- [ ] 5.2 Observe idle behavior for 30 seconds and verify: variable timing, no consecutive repeats, occasional surprise looks
- [ ] 5.3 Run existing unit tests (if any) and confirm no regressions
