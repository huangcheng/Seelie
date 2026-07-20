## ADDED Requirements

### Requirement: All sprite animations are addressable by name
The system SHALL expose every animation defined in `animations.json` through the `buildNameMap()` name-mapping function.

#### Scenario: Developer requests an unmapped animation
- **WHEN** `playAnimation("checking")` is called
- **THEN** the animation engine plays the `CheckingSomething` sprite animation

#### Scenario: All idle variants are reachable
- **WHEN** `playAnimation("idle_head_scratch")` is called
- **THEN** the animation engine plays the `IdleHeadScratch` sprite animation
