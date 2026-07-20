## ADDED Requirements

### Requirement: Animation engine tracks last idle state
The system SHALL maintain internal state tracking the most recently played idle or surprise animation to support anti-repeat logic.

#### Scenario: Idle animation completes
- **WHEN** an idle or surprise animation begins playing
- **THEN** the animation engine records that animation name as the last idle state

### Requirement: Idle timer configuration is dynamic
The system SHALL restart the idle timer with a newly computed random interval after each idle cycle.

#### Scenario: Timer restarts after surprise pick
- **WHEN** a surprise animation finishes and the idle timer is restarted
- **THEN** the timer interval is recomputed as a new random value in [1000, 4000] milliseconds
